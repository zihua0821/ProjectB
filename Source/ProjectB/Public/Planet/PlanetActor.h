// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetActor.generated.h"

UCLASS()
class PROJECTB_API APlanetActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetActor();

protected:
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;

public:
	virtual void Tick(float DeltaTime) override;

	// 根组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 星球球体网格（地表）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SphereMeshComponent;
	
	// ==================== 星球配置 ====================

	// 星球半径（厘米）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config",
	meta = (DisplayName = "星球半径", ClampMin = "1000", ClampMax = "50000"))
	float Radius = 50000.0f;

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

	// ==================== 自动计算的参数（只读） ====================

	// 纬度步长（度）- 自动计算
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Grid|Auto",
	meta = (DisplayName = "纬度步长（自动）"))
	float LatStep = 1.0f;

	// 经度步长（度）- 自动计算
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Grid|Auto",
	meta = (DisplayName = "经度步长（自动）"))
	float LonStep = 1.0f;

	// 总地块数量 - 自动计算
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Grid|Auto",
	meta = (DisplayName = "总地块数量"))
	int32 TotalPlots = 0;

private:
	void GenerateGridLines();
	FVector LatLonToWorldCoordinates(int32 LatIdx, int32 LonIdx) const;
};


