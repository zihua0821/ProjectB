// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PlanetConfigDataAsset.h"
#include "PlanetData.h"
#include "PlanetInterface.h"
#include "Crops/CropData.h"
#include "GameFramework/Actor.h"
#include "Tools/FarmSaveGame.h"
#include "PlanetActor.generated.h"

class UCellManagerComponent;
class UOrbitManagerComponent;
class UCropManagerComponent;
class UFarmlandConfigDataAsset;
class UFarmlandComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlanetInfoChanged, FPlanetInfo, PlanetInfo);

UCLASS()
class PROJECTB_API APlanetActor : public AActor, public IPlanetInterface
{
	GENERATED_BODY()

public:
	APlanetActor();

protected:
	virtual void BeginPlay() override;

	virtual void PostInitializeComponents() override;
public:

	// 获取cell组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UCellManagerComponent* GetCellManagerComponent() const { return CellManagerComponent; }
	
	// 获取地块组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UFarmlandComponent* GetFarmlandComponent() const { return FarmlandComponent; }
	
	// 处理左键点击
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords) const;

	// 地块轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void OutlinePlot(FIntPoint Coords, bool bOutline = true) const;

	// 获取农作物管理组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UCropManagerComponent* GetCropManagerComponent() const { return CropManagerComponent; }
	
	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;

	// 获取地块农作物信息
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	FPlotCropInfo GetPlotCropInfo(const FIntPoint& PlotCoords) const;

	// 设置星球数据
	virtual void SetPlanetInfo_Implementation(const FPlanetInfo& InPlanetData) override { PlanetData = InPlanetData; };

	// 获取星球数据
	virtual FPlanetInfo GetPlanetInfo_Implementation() const override { return PlanetData; }

	// 获取星球数据 (引用)
	virtual FPlanetInfo& GetPlanetInfoRef() override { return PlanetData; };

	// 通知数据已改变
	virtual void CallPlanetInfoChanged_Implementation() override;

	// 更新旋转
	virtual void UpdateRotation_Implementation(float NewAngle) override;

	// 获取轨道定义
	virtual FOrbitData GetOrbitData_Implementation(FName OrbitName) const override;

	// 获取单元格坐标Transform
	virtual FTransform GetCellWorldTransform_Implementation(FIntPoint CellCoord) const override;

	// 获取地块保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FFarmLandPlotSaveGameData> GetPlotSaveGameData() const;

	// 获取地块农作物保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FCropSaveGameData> GetCropSaveGameData() const;

	// 设置地块保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData);

	// 设置地块农作物保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetCropSaveGameData(const TArray<FCropSaveGameData>& InPlotCropSaveGameData);
	
	
	// 根组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 星球球体网格（地表）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SphereMeshComponent;

	// cell组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCellManagerComponent> CellManagerComponent;
	
	// 地块组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFarmlandComponent> FarmlandComponent;

	// 农作物管理组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCropManagerComponent> CropManagerComponent;

	// 轨道管理组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UOrbitManagerComponent> OrbitManagerComponent;

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

	UPROPERTY(BlueprintAssignable)
	FOnPlanetInfoChanged OnPlanetInfoChanged;
private:
	// 初始化组件
	void SetupComponents();
	void GenerateGridLines();
	FVector LatLonToWorldCoordinates(int32 LatIdx, int32 LonIdx) const;

	FPlanetInfo PlanetData;
	// ==================== 自动计算的参数（只读） ====================

	// 纬度步长（度）- 自动计算
	float LatStep = 1.0f;

	// 经度步长（度）- 自动计算
	float LonStep = 1.0f;

	// 总地块数量 - 自动计算
	int32 TotalPlots = 0;
};



