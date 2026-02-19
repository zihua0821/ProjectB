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
class UCropBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlotStateChanged, FIntPoint, Coords, FPlotStateData, NewPlotState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCropPlantedOrRemoved, FIntPoint, Coords);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UFarmlandComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UFarmlandComponent();

	// =================== 事件 ====================
	// 地块状态改变
	UPROPERTY(BlueprintAssignable, Category = "Farmland|Events")
	FOnPlotStateChanged OnPlotStateChanged;

	// 农作物播种或移除
	UPROPERTY(BlueprintAssignable, Category = "Farmland|Events")
	FOnCropPlantedOrRemoved OnCropPlantedOrRemoved;
	
	// ==================== 配置接口 ====================
	// 设置地块配置数据
	void SetFarmlandConfig(const TObjectPtr<UFarmlandConfigDataAsset>& InFarmlandConfig){FarmlandConfig = InFarmlandConfig;}

	// 设置农作物数据表
	void SetCropDataTable(const TObjectPtr<UDataTable>& InCropDataTable){CropDataTable = InCropDataTable;}
	
	// =================== 种植接口 ====================
	// 左键点击
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

	// 移除作物
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void RemoveCrop(FIntPoint Coords);
	
	// 种植作物
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void PlantCrop(FIntPoint Coords);
    
	// 收获作物
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	bool HarvestCrop(FIntPoint Coords);

	// 作物干枯
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void CropDried(FIntPoint Coords);

	// 干枯恢复
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void UnCropDried(FIntPoint Coords);

	// 创建轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void CreateOutline(FIntPoint Coords);

	// 移除轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void RemoveOutline(FIntPoint Coords);
	
	// 获取作物
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Farmland")
	UCropBase* GetCropAtPlot(int32 PlotX, int32 PlotY) const;
    
	// 更新所有作物生长
	void UpdateCrops(float DeltaTime);

	// 世界坐标转换为网格坐标
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FIntPoint WorldToCellCoordinates(const FVector& WorldPosition) const;
    
	// 网格坐标转换为世界坐标
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FVector CellToWorldCoordinates(int32 CellX, int32 CellY) const;
    
	// 获取鼠标点击的网格坐标（用于蓝图）
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	bool GetClickedCellCoordinates(const FVector& ClickLocation, FIntPoint& OutCellCoords) const;

	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland|Coordinate")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;

	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FFarmLandPlotSaveGameData> GetPlotSaveGameData();

	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData){PlotSaveGameData = InPlotSaveGameData;}
protected:
	virtual void BeginPlay() override;

private:
	// 地块网格配置
	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	// 地块信息索引
	UPROPERTY()
	TMap<FIntPoint, FFarmLandPlotInfo> PlotInfoIndex;

	// 土地单元格渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> LandCellRenderer;

	// 杂草渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> WeedsRenderer;

	// 农作物数据表
	UPROPERTY()
	TObjectPtr<UDataTable> CropDataTable;

	// 农作物渲染组件
	UPROPERTY()
	TMap<FStaticMeshHISMKey, UInstancedStaticMeshComponent*> CropHISMComponentMap;

	// 轮廓渲染组件
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent>  OutlineHISMComponent;

	UPROPERTY()
	TObjectPtr<ABasePlayerController>  BasePlayerController;

	UPROPERTY()
	TArray<FFarmLandPlotSaveGameData> PlotSaveGameData;
    
	// 辅助方法
	void InitializeLandCells();
	int32 GetPlotIndex(const FIntPoint& PlotCoords) const;
	FVector GetPlotWorldPosition(int32 X, int32 Y) const;
	void UpdateCropVisual(int32 PlotIndex, UCropBase* Crop);

	void SetCropDisease(FIntPoint Coords,bool bDisease, bool bLeafs = false);
	void UpdateOutlineColor();
	void CreateDefaultLandCells();
	bool GetCellBounds(int32 X, int32 Y, FBox& OutBounds) const;

	// 获取或创建渲染器
	TObjectPtr<UInstancedStaticMeshComponent> GetOrCreateRenderer(FName CropID);

	TObjectPtr<UInstancedStaticMeshComponent> GetOrCreateCropHISMC(const FFarmLandPlotInfo& PlotInfo);

	UFUNCTION()
	void OnCropGrowthStageChanged(FIntPoint Coords, ECropGrowthStage NewGrowthStage);

	UFUNCTION()
	void OnEditModeChanged(EEditMode NewEditMode);
public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};



