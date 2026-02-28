// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "Components/SceneComponent.h"
#include "Crops/CropData.h"
#include "Tools/FarmSaveGame.h"
#include "FarmlandComponent.generated.h"

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
	
	// 设置地块配置数据
	void SetFarmlandConfig(const TObjectPtr<UFarmlandConfigDataAsset>& InFarmlandConfig){FarmlandConfig = InFarmlandConfig;}

	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;
	
	// 设置地块状态
	void SetPlotState(const FIntPoint& PlotCoords, EPlotState State, bool bEnable = true);

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
	
	// 创建轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void CreateOutline(FIntPoint Coords);

	// 移除轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void RemoveOutline(FIntPoint Coords);
	
	// 世界坐标转换为网格坐标
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FIntPoint WorldToCellCoordinates(const FVector& WorldPosition) const;
    
	// 网格坐标转换为世界坐标
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FVector CellToWorldCoordinates(int32 CellX, int32 CellY) const;
    
	// 获取鼠标点击的网格坐标（用于蓝图）
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	bool GetClickedCellCoordinates(const FVector& ClickLocation, FIntPoint& OutCellCoords) const;
	
	// ==================== 其他方法 ====================
	// 农作物每次生长
	void OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost);

protected:
	virtual void BeginPlay() override;

private:
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
	TObjectPtr<UInstancedStaticMeshComponent>  OutlineHISMComponent;

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
    
	// ===================== 辅助方法 =====================
	
	// 初始化地块
	void InitializeLandCells();
	
	// 获取地块坐标
	int32 GetPlotIndex(const FIntPoint& PlotCoords) const;

	// 更新轮廓颜色
	void UpdateOutlineColor() const;
	
	// 创建默认地块
	void CreateDefaultLandCells();
	
	// 获取地块包围盒
	bool GetCellBounds(const FIntPoint& Coords, FBox& OutBounds) const;

	// 编辑模式事件响应
	UFUNCTION()
	void OnEditModeChanged(EEditMode NewEditMode);
	
	// 开始杂草扩散定时器
	void StartWeedsSpreadTimers();

	// 开始虫害扩散定时器
	void StartPestsSpreadTimers();
	
	// 杂草扩散逻辑
	UFUNCTION()
	void SpreadWeeds();

	// 虫害扩散逻辑
	UFUNCTION()
	void SpreadPests();

	// 获取相邻地块坐标（八邻域）
	void GetNeighborCoords(FIntPoint Coords, TArray<FIntPoint>& OutNeighbors) const;
};



