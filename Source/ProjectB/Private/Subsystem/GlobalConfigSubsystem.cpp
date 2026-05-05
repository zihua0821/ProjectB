// Fill out your copyright notice in the Description page of Project Settings.


#include "Subsystem/GlobalConfigSubsystem.h"

void UGlobalConfigSubsystem::SetConfigs(UFarmlandConfigDataAsset* InFarmlandConfig,
	UPlanetConfigDataAsset* InPlanetConfig, UDataTable* InCropTable)
{
	FarmlandConfig = InFarmlandConfig;
	PlanetConfig = InPlanetConfig;
	CropDataTable = InCropTable;
}
