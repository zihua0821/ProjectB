// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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
    
	// 单元格网格模型（每个单元格显示的土地）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid", 
			  meta = (DisplayName = "土地网格模型"))
	TObjectPtr<UStaticMesh> LandCellMesh;
    
	// 地面网格模型（整个农场的基础地面）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "地面网格模型"))
	TObjectPtr<UStaticMesh> GroundMesh;
    
	// 单元格大小
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "单元格大小", ClampMin = "10.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "500.0"))
	FVector2D CellSize = FVector2D(200.f, 200.f);
    
	// X方向单元格数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "X方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeX = 10;
    
	// Y方向单元格数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "Y方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeY = 10;
    
	// 单元格间距（单元格之间的空隙）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "单元格间距", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-100.0", UIMax = "100.0"))
	float CellSpacing = 0.0f;
    
	// 土地网格高度偏移（相对于地面）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "土地高度", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-100.0", UIMax = "100.0"))
	float LandHeightOffset = 30.0f;

	// 地面扩展
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",
			  meta = (DisplayName = "地面扩展", ClampMin = "-500.0", ClampMax = "500.0", UIMin = "-500.0", UIMax = "500.0"))
	float GroundExtension = 100.0f;

	// 是否初始化所有地块
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Grid",meta = (DisplayName = "是否初始化所有地块"))
	bool bInitAllLandCells = false;
    
	// 是否显示单元格调试信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Debug",
			  meta = (DisplayName = "显示调试信息"))
	bool bShowDebugGrid = false;

	// 调试信息持续时间
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Debug",
			  meta = (DisplayName = "调试信息持续时间"))
	float DebugDuration = 1.f;
	
	// 是否自动更新地块组件配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Farmland|Debug",
			  meta = (DisplayName = "自动更新配置"))
	bool bAutoUpdateConfig = true;

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

	TObjectPtr<UStaticMesh> GetLandCellMesh() const { return LandCellMesh; }

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


