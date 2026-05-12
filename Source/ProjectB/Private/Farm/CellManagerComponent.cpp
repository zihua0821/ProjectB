#include "Farm/CellManagerComponent.h"
#include "MassCommandBuffer.h"
#include "MassEntityConfigAsset.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassRepresentationSubsystem.h"
#include "Mass/CellMassFragments.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Crops/CropData.h"
#include "Engine/DataTable.h"
#include "Planet/PlanetInterface.h"
#include "Player/BasePlayerController.h"
#include "ProjectB/ProjectB.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Subsystem/ISMManager.h"
#include "MassRepresentation/Public/MassRepresentationFragments.h"

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

	if (UWorld* World = GetWorld())
	{
		// 每 0.5 秒检查一次队列，避免高频轮询
		World->GetTimerManager().SetTimer(MatureQueueProcessTimer,
			this,
			&UCellManagerComponent::ProcessMatureQueue,
			0.5f, true);
	}
}

void UCellManagerComponent::ProcessMatureQueue()
{
	UGlobalConfigSubsystem* ConfigSub = GetWorld()->GetSubsystem<UGlobalConfigSubsystem>();
	if (!ConfigSub) return;

	FIntPoint Coords;
	// 队列是 MPSC 安全的，游戏线程消费无问题
	while (ConfigSub->MatureCropQueue.Dequeue(Coords))
	{
		// 可选：检查地块是否仍然有效
		FMassEntityHandle Entity;
		if (!GetCellEntity(Coords, Entity)) continue;

		// 随机延迟 5~10 秒后收获
		FTimerHandle TimerHandle;
		GetWorld()->GetTimerManager().SetTimer(
			TimerHandle,
			FTimerDelegate::CreateUObject(this, &UCellManagerComponent::OnAutoHarvest, Coords),
			FMath::RandRange(1.0f, 5.0f),
			false
			);
	}
}

void UCellManagerComponent::OnAutoHarvest(FIntPoint Coords)
{
	HarvestCrop(Coords); 

	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateUObject(this, &UCellManagerComponent::OnRemoveCrop, Coords),
		FMath::RandRange(1.0f, 5.0f),
		false
		);
	
}

void UCellManagerComponent::OnRemoveCrop(FIntPoint Coords)
{
	RemoveCrop(Coords);
	
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateUObject(this, &UCellManagerComponent::OnRePlantCrop, Coords),
		FMath::RandRange(1.0f, 5.0f),
		false
		);
}

void UCellManagerComponent::OnRePlantCrop(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	
	FPlotStateFragment* PlotState = EntityManager.GetFragmentDataPtr<FPlotStateFragment>(Entity);
	if (PlotState && PlotState->StateData.HasState(EPlotState::Plowed))
	{
		TArray<FName> CropNames;
		for (auto& RowPair : CropDataTable->GetRowMap())
		{
			const FCropTableRow* Row = reinterpret_cast<FCropTableRow*>(RowPair.Value);
			if (!Row) continue;
			CropNames.Add(Row->CropID);
		}
		const FString RandomCrop = CropNames[FMath::RandRange(0, CropNames.Num() - 1)].ToString();
		// const FString RandomCrop = FString("Corn");
		PlantCrop(Coords, RandomCrop);
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
		ConfigSub->SetConfigs(FarmlandConfig, PlanetConfig, CropDataTable);
		ConfigSub->SetISMAttachParent(this);
	}
	
	CreateMassCell();

	UE_LOG(LogTemp, Log, TEXT("CellManagerComponent initialized: %d cells"), CellEntities.Num());

	PrecreateISMComponents();
}

void UCellManagerComponent::CreateMassCell()
{
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	const int32 NumLatCells = PlanetConfig->NumLat - 1;
	const int32 NumLonCells = PlanetConfig->NumLon;
	const int32 TotalCells = NumLatCells * NumLonCells;

	UMassRepresentationSubsystem* RepresentationSubsystem = GetWorld()->GetSubsystem<UMassRepresentationSubsystem>();
	
	FStaticMeshInstanceVisualizationDesc CellVisualDesc;
	// 配置网格体
	CellVisualDesc.Meshes.AddZeroed(1);
	CellVisualDesc.Meshes[0].Mesh = FarmlandConfig->LandCellMesh;

	FStaticMeshInstanceVisualizationDescHandle VisualHandle = RepresentationSubsystem->FindOrAddStaticMeshDesc(CellVisualDesc);
	
	// 批量创建实体
	TArray<const UScriptStruct*> FragmentTypes;
	FragmentTypes.Add(FCellStaticFragment::StaticStruct());
	FragmentTypes.Add(FHighlightFragment::StaticStruct());
	FragmentTypes.Add(FPlotRenderFragment::StaticStruct());
	FragmentTypes.Add(FMassRepresentationFragment::StaticStruct());
	
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
	
	// 预分配容器
	TArray<FTransform> PlotTransforms;
	TArray<float> AllPlotCustomData;
	
	CellEntities.SetNum(TotalCells);
	PlotTransforms.Reserve(TotalCells);
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

        	FMassRepresentationFragment& RepFragment = EntityManager.GetFragmentDataChecked<FMassRepresentationFragment>(Entity);
        	RepFragment.StaticMeshDescHandle = VisualHandle;
        	RepFragment.PrevLODSignificance = 0.0f;

        	const int32 FlatIndex = LatIdx * NumLonCells + LonIdx;
        	CellEntities[FlatIndex] = Entity;
        	
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
	UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>();
	if (!ISMManager) return;

	if (PlotTransforms.Num() > 0)
	{
		FISMComponentParams PlotISMParams;
		PlotISMParams.AttachParent = this;
		// 地块
		TArray<FGlobalInstanceId> PlotIds = ISMManager->BatchAddInstances(
			FarmlandConfig->LandCellMesh,
			PlotTransforms,
			FarmlandConfig->NumCustomDataFloats,
			PlotISMParams,
			AllPlotCustomData
			);
		for (int32 i = 0; i < PlotIds.Num(); i++)
		{
			const FMassEntityHandle Entity = CellEntities[i];
			FPlotRenderFragment& RenderFrag = EntityManager.GetFragmentDataChecked<FPlotRenderFragment>(Entity);
			RenderFrag.InstanceId = PlotIds[i];
		}
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

void UCellManagerComponent::UpdateOutline(const FIntPoint Coords, const bool bEnable)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	if (bEnable)
	{
		CurrentOutlineCoords = Coords;
	}
	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 构建一个命令，延迟修改实体的高亮 Fragment
	EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
		[&, Entity, bEnable](const FMassEntityManager& EM)
		{
			if (!EM.IsEntityActive(Entity)) return;

			if (FHighlightFragment* HighlightFragment = EM.GetFragmentDataPtr<FHighlightFragment>(Entity))
			{
				HighlightFragment->bHighlighted = bEnable;

				if (bEnable)
				{
					HighlightFragment->HighlightColor = GetOutlineColorForEditMode();
				}
			}
		}
	);
}

bool UCellManagerComponent::GetCellCoordsFromScreenMouse(FIntPoint& OutCellCoords, float& OutWorldDistance) const
{
    if (!PlanetConfig) return false;

    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return false;

    float MouseX, MouseY;
    if (!PC->GetMousePosition(MouseX, MouseY)) return false;

    FVector WorldLoc, WorldDir;
    if (!PC->DeprojectScreenPositionToWorld(MouseX, MouseY, WorldLoc, WorldDir)) return false;

    // 射线与球面求交 (星球中心在原点，半径为 PlanetConfig->Radius)
    const float R = PlanetConfig->Radius;
    const FVector Origin(0, 0, 0);   // 假设星球中心在世界原点，如果不是，需加上星球Actor的位置

    // 二次方程求解: |RayStart + t * RayDir|^2 = R^2
    const float a = FVector::DotProduct(WorldDir, WorldDir);
    const float b = 2.0f * FVector::DotProduct(WorldLoc, WorldDir);
    const float c = FVector::DotProduct(WorldLoc, WorldLoc) - R * R;

    const float Discriminant = b * b - 4.0f * a * c;
    if (Discriminant < 0.0f) return false;   // 未击中球体

    const float t = (-b - FMath::Sqrt(Discriminant)) / (2.0f * a); // 取近处交点
    if (t < 0.0f) return false;   // 交点在射线后面

    FVector Intersection = WorldLoc + t * WorldDir;
    OutWorldDistance = t;

    // 交点转球坐标 (经纬度)
    FVector Dir = Intersection.GetSafeNormal();
    // 纬度: asin(Z / R)，单位弧度
    float LatRad = FMath::Asin(FMath::Clamp(Dir.Z, -1.0f, 1.0f));
    // 经度: atan2(Y, X)，范围 (-pi, pi]
    float LonRad = FMath::Atan2(Dir.Y, Dir.X);

    // 转为角度
    float LatDeg = FMath::RadiansToDegrees(LatRad);
    float LonDeg = FMath::RadiansToDegrees(LonRad);
    // 经度规范化到 [0, 360)
    if (LonDeg < 0.0f) LonDeg += 360.0f;

    // 根据配置的 MinLat, MaxLat, NumLat, NumLon 计算单元格坐标
    const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
    const float LonStep = 360.0f / PlanetConfig->NumLon;

    // 检查是否在纬度范围内
    if (LatDeg < PlanetConfig->MinLat || LatDeg > PlanetConfig->MaxLat)
        return false;   // 超出单元格区域

    int32 LatIdx = FMath::FloorToInt((LatDeg - PlanetConfig->MinLat) / LatStep);
    int32 LonIdx = FMath::FloorToInt(LonDeg / LonStep);

    // 边界保护
    LatIdx = FMath::Clamp(LatIdx, 0, PlanetConfig->NumLat - 2);
    LonIdx = LonIdx % PlanetConfig->NumLon;

    OutCellCoords.X = LatIdx;
    OutCellCoords.Y = LonIdx;
    return true;
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

TArray<FIntPoint> UCellManagerComponent::GetAllCoords()
{
	TArray<FIntPoint> Coords;
	if (!PlanetConfig) return Coords;

	const int32 NumLatCells = PlanetConfig->NumLat - 1;
	const int32 NumLonCells = PlanetConfig->NumLon;
	Coords.Reserve(NumLatCells * NumLonCells);

	for (int32 Lat = 0; Lat < NumLatCells; ++Lat)
	{
		for (int32 Lon = 0; Lon < NumLonCells; ++Lon)
		{
			Coords.Add(FIntPoint(Lat, Lon));
		}
	}
	return Coords;
}

void UCellManagerComponent::Test()
{
	if (!PlanetConfig) return;

	// 1. 获取所有坐标
	TArray<FIntPoint> AllCoords = GetAllCoords();
	if (AllCoords.Num() == 0) return;

	// 2. 批量耕地（全部通过 Defer 入队，帧末执行）
	for (const FIntPoint& Coords : AllCoords)
	{
		PlowLand(Coords); // 直接调用已有的耕地方法
	}

	// 3. 延迟一帧后批量种植
	// 使用 Timer 延迟 0.05 秒（足够让 Defer 命令完成）
	FTimerDelegate PlantDelegate;
	PlantDelegate.BindLambda([this, AllCoords]()
	{
		for (const FIntPoint& Coords : AllCoords)
		{
			WetLand(Coords);
			FertilizeLand(Coords);
			PlantCrop(Coords, BasePlayerController->GetSelectedCropName());
		}
	});

	FTimerHandle TestPlantTimer;
	GetWorld()->GetTimerManager().SetTimer(TestPlantTimer, PlantDelegate, 1.f, false);
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

    const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

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
    EntityManager.Defer().AddFragment<FCropStaticConfigFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropStateFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropGrowthTimerFragment>(Entity);
    EntityManager.Defer().AddFragment<FCropQualityFragment>(Entity);
	EntityManager.Defer().AddFragment<FCropRenderFragment>(Entity);

    // 初始化所有作物数据
    EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
        [Entity, CropID, Row, PlotState](const FMassEntityManager& EM)
        {
            if (!EM.IsEntityActive(Entity)) return;

            // 标识数据
        	const int32 VariantsIndex = FMath::RandRange(0, Row->MeshVariants.Num() - 1);
        	const FCropMeshVariant& Variant = Row->MeshVariants[VariantsIndex];
        	
            FCropStaticConfigFragment& CropStaticConfig = EM.GetFragmentDataChecked<FCropStaticConfigFragment>(Entity);
            CropStaticConfig.CropID = CropID;
        	CropStaticConfig.GrowthSeedTime = Row->GrowthSeedTime;
			CropStaticConfig.GrowthSeedlingTime = Row->GrowthSeedlingTime;
			CropStaticConfig.MoistureConsumePerSecond = Row->MoistureConsumePerSecond;
			CropStaticConfig.FertilityConsumePerSecond = Row->FertilityConsumePerSecond;
        	CropStaticConfig.SeedMesh      = Variant.SeedMesh;
			CropStaticConfig.SeedlingMesh  = Variant.SeedlingMesh;
			CropStaticConfig.MatureMesh    = Variant.MatureMesh;
			CropStaticConfig.HarvestedMesh = Variant.HarvestedMesh;
        	CropStaticConfig.CropNumCustomDataFloats = Row->CropNumCustomDataFloats;

            // 生长阶段与状态
            FCropStateFragment& CropState = EM.GetFragmentDataChecked<FCropStateFragment>(Entity);
            CropState.CropGrowthStage = ECropGrowthStage::Seed;
            CropState.CropStateData.AddState(ECropState::NeedsWater);
            CropState.CropStateData.AddState(ECropState::NeedsFertilizer);

            // 根据地块初始状态调整作物状态
            if (PlotState->StateData.HasState(EPlotState::Watered))
                CropState.CropStateData.RemoveState(ECropState::NeedsWater);
            if (PlotState->StateData.HasState(EPlotState::Fertilized))
                CropState.CropStateData.RemoveState(ECropState::NeedsFertilizer);
            if (PlotState->StateData.HasState(EPlotState::WeedInfested))
                CropState.CropStateData.AddState(ECropState::WeedInfested);
            if (PlotState->StateData.HasState(EPlotState::PestInfested))
                CropState.CropStateData.AddState(ECropState::PestInfested);

        	//-----------------
            // 生长计算随机初始生长时间
            FCropGrowthTimerFragment& Timer = EM.GetFragmentDataChecked<FCropGrowthTimerFragment>(Entity);
            Timer.TotalGrowthTime = FMath::RandRange(0.f, Row->GrowthSeedTime + Row->GrowthSeedlingTime);
            const float SeedlingStart = CropStaticConfig.GrowthSeedTime;
            const float MatureStart = CropStaticConfig.GrowthSeedTime + CropStaticConfig.GrowthSeedlingTime;
            if (Timer.TotalGrowthTime < SeedlingStart)
            {
	            CropState.CropGrowthStage = ECropGrowthStage::Seed;
	            Timer.CurrentStageGrowthTime = Timer.TotalGrowthTime;
            }
            else if (Timer.TotalGrowthTime < MatureStart)
            {
	            CropState.CropGrowthStage = ECropGrowthStage::Seedling;
	            Timer.CurrentStageGrowthTime = Timer.TotalGrowthTime - SeedlingStart;
            }
            else
            {
	            CropState.CropGrowthStage = ECropGrowthStage::Mature;
	            Timer.CurrentStageGrowthTime = Timer.TotalGrowthTime - MatureStart;
            }
            //-----------------
        });
}

bool UCellManagerComponent::HarvestCrop(FIntPoint Coords)
{
	if (!OwnerActor.IsValid()) return  false;
    FMassEntityHandle Entity;
    if (!GetCellEntity(Coords, Entity)) return false;

    FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

    // 检查是否有作物且为成熟阶段
    FCropStateFragment* CropState = EntityManager.GetFragmentDataPtr<FCropStateFragment>(Entity);
    if (!CropState || CropState->CropGrowthStage != ECropGrowthStage::Mature) return false;

    FCropStaticConfigFragment* CropId = EntityManager.GetFragmentDataPtr<FCropStaticConfigFragment>(Entity);
    if (!CropId) return false;

    // 从数据表读取售价配置
    const FCropTableRow* Row = CropDataTable->FindRow<FCropTableRow>(CropId->CropID, TEXT("HarvestCrop"));
    if (!Row) return false;

    // 读取品质数据并计算售价
    FCropQualityFragment* Quality = EntityManager.GetFragmentDataPtr<FCropQualityFragment>(Entity);
    FCropGrowthTimerFragment* Timer = EntityManager.GetFragmentDataPtr<FCropGrowthTimerFragment>(Entity);
    if (!Quality || !Timer) return false;

    float SalePriceMultiplier = 1.0f;
    switch (Quality->QualityData.GetQuality(Timer->TotalGrowthTime))
    {
    case ECropQuality::Poor:      SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Poor; break;
    case ECropQuality::Normal:    SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Normal; break;
    case ECropQuality::Good:      SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Good; break;
    case ECropQuality::Excellent: SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Excellent; break;
    }
    const int32 CropSalePrice = FMath::RoundToInt32(Row->BaseSalePrice * SalePriceMultiplier);

	// 处理星球能量增加
	IPlanetInterface* PlanetInterface = Cast<IPlanetInterface>(OwnerActor.Get());
	auto& [PlanetPower] = PlanetInterface->GetPlanetInfoRef();
	PlanetPower += CropSalePrice;
	IPlanetInterface::Execute_CallPlanetInfoChanged(OwnerActor.Get());

    // 将作物状态改为已收获（渲染处理器会自动处理 Mesh 切换）
    CropState->CropGrowthStage = ECropGrowthStage::Harvested;
    CropState->CropStateData.RemoveState(ECropState::ReadyForHarvest);
	
    return true;
}

void UCellManagerComponent::RemoveCrop(FIntPoint Coords)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(Coords, Entity)) return;

	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 检查是否有作物
	FCropStateFragment* CropState = EntityManager.GetFragmentDataPtr<FCropStateFragment>(Entity);
	if (!CropState) return;

	// 将作物状态改为已收获（渲染处理器会自动处理 Mesh 切换）
	CropState->CropGrowthStage = ECropGrowthStage::None;
}

void UCellManagerComponent::CropDried(FIntPoint Coords)
{
}

void UCellManagerComponent::RemoveCropDried(FIntPoint Coords)
{
}

FLinearColor UCellManagerComponent::GetOutlineColorForEditMode() const
{
	FLinearColor OutlineColor = FLinearColor::White;
	
	UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>();
	if (!ISMManager) return OutlineColor;
	
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
	case EEditMode::RemoveCrop:
		OutlineColor = FLinearColor::Red;
		break;
	default:
		OutlineColor = FLinearColor::White;
	}
	return OutlineColor;
}

void UCellManagerComponent::OnEditModeChanged(EEditMode NewEditMode)
{
	FMassEntityHandle Entity;
	if (!GetCellEntity(CurrentOutlineCoords, Entity)) return;

	const FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
		[Entity, this](const FMassEntityManager& EM)
		{
			if (!EM.IsEntityActive(Entity)) return;

			if (FHighlightFragment* HighlightFragment = EM.GetFragmentDataPtr<FHighlightFragment>(Entity))
			{
				HighlightFragment->HighlightColor = GetOutlineColorForEditMode();
			}
		}
	);
}

void UCellManagerComponent::PrecreateISMComponents()
{
	if (!CropDataTable) return;
	UISMManager* ISMMgr = GetWorld()->GetSubsystem<UISMManager>();
	if (!ISMMgr) return;

	// 创建网格体组件
	FISMComponentParams CellISMParams;
	CellISMParams.AttachParent = this;
	CellISMParams.bDisallowNanite = true;
	CellISMParams.bCastShadow = false;
	ISMMgr->GetOrCreateISMComponent(FarmlandConfig->OutlineMesh, 16, CellISMParams);

	// 创建作物网格体组件
	FISMComponentParams CropParams;
	CropParams.AttachParent = this;
	CropParams.CollisionEnabled = ECollisionEnabled::NoCollision;
	CropParams.bCastShadow = true;
	CropParams.MinDrawDistance = 10000.f;
	CropParams.MaxDrawDistance = 20000.f;

	TMap<UStaticMesh*, int32> UniqueMeshes;
	// 收集所有作物变体中所有阶段的网格体
	for (auto& RowPair : CropDataTable->GetRowMap())
	{
		const FCropTableRow* Row = reinterpret_cast<FCropTableRow*>(RowPair.Value);
		if (!Row) continue;
		int32 CropNumCustomDataFloats = Row->CropNumCustomDataFloats;
		for (const FCropMeshVariant& Variant : Row->MeshVariants)
		{
			if (Variant.SeedMesh) UniqueMeshes.Add(Variant.SeedMesh, CropNumCustomDataFloats);
			if (Variant.SeedlingMesh) UniqueMeshes.Add(Variant.SeedlingMesh, CropNumCustomDataFloats);
			if (Variant.MatureMesh) UniqueMeshes.Add(Variant.MatureMesh, CropNumCustomDataFloats);
			if (Variant.HarvestedMesh) UniqueMeshes.Add(Variant.HarvestedMesh, CropNumCustomDataFloats);
		}
	}
	// 为每个唯一网格创建 ISM 组件
	for (const auto& Pair : UniqueMeshes)
	{
		ISMMgr->GetOrCreateISMComponent(Pair.Key, Pair.Value, CropParams);
	}
}
