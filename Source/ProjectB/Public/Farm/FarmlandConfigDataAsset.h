// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FarmlandConfigDataAsset.generated.h"

/**
 * 农田配置数据资产
 * 用于存储和管理农田的各种配置参数
 */
UCLASS(BlueprintType, Blueprintable)
class PROJECTB_API UFarmlandConfigDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== 网格配置 ====================
	// X方向单元格数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "X方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeX = 5;
    
	// Y方向单元格数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "Y方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeY = 5;
    
	// 单元格间距（单元格之间的空隙）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "单元格间距", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-100.0", UIMax = "100.0"))
	float CellSpacing = 20.0f;
    
	// 土地网格高度偏移（相对于地面）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "土地高度", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-100.0", UIMax = "100.0"))
	float LandHeightOffset = 30.0f;

	// 地面扩展
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "地面扩展", ClampMin = "-500.0", ClampMax = "500.0", UIMin = "-500.0", UIMax = "500.0"))
	float GroundExtension = 100.0f;

	// 是否初始化所有地块
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "是否初始化所有地块"))
	bool bInitAllLandCells = false;

	// 自定义数据浮点数数量(材质中设置的PerInstanceCustomData数量)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "自定义数据浮点数数量"))
	int32 NumCustomDataFloats = 2;

	// ==================== 地面配置 ====================
	// 是否使用地面
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Ground",
			  meta = (DisplayName = "是否使用地面"))
	bool bUseGround = true;
	
	// 地面网格模型（整个农场的基础地面）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Ground",
			  meta = (DisplayName = "地面网格模型"))
	TObjectPtr<UStaticMesh> GroundMesh;

	// ===================== 单元格配置 ====================
	// 单元格网格模型（每个单元格显示的土地）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell", 
			  meta = (DisplayName = "单元格网格模型"))
	TObjectPtr<UStaticMesh> LandCellMesh;
	
	// 单元格大小
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell",
			  meta = (DisplayName = "单元格大小", ClampMin = "10.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "500.0"))
	FVector2D CellSize = FVector2D(200.f, 200.f);
    
	// ==================== 调试配置 ====================
	
	// 是否显示单元格调试信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Configuration",
			  meta = (DisplayName = "显示调试信息"))
	bool bShowDebugGrid = false;

	// 调试信息持续时间
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Configuration",
			  meta = (DisplayName = "调试信息持续时间"))
	float DebugDuration = 1.f;
	
	// 是否自动更新地块组件配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Configuration",
			  meta = (DisplayName = "自动更新配置"))
	bool bAutoUpdateConfig = true;

	// ==================== 实用方法 ====================
	
	// 计算农场总大小
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Utility")
	FVector2D CalculateFarmTotalSize() const
	{
		float Width = GridSizeX * CellSize.X + (GridSizeX - 1) * CellSpacing + GroundExtension * 2.f;
		float Height = GridSizeY * CellSize.Y + (GridSizeY - 1) * CellSpacing + GroundExtension * 2.f;
		return FVector2D(Width, Height);
	}

	// 验证配置是否有效
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Validation")
	bool IsValidConfiguration() const
	{
		return LandCellMesh != nullptr && 
			   GroundMesh != nullptr &&
			   CellSize.X > 0 && CellSize.Y > 0 &&
			   GridSizeX > 0 && GridSizeY > 0;
	}

	// 获取总单元格数量
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Utility")
	int32 GetTotalCellCount() const
	{
		return GridSizeX * GridSizeY;
	}

	// 检查坐标是否在有效范围内
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Utility")
	bool IsCellCoordinateValid(int32 CellX, int32 CellY) const
	{
		return CellX >= 0 && CellX < GridSizeX && 
			   CellY >= 0 && CellY < GridSizeY;
	}
};