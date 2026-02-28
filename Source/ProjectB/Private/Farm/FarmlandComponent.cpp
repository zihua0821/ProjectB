// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Algo/RandomShuffle.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Player/BasePlayerController.h"
#include "Tools/ISMTools.h"


UFarmlandComponent::UFarmlandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	
}

void UFarmlandComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
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

void UFarmlandComponent::ResetLand(const FIntPoint Coords)
{
	ClearWeeds(Coords);
	ClearPest(Coords);
	RemoveLand(Coords);
}

void UFarmlandComponent::RemoveLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	if (!UISMTools::RemoveInstanceByTransform(LandCellRenderer, PlotInfo.Transform))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	
	PlotInfo.StateData.Reset();
	PlotInfo.MoisturePercentage = 0.f;
	PlotInfo.FertilityPercentage = 0.f;
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::PlowLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (!PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not empty"));
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"));
		return;
	}
	if (int32 PlotIndex; UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot already exists"));
		return;
	}

	const int32 Index = LandCellRenderer->AddInstance(PlotInfo.Transform);
	LandCellRenderer->SetCustomDataValue(Index, PlotCustomData::Opacity, 1.f);
	SetPlotState(Coords, EPlotState::Plowed, true);
}

void UFarmlandComponent::WetLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	LandCellRenderer->SetCustomDataValue(PlotIndex, PlotCustomData::Wetness, 1.f);
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage + FarmlandConfig->IncreasedMoistureWithEachIrrigation, 0.f, 1.f);
	OnPlotMoistureChanged.Broadcast(Coords, PlotInfo.MoisturePercentage);
	SetPlotState(Coords, EPlotState::Watered, true);
}

void UFarmlandComponent::LandDried(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::Watered))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not watered"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	LandCellRenderer->SetCustomDataValue(PlotIndex, PlotCustomData::Wetness, 0.f);
	SetPlotState(Coords, EPlotState::Watered, false);
}

void UFarmlandComponent::FertilizeLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	LandCellRenderer->SetCustomDataValue(PlotIndex, PlotCustomData::Fertility, 1.f);
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage + FarmlandConfig->IncreasedFertilityWithEachFertilization, 0.f, 1.f);
	OnPlotFertilityChanged.Broadcast(Coords, PlotInfo.FertilityPercentage);
	SetPlotState(Coords, EPlotState::Fertilized, true);
}

void UFarmlandComponent::ClearFertilize(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::Fertilized))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not Fertilized"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	LandCellRenderer->SetCustomDataValue(PlotIndex, PlotCustomData::Fertility, 0.f);
	SetPlotState(Coords, EPlotState::Fertilized, false);
}

void UFarmlandComponent::GenerateWeeds(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	// if (PlotInfo.StateData.IsEmpty())
	// {
	// 	UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
	// 	return;
	// }
	if (PlotInfo.StateData.HasState(EPlotState::WeedInfested))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already WeedInfested"))
		return;
	}
	
	if (WeedsRenderer == nullptr)
	{
		WeedsRenderer = NewObject<UInstancedStaticMeshComponent>(GetOwner());
		WeedsRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		WeedsRenderer->RegisterComponent();
		WeedsRenderer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeedsRenderer->SetCollisionResponseToAllChannels(ECR_Ignore);
		check(FarmlandConfig->WeedsMesh);
		WeedsRenderer->SetStaticMesh(FarmlandConfig->WeedsMesh);
	}

	if (PlotInfo.WeedsTransform.Num() > 0)
	{
		for (FTransform& Transform : PlotInfo.WeedsTransform)
		{
			WeedsRenderer->AddInstance(Transform);
		}
	}
	else
	{
		const int32 WeedNum = FarmlandConfig->WeedNum;
		const FVector PlotLocation = PlotInfo.Transform.GetLocation();
		const FVector2D PlotSize = FarmlandConfig->CellSize;
		// 中心原点
		const float HalfWidth = PlotSize.X * 0.5f;
		const float HalfHeight = PlotSize.Y * 0.5f;
		// 计算地块实际边界
		const float MinX = PlotLocation.X - HalfWidth;
		const float MaxX = PlotLocation.X + HalfWidth;
		const float MinY = PlotLocation.Y - HalfHeight;
		const float MaxY = PlotLocation.Y + HalfHeight;
    
		// 1. 计算网格划分
		const int32 GridSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(WeedNum)));
		const float CellWidth = (MaxX - MinX) / GridSize;
		const float CellHeight = (MaxY - MinY) / GridSize;
    
		// 2. 在每个网格内生成一个杂草
		for (int32 i = 0; i < WeedNum; i++)
		{
			// 计算网格索引
			const int32 GridX = i % GridSize;
			const int32 GridY = i / GridSize;

			const float GridCenterX = MinX + (GridX + 0.5f) * CellWidth;
			const float GridCenterY = MinY + (GridY + 0.5f) * CellHeight;
        
			// 随机偏移
			const float OffsetX = FMath::FRandRange(-CellWidth * 0.4f, CellWidth * 0.4f);
			const float OffsetY = FMath::FRandRange(-CellHeight * 0.4f, CellHeight * 0.4f);

			const float FinalX = FMath::Clamp(GridCenterX + OffsetX, MinX, MaxX);
			const float FinalY = FMath::Clamp(GridCenterY + OffsetY, MinY, MaxY);

			FVector WeedLocation(FinalX, FinalY, PlotLocation.Z);
		
			FTransform InstanceLocalTransform;
			InstanceLocalTransform.SetLocation(WeedLocation);
			InstanceLocalTransform.SetScale3D(FVector::One() * FarmlandConfig->WeedsScale);
			InstanceLocalTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->WeedsOffsetZ));
			FTransform WorldTransform = InstanceLocalTransform * GetComponentTransform();
			UISMTools::AdjustTransformToGround(WorldTransform, GetWorld(), nullptr, true);
			FTransform NewLocalTransform = WorldTransform.GetRelativeTransform(GetComponentTransform());
			WeedsRenderer->AddInstance(NewLocalTransform);
			PlotInfo.WeedsTransform.Add(NewLocalTransform);
		}
	}
	
	SetPlotState(Coords, EPlotState::WeedInfested, true);
	StartWeedsSpreadTimers();
}

void UFarmlandComponent::ClearWeeds(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::WeedInfested) && PlotInfo.WeedsTransform.Num() == 0)
	{
		return;
	}
	if (WeedsRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("WeedsRenderer is nullptr"))
		return;
	}
	for (const FTransform WeedTransform : PlotInfo.WeedsTransform)
	{
		UISMTools::RemoveInstanceByTransform(WeedsRenderer, WeedTransform);
	}
	PlotInfo.WeedsTransform.Empty();
	SetPlotState(Coords, EPlotState::WeedInfested, false);
}

void UFarmlandComponent::GeneratePest(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::PestInfested))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already PestInfested"))
		return;
	}

	check(FarmlandConfig->PestNiagaraSystem)
	const FVector PestLocation = PlotInfo.Transform.GetLocation() + FVector(0.f, 0.f, FarmlandConfig->PestHeight);
	UNiagaraComponent* PestParticle = UNiagaraFunctionLibrary::SpawnSystemAttached(
		FarmlandConfig->PestNiagaraSystem,this, NAME_None,
		PestLocation, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false);
	
	PestParticle->SetFloatParameter("PestRadius", FarmlandConfig->PestRadius);
	PlotInfo.PestNiagaraComponent = PestParticle;
	SetPlotState(Coords, EPlotState::PestInfested, true);
	StartPestsSpreadTimers();
}

void UFarmlandComponent::ClearPest(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::PestInfested) && PlotInfo.PestNiagaraComponent == nullptr)
	{
		return;
	}
	PlotInfo.PestNiagaraComponent->Deactivate();  // 先停用
	PlotInfo.PestNiagaraComponent->DestroyComponent();  // 销毁组件
	PlotInfo.PestNiagaraComponent = nullptr;  // 置空指针
	SetPlotState(Coords, EPlotState::PestInfested, false);
}

void UFarmlandComponent::UpdateOutlineColor() const
{
	if (!OutlineHISMComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("OutlineHISMComponent is nullptr"))
		return;
	}
	check(FarmlandConfig->OutlineMaterial);
	UMaterialInstanceDynamic* DynamicOutlineMaterialInstance = OutlineHISMComponent->CreateDynamicMaterialInstance(0, FarmlandConfig->OutlineMaterial);
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
	DynamicOutlineMaterialInstance->SetVectorParameterValue("SelfLuminousColor", OutlineColor);
	OutlineHISMComponent->SetMaterial(0,DynamicOutlineMaterialInstance);
}

void UFarmlandComponent::CreateOutline(FIntPoint Coords)
{
	if (!PlotInfoIndex.Contains(Coords))
	{
		UE_LOG(LogTemp, Error, TEXT("PlotInfo not found"))
		return;
	}
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (OutlineHISMComponent == nullptr)
	{
		OutlineHISMComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner());
		OutlineHISMComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		OutlineHISMComponent->RegisterComponent();
		OutlineHISMComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutlineHISMComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		check(FarmlandConfig->LandCellMesh);
		OutlineHISMComponent->SetStaticMesh(FarmlandConfig->LandCellMesh);
		OutlineHISMComponent->bDisallowNanite = true;
		
	}
	FTransform InstanceTransform = PlotInfo.Transform;
	InstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
	OutlineHISMComponent->AddInstance(InstanceTransform);
	UpdateOutlineColor();
}

void UFarmlandComponent::RemoveOutline(FIntPoint Coords)
{
	if (!PlotInfoIndex.Contains(Coords))
	{
		return;
	}
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (OutlineHISMComponent == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("OutlineHISMComponent is null"))
		return;
	}

	FTransform InstanceTransform = PlotInfo.Transform;
	InstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
	if (!UISMTools::RemoveInstanceByTransform(OutlineHISMComponent,InstanceTransform))
	{
		UE_LOG(LogTemp, Error, TEXT("无法移除实例"))
	}
	
}

FIntPoint UFarmlandComponent::WorldToCellCoordinates(const FVector& WorldPosition) const
{
	// 将世界坐标转换到组件的本地空间
	const FVector LocalPosition = GetComponentTransform().InverseTransformPosition(WorldPosition);
    
	// 计算农场总大小（考虑间距+拓展）
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
    
	// 计算本地坐标相对于农场边界的偏移
	const float HalfWidth = TotalWidth * 0.5f;
	const float HalfHeight = TotalHeight * 0.5f;
    
	// 计算归一化坐标 [0, 1]
	const float NormX = (LocalPosition.X + HalfWidth) / TotalWidth;
	const float NormY = (LocalPosition.Y + HalfHeight) / TotalHeight;
    
	// 转换为网格坐标
	const int32 CellX = FMath::FloorToInt(NormX * FarmlandConfig->GridSizeX);
	const int32 CellY = FMath::FloorToInt(NormY * FarmlandConfig->GridSizeY);
    
	// 边界检查
	if (CellX >= 0 && CellX < FarmlandConfig->GridSizeX && CellY >= 0 && CellY < FarmlandConfig->GridSizeY)
	{
		return FIntPoint(CellX, CellY);
	}
    
	// 返回无效坐标
	return FIntPoint(INDEX_NONE, INDEX_NONE);
}

FVector UFarmlandComponent::CellToWorldCoordinates(const int32 CellX, const int32 CellY) const
{
	// 计算农场总大小
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
    
	// 计算从中心开始的偏移
	const float StartX = -TotalWidth * 0.5f + FarmlandConfig->CellSize.X * 0.5f;
	const float StartY = -TotalHeight * 0.5f + FarmlandConfig->CellSize.Y * 0.5f;
    
	// 计算本地位置
	const float LocalX = StartX + CellX * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension);
	const float LocalY = StartY + CellY * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension);

	const FVector LocalPosition(LocalX, LocalY, 0);
    
	// 转换到世界空间
	return GetComponentTransform().TransformPosition(LocalPosition);
}

bool UFarmlandComponent::GetClickedCellCoordinates(const FVector& ClickLocation, FIntPoint& OutCellCoords) const
{
	// 先通过网格计算获得候选索引
	FIntPoint Candidate = WorldToCellCoordinates(ClickLocation);
	if (Candidate.X == INDEX_NONE || Candidate.Y == INDEX_NONE)
	{
		return false; // 超出农场范围
	}

	// 将世界坐标转换到组件本地空间
	const FVector LocalPos = GetComponentTransform().InverseTransformPosition(ClickLocation);

	// 获取候选地块的边界
	FBox CellBounds;
	if (GetCellBounds(Candidate, CellBounds))
	{
		// 检查点是否在地块内部（包括边界，可根据需求使用 IsInside 或 IsInsideOrOn）
		if (CellBounds.IsInside(LocalPos))
		{
			OutCellCoords = Candidate;
			return true;
		}
	}

	// 点击在间隙中
	return false;
}

FFarmLandPlotInfo UFarmlandComponent::GetPlotInfo(const FIntPoint& PlotCoords) const
{
	if (PlotInfoIndex.Contains(PlotCoords))
	{
		return *PlotInfoIndex.Find(PlotCoords);
	}
	return FFarmLandPlotInfo();
}

void UFarmlandComponent::SetPlotState(const FIntPoint& PlotCoords, EPlotState State, bool bEnable)
{
	if (!PlotInfoIndex.Contains(PlotCoords))
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPlotState: Plot %s not found"), *PlotCoords.ToString());
		return;
	}
	
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlotCoords);
	if (bEnable)
	{
		PlotInfo.StateData.AddState(State);
	}
	else
	{
		PlotInfo.StateData.RemoveState(State);
	}
	
	// 广播状态变更事件
	OnPlotStateChanged.Broadcast(PlotCoords, PlotInfo.StateData);
}

TArray<FFarmLandPlotSaveGameData> UFarmlandComponent::GetPlotSaveGameData()
{
	PlotSaveGameData.Empty();
	for (const auto& PlotInfoTuple : PlotInfoIndex)
	{
		FFarmLandPlotSaveGameData PlotSaveGameDataItem;
		PlotSaveGameDataItem.Coords = PlotInfoTuple.Key;
		PlotSaveGameDataItem.StateData = PlotInfoTuple.Value.StateData;
		PlotSaveGameDataItem.Transform = PlotInfoTuple.Value.Transform;
		PlotSaveGameDataItem.WeedsTransform = PlotInfoTuple.Value.WeedsTransform;
		// if (PlotInfoTuple.Value.Crop)
		// {
		// 	PlotSaveGameDataItem.CropData = PlotInfoTuple.Value.Crop->GetCropData();
		// }
		PlotSaveGameData.Add(PlotSaveGameDataItem);
	}
	return PlotSaveGameData;
}

void UFarmlandComponent::BeginPlay()
{
	Super::BeginPlay();

	BasePlayerController = Cast<ABasePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BasePlayerController)
	{
		BasePlayerController->OnEditModeChanged.AddDynamic(this, &ThisClass::OnEditModeChanged);
	}
	InitializeLandCells();

	StartPestsSpreadTimers();
	StartWeedsSpreadTimers();
}

void UFarmlandComponent::CreateDefaultLandCells()
{
	// 计算缩放因子
	// 获取土地网格的原始边界
	const FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
	const FVector CellExtent = CellBounds.BoxExtent;
	// 计算需要的缩放
	const float ScaleFactorX = FarmlandConfig->CellSize.X / (CellExtent.X * 2.f);
	const float ScaleFactorY = FarmlandConfig->CellSize.Y / (CellExtent.Y * 2.f);
	const float ScaleFactorZ = FarmlandConfig->LandHeightOffset / (CellExtent.Z * 2.f);
	const FVector InstanceScale(ScaleFactorX, ScaleFactorY, ScaleFactorZ);
    
	// 计算总偏移（从中心开始）
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing;
	const float StartX = -TotalWidth * 0.5f + FarmlandConfig->CellSize.X * 0.5f;
	const float StartY = -TotalHeight * 0.5f + FarmlandConfig->CellSize.Y * 0.5f;
    
	// 创建所有土地单元格实例
	for (int32 Y = 0; Y < FarmlandConfig->GridSizeY; Y++)
	{
		for (int32 X = 0; X < FarmlandConfig->GridSizeX; X++)
		{
			// 计算位置
			const float PosX = StartX + X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
			const float PosY = StartY + Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing);
			float PosZ = FarmlandConfig->LandHeightOffset / 2.0f;
			// if (FarmlandConfig->bUseGround)
			// {
			// 	PosZ = FarmlandConfig->LandHeightOffset / 2.0f;
			// }
			
			// 创建变换
			FTransform CellTransform;
			CellTransform.SetLocation(FVector(PosX, PosY, PosZ));
			CellTransform.SetScale3D(InstanceScale);
			
			// 创建信息
			FFarmLandPlotInfo PlotInfo;
			PlotInfo.Transform = CellTransform;
			
			// 记录映射
			PlotInfoIndex.Add(FIntPoint(X, Y), PlotInfo);
			
			// 初始化所有地块
			if (FarmlandConfig->bInitAllLandCells && PlotSaveGameData.Num() == 0)
			{
				PlowLand(FIntPoint(X, Y));
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("Initialized %d land cells for %dx%d grid by Default"), 
	       LandCellRenderer->GetInstanceCount(), FarmlandConfig->GridSizeX, FarmlandConfig->GridSizeY);
}

bool UFarmlandComponent::GetCellBounds(const FIntPoint& Coords, FBox& OutBounds) const
{
	if (!FarmlandConfig || Coords.X < 0 || Coords.X >= FarmlandConfig->GridSizeX || Coords.Y < 0 || Coords.Y >= FarmlandConfig->GridSizeY)
	{
		return false;
	}

	// 计算农场总尺寸
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + 
							 (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + 
							 FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + 
							  (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + 
							  FarmlandConfig->GroundExtension * 2.f;
	const float HalfWidth = TotalWidth * 0.5f;
	const float HalfHeight = TotalHeight * 0.5f;

	// 计算地块左下角（本地坐标）
	float Left = -HalfWidth + FarmlandConfig->GroundExtension + 
				 Coords.X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
	float Bottom = -HalfHeight + FarmlandConfig->GroundExtension + 
				   Coords.Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing); // 假设 Y 向上

	float Right = Left + FarmlandConfig->CellSize.X;
	float Top = Bottom + FarmlandConfig->CellSize.Y;

	// 返回包围盒（Z 轴范围放宽，假设地块在 Z=0 附近）
	OutBounds = FBox(FVector(Left, Bottom, -100.f), FVector(Right, Top, 100.f));
	return true;
}

void UFarmlandComponent::InitializeLandCells()
{
	ensure(FarmlandConfig);
	ensure(FarmlandConfig->LandCellMesh);

	if (LandCellRenderer != nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCells have been Initialized"));
		return;
	}
	LandCellRenderer = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	LandCellRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	LandCellRenderer->RegisterComponent();
	LandCellRenderer->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LandCellRenderer->SetCollisionResponseToAllChannels(ECR_Block);
	check(FarmlandConfig->LandCellMesh);
	LandCellRenderer->SetStaticMesh(FarmlandConfig->LandCellMesh);
	// 设置自定义数据数量 你必须告诉 ISM 组件：“每个实例需要占用多少个浮点位？”
	LandCellRenderer->SetNumCustomDataFloats(FarmlandConfig->NumCustomDataFloats);

	CreateDefaultLandCells();
	if (PlotSaveGameData.Num() > 0)
	{
		for (auto& PlotSaveGameDataItem : PlotSaveGameData)
		{
			FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlotSaveGameDataItem.Coords);
			PlotInfo.WeedsTransform = PlotSaveGameDataItem.WeedsTransform;
			if (PlotSaveGameDataItem.StateData.HasState(EPlotState::Plowed))
			{
				PlowLand(PlotSaveGameDataItem.Coords);
				if (PlotSaveGameDataItem.StateData.HasState(EPlotState::Watered))
				{
					WetLand(PlotSaveGameDataItem.Coords);
				}
				if (PlotSaveGameDataItem.StateData.HasState(EPlotState::Fertilized))
				{
					FertilizeLand(PlotSaveGameDataItem.Coords);
				}
			}
			if (PlotSaveGameDataItem.StateData.HasState(EPlotState::WeedInfested))
			{
				GenerateWeeds(PlotSaveGameDataItem.Coords);
			}
			if (PlotSaveGameDataItem.StateData.HasState(EPlotState::PestInfested))
			{
				GeneratePest(PlotSaveGameDataItem.Coords);
			}

			// if (PlotSaveGameDataItem.CropData.CropID != NAME_None)
			// {
			// 	const FCropTableRow* CropTableRow = CropDataTable->FindRow<FCropTableRow>(PlotSaveGameDataItem.CropData.CropID, TEXT("UFarmlandComponent::InitializeLandCells"));
			// 	PlotInfo.CropMeshVariant = CropTableRow->MeshVariants[PlotSaveGameDataItem.CropData.VariantsIndex];
			// 	PlotInfo.CropNumCustomDataFloats = CropTableRow->CropNumCustomDataFloats;
			// 	UCropBase* Crop = NewObject<UCropBase>();
			// 	PlotInfo.Crop = Crop;
			// 	Crop->OnPlantGrowthStageChanged.AddDynamic(this, &ThisClass::OnCropGrowthStageChanged);
			// 	Crop->InitializeCrop(this, PlotSaveGameDataItem.CropData);
			// }
		}
		UE_LOG(LogTemp, Log, TEXT("Initialized %d land cells for %dx%d grid by PlotSaveGameData"), 
		   LandCellRenderer->GetInstanceCount(), FarmlandConfig->GridSizeX, FarmlandConfig->GridSizeY);
	}

	
	
}

int32 UFarmlandComponent::GetPlotIndex(const FIntPoint& PlotCoords) const
{
	if (PlotCoords.X < 0 || PlotCoords.X >= FarmlandConfig->GridSizeX || PlotCoords.Y < 0 || PlotCoords.Y >= FarmlandConfig->GridSizeY)
	{
		return INDEX_NONE;
	}
	return PlotCoords.Y * FarmlandConfig->GridSizeX + PlotCoords.X;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void UFarmlandComponent::OnEditModeChanged(EEditMode NewEditMode)
{
	UpdateOutlineColor();
}

void UFarmlandComponent::OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage - MoistureCost, 0.f, 1.f);
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage - FertilityCost, 0.f, 1.f);
	OnPlotMoistureChanged.Broadcast(Coords, PlotInfo.MoisturePercentage);
	OnPlotFertilityChanged.Broadcast(Coords, PlotInfo.FertilityPercentage);

	// 判断水分状态
	if (PlotInfo.MoisturePercentage >= 0.2f && PlotInfo.MoisturePercentage <= 0.8f)// 正常
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is normal: %.2f"), PlotInfo.MoisturePercentage);
	}
	else if (PlotInfo.MoisturePercentage > 0.8f)// 过高
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is excessive: %.2f"), PlotInfo.MoisturePercentage);
	}
	else// 过低
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is insufficient: %.2f"), PlotInfo.MoisturePercentage);
		LandDried(Coords);
	}

	// 判断肥力状态
	if (PlotInfo.FertilityPercentage >= 0.2f && PlotInfo.FertilityPercentage <= 0.8f)// 正常
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is normal: %.2f"), PlotInfo.FertilityPercentage);
	}
	else if (PlotInfo.FertilityPercentage > 0.8f)// 过高
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is excessive: %.2f"), PlotInfo.FertilityPercentage);
	}
	else// 过低
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is insufficient: %.2f"), PlotInfo.FertilityPercentage);
		ClearFertilize(Coords);
	}
}

void UFarmlandComponent::StartWeedsSpreadTimers()
{
	if (GetWorld()->GetTimerManager().IsTimerActive(WeedSpreadTimerHandle))
	{
		return;
	}
	if (FarmlandConfig)
	{
		GetWorld()->GetTimerManager().SetTimer(WeedSpreadTimerHandle, this, &ThisClass::SpreadWeeds,
												FarmlandConfig->WeedSpreadInterval, true);
	}
}

void UFarmlandComponent::StartPestsSpreadTimers()
{
	if (GetWorld()->GetTimerManager().IsTimerActive(PestSpreadTimerHandle))
	{
		return;
	}
	if (FarmlandConfig)
	{
		GetWorld()->GetTimerManager().SetTimer(PestSpreadTimerHandle, this, &ThisClass::SpreadPests,
												FarmlandConfig->PestSpreadInterval, true);
	}
}

void UFarmlandComponent::SpreadWeeds()
{
	if (!FarmlandConfig) return;

	// 收集当前所有有杂草的地块
	TArray<FIntPoint> InfectedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::WeedInfested))
		{
			InfectedPlots.Add(Pair.Key);
		}
	}

	// 如果没有被感染的地块，则停止定时器
	if (InfectedPlots.Num() == 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(WeedSpreadTimerHandle);
		return;
	}

	// 随机顺序处理，避免偏向性
	Algo::RandomShuffle(InfectedPlots);
	
	for (const FIntPoint& Source : InfectedPlots)
	{
		TArray<FIntPoint> Neighbors;
		GetNeighborCoords(Source, Neighbors);

		for (const FIntPoint& Target : Neighbors)
		{
			FFarmLandPlotInfo& TargetPlot = PlotInfoIndex.FindChecked(Target);

			// 如果目标地块已经感染，跳过
			if (TargetPlot.StateData.HasState(EPlotState::WeedInfested))
				continue;

			// 检查是否可以传播到目标（根据配置和地块状态）
			bool bCanSpread = true;
			if (!FarmlandConfig->bWeedCanSpreadToEmptyPlots)
			{
				// 需要目标地块已耕地
				if (!TargetPlot.StateData.HasState(EPlotState::Plowed))
					bCanSpread = false;
			}

			if (bCanSpread && FMath::FRand() < FarmlandConfig->WeedSpreadProbability)
			{
				// 调用现有的生成杂草方法（该方法会更新状态和视觉）
				GenerateWeeds(Target);
			}
		}
	}
}

void UFarmlandComponent::SpreadPests()
{
	if (!FarmlandConfig) return;

	// 收集所有有虫害的地块
	TArray<FIntPoint> InfestedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::PestInfested))
		{
			InfestedPlots.Add(Pair.Key);
		}
	}

	// 如果没有被感染的地块，则停止定时器
	if (InfestedPlots.Num() == 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(PestSpreadTimerHandle);
		return;
	}
	
	// 随机顺序处理，避免偏向性
	Algo::RandomShuffle(InfestedPlots);

	for (const FIntPoint& Source : InfestedPlots)
	{
		TArray<FIntPoint> Neighbors;
		GetNeighborCoords(Source, Neighbors);

		for (const FIntPoint& Target : Neighbors)
		{
			FFarmLandPlotInfo& TargetPlot = PlotInfoIndex.FindChecked(Target);

			if (TargetPlot.StateData.HasState(EPlotState::PestInfested))
				continue;

			// 虫害传播条件：通常需要目标有作物，或至少已耕地
			bool bCanSpread = true;
			if (!FarmlandConfig->bPestCanSpreadToEmptyPlots)
			{
				// 要求目标地块有作物
				if (TargetPlot.StateData.HasState(EPlotState::CropPlanted))
					bCanSpread = false;
			}

			if (bCanSpread && FMath::FRand() < FarmlandConfig->PestSpreadProbability)
			{
				GeneratePest(Target);
			}
		}
	}
}

void UFarmlandComponent::GetNeighborCoords(FIntPoint Coords, TArray<FIntPoint>& OutNeighbors) const
{
	// 八个方向偏移量：上下左右 + 四个对角线
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1), // 四邻域
		FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) // 对角线
	};

	for (const FIntPoint& Dir : Directions)
	{
		FIntPoint Neighbor = Coords + Dir;
		// 边界检查
		if (Neighbor.X >= 0 && Neighbor.X < FarmlandConfig->GridSizeX &&
			Neighbor.Y >= 0 && Neighbor.Y < FarmlandConfig->GridSizeY)
		{
			OutNeighbors.Add(Neighbor);
		}
	}
}

