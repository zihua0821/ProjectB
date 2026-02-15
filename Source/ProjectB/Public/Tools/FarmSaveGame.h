// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "GameFramework/SaveGame.h"
#include "FarmSaveGame.generated.h"

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

	// 地块Transform
	UPROPERTY()
	FTransform Transform;

	// 地块状态
	UPROPERTY()
	FPlotStateData StateData;

	// 杂草Transform数组
	UPROPERTY()
	TArray<FTransform> WeedsTransform;

	// 农作物
	UPROPERTY()
	FCropData CropData;
};

/**
 * 农田数据结构
 */
USTRUCT(BlueprintType)
struct FFarmlandSaveGameData
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	TSoftObjectPtr<UFarmlandConfigDataAsset> FarmlandConfigPath;

	UPROPERTY()
	TSoftObjectPtr<UDataTable> CropDataTablePath;

	UPROPERTY()
	TArray<FFarmLandPlotSaveGameData> Plots;
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
	TArray<FFarmlandSaveGameData> Farmlands;  // 所有地块的数据
};
