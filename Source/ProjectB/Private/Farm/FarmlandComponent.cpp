// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Crops/CropBase.h"
#include "Farm/FarmlandActor.h"
#include "Player/BasePlayerController.h"
#include "Tools/ISMTools.h"


// Sets default values for this component's properties
UFarmlandComponent::UFarmlandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; 
	
}

void UFarmlandComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
{
	switch (EditMode)
	{
	case None:
		break;
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
	case EEditMode::PlantCrop:
		PlantCrop(Coords);
		break;
	case EEditMode::HarvestCrop:
		HarvestCrop(Coords);
		break;
	case EEditMode::CropDried:
		LandDried(Coords);
		break;
	case EEditMode::UnCropDried:
		ResetLand(Coords);
		break;
	case EEditMode::RemoveCrop:
		RemoveCrop(Coords);
		break;
	case Count:
		break;
	
	}
}

void UFarmlandComponent::ResetLand(const FIntPoint Coords)
{
	ClearWeeds(Coords);
	ClearPest(Coords);
	RemoveCrop(Coords);
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
	PlotInfo.StateData.AddState(EPlotState::Plowed);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::WetLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::Watered))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already watered"))
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
	PlotInfo.StateData.AddState(EPlotState::Watered);
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage + 0.8f, 0.f, 2.f);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
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
	PlotInfo.StateData.RemoveState(EPlotState::Watered);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::FertilizeLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::Fertilized))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already Fertilized"))
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
	PlotInfo.StateData.AddState(EPlotState::Fertilized);
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage + 0.8f, 0.f, 2.f);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
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
	PlotInfo.StateData.RemoveState(EPlotState::Fertilized);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::GenerateWeeds(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
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
			WeedsRenderer->AddInstance(InstanceLocalTransform);
			PlotInfo.WeedsTransform.Add(InstanceLocalTransform);
		}
	}
	
 	PlotInfo.StateData.AddState(EPlotState::WeedInfested);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
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
	PlotInfo.StateData.RemoveState(EPlotState::WeedInfested);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
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
	PlotInfo.StateData.AddState(EPlotState::PestInfested);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
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
	PlotInfo.StateData.RemoveState(EPlotState::PestInfested);
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::RemoveCrop(FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"));
		return;
	}
	if (PlotInfo.Crop == nullptr)
	{
		return;
	}
	// 清理当前的实例
	UStaticMesh* CurrentGrowthStageMesh = nullptr;
	switch (PlotInfo.Crop->GetCropData().CropGrowthStage) {
	case ECropGrowthStage::None:
		CurrentGrowthStageMesh = nullptr;
		break;
	case ECropGrowthStage::Seed:
		CurrentGrowthStageMesh = PlotInfo.CropMeshVariant.SeedMesh;
		break;
	case ECropGrowthStage::Seedling:
		CurrentGrowthStageMesh = PlotInfo.CropMeshVariant.SeedlingMesh;
		break;
	case ECropGrowthStage::Mature:
		CurrentGrowthStageMesh = PlotInfo.CropMeshVariant.MatureMesh;
		break;
	case ECropGrowthStage::Harvested:
		CurrentGrowthStageMesh = PlotInfo.CropMeshVariant.HarvestedMesh;
		break;
	}
	if (CurrentGrowthStageMesh)
	{
		if (CropHISMComponentMap.Contains(FStaticMeshHISMKey(CurrentGrowthStageMesh)))
		{
			if (UInstancedStaticMeshComponent* CurrentInstanceHISMComponent = CropHISMComponentMap.FindChecked(FStaticMeshHISMKey(CurrentGrowthStageMesh)))
			{
				UISMTools::RemoveInstanceByTransform(CurrentInstanceHISMComponent, PlotInfo.CropTransform);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("CurrentInstanceHISMComponent not found for %s"), *CurrentGrowthStageMesh->GetName());
				return;
			}
		}
	}
	// 清除作物对象
	PlotInfo.Crop->OnPlantGrowthStageChanged.RemoveAll(this);
	PlotInfo.Crop->OnCropGrownThisTime.Unbind();
	PlotInfo.Crop = nullptr;
	PlotInfo.CropMeshVariant = FCropMeshVariant();
	PlotInfo.CropNumCustomDataFloats = -1;
	PlotInfo.CropTransform = FTransform();
	OnCropPlantedOrRemoved.Broadcast(Coords);
}

void UFarmlandComponent::PlantCrop(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.Crop)
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already Planted"))
		return;
	}
	const FName CropID = FName(BasePlayerController->GetSelectedCropName());
	const FCropTableRow* CropTableRow = CropDataTable->FindRow<FCropTableRow>(CropID, TEXT("UFarmlandComponent::PlantCrop"));
	if (!CropTableRow)
	{
		UE_LOG(LogTemp, Error, TEXT("无法种植作物: %s，数据不存在"), *CropID.ToString());
		return;
	}
	if (CropTableRow->MeshVariants.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("无法种植作物: %s，MeshVariants为空"), *CropID.ToString())
	}
	const int32 VariantsIndex = FMath::RandRange(0, CropTableRow->MeshVariants.Num() - 1);
	PlotInfo.CropMeshVariant = CropTableRow->MeshVariants[VariantsIndex];
	PlotInfo.CropNumCustomDataFloats = CropTableRow->CropNumCustomDataFloats;
	FTransform CropTransform = PlotInfo.Transform;
	CropTransform.AddToTranslation(FarmlandConfig->CropOffset);
	PlotInfo.CropTransform = CropTransform;
	UCropBase* Crop = NewObject<UCropBase>(this);
	PlotInfo.Crop = Crop;
	FCropData CropData;
	CropData.CropID = CropID;
	CropData.Coords = Coords;
	CropData.VariantsIndex = VariantsIndex;
	CropData.GrowthSeedTime = CropTableRow->GrowthSeedTime;
	CropData.GrowthSeedlingTime = CropTableRow->GrowthSeedlingTime;
	CropData.CropGrowthStage = ECropGrowthStage::Seed;
	if (PlotInfo.StateData.HasState(EPlotState::Watered))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsWater);
	}
	if (PlotInfo.StateData.HasState(EPlotState::Fertilized))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
	}
	Crop->OnPlantGrowthStageChanged.AddDynamic(this, &ThisClass::OnCropGrowthStageChanged);
	Crop->OnCropGrownThisTime.BindDynamic(this, &ThisClass::OnCropGrownThisTime);
	Crop->InitializeCrop(this, CropData);
	OnCropPlantedOrRemoved.Broadcast(Coords);
}

bool UFarmlandComponent::HarvestCrop(FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return false;
	}
	if (!PlotInfo.Crop)
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not planted"))
		return false;
	}
	if (PlotInfo.Crop->GetCropData().CropGrowthStage != ECropGrowthStage::Mature)
	{
		UE_LOG(LogTemp, Error, TEXT("Crop is not mature"))
		return false;
	}
	PlotInfo.Crop->HarvestCrop();
	return true;

}

void UFarmlandComponent::CropDried(FIntPoint Coords)
{
	SetCropDisease(Coords, true, true);
}

void UFarmlandComponent::UnCropDried(FIntPoint Coords)
{
	SetCropDisease(Coords, false, false);
}

void UFarmlandComponent::UpdateOutlineColor()
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


UCropBase* UFarmlandComponent::GetCropAtPlot(int32 PlotX, int32 PlotY) const
{
	return nullptr;
}

void UFarmlandComponent::UpdateCrops(float DeltaTime)
{
	for (const auto& PlotInfoTuple : PlotInfoIndex)
	{
		if (PlotInfoTuple.Value.Crop)
		{
			PlotInfoTuple.Value.Crop->UpdateCrop(DeltaTime);
		}
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
	if (GetCellBounds(Candidate.X, Candidate.Y, CellBounds))
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
		if (PlotInfoTuple.Value.Crop)
		{
			PlotSaveGameDataItem.CropData = PlotInfoTuple.Value.Crop->GetCropData();
		}
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

bool UFarmlandComponent::GetCellBounds(int32 X, int32 Y, FBox& OutBounds) const
{
	if (!FarmlandConfig || X < 0 || X >= FarmlandConfig->GridSizeX || Y < 0 || Y >= FarmlandConfig->GridSizeY)
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
				 X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
	float Bottom = -HalfHeight + FarmlandConfig->GroundExtension + 
				   Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing); // 假设 Y 向上

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

			if (PlotSaveGameDataItem.CropData.CropID != NAME_None)
			{
				const FCropTableRow* CropTableRow = CropDataTable->FindRow<FCropTableRow>(PlotSaveGameDataItem.CropData.CropID, TEXT("UFarmlandComponent::InitializeLandCells"));
				PlotInfo.CropMeshVariant = CropTableRow->MeshVariants[PlotSaveGameDataItem.CropData.VariantsIndex];
				PlotInfo.CropNumCustomDataFloats = CropTableRow->CropNumCustomDataFloats;
				UCropBase* Crop = NewObject<UCropBase>();
				PlotInfo.Crop = Crop;
				Crop->OnPlantGrowthStageChanged.AddDynamic(this, &ThisClass::OnCropGrowthStageChanged);
				Crop->InitializeCrop(this, PlotSaveGameDataItem.CropData);
			}
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

FVector UFarmlandComponent::GetPlotWorldPosition(int32 X, int32 Y) const
{
	return FVector::ZeroVector;
}

void UFarmlandComponent::UpdateCropVisual(int32 PlotIndex, UCropBase* Crop)
{
}

void UFarmlandComponent::SetCropDisease(FIntPoint Coords, bool bDisease, bool bLeafs)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.Crop)
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not planted"))
	}

	UStaticMesh* Mesh = nullptr;
	switch (PlotInfo.Crop->GetCropData().CropGrowthStage) {
	case ECropGrowthStage::None:
		Mesh = nullptr;
		break;
	case ECropGrowthStage::Seed:
		Mesh = PlotInfo.CropMeshVariant.SeedMesh;
		break;
	case ECropGrowthStage::Seedling:
		Mesh = PlotInfo.CropMeshVariant.SeedlingMesh;
		break;
	case ECropGrowthStage::Mature:
		Mesh = PlotInfo.CropMeshVariant.MatureMesh;
		break;
	case ECropGrowthStage::Harvested:
		Mesh = PlotInfo.CropMeshVariant.HarvestedMesh;
		break;
	}
	UInstancedStaticMeshComponent* CropRenderer = nullptr;
	if (CropHISMComponentMap.Contains(FStaticMeshHISMKey(Mesh)))
	{
		CropRenderer = *CropHISMComponentMap.Find(FStaticMeshHISMKey(Mesh));
	}
	if (!CropRenderer)
	{
		UE_LOG(LogTemp, Error, TEXT("Crop renderer not found"));
		return;
	}
	int32 CropIndex;
	UISMTools::GetInstanceIndexByTransform(CropRenderer, PlotInfo.Transform, CropIndex);
	if (bDisease)
	{
		CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::Disease, 1.f);
		if (bLeafs)
		{
			CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 1.f);
			PlotInfo.Crop->GetCropDataRef().CropStateData.AddState(ECropState::NeedsWater);
		}
		else
		{
			CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 0.f);
			PlotInfo.Crop->GetCropDataRef().CropStateData.AddState(ECropState::NeedsWater);
		}
	}
	else
	{
		CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::Disease, 0.f);
		CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 0.f);
		PlotInfo.Crop->GetCropDataRef().CropStateData.RemoveState(ECropState::NeedsWater);
	}
	
}

TObjectPtr<UInstancedStaticMeshComponent> UFarmlandComponent::GetOrCreateRenderer(FName CropID)
{
	return nullptr;
}

TObjectPtr<UInstancedStaticMeshComponent> UFarmlandComponent::GetOrCreateCropHISMC(const FFarmLandPlotInfo& PlotInfo)
{
	UStaticMesh* Mesh = nullptr;
	if (!PlotInfo.Crop)
	{
		UE_LOG(LogTemp, Error, TEXT("Crop is null"))
		return nullptr;
	}
	switch (PlotInfo.Crop->GetCropData().CropGrowthStage) {
	case ECropGrowthStage::None:
		Mesh = nullptr;
		break;
	case ECropGrowthStage::Seed:
		Mesh = PlotInfo.CropMeshVariant.SeedMesh;
		break;
	case ECropGrowthStage::Seedling:
		Mesh = PlotInfo.CropMeshVariant.SeedlingMesh;
		break;
	case ECropGrowthStage::Mature:
		Mesh = PlotInfo.CropMeshVariant.MatureMesh;
		break;
	case ECropGrowthStage::Harvested:
		Mesh = PlotInfo.CropMeshVariant.HarvestedMesh;
		break;
	}
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	if (CropHISMComponentMap.Contains(FStaticMeshHISMKey(Mesh)))
	{
		return CropHISMComponentMap.FindChecked(FStaticMeshHISMKey(Mesh));
	}
	UInstancedStaticMeshComponent* TargetComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	TargetComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	TargetComponent->RegisterComponent();
	TargetComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetComponent->SetCollisionResponseToAllChannels(ECR_Block);
	TargetComponent->SetStaticMesh(Mesh);
	TargetComponent->SetNumCustomDataFloats(PlotInfo.CropNumCustomDataFloats);
	CropHISMComponentMap.Add(FStaticMeshHISMKey(Mesh), TargetComponent);
	return TargetComponent;
}

void UFarmlandComponent::OnCropGrowthStageChanged(FIntPoint Coords, ECropGrowthStage NewGrowthStage)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	UStaticMesh* OldGrowthStageMesh = nullptr;
	switch (NewGrowthStage) {
	case ECropGrowthStage::None:
		OldGrowthStageMesh = nullptr;
		break;
	case ECropGrowthStage::Seed:
		OldGrowthStageMesh = nullptr;
		break;
	case ECropGrowthStage::Seedling:
		OldGrowthStageMesh = PlotInfo.CropMeshVariant.SeedMesh;
		break;
	case ECropGrowthStage::Mature:
		OldGrowthStageMesh = PlotInfo.CropMeshVariant.SeedlingMesh;
		break;
	case ECropGrowthStage::Harvested:
		OldGrowthStageMesh = PlotInfo.CropMeshVariant.MatureMesh;
		break;
	}

	// 清理之前的实例
	if (OldGrowthStageMesh)
	{
		if (CropHISMComponentMap.Contains(FStaticMeshHISMKey(OldGrowthStageMesh)))
		{
			if (UInstancedStaticMeshComponent* OldInstanceHISMComponent = CropHISMComponentMap.FindChecked(FStaticMeshHISMKey(OldGrowthStageMesh)))
			{
				UISMTools::RemoveInstanceByTransform(OldInstanceHISMComponent, PlotInfo.CropTransform);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("OldInstanceHISMComponent not found for %s"), *OldGrowthStageMesh->GetName());
				return;
			}
		}
	}
	
	// 创建新的实例
	UInstancedStaticMeshComponent* NewInstanceHISMComponent = GetOrCreateCropHISMC(PlotInfo);
	if (NewInstanceHISMComponent)
	{
		const int32 InstanceIndex = NewInstanceHISMComponent->AddInstance(PlotInfo.CropTransform);
		if (PlotInfo.Crop->GetCropData().CropStateData.HasState(ECropState::Withers))
		{
			NewInstanceHISMComponent->SetCustomDataValue(InstanceIndex, CropCustomData::Disease, 1.f);
			NewInstanceHISMComponent->SetCustomDataValue(InstanceIndex, CropCustomData::IsLeaf, 1.f);
		}
		else
		{
			NewInstanceHISMComponent->SetCustomDataValue(InstanceIndex, CropCustomData::Disease, 0.f);
			NewInstanceHISMComponent->SetCustomDataValue(InstanceIndex, CropCustomData::IsLeaf, 0.f);
		}
	}
	
	if (const UEnum* GrowthStageEnumPtr = StaticEnum<ECropGrowthStage>())
	{
		const FText GrowthStageDisplayName = GrowthStageEnumPtr->GetDisplayNameTextByValue(static_cast<int64>(NewGrowthStage));
        const FString CropId = PlotInfo.Crop->GetCropData().CropID.ToString();
		UE_LOG(LogTemp, Log, TEXT("(%d, %d) [%s] GrowthStageChange to %s"), Coords.X, Coords.Y, *CropId, *GrowthStageDisplayName.ToString());
	}
}

void UFarmlandComponent::OnEditModeChanged(EEditMode NewEditMode)
{
	UpdateOutlineColor();
}

void UFarmlandComponent::OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage - MoistureCost, 0.f, 1.f);
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage - FertilityCost, 0.f, 1.f);

	UCropBase* Crop = PlotInfo.Crop;
	if (Crop == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Crop is null"));
		return;
	}
	Crop->HandleCropMoisture(PlotInfo.MoisturePercentage);
	Crop->HandleCropFertility(PlotInfo.FertilityPercentage);
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

void UFarmlandComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateCrops(DeltaTime);
}

