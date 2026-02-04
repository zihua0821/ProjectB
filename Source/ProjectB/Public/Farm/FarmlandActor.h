// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FarmlandConfigDataAsset.h"
#include "Crops/CropData.h"
#include "GameFramework/Actor.h"
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

	// ==================== 配置属性 ====================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Farmland, meta = (DisplayName = "农田配置"))
    TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;
	
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

	// ==================== 公开方法 ====================
    
	// 获取地块组件
	UFUNCTION(BlueprintCallable, Category = "Farmland")
	UFarmlandComponent* GetFarmlandComponent() const { return FarmlandComponent; }
    
	// 获取农场边界（用于UI或碰撞检测）
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Farmland")
	FBox GetFarmBounds() const;
    
	// 获取指定单元格的世界位置
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Farmland")
	FVector GetCellWorldLocation(int32 CellX, int32 CellY) const;
	
	// 获取鼠标点击的单元格坐标（封装方法）
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	bool GetCellCoordinatesFromClick(const FVector& ClickLocation, FIntPoint& OutCellCoords) const;
    
	// 检查坐标是否有效
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	bool AreCellCoordinatesValid(int32 CellX, int32 CellY) const;
    
	// 获取点击的单元格中心位置
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	FVector GetCellCenterFromClick(const FVector& ClickLocation) const;

	// 获取地块信息
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	FFarmLandPlotInfo GetPlotInfo(const FIntPoint& PlotCoords) const;

	// ====================  农田交互 ====================
	// 耕地
	UFUNCTION(BlueprintCallable, Category = "Farmland|Interaction")
	void PlowLand(FIntPoint PlowCoords) const;
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
	
	int32 GetPlotIndex(int32 X, int32 Y) const;
    
	// ==================== 私有属性 ====================
	// 实例索引
	UPROPERTY()
	TMap<int32, int32> PlotToLandInstanceIndex;
	// 地块信息索引
	UPROPERTY()
	TMap<FIntPoint, FFarmLandPlotInfo> PlotInfoIndex;
	// 实例化渲染组件数组（每个单元格一个土地模型）
	UPROPERTY()
	TArray<UStaticMeshComponent*> LandCellComponents;

	    
	// 土地单元格渲染组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Farmland|Config", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInstancedStaticMeshComponent> LandCellRenderer;
	
	bool bComponentsInitialized = false;
};


