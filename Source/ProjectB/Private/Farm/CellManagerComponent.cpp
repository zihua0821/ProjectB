#include "Farm/CellManagerComponent.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Mass/CellMassFragments.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Subsystem/ISMPoolManager.h"
#include "Crops/CropData.h"
#include "Engine/DataTable.h"
#include "Planet/PlanetInterface.h"
#include "Player/BasePlayerController.h"
#include "ProjectB/ProjectB.h"
#include "Subsystem/GlobalConfigSubsystem.h"

UCellManagerComponent::UCellManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // 不再需要 Tick
}

void UCellManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerActor = GetOwner();

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
	
	if (UGlobalConfigSubsystem* ConfigSub = GetWorld()->GetSubsystem<UGlobalConfigSubsystem>())
	{
		ConfigSub->SetConfigs(FarmlandConfig, PlanetConfig);
	}
	
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
	// FragmentTypes.Add(FPlotStateFragment::StaticStruct());
	// FragmentTypes.Add(FCropPlotTag::StaticStruct());
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
	TArray<FTransform> PlotTransforms;
	TArray<float> AllCellCustomData;
	TArray<float> AllPlotCustomData;
	
	CellEntities.SetNum(TotalCells);
	OutlineTransforms.Reserve(TotalCells);
	PlotTransforms.Reserve(TotalCells);
	AllCellCustomData.Reserve(TotalCells * 16);
	AllPlotCustomData.Reserve(TotalCells * FarmlandConfig->NumCustomDataFloats);
	
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

        	PlotTransforms.Add(CellTransform);
        	
        	// 写入静态 Fragment
        	FCellStaticFragment& StaticFrag = EntityManager.GetFragmentDataChecked<FCellStaticFragment>(Entity);
        	StaticFrag.Coords = FIntPoint(LatIdx, LonIdx);
        	StaticFrag.Transform = CellTransform;
        	StaticFrag.CellType = ECellType::Empty;

        	// FPlotStateFragment& PlotState = EntityManager.GetFragmentDataChecked<FPlotStateFragment>(Entity);
        	// // 根据配置默认创建农场地块
        	// if (FarmlandConfig->bInitAllLandCells)
        	// {
        	// 	PlotState.StateData.AddState(EPlotState::Plowed);
        	// 	StaticFrag.CellType = ECellType::Crop;
        	// }

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

        	TArray<float> CellCustomData =
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
        	AllCellCustomData.Append(CellCustomData);

        	TArray<float> PlotCustomData =
			{
        		0.f,										  // Opacity
				0.f,										  // Wetness
				0.f,										  // Fertility
			};
        	AllPlotCustomData.Append(PlotCustomData);
        }
    }

	// ==================== 批量添加 ISM 实例 ====================
	UISMPoolManager* PoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();
	if (!PoolManager) return;
	
	if (OutlineTransforms.Num() > 0)
	{
		FISMComponentParams CellISMParams;
		CellISMParams.AttachParent = this;
		CellISMParams.CollisionObjectType = ECC_Cell;
		CellISMParams.CollisionEnabled = ECollisionEnabled::QueryOnly;
		CellISMParams.bDisallowNanite = true;
		CellISMParams.bCastShadow = false;

		// 单元格
		PoolManager->BatchAddInstances(FarmlandConfig->OutlineMesh, OutlineTransforms, 16, CellISMParams, AllCellCustomData);
	}

	if (PlotTransforms.Num() > 0)
	{
		FISMComponentParams PlotISMParams;
		PlotISMParams.AttachParent = this;
		// 地块
		PoolManager->BatchAddInstances(FarmlandConfig->LandCellMesh, PlotTransforms, FarmlandConfig->NumCustomDataFloats, PlotISMParams, AllPlotCustomData);
	}
}

void UCellManagerComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
{
	switch (EditMode)
	{
	case EEditMode::None:
		break;
	case EEditMode::Reset:
		ResetLand(Coords);
		break;
	case EEditMode::Plow:
		PlowLand(Coords);
		break;
	case EEditMode::UnPlow:
		RemoveLand(Coords);
		break;
	case EEditMode::Wet:
		WetLand(Coords);
		break;
	case EEditMode::UnWet:
		LandDried(Coords);
		break;
	case EEditMode::Fertilize:
		FertilizeLand(Coords);
		break;
	case EEditMode::UnFertilize:
		ClearFertilize(Coords);
		break;
	case EEditMode::Weed:
		GenerateWeeds(Coords);
		break;
	case EEditMode::UnWeed:
		ClearWeeds(Coords);
		break;
	case EEditMode::Pest:
		GeneratePest(Coords);
		break;
	case EEditMode::UnPest:
		ClearPest(Coords);
		break;
	case EEditMode::PlantCrop:
		PlantCrop(Coords, BasePlayerController->GetSelectedCropName());
		break;
	case EEditMode::HarvestCrop:
		HarvestCrop(Coords);
		break;
	case EEditMode::CropDried:
		CropDried(Coords);
		break;
	case EEditMode::UnCropDried:
		RemoveCropDried(Coords);
		break;
	case EEditMode::RemoveCrop:
		RemoveCrop(Coords);
		break;
	case EEditMode::Count:
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
	ClearWeeds(Coords);
	ClearPest(Coords);
	RemoveLand(Coords);
}

void UCellManagerComponent::PlowLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	if (EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity)) return;
	
	EntityManager.Defer().AddFragment<FPlotStateFragment>(Entity);
	
	EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
	   [Entity](const FMassEntityManager& EM)
	   {
		   if (!EM.IsEntityActive(Entity)) return;

		   FPlotStateFragment& State = EM.GetFragmentDataChecked<FPlotStateFragment>(Entity);
		   if (!State.StateData.HasState(EPlotState::Plowed))
		   {
			   State.StateData.AddState(EPlotState::Plowed);
			   EM.GetFragmentDataChecked<FCellStaticFragment>(Entity).CellType = ECellType::Crop;
		   }
	   }
   );
}

void UCellManagerComponent::RemoveLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;
	
	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	const FMassArchetypeHandle Archetype = EntityManager.GetArchetypeForEntity(Entity);
	if (!Archetype.IsValid()) return;
	const FMassArchetypeCompositionDescriptor& Composition = EntityManager.GetArchetypeComposition(Archetype);

	// 如果有作物则不允许移除耕地
	if (Composition.Tags.Contains(*FCropTag::StaticStruct())) return;

	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;
	
	PlotStateFragment->StateData.RemoveState(EPlotState::Plowed);

	// 恢复单元格类型为空地
	EntityManager.GetFragmentDataChecked<FCellStaticFragment>(Entity).CellType = ECellType::Empty;
}

void UCellManagerComponent::WetLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->MoisturePercentage = FMath::Clamp(PlotStateFragment->MoisturePercentage + FarmlandConfig->IncreasedMoistureWithEachIrrigation, 0.0f, 1.0f);
	PlotStateFragment->StateData.AddState(EPlotState::Watered);
}

void UCellManagerComponent::LandDried(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;

	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->StateData.RemoveState(EPlotState::Watered);
}

void UCellManagerComponent::FertilizeLand(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->FertilityPercentage = FMath::Clamp(PlotStateFragment->FertilityPercentage + FarmlandConfig->IncreasedFertilityWithEachFertilization, 0.0f, 1.0f);
	PlotStateFragment->StateData.AddState(EPlotState::Fertilized);
}

void UCellManagerComponent::ClearFertilize(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->StateData.RemoveState(EPlotState::Fertilized);
}

void UCellManagerComponent::GenerateWeeds(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;

	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	if (PlotStateFragment->StateData.HasState(EPlotState::WeedInfested)) return;

	PlotStateFragment->StateData.AddState(EPlotState::WeedInfested);
}

void UCellManagerComponent::ClearWeeds(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->StateData.RemoveState(EPlotState::WeedInfested);
}

void UCellManagerComponent::GeneratePest(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;

	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	if (PlotStateFragment->StateData.HasState(EPlotState::PestInfested)) return;

	PlotStateFragment->StateData.AddState(EPlotState::PestInfested);
}

void UCellManagerComponent::ClearPest(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotStateFragment = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotStateFragment == nullptr) return;
	
	if (!PlotStateFragment->StateData.HasState(EPlotState::Plowed)) return;

	PlotStateFragment->StateData.RemoveState(EPlotState::PestInfested);
}

void UCellManagerComponent::PlantCrop(FIntPoint Coords, const FString& CropName)
{
	if (!OwnerActor.IsValid()) return;
    FMassEntityHandle Entity;
    if (!GetCellEntity(Coords, Entity)) return;

    FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

    // 检查地块是否可种植：必须有 FPlotStateFragment 且已耕地，且没有作物
    FPlotStateFragment* PlotState = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
    if (!PlotState || !PlotState->StateData.HasState(EPlotState::Plowed)) return;

    // 检查是否已有作物
    if (EntityManager.GetFragmentDataPtr<FCropStateFragment>(Entity)) return;

    // 从数据表读取作物配置
    FName CropID = FName(CropName);
    const FCropTableRow* Row = CropDataTable->FindRow<FCropTableRow>(CropID, TEXT("PlantCrop"));
    if (!Row) return;

	// 处理星球能量消耗
	IPlanetInterface* PlanetInterface = Cast<IPlanetInterface>(OwnerActor.Get());
	auto& [PlanetPower] = PlanetInterface->GetPlanetInfoRef();
	if (PlanetPower < Row->SeedPrice) return;
	PlanetPower -= Row->SeedPrice;
	IPlanetInterface::Execute_CallPlanetInfoChanged(OwnerActor.Get());
	
    // 通过命令缓冲添加作物 Tag 和全部 Fragment
    EntityManager.Defer().AddFragment<FCropIdentifierFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropStateFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropGrowthTimerFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropQualityFragment>(Entity);

    // 初始化所有作物数据
    EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
        [Entity, CropID, Row, PlotState](FMassEntityManager& EM)
        {
            if (!EM.IsEntityActive(Entity)) return;

            // 标识数据
            FCropIdentifierFragment& Id = EM.GetFragmentDataChecked<FCropIdentifierFragment>(Entity);
            Id.CropID = CropID;
            Id.VariantsIndex = FMath::RandRange(0, Row->MeshVariants.Num() - 1);

            // 生长阶段与状态
            FCropStateFragment& CropState = EM.GetFragmentDataChecked<FCropStateFragment>(Entity);
            CropState.CropGrowthStage = ECropGrowthStage::Seed;
            CropState.CropStateData.Reset();
            CropState.CropStateData.AddState(ECropState::NeedsWater);
            CropState.CropStateData.AddState(ECropState::NeedsFertilizer);

            // 根据地块初始状态调整作物状态
        	
        	// 捕获地块初始状态（在命令执行时读取，确保正确）
			const bool bInitiallyWatered = PlotState->StateData.HasState(EPlotState::Watered);
			const bool bInitiallyFertilized = PlotState->StateData.HasState(EPlotState::Fertilized);
			const bool bHasWeeds = PlotState->StateData.HasState(EPlotState::WeedInfested);
			const bool bHasPests = PlotState->StateData.HasState(EPlotState::PestInfested);
            if (bInitiallyWatered)
                CropState.CropStateData.RemoveState(ECropState::NeedsWater);
            if (bInitiallyFertilized)
                CropState.CropStateData.RemoveState(ECropState::NeedsFertilizer);
            if (bHasWeeds)
                CropState.CropStateData.AddState(ECropState::WeedInfested);
            if (bHasPests)
                CropState.CropStateData.AddState(ECropState::PestInfested);

            // 生长计时
        	// 计算随机初始生长时间
        	const float InitialGrowthTime = FMath::RandRange(0.f, Row->GrowthSeedTime + Row->GrowthSeedlingTime);

            FCropGrowthTimerFragment& Timer = EM.GetFragmentDataChecked<FCropGrowthTimerFragment>(Entity);
            Timer.TotalGrowthTime = InitialGrowthTime;
            Timer.CurrentStageGrowthTime = InitialGrowthTime;

            // 品质数据
            FCropQualityFragment& Quality = EM.GetFragmentDataChecked<FCropQualityFragment>(Entity);
            Quality.QualityData.Reset();
        });
}

bool UCellManagerComponent::HarvestCrop(FIntPoint Coords)
{
	return false;
}

void UCellManagerComponent::RemoveCrop(FIntPoint Coords)
{
}

void UCellManagerComponent::CropDried(FIntPoint Coords)
{
}

void UCellManagerComponent::RemoveCropDried(FIntPoint Coords)
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
	default:
		OutlineColor = FLinearColor::White;
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
