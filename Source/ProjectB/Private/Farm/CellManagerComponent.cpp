// CellManagerComponent.cpp
#include "Farm/CellManagerComponent.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass/CellMassFragments.h"
// #include "FarmGlobalConfigSubsystem.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Subsystem/ISMPoolManager.h"
#include "Crops/CropData.h"
#include "Engine/DataTable.h"
#include "Player/BasePlayerController.h"
#include "ProjectB/ProjectB.h"

UCellManagerComponent::UCellManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // 不再需要 Tick
}

void UCellManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	// 初始化会在外部调用 Initialize()，此处留空

	BasePlayerController = Cast<ABasePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BasePlayerController)
	{
		BasePlayerController->OnEditModeChanged.AddDynamic(this, &ThisClass::OnEditModeChanged);
	}
}

void UCellManagerComponent::Initialize(UPlanetConfigDataAsset* InPlanetConfig, UFarmlandConfigDataAsset* InFarmConfig, UDataTable* InCropTable)
{
	if (!InPlanetConfig || !InFarmConfig)
	{
		UE_LOG(LogTemp, Error, TEXT("CellManagerComponent::Initialize - Invalid configs"));
		return;
	}

	PlanetConfig = InPlanetConfig;
	FarmlandConfig = InFarmConfig;
	CropDataTable = InCropTable;
	
	CreateMassCell();

	UE_LOG(LogTemp, Log, TEXT("CellManagerComponent initialized: %d cells"), CellEntities.Num());
}

void UCellManagerComponent::CreateMassCell()
{
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	const int32 NumLatCells = PlanetConfig->NumLat - 1;
	const int32 NumLonCells = PlanetConfig->NumLon;
	const int32 TotalCells = NumLatCells * NumLonCells;
	
	// 批量创建实体
	TArray<const UScriptStruct*> FragmentTypes;
	FragmentTypes.Add(FCellStaticFragment::StaticStruct());
	FMassArchetypeHandle ArchetypeHandle = EntityManager.CreateArchetype(FragmentTypes);
	TArray<FMassEntityHandle> Entities;
	EntityManager.BatchCreateEntities(
		ArchetypeHandle,
		TotalCells,
		Entities
	);
	
    // 计算缩放因子
	const FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
	const FVector CellExtent = CellBounds.BoxExtent;
	const float BaseScaleFactorX = FarmlandConfig->CellSize.X / (CellExtent.X * 2.f);
	const float ScaleFactorY = FarmlandConfig->CellSize.Y / (CellExtent.Y * 2.f);
	const float ScaleFactorZ = FarmlandConfig->LandHeightOffset / (CellExtent.Z * 2.f);

	// 纬度收缩参数
	const float MinLatitudeScale = FarmlandConfig->MinLatitudeScale;  // 边缘最小缩放系数（Y 轴缩到 70%）

    // 自动计算经纬度步长
    const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
    const float LonStep = 360.0f / PlanetConfig->NumLon;

	// 转换为弧度
	const float LatStepRad = FMath::DegreesToRadians(LatStep);
	const float LonStepRad = FMath::DegreesToRadians(LonStep);
	
    
	// 预分配容器
	TArray<FTransform> OutlineTransforms;
	TArray<float> AllCustomData;
	CellEntities.SetNum(TotalCells);
	OutlineTransforms.Reserve(TotalCells);
	AllCustomData.Reserve(TotalCells * 16);
	
    // 遍历经纬度网格生成地块
    for (int32 LatIdx = 0; LatIdx <NumLatCells; LatIdx++)
    {
        for (int32 LonIdx = 0; LonIdx < NumLonCells; LonIdx++)
        {
        	const int32 EntityIndex = LatIdx * NumLonCells + LonIdx;
        	const FMassEntityHandle Entity = Entities[EntityIndex];
        	
            // 将索引转换为弧度
        	const float CenterLatRad = FMath::DegreesToRadians(PlanetConfig->MinLat + (LatIdx + 0.5f) * LatStep);
        	const float CenterLonRad = FMath::DegreesToRadians((LonIdx + 0.5f) * LonStep);

            // 球面坐标转世界坐标（Z 轴向上）
            const float CenterCosLat = FMath::Cos(CenterLatRad);
            const float CenterSinLat = FMath::Sin(CenterLatRad);
            const float CenterCosLon = FMath::Cos(CenterLonRad);
            const float CenterSinLon = FMath::Sin(CenterLonRad);

            FVector PlotLocation(
                PlanetConfig->Radius * CenterCosLat * CenterCosLon,
                PlanetConfig->Radius * CenterCosLat * CenterSinLon,
                PlanetConfig->Radius * CenterSinLat
            );

        	// 计算地块朝向：让 Z 轴指向球心外（径向）
        	const FVector NormalDirection = PlotLocation.GetSafeNormal();
        	PlotLocation += NormalDirection * FarmlandConfig->CellOffsetZ;
        	const FRotator PlotRotation = FRotationMatrix::MakeFromZ(NormalDirection).Rotator();

        	// 计算纬度缩放系数（基于纬度角，Z 轴是上下方向）
        	const float CurrentLatDeg = PlanetConfig->MinLat + (LatIdx + 0.5f) * LatStep;
        	const float AbsLatFromEquator = FMath::Abs(CurrentLatDeg);  // 距离赤道的纬度
        	const float MaxLatFromEquator = FMath::Max(FMath::Abs(PlanetConfig->MinLat), FMath::Abs(PlanetConfig->MaxLat));
        	const float LatitudeFactor = FMath::Clamp(AbsLatFromEquator / MaxLatFromEquator, 0.f, 1.f);
        	const float LatitudeScale = FMath::Lerp(1.0f, MinLatitudeScale, LatitudeFactor);

        	// 应用纬度收缩（只缩 Y 轴）
        	FTransform CellTransform;
        	CellTransform.SetLocation(PlotLocation);
        	CellTransform.SetRotation(PlotRotation.Quaternion());
        	CellTransform.SetScale3D(FVector(BaseScaleFactorX * LatitudeScale, ScaleFactorY, ScaleFactorZ));
        	
        	// 写入静态 Fragment
        	FCellStaticFragment& StaticFrag = EntityManager.GetFragmentDataChecked<FCellStaticFragment>(Entity);
        	StaticFrag.Coords = FIntPoint(LatIdx, LonIdx);
        	StaticFrag.Transform = CellTransform;
        	StaticFrag.CellType = ECellType::Empty;

        	// 根据配置默认创建农场地块
        	if (FarmlandConfig->bInitAllLandCells)
        	{
        		EntityManager.AddTagToEntity(Entity, FCropPlotTag::StaticStruct());
        		EntityManager.AddFragmentToEntity(
					Entity, FPlotStateFragment::StaticStruct(),
					[](void* Fragment, const UScriptStruct&)
					{
						static_cast<FPlotStateFragment*>(Fragment)->StateData.AddState(EPlotState::Plowed);
					});
        		StaticFrag.CellType = ECellType::Crop;
        	}

        	const int32 FlatIndex = LatIdx * NumLonCells + LonIdx;
        	CellEntities[FlatIndex] = Entity;
        	
        	// ==================== 计算地块四个顶点（用于 ISM 高亮） ====================
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

        	// 转换到组件局部空间
        	FVector LocalVertexPositions[4];
        	for (int32 i = 0; i < 4; i++)
        	{
        		LocalVertexPositions[i] = GetComponentTransform().InverseTransformPosition(VertexPositions[i]);
        	}
        	
        	// 创建轮廓
        	// 当前地块中心纬度（用于计算经度方向宽度）
        	const float CurrentLatRad = FMath::DegreesToRadians(CurrentLatDeg);

        	// 计算单元格实际尺寸（球面几何）
        	const float CellHeight = PlanetConfig->Radius * LatStepRad;                    // 南北向
        	const float CellWidth = PlanetConfig->Radius * FMath::Cos(CurrentLatRad) * LonStepRad;  // 东西向

        	// 获取轮廓网格体自身尺寸
        	FBoxSphereBounds OutlineBounds = FarmlandConfig->OutlineMesh->GetBounds();
        	const FVector OutlineExtent = OutlineBounds.BoxExtent;

        	// 计算轮廓缩放（匹配单元格实际大小）
        	const float OutlineScaleX = CellWidth / (OutlineExtent.X * 2.f);
        	const float OutlineScaleY = CellHeight / (OutlineExtent.Y * 2.f);
        	const float OutlineScaleZ = 1.0f;

        	// 创建轮廓实例（使用独立计算的缩放）
        	FTransform OutlineInstanceTransform = CellTransform;
        	OutlineInstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
        	OutlineInstanceTransform.SetScale3D(FVector(OutlineScaleX, OutlineScaleY, OutlineScaleZ));

        	// 收集数据
        	OutlineTransforms.Add(OutlineInstanceTransform);

        	TArray<float> CustomData =
        	{
        		0.f,										  // Opacity
        		1.f,										  // R
        		1.f,										  // G
        		1.f,										  // B
        		static_cast<float>(LocalVertexPositions[0].X),// X00
        		static_cast<float>(LocalVertexPositions[0].Y),// Y00
        		static_cast<float>(LocalVertexPositions[0].Z),// Z00
        		static_cast<float>(LocalVertexPositions[1].X),// X01
        		static_cast<float>(LocalVertexPositions[1].Y),// Y01
        		static_cast<float>(LocalVertexPositions[1].Z),// Z01
        		static_cast<float>(LocalVertexPositions[2].X),// X10
        		static_cast<float>(LocalVertexPositions[2].Y),// Y10
        		static_cast<float>(LocalVertexPositions[2].Z),// Z10
        		static_cast<float>(LocalVertexPositions[3].X),// X11
        		static_cast<float>(LocalVertexPositions[3].Y),// Y11
        		static_cast<float>(LocalVertexPositions[3].Z),// Z11
        	};
        	AllCustomData.Append(CustomData);
        }
    }

	// ==================== 批量添加 ISM 实例 ====================
	if (OutlineTransforms.Num() > 0)
	{
		UISMPoolManager* PoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();
		if (!PoolManager) return;
		
		FISMComponentParams ISMParams;
		ISMParams.AttachParent = this;
		ISMParams.CollisionObjectType = ECC_Cell;
		ISMParams.CollisionEnabled = ECollisionEnabled::QueryOnly;
		ISMParams.bDisallowNanite = true;
		ISMParams.bCastShadow = false;
		
		PoolManager->BatchAddInstances(FarmlandConfig->OutlineMesh, OutlineTransforms, 16, ISMParams, AllCustomData);
	}
}

void UCellManagerComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
{
	switch (EditMode)
	{
	case Reset:
		ResetLand(Coords);
		break;
	case Plow:
		PlowLand(Coords);
		break;
	case UnPlow:
		RemoveLand(Coords);
		break;
	case Wet:
		WetLand(Coords);
		break;
	case UnWet:
		LandDried(Coords);
		break;
	case Fertilize:
		FertilizeLand(Coords);
		break;
	case UnFertilize:
		ClearFertilize(Coords);
		break;
	case Weed:
		GenerateWeeds(Coords);
		break;
	case UnWeed:
		ClearWeeds(Coords);
		break;
	case Pest:
		GeneratePest(Coords);
		break;
	case UnPest:
		ClearPest(Coords);
		break;
	default:
		break;
	}
}

TArray<FFarmLandPlotSaveGameData> UCellManagerComponent::GetSaveData() const
{
	TArray<FFarmLandPlotSaveGameData> SaveData;
	// 遍历所有实体，序列化 Fragment 到 SaveData
	// 详细实现可后续补充
	return SaveData;
}

void UCellManagerComponent::ApplySaveData(const TArray<FFarmLandPlotSaveGameData>& Data)
{
	// 加载数据后修改对应实体 Fragment
}

void UCellManagerComponent::UpdateOutline(FIntPoint Coords, bool bEnable)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;
	
	UISMPoolManager* PoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();
	if (!PoolManager) return;

	const int32 InstanceIndex = Coords.X * PlanetConfig->NumLon + Coords.Y;
	if (bEnable && BasePlayerController->GetEditMode() != EEditMode::None)
	{
		CurrentOutlineCoords = Coords;
		PoolManager->UpdateInstanceCustomData(FarmlandConfig->OutlineMesh, InstanceIndex, CellOutlineCustomData::Opacity, 0.1f);
		UpdateOutlineColor(Coords);
	}
	else
	{
		CurrentOutlineCoords = FIntPoint(-1, -1);
		PoolManager->UpdateInstanceCustomData(FarmlandConfig->OutlineMesh, InstanceIndex, CellOutlineCustomData::Opacity, 0.f);
	}
}

bool UCellManagerComponent::GetCellCoordsByRaycast(const FVector& RayStart, const FVector& RayEnd,
	FIntPoint& OutCellCoords, float& OutHitDistance) const
{
	UISMPoolManager* PoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();
	if (!PoolManager) return false;

	const FVector NewRayEnd = FMath::Lerp(RayStart, RayEnd, 1.1f);
	// 使用 ISM 的 HitTest 进行精确检测
	FHitResult HitResult;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FarmlandClick), false);
    
	// 执行射线检测（针对 ISM 组件）
	if (PoolManager->LineTraceISMInstances(FarmlandConfig->OutlineMesh, HitResult, RayStart, NewRayEnd, QueryParams))
	{
		OutHitDistance = HitResult.Distance;
        
		if (HitResult.Item >= 0 && HitResult.Item < CellEntities.Num())
		{
			// 通过扁平索引计算坐标
			OutCellCoords.X = HitResult.Item / PlanetConfig->NumLon; // LatIdx
			OutCellCoords.Y = HitResult.Item % PlanetConfig->NumLon; // LonIdx
			return true;
		}
	}
    
	return false;
}

bool UCellManagerComponent::GetCellEntity(const FIntPoint Coords, FMassEntityHandle& OutEntity)
{
	if (Coords.X < 0.f || Coords.Y < 0.f) return false;
	
	const int32 FlatIndex = Coords.X * PlanetConfig->NumLon + Coords.Y;
	if (CellEntities.IsValidIndex(FlatIndex) && CellEntities[FlatIndex].IsValid())
	{
		OutEntity = CellEntities[FlatIndex];
		return true;
	}
	
	return false;
}

void UCellManagerComponent::ResetLand(FIntPoint Coords)
{
}

void UCellManagerComponent::PlowLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	const FMassArchetypeHandle Archetype = EntityManager.GetArchetypeForEntity(Entity);
	if (!Archetype.IsValid()) return;
	const FMassArchetypeCompositionDescriptor& Composition = EntityManager.GetArchetypeComposition(Archetype);
	if (Composition.Tags.Contains(*FCropPlotTag::StaticStruct())) return;

	// 添加 CropPlotTag
	EntityManager.AddTagToEntity(Entity, FCropPlotTag::StaticStruct());
	EntityManager.AddFragmentToEntity(
			Entity, FPlotStateFragment::StaticStruct(),
			[](void* Fragment, const UScriptStruct&)
			{
				static_cast<FPlotStateFragment*>(Fragment)->StateData.AddState(EPlotState::Plowed);
			});
	// 更新 CellType
	EntityManager.GetFragmentDataChecked<FCellStaticFragment>(Entity).CellType = ECellType::Crop;
}

void UCellManagerComponent::RemoveLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	const FMassArchetypeHandle Archetype = EntityManager.GetArchetypeForEntity(Entity);
	if (!Archetype.IsValid()) return;
	const FMassArchetypeCompositionDescriptor& Composition = EntityManager.GetArchetypeComposition(Archetype);
	if (!Composition.Tags.Contains(*FCropPlotTag::StaticStruct())) return;

	// 如果有作物则不允许移除耕地
	if (Composition.Tags.Contains(*FCropTag::StaticStruct())) return;
	
	EntityManager.RemoveTagFromEntity(Entity, FCropPlotTag::StaticStruct());
	EntityManager.RemoveFragmentFromEntity(Entity, FPlotStateFragment::StaticStruct());
	EntityManager.GetFragmentDataChecked<FCellStaticFragment>(Entity).CellType = ECellType::Empty;
}

void UCellManagerComponent::WetLand(FIntPoint Coords)
{
}

void UCellManagerComponent::LandDried(FIntPoint Coords)
{
}

void UCellManagerComponent::FertilizeLand(FIntPoint Coords)
{
}

void UCellManagerComponent::ClearFertilize(FIntPoint Coords)
{
}

void UCellManagerComponent::GenerateWeeds(FIntPoint Coords)
{
}

void UCellManagerComponent::ClearWeeds(FIntPoint Coords)
{
}

void UCellManagerComponent::GeneratePest(FIntPoint Coords)
{
}

void UCellManagerComponent::ClearPest(FIntPoint Coords)
{
}

void UCellManagerComponent::UpdateOutlineColor(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;
	
	UISMPoolManager* PoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();
	if (!PoolManager) return;

	FLinearColor OutlineColor = FLinearColor::Green;
	switch (BasePlayerController->GetEditMode()) {
	case None:
		OutlineColor = FLinearColor::White;
		break;
	case Reset:
		OutlineColor = FLinearColor::Red;
		break;
	case Plow:
		OutlineColor = FLinearColor::Yellow;
		break;
	case UnPlow:
		OutlineColor = FLinearColor::Red;
		break;
	case Wet:
		OutlineColor = FLinearColor::Blue;
		break;
	case UnWet:
		OutlineColor = FLinearColor::Red;
		break;
	case Fertilize:
		OutlineColor = FLinearColor::Gray;
		break;
	case UnFertilize:
		OutlineColor = FLinearColor::Red;
		break;
	case Weed:
		OutlineColor = FLinearColor::Green;
		break;
	case UnWeed:
		OutlineColor = FLinearColor::Red;
		break;
	case Pest:
		OutlineColor = FLinearColor::Gray;
		break;
	case UnPest:
		OutlineColor = FLinearColor::Red;
		break;
	case EEditMode::PlantCrop:
		OutlineColor = FLinearColor::Yellow;
		break;
	case EEditMode::HarvestCrop:
		OutlineColor = FLinearColor::Yellow;
		break;
	case EEditMode::CropDried:
		OutlineColor = FLinearColor::Gray;
		break;
	case EEditMode::UnCropDried:
		OutlineColor = FLinearColor::Red;
		break;
	case RemoveCrop:
		OutlineColor = FLinearColor::Red;
		break;
	case Count:
		break;
	}

	const int32 InstanceIndex = Coords.X * PlanetConfig->NumLon + Coords.Y;
	PoolManager->UpdateInstanceCustomData(FarmlandConfig->OutlineMesh, InstanceIndex, CellOutlineCustomData::R, OutlineColor.R);
	PoolManager->UpdateInstanceCustomData(FarmlandConfig->OutlineMesh, InstanceIndex, CellOutlineCustomData::G, OutlineColor.G);
	PoolManager->UpdateInstanceCustomData(FarmlandConfig->OutlineMesh, InstanceIndex, CellOutlineCustomData::B, OutlineColor.B);

}

void UCellManagerComponent::OnEditModeChanged(EEditMode NewEditMode)
{
	UpdateOutlineColor(CurrentOutlineCoords);
}
