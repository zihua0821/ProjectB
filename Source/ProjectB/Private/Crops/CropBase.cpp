// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Crops/CropBase.h"

UCropBase::UCropBase()
{
}

void UCropBase::InitializeCrop(const FCropDataRow& Data, const FDateTime& InPlantTime)
{
}

void UCropBase::UpdateCrop(float DeltaTime)
{
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
}
