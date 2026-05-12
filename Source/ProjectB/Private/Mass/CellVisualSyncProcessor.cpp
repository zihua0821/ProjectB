// Fill out your copyright notice in the Description page of Project Settings.


#include "Mass/CellVisualSyncProcessor.h"

#include "MassExecutionContext.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Mass/CellMassFragments.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Subsystem/ISMManager.h"

UCellVisualSyncProcessor::UCellVisualSyncProcessor()
{
	bAutoRegisterWithProcessingPhases = true;
}

void UCellVisualSyncProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FCellStaticFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FHighlightFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.RegisterWithProcessor(*this);
}

void UCellVisualSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const UWorld* World = EntityManager.GetWorld();
	if (!World) return;

	UISMManager* ISMManager = World->GetSubsystem<UISMManager>();
	if (!ISMManager) return;
	
	const UGlobalConfigSubsystem* ConfigSub = World->GetSubsystem<UGlobalConfigSubsystem>();
	if (!ConfigSub) return;

	const UFarmlandConfigDataAsset* FarmlandConfig = ConfigSub->GetFarmlandConfig();
	const UPlanetConfigDataAsset* PlanetConfig = ConfigSub->GetPlanetConfig();

	// 收集待移除的实例
	TArray<FGlobalInstanceId> PendingRemoves;

	// 收集待添加的实例
	TArray<TPair<TArray<float>, TPair<FTransform, FMassEntityHandle>>> PendingAdds;

	// 收集待批量更新的实例
	TArray<TPair<FGlobalInstanceId, TArray<float>>> PendingUpdates;
	
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& InContext)
    {
        const TConstArrayView<FCellStaticFragment> StaticList = InContext.GetFragmentView<FCellStaticFragment>();
        const TArrayView<FHighlightFragment> StateList = InContext.GetMutableFragmentView<FHighlightFragment>();
        const int32 NumEntities = InContext.GetNumEntities();
        
        for (int32 i = 0; i < NumEntities; ++i)
        {
        	const FCellStaticFragment& CellStaticFragment = StaticList[i];
        	FHighlightFragment& HighlightFragment = StateList[i];

	        const bool bHighlightChanged = HighlightFragment.bHighlighted != HighlightFragment.LastHighlightedState;
	        const bool bColorChanged = HighlightFragment.HighlightColor != HighlightFragment.LastHighlightColor;
        	// 有变化时更新实例
        	if (bHighlightChanged || bColorChanged) 
        	{
        		// 计算CellCustomData
		        TArray<FVector> LocalVertexPositions = CalculateCellVertexPositions(
			        PlanetConfig,
			        CellStaticFragment.Coords.X,
			        CellStaticFragment.Coords.Y
		        );
		        TConstArrayView<float> CellCustomData =
		        {
			        0.1f, // Opacity
			        HighlightFragment.HighlightColor.R, // R
			        HighlightFragment.HighlightColor.G, // G
			        HighlightFragment.HighlightColor.B, // B
			        static_cast<float>(LocalVertexPositions[0].X), // X00
			        static_cast<float>(LocalVertexPositions[0].Y), // Y00
			        static_cast<float>(LocalVertexPositions[0].Z), // Z00
			        static_cast<float>(LocalVertexPositions[1].X), // X01
			        static_cast<float>(LocalVertexPositions[1].Y), // Y01
			        static_cast<float>(LocalVertexPositions[1].Z), // Z01
			        static_cast<float>(LocalVertexPositions[2].X), // X10
			        static_cast<float>(LocalVertexPositions[2].Y), // Y10
			        static_cast<float>(LocalVertexPositions[2].Z), // Z10
			        static_cast<float>(LocalVertexPositions[3].X), // X11
			        static_cast<float>(LocalVertexPositions[3].Y), // Y11
			        static_cast<float>(LocalVertexPositions[3].Z), // Z11
		        };

        		// 高亮状态变化时
		        if (bHighlightChanged)
		        {
					if (HighlightFragment.bHighlighted)
					{
						PendingAdds.Emplace(
							CellCustomData,
							TPair<FTransform, FMassEntityHandle>(
								CalculateOutlineInstanceTransform(PlanetConfig, FarmlandConfig, CellStaticFragment.Transform, CellStaticFragment.Coords.X),
								InContext.GetEntity(i)
								)
							);
					}
					else
					{
						PendingRemoves.Add(HighlightFragment.InstanceId);
					}
		        	HighlightFragment.LastHighlightedState = HighlightFragment.bHighlighted;
		        }
        		
        		// 颜色变化时
		        if (bColorChanged)
		        {
		        	PendingUpdates.Emplace(HighlightFragment.InstanceId, CellCustomData);
		        	HighlightFragment.LastHighlightColor = HighlightFragment.HighlightColor;
		        }
        	}
        }
    });

	 // ---- 批量移除 ----
    if (PendingRemoves.Num() > 0)
    {
    	ISMManager->BatchRemoveInstances(FarmlandConfig->OutlineMesh, PendingRemoves);
    }

    // ---- 批量添加 ----
    if (PendingAdds.Num() > 0)
    {
    	TArray<float> CellsCustomData;
    	TArray<FTransform> Transforms;
    	TArray<FMassEntityHandle> Entities;
    	
    	for (const TPair<TArray<float>, TPair<FTransform, FMassEntityHandle>>& Pair : PendingAdds)
    	{
    		CellsCustomData.Append(Pair.Key);
    		Transforms.Add(Pair.Value.Key);
    		Entities.Add(Pair.Value.Value);
    	}
    	
    	TArray<FGlobalInstanceId> NewIds = ISMManager->BatchAddInstances(
				FarmlandConfig->OutlineMesh,
				Transforms,
				16,
				FISMComponentParams(),
				CellsCustomData
				);
    	check(NewIds.Num() == Entities.Num());
	   
    	// 写回实例 ID
    	for (int32 j = 0; j < NewIds.Num(); ++j)
    	{
    		if (EntityManager.IsEntityActive(Entities[j]))
    		{
    			EntityManager.GetFragmentDataChecked<FHighlightFragment>(Entities[j]).InstanceId = NewIds[j];
    		}
    	}
    }

	// ---- 批量更新 ----
	if (PendingUpdates.Num() > 0)
	{
		TArray<FGlobalInstanceId> Ids;
		TArray<float> CellsCustomData;
		for (const TPair<FGlobalInstanceId, TArray<float>>& Pair : PendingUpdates)
		{
			Ids.Add(Pair.Key);
			CellsCustomData.Append(Pair.Value);
		}
		ISMManager->BatchSetInstanceCustomData(FarmlandConfig->OutlineMesh, Ids, CellsCustomData);
	}
}

TArray<FVector> UCellVisualSyncProcessor::CalculateCellVertexPositions(const UPlanetConfigDataAsset* PlanetConfig, const int32 LatIdx, const int32 LonIdx)
{
	// 自动计算经纬度步长
	const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
	const float LonStep = 360.0f / PlanetConfig->NumLon;
	
	// 当前地块的经纬度边界
	const float MinLat = PlanetConfig->MinLat + LatIdx * LatStep;
	const float MaxLat = MinLat + LatStep;
	const float MinLon = LonIdx * LonStep;
	const float MaxLon = MinLon + LonStep;

	// 四个角点的经纬度（单位：度）
	// 顺序：V1=左下 (南西), V2=右下 (南东), V3=左上 (北西), V4=右上 (北东)
	const float LatDegrees[4] = { MinLat, MinLat, MaxLat, MaxLat };
	const float LonDegrees[4] = { MinLon, MaxLon, MinLon, MaxLon };

	// 转换为球面坐标（四个顶点，相对于世界原点）
	FVector VertexPositions[4];
	for (int32 i = 0; i < 4; i++)
	{
		const float LatRad = FMath::DegreesToRadians(LatDegrees[i]);
		const float LonRad = FMath::DegreesToRadians(LonDegrees[i]);
		const float CosLat = FMath::Cos(LatRad);
		const float SinLat = FMath::Sin(LatRad);
		const float CosLon = FMath::Cos(LonRad);
		const float SinLon = FMath::Sin(LonRad);

		VertexPositions[i] = FVector(
			PlanetConfig->Radius * CosLat * CosLon,
			PlanetConfig->Radius * CosLat * SinLon,
			PlanetConfig->Radius * SinLat
		);
	}
	return TArray<FVector>(VertexPositions, 4);
}

FTransform UCellVisualSyncProcessor::CalculateOutlineInstanceTransform(const UPlanetConfigDataAsset* PlanetConfig,
	const UFarmlandConfigDataAsset* FarmlandConfig, const FTransform& CellTransform, const float LatIdx)
{
	// 计算轮廓缩放
	const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
	const float LonStep = 360.0f / PlanetConfig->NumLon;
	const float LatStepRad = FMath::DegreesToRadians(LatStep);
	const float LonStepRad = FMath::DegreesToRadians(LonStep);
	const float CurrentLatDeg = PlanetConfig->MinLat + (LatIdx + 0.5f) * LatStep;
	// 当前地块中心纬度（用于计算经度方向宽度）
	const float CurrentLatRad = FMath::DegreesToRadians(CurrentLatDeg);

	// 计算单元格实际尺寸（球面几何）
	const float CellHeight = PlanetConfig->Radius * LatStepRad; // 南北向
	const float CellWidth = PlanetConfig->Radius * FMath::Cos(CurrentLatRad) * LonStepRad; // 东西向

	// 获取轮廓网格体自身尺寸
	const FBoxSphereBounds OutlineBounds = FarmlandConfig->OutlineMesh->GetBounds();
	const FVector OutlineExtent = OutlineBounds.BoxExtent;

	// 计算轮廓缩放（匹配单元格实际大小）
	const float OutlineScaleX = CellWidth / (OutlineExtent.X * 2.f);
	const float OutlineScaleY = CellHeight / (OutlineExtent.Y * 2.f);
	constexpr float OutlineScaleZ = 1.0f;

	// 创建轮廓实例（使用独立计算的缩放）
	FTransform OutlineInstanceTransform = CellTransform;
	OutlineInstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
	OutlineInstanceTransform.SetScale3D(FVector(OutlineScaleX, OutlineScaleY, OutlineScaleZ));
	
	return OutlineInstanceTransform;
}
