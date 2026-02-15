// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Crops/CropBase.h"

UCropBase::UCropBase()
{
}

void UCropBase::InitializeCrop(FCropData& InCropData)
{
	CropData = InCropData;
	OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
}

void UCropBase::UpdateCrop(float DeltaTime)
{
	CropData.TotalGrowthTime += DeltaTime;

	switch (CropData.CropGrowthStage)
	{
	case ECropGrowthStage::None:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime = 0.f;
		break;
	case ECropGrowthStage::Seed:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime;
		break;
	case ECropGrowthStage::Seedling:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime - CropData.GrowthSeedTime;
		break;
	case ECropGrowthStage::Mature:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime - CropData.GrowthSeedTime - CropData.GrowthSeedlingTime;
		break;
	case ECropGrowthStage::Harvested:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime - CropData.GrowthSeedTime - CropData.GrowthSeedlingTime;
		break;
	}
	if (CropData.CropGrowthStage == ECropGrowthStage::Seed && CropData.CurrentStageGrowthTime >= CropData.GrowthSeedTime)
	{
		CropData.CropGrowthStage = ECropGrowthStage::Seedling;
		OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
	}
	else if (CropData.CropGrowthStage == ECropGrowthStage::Seedling && CropData.CurrentStageGrowthTime >= CropData.GrowthSeedlingTime)
	{
		CropData.CropGrowthStage = ECropGrowthStage::Mature;
		OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
	}

	
}

void UCropBase::ProcessOfflineGrowth(const FTimespan& OfflineDuration)
{
}

bool UCropBase::WaterCrop(float Amount)
{
	return true;
}

bool UCropBase::FertilizeCrop()
{
	return true;
}

bool UCropBase::RemovePests()
{
	return true;
}

void UCropBase::HarvestCrop()
{
	if (CropData.CropGrowthStage != ECropGrowthStage::Mature)
	{
		UE_LOG(LogTemp, Warning, TEXT("Crop is not mature!"))
		return;
	}
	CropData.CropGrowthStage = ECropGrowthStage::Harvested;
	OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
}
