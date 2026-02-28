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
		OwnerComponent->OnPlotMoistureChanged.AddDynamic(this, &ThisClass::HandlePlotMoistureChanged);
		OwnerComponent->OnPlotFertilityChanged.AddDynamic(this, &ThisClass::HandlePlotFertilityChanged);
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
	// ===== 1. 收获后：既不更新品质，也不生长 =====
	if (CropData.CropGrowthStage == ECropGrowthStage::Harvested)
	{
		return;
	}

	// ===== 2. 种子休眠：种子阶段同时缺水和缺肥时，不更新品质也不生长 =====
	if (CropData.CropGrowthStage == ECropGrowthStage::Seed &&
		CropData.CropStateData.HasState(ECropState::NeedsWater) &&
		CropData.CropStateData.HasState(ECropState::NeedsFertilizer))
	{
		return;
	}

	// ===== 3. 更新品质数据（所有未收获的阶段都要更新，包括成熟）=====
	UpdateCropQuality(DeltaTime);

	// ===== 4. 成熟阶段：只更新品质，不生长，不消耗资源 =====
	if (CropData.CropGrowthStage == ECropGrowthStage::Mature)
	{
		return;
	}

	// ===== 以下为生长逻辑（仅种子和幼苗阶段）=====

	// 计算生长速率
	float GrowthRate = 1.0f;

	// 严重缺水或缺肥 → 完全停止生长（但品质已记录）
	if (CropData.CropStateData.HasState(ECropState::NeedsWater) ||
		CropData.CropStateData.HasState(ECropState::NeedsFertilizer))
	{
		GrowthRate = 0.0f;
	}
	else
	{
		// 其他负面状态按比例减慢生长
		if (CropData.CropStateData.HasState(ECropState::OverWatered))
			GrowthRate *= CropData.GrowthRate_OverWatered;
		if (CropData.CropStateData.HasState(ECropState::NutrientBurn))
			GrowthRate *= CropData.GrowthRate_NutrientBurn;
		if (CropData.CropStateData.HasState(ECropState::WeedInfested))
			GrowthRate *= CropData.GrowthRate_WeedInfested;
		if (CropData.CropStateData.HasState(ECropState::PestInfested))
			GrowthRate *= CropData.GrowthRate_PestInfested;
	}

	// 生长速率为0不生长
	if (GrowthRate <= 0.f)
	{
		return;
	}
	
	CropData.TotalGrowthTime += DeltaTime * GrowthRate;
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
		OnCropGrownThisTime.Execute(CropData.Coords, CropData.MoistureConsumeRate, CropData.FertilityConsumeRate);
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
		if (PreCropState.HasState(ECropState::NeedsWater) && !CropData.CropStateData.HasState(ECropState::OverWatered))
		{
			CropData.CropQualityData.RecordWatering(true);
			OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		}
		if (CropData.CropStateData.HasState(ECropState::OverWatered))
		{
			CropData.CropQualityData.RecordWatering(false);
			OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		}
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
		if (PreCropState.HasState(ECropState::NeedsFertilizer) && !CropData.CropStateData.HasState(ECropState::NutrientBurn))
		{
			CropData.CropQualityData.RecordFertilize(true);
			OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		}
		if (CropData.CropStateData.HasState(ECropState::NutrientBurn))
		{
			CropData.CropQualityData.RecordFertilize(false);
			OnCropCropQualityChanged.Broadcast(CropData.Coords, CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime));
		}
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

void UCropBase::HandleCropPestInfested(bool bIsPestInfested)
{
	if (bIsPestInfested)
	{
		CropData.CropStateData.AddState(ECropState::PestInfested);
	}
	else
	{
		CropData.CropStateData.RemoveState(ECropState::PestInfested);
	}
}

void UCropBase::HandleCropWeedInfested(bool bIsWeedInfested)
{
	if (bIsWeedInfested)
	{
		CropData.CropStateData.AddState(ECropState::WeedInfested);
	}
	else
	{
		CropData.CropStateData.RemoveState(ECropState::WeedInfested);
	}
}

void UCropBase::BeginDestroy()
{
	if (OwnerComponent.IsValid())
	{
		OwnerComponent->OnPlotStateChanged.RemoveAll(this);
		OwnerComponent->OnPlotMoistureChanged.RemoveAll(this);
		OwnerComponent->OnPlotFertilityChanged.RemoveAll(this);
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
	HandleCropPestInfested(NewPlotState.HasState(EPlotState::PestInfested));
	HandleCropWeedInfested(NewPlotState.HasState(EPlotState::WeedInfested));
}

void UCropBase::HandlePlotMoistureChanged(FIntPoint Coords, float NewMoisture)
{
	if (CropData.Coords != Coords) return; 
	HandleCropMoisture(NewMoisture);
}

void UCropBase::HandlePlotFertilityChanged(FIntPoint Coords, float NewFertility)
{
	if (CropData.Coords != Coords) return;
	HandleCropFertility(NewFertility);
}
