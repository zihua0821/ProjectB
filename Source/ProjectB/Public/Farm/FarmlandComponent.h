// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "Components/SceneComponent.h"
#include "Crops/CropData.h"
#include "FarmlandComponent.generated.h"


class UHierarchicalInstancedStaticMeshComponent;
class UCropBase;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UFarmlandComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UFarmlandComponent();
	// ==================== 配置接口 ====================
	// 设置地块配置数据
	void SetFarmlandConfig(const TObjectPtr<UFarmlandConfigDataAsset>& InFarmlandConfig){FarmlandConfig = InFarmlandConfig;}
	
	// =================== 种植接口 ====================
	// 左键点击
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords);

	// 重置地块
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ResetLand(FIntPoint PlowCoords);

	// 耕地
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void PlowLand(FIntPoint PlowCoords);
	
	// 移除耕地
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void RemoveLand(FIntPoint PlowCoords);
	
	// 浇水
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void WetLand(FIntPoint PlowCoords);

	// 变干
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void LandDried(FIntPoint PlowCoords);

	// 施肥
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void FertilizeLand(FIntPoint PlowCoords);

	// 清除施肥
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearFertilize(FIntPoint PlowCoords);

	// 生成杂草
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void GenerateWeeds(FIntPoint PlowCoords);

	// 清除杂草
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearWeeds(FIntPoint PlowCoords);

	// 生成虫害
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void GeneratePest(FIntPoint PlowCoords);

	// 清除虫害
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void ClearPest(FIntPoint PlowCoords);
	
	// 种植作物
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	bool PlantCrop(int32 PlotX, int32 PlotY, FName CropID);
    
	// 收获作物
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	bool HarvestCrop(int32 PlotX, int32 PlotY);
    
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
protected:
	virtual void BeginPlay() override;

private:
	// 地块网格配置
	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	// 地块信息索引
	UPROPERTY()
	TMap<FIntPoint, FFarmLandPlotInfo> PlotInfoIndex;

	// 地块渲染组件
	// 土地单元格渲染组件
	UPROPERTY()
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LandCellRenderer;

	// 杂草渲染组件
	UPROPERTY()
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WeedsRenderer;
	
	// 作物数据映射 [X + Y * GridWidth] -> CropBase
	UPROPERTY()
	TMap<int32, UCropBase*> Crops;
    
	// 作物渲染组件（按作物类型分组）
	UPROPERTY()
	TMap<FName, UHierarchicalInstancedStaticMeshComponent*> CropRenderers;
    
	// 地块索引到作物实例索引的映射
	TMap<int32, int32> PlotToCropInstanceIndex;
    
	// 辅助方法
	void InitializeLandCells();
	int32 GetPlotIndex(const FIntPoint& PlotCoords) const;
	FVector GetPlotWorldPosition(int32 X, int32 Y) const;
	void UpdateCropVisual(int32 PlotIndex, UCropBase* Crop);
    
	// 获取或创建渲染器
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GetOrCreateRenderer(FName CropID);

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};


