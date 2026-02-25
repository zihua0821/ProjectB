// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Crops/CropBase.h"

#include "Farm/FarmlandComponent.h"

UCropBase::UCropBase(): CropData()
{
}

void UCropBase::InitializeCrop(UFarmlandComponent* InComponent, FCropData& InCropData)
{
	OwnerComponent = InComponent;
	if (OwnerComponent.IsValid())
	{
		OwnerComponent->OnPlotStateChanged.AddDynamic(this, &ThisClass::HandlePlotStateChanged);
	}
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
	const bool bIsCropNotGrowth = CropData.CropGrowthStage == ECropGrowthStage::Mature ||
								  CropData.CropGrowthStage == ECropGrowthStage::Harvested ||
								  (CropData.CropGrowthStage == ECropGrowthStage::Seed && CropData.CropStateData.HasState(ECropState::NeedsWater)) ||
								  (CropData.CropGrowthStage == ECropGrowthStage::Seedling && CropData.CropStateData.HasState(ECropState::NeedsFertilizer));
	if (bIsCropNotGrowth)
	{
		return;
	}
	UpdateCropQuality(DeltaTime);
	
	CropData.TotalGrowthTime += DeltaTime;
	switch (CropData.CropGrowthStage)
	{
	case ECropGrowthStage::Seed:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime;
		break;
	case ECropGrowthStage::Seedling:
		CropData.CurrentStageGrowthTime = CropData.TotalGrowthTime - CropData.GrowthSeedTime;
		break;
	default:
		UE_LOG(LogTemp, Error, TEXT("Crop is in Wrong CropGrowthStage!"))
		return;
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
		OnCropGrownThisTime.Execute(CropData.Coords, 0.001f, 0.002f);
	}
}

void UCropBase::ProcessOfflineGrowth(const FTimespan& OfflineDuration)
{
}

void UCropBase::HandleCropMoisture(const float PlotMoisture)
{
	const FCropStateData PreCropState = CropData.CropStateData;
	// 判断水分状态
	if (PlotMoisture >= 0.2f && PlotMoisture <= 0.8f)// 正常
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsWater);
		CropData.CropStateData.RemoveState(ECropState::OverWatered);
	}
	else if (PlotMoisture > 0.8f)// 过高
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsWater);
		CropData.CropStateData.AddState(ECropState::OverWatered);
	}
	else// 过低
	{
		CropData.CropStateData.RemoveState(ECropState::OverWatered);
		CropData.CropStateData.AddState(ECropState::NeedsWater);
	}
	if (PreCropState != CropData.CropStateData)
	{
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
	}
}

void UCropBase::HandleCropFertility(const float PlotFertility)
{
	const FCropStateData PreCropState = CropData.CropStateData;
	// 判断肥力状态
	if (PlotFertility >= 0.2f && PlotFertility <= 0.8f)// 正常
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
		CropData.CropStateData.RemoveState(ECropState::NutrientBurn);
	}
	else if (PlotFertility > 0.8f)// 过高
	{
		CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
		CropData.CropStateData.AddState(ECropState::NutrientBurn);
	}
	else// 过低
	{
		CropData.CropStateData.RemoveState(ECropState::NutrientBurn);
		CropData.CropStateData.AddState(ECropState::NeedsFertilizer);
	}
	if (PreCropState != CropData.CropStateData)
	{
		OnCropStateChanged.Broadcast(CropData.Coords, CropData.CropStateData);
	}
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

void UCropBase::BeginDestroy()
{
	if (OwnerComponent.IsValid())
	{
		OwnerComponent->OnPlotStateChanged.RemoveAll(this);
	}
	UObject::BeginDestroy();
}

void UCropBase::HandlePlotStateChanged(FIntPoint Coords, FPlotStateData NewPlotState)
{
	if (CropData.Coords != Coords)
	{
		UE_LOG(LogTemp, Error, TEXT("Crop is not in the same plot!"))
		return;
	}
	if (!OwnerComponent.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Crop owner component is invalid!"))
		return;
	}
	const FFarmLandPlotInfo& PlotInfo = OwnerComponent->GetPlotInfo(Coords);
	HandleCropMoisture(PlotInfo.MoisturePercentage);
	HandleCropFertility(PlotInfo.FertilityPercentage);
}
