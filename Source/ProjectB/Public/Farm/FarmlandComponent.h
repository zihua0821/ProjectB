// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "Components/SceneComponent.h"
#include "Crops/CropData.h"
#include "Tools/FarmSaveGame.h"
#include "FarmlandComponent.generated.h"

class UPlanetConfigDataAsset;
class ABasePlayerController;
class UInstancedStaticMeshComponent;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlotStateChanged, FIntPoint, Coords, FPlotStateData, NewPlotState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlotParamChanged, FIntPoint, Coords, float, NewPlotParam);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UFarmlandComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UFarmlandComponent();

	// =================== 事件 ====================
	// 地块状态改变
	UPROPERTY(BlueprintAssignable, Category = "Farmland|Events")
	FOnPlotStateChanged OnPlotStateChanged;

	// 地块参数湿度改变
	UPROPERTY(BlueprintAssignable, Category = "Farmland|Events")
	FOnPlotParamChanged OnPlotMoistureChanged;

	// 地块参数肥力改变
	UPROPERTY(BlueprintAssignable, Category = "Farmland|Events")
	FOnPlotParamChanged OnPlotFertilityChanged;
	
	// ==================== 配置接口 ====================

	// 设置星球配置数据
	void SetPlanetConfig(const TObjectPtr<UPlanetConfigDataAsset>& InPlanetConfig) {PlanetConfig = InPlanetConfig;}
	
	// 设置地块配置数据
	void SetFarmlandConfig(const TObjectPtr<UFarmlandConfigDataAsset>& InFarmlandConfig) {FarmlandConfig = InFarmlandConfig;}

	// 获取所有地块的 ISM 组件
	UFUNCTION(BlueprintCallable, Category = "Farmland|Debug")
	UInstancedStaticMeshComponent* GetLandCellRenderer() const { return LandCellRenderer; }

	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;
	
	// 设置地块状态
	void SetPlotState(const FIntPoint& PlotCoords, EPlotState State, bool bEnable = true);

	// ==================== 保存接口 ====================
	
	// 获取地块信息保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FFarmLandPlotSaveGameData> GetPlotSaveGameData();
	
	// 设置地块信息保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData){PlotSaveGameData = InPlotSaveGameData;}
	
	// =================== 地块操作 ====================
	
	// 处理左键点击
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords);

	// 重置地块
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ResetLand(FIntPoint Coords);

	// 耕地
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void PlowLand(FIntPoint Coords);
	
	// 移除耕地
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void RemoveLand(FIntPoint Coords);
	
	// 浇水
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void WetLand(FIntPoint Coords);

	// 变干
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void LandDried(FIntPoint Coords);

	// 施肥
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void FertilizeLand(FIntPoint Coords);

	// 清除施肥
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearFertilize(FIntPoint Coords);

	// 生成杂草
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void GenerateWeeds(FIntPoint Coords);

	// 清除杂草
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearWeeds(FIntPoint Coords);

	// 生成虫害
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void GeneratePest(FIntPoint Coords);

	// 清除虫害
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearPest(FIntPoint Coords);

	// =================== 绘制 ====================

	// 更新轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void UpdateOutline(FIntPoint Coords, bool bEnable);
	
	// 通过射线与地块 ISM 碰撞检测获取坐标
	bool GetPlotByRaycast(const FVector& RayStart, const FVector& RayEnd, FIntPoint& OutCellCoords, float& OutHitDistance) const;
	
	// ==================== 其他方法 ====================
	
	// 农作物每次生长
	void OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost);

protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	
	// 星球配置
	UPROPERTY()
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;

	// 农田配置
	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	// 地块信息Map
	UPROPERTY()
	TMap<FIntPoint, FFarmLandPlotInfo> PlotInfoIndex;

	// 土地单元格渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> LandCellRenderer;

	// 杂草渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WeedsRenderer;

	// 轮廓渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent>  LandCellOutlineRenderer;

	// 玩家控制器
	UPROPERTY()
	TObjectPtr<ABasePlayerController>  BasePlayerController;

	// 地块信息保存数据
	UPROPERTY()
	TArray<FFarmLandPlotSaveGameData> PlotSaveGameData;

	// 杂草扩散定时器
	FTimerHandle WeedSpreadTimerHandle;

	// 害虫扩散定时器
	FTimerHandle PestSpreadTimerHandle;

	// 当前选中的轮廓坐标
	FIntPoint CurrentOutlineCoords = FIntPoint(-1, -1);
    
	// ===================== 辅助方法 =====================
	
	// 初始化地块
	void InitializeLandCells();

	// 更新地块流失
	void UpdatePlotLoss(float DeltaTime);

	// 获取地块坐标
	int32 GetPlotIndex(const FIntPoint& PlotCoords) const;

	// 更新轮廓颜色
	void UpdateOutlineColor(FIntPoint Coords) const;

	// 创建默认地块
	void CreateDefaultLandCells();

	// 编辑模式事件响应
	UFUNCTION()
	void OnEditModeChanged(EEditMode NewEditMode);

	// 处理地块水分变化
	void HandlePlotMoistureChanged(FIntPoint Coords);

	// 处理地块肥力变化
	void HandlePlotFertilityChanged(FIntPoint Coords);

	// 开始杂草扩散定时器
	void StartWeedsSpreadTimers();

	// 开始虫害扩散定时器
	void StartPestsSpreadTimers();

	// 停止杂草扩散定时器
	bool TryStopWeedSpreadTimer();

	// 杂草扩散逻辑
	UFUNCTION()
	void SpreadWeeds();

	// 停止虫害扩散定时器
	bool TryStopPestSpreadTimer();

	// 虫害扩散逻辑
	UFUNCTION()
	void SpreadPests();

	// 获取相邻地块坐标（八邻域）
	void GetNeighborCoords(FIntPoint Coords, TArray<FIntPoint>& OutNeighbors) const;

	// 从保存数据初始化地块
	void InitPlotFromSaveGameData();

	// 更新地块可视化
	void UpdatePlotVisual(const FIntPoint& PlotCoords);

	// 初始化地块渲染组件
	void InitLandCellRenderer();

	// 初始化轮廓渲染组件
	void InitLandCellOutlineRenderer();

};



