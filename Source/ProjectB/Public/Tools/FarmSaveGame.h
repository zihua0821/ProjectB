// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "GameFramework/SaveGame.h"
#include "FarmSaveGame.generated.h"

class UPlanetConfigDataAsset;
class UFarmlandConfigDataAsset;

/**
 * 地块信息结构
 */
USTRUCT(BlueprintType)
struct FFarmLandPlotSaveGameData
{
	GENERATED_BODY()

	// 坐标
	UPROPERTY()
	FIntPoint Coords;

	// 地块状态
	UPROPERTY()
	FPlotStateData StateData;

	// 杂草Transform数组
	UPROPERTY()
	TArray<FTransform> WeedsTransform;

	// 地块水分（百分比）
	UPROPERTY()
	float MoisturePercentage;

	// 地块肥力（百分比）
	UPROPERTY()
	float FertilityPercentage;
};

/**
 * 农作物数据结构
 */
USTRUCT(BlueprintType)
struct FCropSaveGameData
{
	GENERATED_BODY()

	// 唯一ID
	UPROPERTY()
	FName CropID;

	// 农作物坐标
	UPROPERTY()
	FIntPoint Coords;

	// 农作物Transform
	UPROPERTY()
	FTransform Transform;

	// 农作物变体索引
	UPROPERTY()
	int32 VariantsIndex = 0;

	// 农作物当前生长阶段
	UPROPERTY()
	ECropGrowthStage CropGrowthStage = ECropGrowthStage::Seed;

	// 农作物品质
	UPROPERTY()
	FCropQualityData CropQualityData;

	// 农作物状态
	UPROPERTY()
	FCropStateData CropStateData;

	// 农作物总生长时间
	UPROPERTY()
	float TotalGrowthTime = 0.f;

	// 农作物当前阶段生长时间
	UPROPERTY()
	float CurrentStageGrowthTime = 0.f;
};

/**
 * 农田数据结构
 */
USTRUCT(BlueprintType)
struct FPlanetSaveGameData
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	TSoftObjectPtr<UPlanetConfigDataAsset> PlanetConfigPath;
	
	UPROPERTY()
	TSoftObjectPtr<UFarmlandConfigDataAsset> FarmlandConfigPath;

	UPROPERTY()
	TSoftObjectPtr<UDataTable> CropDataTablePath;

	UPROPERTY()
	TArray<FFarmLandPlotSaveGameData> Plots;
	
	UPROPERTY()
	TArray<FCropSaveGameData> Crops;
};

// 全局数据（如金币、时间等）
USTRUCT(BlueprintType)
struct FGlobalSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FDateTime GameTime;        // 游戏内时间
};

/**
 *
 */
UCLASS()
class PROJECTB_API UFarmSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 SaveSlotVersion = 1;  // 版本号，用于以后兼容

	UPROPERTY()
	FGlobalSaveData GlobalData;

	UPROPERTY()
	TArray<FPlanetSaveGameData> Planets; 
};
