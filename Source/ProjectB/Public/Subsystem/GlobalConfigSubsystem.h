// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GlobalConfigSubsystem.generated.h"

class UPlanetConfigDataAsset;
class UFarmlandConfigDataAsset;
/**
 * 
 */
UCLASS()
class PROJECTB_API UGlobalConfigSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 设置配置（由 UCelManagerComponent 初始化时调用） */
	void SetConfigs(UFarmlandConfigDataAsset* InFarmlandConfig, UPlanetConfigDataAsset* InPlanetConfig);

	/** 获取农场配置 */
	UFarmlandConfigDataAsset* GetFarmlandConfig() const { return FarmlandConfig; }

	/** 获取星球配置 */
	UPlanetConfigDataAsset* GetPlanetConfig() const { return PlanetConfig; }

private:
	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	UPROPERTY()
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;
};
