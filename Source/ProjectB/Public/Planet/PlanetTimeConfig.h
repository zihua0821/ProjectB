// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetTimeConfig.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UPlanetTimeConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ================================ 日夜循环配置 ================================

	// 白天时长（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight Cycle", meta = (ClampMin = "0.1"))
	float DayDurationSeconds = 60.0f;

	// 夜晚时长（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight Cycle", meta = (ClampMin = "0.1"))
	float NightDurationSeconds = 60.0f;

	// ================================ 四季循环配置 ================================

	// 是否启用四季循环
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Season Cycle")
	bool bEnableSeasons = true;

	// 每季时长（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Season Cycle", meta = (ClampMin = "0.1", EditCondition = "bEnableSeasons"))
	float SeasonDurationSeconds = 600.0f;

	// 地轴倾斜角（度），影响晨昏线偏移幅度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Season Cycle", meta = (ClampMin = "0", ClampMax = "90", EditCondition = "bEnableSeasons"))
	float AxialTilt = 23.44f;

	// ================================ 时间控制配置 ================================

	// 初始时间（0-24 小时制）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time Control")
	float InitialTimeOfDay = 12.0f;

	// 初始季节进度（0-1，0=春分，0.25=夏至，0.5=秋分，0.75=冬至）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time Control", meta = (ClampMin = "0", ClampMax = "1"))
	float InitialSeasonProgress = 0.0f;

	// 时间流速倍数（0=暂停，1=正常，2=2 倍速）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time Control", meta = (ClampMin = "0"))
	float TimeSpeedMultiplier = 1.0f;
};
