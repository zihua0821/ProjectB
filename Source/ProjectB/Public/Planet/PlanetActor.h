// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PlanetConfigDataAsset.h"
#include "GameFramework/Actor.h"
#include "PlanetActor.generated.h"

class UFarmlandConfigDataAsset;
class UFarmlandComponent;

UCLASS()
class PROJECTB_API APlanetActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetActor();

protected:
	virtual void BeginPlay() override;

	virtual void PostInitializeComponents() override;
public:

	// 获取地块组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UFarmlandComponent* GetFarmlandComponent() const { return FarmlandComponent; }
	
	// 根组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 星球球体网格（地表）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SphereMeshComponent;

	// 地块组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFarmlandComponent> FarmlandComponent;

	// ==================== 配置属性 ====================

	// 星球配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "星球配置"))
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;

	// 农田配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农田配置"))
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	// 农作物配置表
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农作物配置表"))
	TObjectPtr<UDataTable> CropDataTable;


private:
	// 初始化组件
	void SetupComponents();
	void GenerateGridLines();
	FVector LatLonToWorldCoordinates(int32 LatIdx, int32 LonIdx) const;

	// ==================== 自动计算的参数（只读） ====================

	// 纬度步长（度）- 自动计算
	float LatStep = 1.0f;

	// 经度步长（度）- 自动计算
	float LonStep = 1.0f;

	// 总地块数量 - 自动计算
	int32 TotalPlots = 0;
};


