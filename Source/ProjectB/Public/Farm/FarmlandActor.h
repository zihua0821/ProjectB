// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "Crops/CropData.h"
#include "GameFramework/Actor.h"
#include "Tools/FarmSaveGame.h"
#include "FarmlandActor.generated.h"

class UFarmlandComponent;

UCLASS()
class PROJECTB_API AFarmlandActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AFarmlandActor();

protected:
	// 根
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 地块组件
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Components")
	TObjectPtr<UFarmlandComponent> FarmlandComponent;

	// 地面网格（整个农场的地面）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland|Components")
	TObjectPtr<UStaticMeshComponent> GroundMeshComponent;
	
	virtual void BeginPlay() override;
	
	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void PostInitializeComponents() override;

// #if WITH_EDITOR
// 	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
// 	virtual void PostEditMove(bool bFinished) override;
// #endif
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// ==================== 配置属性 ====================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农田配置"))
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农作物配置表"))
	TObjectPtr<UDataTable> CropDataTable;
	
	// ==================== 公开方法 ====================
	// 获取地块组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UFarmlandComponent* GetFarmlandComponent() const { return FarmlandComponent; }
    
	// 获取指定单元格的世界位置
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Farmland")
	FVector GetCellWorldLocation(int32 CellX, int32 CellY) const;
	
	// 获取鼠标点击的单元格坐标（封装方法）
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	bool GetCellCoordinatesFromClick(const FVector& ClickLocation, FIntPoint& OutCellCoords) const;
	
	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;

	UFUNCTION(BlueprintCallable, Category = "Farmland")
	TArray<FFarmLandPlotSaveGameData> GetPlotSaveGameData() const;

	UFUNCTION(BlueprintCallable, Category = "Farmland")
	void SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData);
	// ====================  农田交互 ====================
	// 处理左键点击
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords) const;

	// 种植作物
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void PlantCrop(FIntPoint PlowCoords, FName CropID);

	// 收获作物
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	bool HarvestCrop(FIntPoint Coords);

	// 设置作物干旱
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void SetCropDry(FIntPoint Coords,bool bDry);

	// 地块轮廓
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void OutlinePlot(FIntPoint Coords, bool bOutline = true) const;

private:
	// ==================== 私有方法 ====================
    
	// 初始化组件
	void InitializeComponents();
    
	// 更新地面网格大小
	void UpdateGroundMeshSize() const;
    
	// 调试绘制
	void DrawDebugGrid() const;
    
	// 计算农场总大小
	FVector2D CalculateFarmTotalSize() const;
	
	
	bool bComponentsInitialized = false;
};



