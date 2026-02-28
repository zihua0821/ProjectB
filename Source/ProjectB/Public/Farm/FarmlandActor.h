// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "GameFramework/Actor.h"
#include "Tools/FarmSaveGame.h"
#include "FarmlandActor.generated.h"

class UFarmlandComponent;
class UCropManagerComponent;

UCLASS()
class PROJECTB_API AFarmlandActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AFarmlandActor();

protected:
	// 根
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland|Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 地块组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland|Components")
	TObjectPtr<UFarmlandComponent> FarmlandComponent;

	// 农作物管理组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland|Components")
	TObjectPtr<UCropManagerComponent> CropManagerComponent;

	// 地面网格（整个农场的地面）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland|Components")
	TObjectPtr<UStaticMeshComponent> GroundMeshComponent;
	
	virtual void BeginPlay() override;
	
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void PostInitializeComponents() override;
	
public:
	virtual void Tick(float DeltaTime) override;

	// ==================== 配置属性 ====================

	// 农田配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农田配置"))
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	// 农作物配置表
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农作物配置表"))
	TObjectPtr<UDataTable> CropDataTable;
	
	// ==================== 公开方法 ====================
	
	// 获取地块组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UFarmlandComponent* GetFarmlandComponent() const { return FarmlandComponent; }

	// 获取农作物管理组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UCropManagerComponent* GetCropManagerComponent() const { return CropManagerComponent; }
	
	// 获取鼠标点击的单元格坐标（封装方法）
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	bool GetCellCoordinatesFromClick(const FVector& ClickLocation, FIntPoint& OutCellCoords) const;
	
	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;

	// 获取地块农作物信息
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	FPlotCropInfo GetPlotCropInfo(const FIntPoint& PlotCoords) const;

	// 获取地块保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FFarmLandPlotSaveGameData> GetPlotSaveGameData() const;

	// 设置地块保存数据
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData);
	
	// ====================  农田交互 ====================
	
	// 处理左键点击
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords) const;
	
	// 地块轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void OutlinePlot(FIntPoint Coords, bool bOutline = true) const;

private:
	// ==================== 私有方法 ====================
    
	// 初始化组件
	void SetupComponents();
    
	// 更新地面网格大小
	void UpdateGroundMeshSize() const;
    
	// 调试绘制
	void DrawDebugGrid() const;
    
	// 计算农场总大小
	FVector2D CalculateFarmTotalSize() const;
	
	// 获取指定单元格的世界位置
	FVector GetCellWorldLocation(const FIntPoint& CellCoords) const;

	// 组件是否初始化
	bool bComponentsInitialized = false;
};



