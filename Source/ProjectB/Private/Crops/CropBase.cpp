// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Crops/CropBase.h"

UCropBase::UCropBase(): CropData()
{
}

void UCropBase::InitializeCrop(FCropData& InCropData)
{
	CropData = InCropData;
	OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
	OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
	OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.Quality);
}

void UCropBase::UpdateCropQuality(float DeltaTime)
{
	if (CropData.CropStateData.HasState(ECropState::NeedsWater))
	{
		CropData.CropQualityData.AddWaterDeficitTime(DeltaTime);
	}
	else if (CropData.CropStateData.HasState(ECropState::OverWatered))
	{
		CropData.CropQualityData.AddOverwaterTime(DeltaTime);
	}
	else
	{
		CropData.CropQualityData.AddIdealMoistureTime(DeltaTime);
	}
	if (CropData.CropStateData.HasState(ECropState::NeedsFertilizer))
	{
		CropData.CropQualityData.AddFertilizerDeficitTime(DeltaTime);
	}
	else if (CropData.CropStateData.HasState(ECropState::NutrientBurn))
	{
		CropData.CropQualityData.AddOverfertilizeTime(DeltaTime);
	}
	else
	{
		CropData.CropQualityData.AddIdealFertilityTime(DeltaTime);
	}
	if (CropData.CropStateData.HasState(ECropState::WeedInfested))
	{
		CropData.CropQualityData.AddWeedInfestedTime(DeltaTime);
	}
	else
	{
		CropData.CropQualityData.AddNoWeedTime(DeltaTime);
	}
	if (CropData.CropStateData.HasState(ECropState::PestInfested))
	{
		CropData.CropQualityData.AddPestInfestedTime(DeltaTime);
	}
	else
	{
		CropData.CropQualityData.AddNoPestTime(DeltaTime);
	}
}

void UCropBase::UpdateCrop(float DeltaTime)
{
	if (CropData.CropGrowthStage == ECropGrowthStage::Mature || CropData.CropGrowthStage == ECropGrowthStage::Harvested)
	{
		return;
	}
	UpdateCropQuality(DeltaTime);
	
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
	// 判断是否需要转换成下一阶段
	if (CropData.CropGrowthStage == ECropGrowthStage::Seed && CropData.CurrentStageGrowthTime >= CropData.GrowthSeedTime) // 种子-发芽
	{
		CropData.CropGrowthStage = ECropGrowthStage::Seedling;
		OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
	}
	else if (CropData.CropGrowthStage == ECropGrowthStage::Seedling && CropData.CurrentStageGrowthTime >= CropData.GrowthSeedlingTime) // 幼苗-成熟
	{
		CropData.CropGrowthStage = ECropGrowthStage::Mature;
		CropData.CropStateData.AddState(ECropState::ReadyForHarvest);
		OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
	}

	if (OnCropGrownThisTime.IsBound())
	{
		OnCropGrownThisTime.Execute(CropData.Coords, 0.01f, 0.02f);
	}
}

void UCropBase::ProcessOfflineGrowth(const FTimespan& OfflineDuration)
{
}

bool UCropBase::WaterCrop(float Amount)
{
	if (CropData.CropStateData.HasState(ECropState::NeedsWater))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsWater);
		CropData.CropQualityData.RecordWatering(true);
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		return true;
	}
	if (!CropData.CropStateData.HasState(ECropState::OverWatered))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsWater);
		CropData.CropStateData.AddState(ECropState::OverWatered);
		CropData.CropQualityData.RecordWatering(false);
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		return true;
	}
	return false;
}

bool UCropBase::FertilizeCrop()
{
	if (CropData.CropStateData.HasState(ECropState::NeedsFertilizer))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
		CropData.CropQualityData.RecordFertilize(true);
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		return true;
	}
	if (!CropData.CropStateData.HasState(ECropState::NutrientBurn))
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
		CropData.CropStateData.AddState(ECropState::NutrientBurn);
		CropData.CropQualityData.RecordFertilize(false);
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
		OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		return true;
	}
	return false;
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
	CropData.CropStateData.RemoveState(ECropState::ReadyForHarvest);
	OnPlantGrowthStageChanged.Broadcast(CropData.Coords, CropData.CropGrowthStage);
	OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
}
