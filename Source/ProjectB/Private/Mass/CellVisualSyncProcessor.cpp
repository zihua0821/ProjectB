// CellVisualSyncProcessor.cpp
// 实现：格子高亮轮廓的视觉同步（类头与线程说明见 .h）

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
	bRequiresGameThreadExecution = true;
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

	// 轮廓计算参数快照（GT 预提取——worker 段不访问 UObject；样板纪律）
	FCellOutlineCalcParams CalcParams;
	if (PlanetConfig)
	{
		CalcParams.Radius = PlanetConfig->Radius;
		CalcParams.MinLat = PlanetConfig->MinLat;
		CalcParams.MaxLat = PlanetConfig->MaxLat;
		CalcParams.NumLat = PlanetConfig->NumLat;
		CalcParams.NumLon = PlanetConfig->NumLon;
	}
	if (FarmlandConfig && FarmlandConfig->OutlineMesh)
	{
		CalcParams.OutlineExtent = FarmlandConfig->OutlineMesh->GetBounds().BoxExtent;
		CalcParams.OutlineOffsetZ = FarmlandConfig->OutlineOffsetZ;
	}

	// ---- 并行收集：chunk 在 worker 并行处理，局部缓冲锁合并（样板：CropVisualSyncProcessor）----
	// （GT 强制保留：批量提交段需游戏线程调 ISMManager；收集段由 ParallelForEach 分发 worker）
	TArray<FGlobalInstanceId> PendingRemoves;
	TArray<TPair<TArray<float>, TPair<FTransform, FMassEntityHandle>>> PendingAdds;
	TArray<TPair<FGlobalInstanceId, TArray<float>>> PendingUpdates;
	FCriticalSection CollectLock;
	
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& InContext)
    {
        // 当前 chunk 的局部收集缓冲（worker 并行——无竞争）
        TArray<FGlobalInstanceId> LocalRemoves;
        TArray<TPair<TArray<float>, TPair<FTransform, FMassEntityHandle>>> LocalAdds;
        TArray<TPair<FGlobalInstanceId, TArray<float>>> LocalUpdates;

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
			        CalcParams,
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
						LocalAdds.Emplace(
							CellCustomData,
							TPair<FTransform, FMassEntityHandle>(
								CalculateOutlineInstanceTransform(CalcParams, CellStaticFragment.Transform, CellStaticFragment.Coords.X),
								InContext.GetEntity(i)
								)
							);
					}
					else
					{
						LocalRemoves.Add(HighlightFragment.InstanceId);
					}
		        	HighlightFragment.LastHighlightedState = HighlightFragment.bHighlighted;
		        	HighlightFragment.LastHighlightColor = HighlightFragment.HighlightColor;
		        }
        		
        		// 颜色变化时
		        else if (bColorChanged)
		        {
		        	LocalUpdates.Emplace(HighlightFragment.InstanceId, CellCustomData);
		        	HighlightFragment.LastHighlightColor = HighlightFragment.HighlightColor;
		        }
        	}
        }

        // 本 chunk 局部缓冲合并到共享缓冲（锁保护跨 worker 并发写）
        if (LocalRemoves.Num() > 0 || LocalAdds.Num() > 0 || LocalUpdates.Num() > 0)
        {
        	FScopeLock Lock(&CollectLock);
        	PendingRemoves.Append(MoveTemp(LocalRemoves));
        	PendingAdds.Append(MoveTemp(LocalAdds));
        	PendingUpdates.Append(MoveTemp(LocalUpdates));
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
    	
    		// 轮廓实例坐标（分桶输入：与地块分桶同源格子坐标，分桶后可按视角剔除）
    		TArray<FIntPoint> GridCoords;
    		GridCoords.Reserve(Entities.Num());
    		for (const FMassEntityHandle& Entity : Entities)
    		{
    			const FCellStaticFragment* StaticFrag = EntityManager.GetFragmentDataPtr<FCellStaticFragment>(Entity);
    			GridCoords.Add(StaticFrag ? StaticFrag->Coords : FIntPoint::ZeroValue);
    		}
    	
    	    TArray<FGlobalInstanceId> NewIds = ISMManager->BatchAddInstances(
				FarmlandConfig->OutlineMesh,
				Transforms,
				GridCoords,        // 空间分桶依据：轮廓实例走球面分桶
				0,                 // 逻辑分桶依据：默认组（轮廓不分组）
				CellOutlineCustomData::Count,
				FISMComponentParams(),
				true,              // bWorldSpace：轮廓变换继承自地块世界坐标
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
		ISMManager->BatchUpdateInstanceCustomData(FarmlandConfig->OutlineMesh, Ids, CellsCustomData);
	}
}

TArray<FVector> UCellVisualSyncProcessor::CalculateCellVertexPositions(const FCellOutlineCalcParams& Params, const int32 LatIdx, const int32 LonIdx)
{
	// 自动计算经纬度步长
	const float LatStep = (Params.MaxLat - Params.MinLat) / (Params.NumLat - 1);
	const float LonStep = 360.0f / Params.NumLon;
	
	// 当前地块的经纬度边界
	const float MinLat = Params.MinLat + LatIdx * LatStep;
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
			Params.Radius * CosLat * CosLon,
			Params.Radius * CosLat * SinLon,
			Params.Radius * SinLat
		);
	}
	return TArray<FVector>(VertexPositions, 4);
}

FTransform UCellVisualSyncProcessor::CalculateOutlineInstanceTransform(const FCellOutlineCalcParams& Params,
	const FTransform& CellTransform, const float LatIdx)
{
	// 计算轮廓缩放
	const float LatStep = (Params.MaxLat - Params.MinLat) / (Params.NumLat - 1);
	const float LonStep = 360.0f / Params.NumLon;
	const float LatStepRad = FMath::DegreesToRadians(LatStep);
	const float LonStepRad = FMath::DegreesToRadians(LonStep);
	const float CurrentLatDeg = Params.MinLat + (LatIdx + 0.5f) * LatStep;
	// 当前地块中心纬度（用于计算经度方向宽度）
	const float CurrentLatRad = FMath::DegreesToRadians(CurrentLatDeg);

	// 计算单元格实际尺寸（球面几何）
	const float CellHeight = Params.Radius * LatStepRad; // 南北向
	const float CellWidth = Params.Radius * FMath::Cos(CurrentLatRad) * LonStepRad; // 东西向

	// 获取轮廓网格体自身尺寸（GT 段预提取快照）
	const FVector OutlineExtent = Params.OutlineExtent;

	// 计算轮廓缩放（匹配单元格实际大小）
	const float OutlineScaleX = CellWidth / (OutlineExtent.X * 2.f);
	const float OutlineScaleY = CellHeight / (OutlineExtent.Y * 2.f);
	constexpr float OutlineScaleZ = 1.0f;

	// 创建轮廓实例（使用独立计算的缩放）
	FTransform OutlineInstanceTransform = CellTransform;
	OutlineInstanceTransform.AddToTranslation(FVector(0.f, 0.f, Params.OutlineOffsetZ));
	OutlineInstanceTransform.SetScale3D(FVector(OutlineScaleX, OutlineScaleY, OutlineScaleZ));
	
	return OutlineInstanceTransform;
}
