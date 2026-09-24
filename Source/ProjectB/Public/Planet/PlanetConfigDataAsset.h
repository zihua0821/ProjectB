// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OrbitManagerComponent.h"
#include "PlanetConfigDataAsset.generated.h"

/**
 * 
 */
UCLASS(BlueprintType, Blueprintable)
class PROJECTB_API UPlanetConfigDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== 星球配置 ====================

	// 星球半径（厘米）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config",
	meta = (DisplayName = "星球半径", ClampMin = "1000", ClampMax = "50000"))
	float Radius = 8000.0f;

	// 球体网格资源（默认使用引擎基础球体）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config",
	meta = (DisplayName = "球体网格"))
	TObjectPtr<UStaticMesh> SphereMesh;

	// 球体材质
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config",
	meta = (DisplayName = "地表材质"))
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	// ==================== 网格配置 ====================

	// 最小纬度（度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Grid",
	meta = (DisplayName = "最小纬度", ClampMin = "-90", ClampMax = "0"))
	float MinLat = -60.0f;

	// 最大纬度（度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Grid",
	meta = (DisplayName = "最大纬度", ClampMax = "90", ClampMax = "90"))
	float MaxLat = 60.0f;

	// 纬度格点数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Grid",
	meta = (DisplayName = "纬度格点数量", ClampMin = "2", ClampMax = "500"))
	int32 NumLat = 13;

	// 经度格点数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Grid",
	meta = (DisplayName = "经度格点数量", ClampMin = "4", ClampMax = "1000"))
	int32 NumLon = 36;

	// ==================== 轨道配置 ====================

	// 轨道定义列表（数据化：OrbitManager 启动时按此创建轨道）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Orbit",
	meta = (DisplayName = "轨道定义列表"))
	TArray<FOrbitDefinition> OrbitDefinitions;

	// ==================== 卫星作业配置 ====================

	// 悬停高度（cm；离轨作业时悬停点距球面的固定距离）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Work",
	meta = (DisplayName = "悬停高度(cm)", ClampMin = "0.0"))
	float HoverHeight = 1000.0f;

};
