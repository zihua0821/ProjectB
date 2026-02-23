// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FarmlandConfigDataAsset.generated.h"

class UNiagaraSystem;
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

	// 作物z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell",
			  meta = (DisplayName = "单元格z偏移"))
	float CellOffsetZ;

	// ============== 作物配置 ==============
	// 作物z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Crop",
			  meta = (DisplayName = "作物z偏移"))
	FVector CropOffset;
	
	// ============== 杂草配置 ==============
	// 杂草模型
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Weeds", 
			  meta = (DisplayName = "杂草模型"))
	TObjectPtr<UStaticMesh> WeedsMesh;

	// 地块杂草数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Weeds", 
			  meta = (DisplayName = "地块杂草数量"))
	int32 WeedNum = 9;

	// 杂草缩放
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Weeds", 
			  meta = (DisplayName = "杂草缩放"))
	float WeedsScale = 1.f;

	// 杂草z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Weeds", 
			  meta = (DisplayName = "杂草z偏移"))
	float WeedsOffsetZ = 10.f;
	
	// ============== 虫害配置 ==============
	// 虫害粒子系统
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Pest", 
			  meta = (DisplayName = "虫害粒子系统"))
	TObjectPtr<UNiagaraSystem> PestNiagaraSystem;

	// 虫害高度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Pest", 
			  meta = (DisplayName = "虫害高度"))
	float PestHeight = 100.f;
	
	// 虫害粒子半径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Pest", 
			  meta = (DisplayName = "虫害粒子半径"))
	float PestRadius = 50.f;

	// ==================== 选中配置 ====================
	// 高亮材质
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Outline", 
			  meta = (DisplayName = "高亮材质"))
	TObjectPtr<UMaterialInstance> OutlineMaterial;

	// 高亮材质偏移Z
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Outline", 
			  meta = (DisplayName = "高亮材质偏移Z"))
	float OutlineOffsetZ = 1.f;

	
	// ==================== 调试配置 ====================
	// 是否显示单元格调试信息
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Configuration",
			  meta = (DisplayName = "显示调试信息"))
	bool bShowDebugGrid = false;

	// 调试信息持续时间
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug Configuration",
			  meta = (DisplayName = "调试信息持续时间"))
	float DebugDuration = 1.f;

};