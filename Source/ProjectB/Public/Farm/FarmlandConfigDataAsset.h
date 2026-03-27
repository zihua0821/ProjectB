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
	UE_DEPRECATED("0.2", "已废弃")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "X方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeX = 5;
    
	// Y方向单元格数量
	UE_DEPRECATED("0.2", "已废弃")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "Y方向数量", ClampMin = "1", ClampMax = "50", UIMin = "1", UIMax = "50"))
	int32 GridSizeY = 5;
    
	// 单元格间距（单元格之间的空隙）
	UE_DEPRECATED("0.2", "已废弃")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration",
			  meta = (DisplayName = "单元格间距", ClampMin = "-100.0", ClampMax = "100.0", UIMin = "-100.0", UIMax = "100.0"))
	float CellSpacing = 20.0f;
    
	

	// 地面扩展
	UE_DEPRECATED("0.2", "已废弃")
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
	UE_DEPRECATED("0.2", "已废弃")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Ground",
			  meta = (DisplayName = "是否使用地面"))
	bool bUseGround = true;
	
	// 地面网格模型（整个农场的基础地面）
	UE_DEPRECATED("0.2", "已废弃")
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
			  meta = (DisplayName = "单元格大小"))
	FVector2D CellSize = FVector2D(400.f, 400.f);

	// 边缘最小缩放系数
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell",
			  meta = (DisplayName = "边缘最小缩放系数", ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float MinLatitudeScale = 0.5f;
	
	// z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell",
			  meta = (DisplayName = "单元格z偏移"))
	float CellOffsetZ;

	// 单元格高度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid Configuration|Cell",
			  meta = (DisplayName = "单元格高度"))
	float LandHeightOffset = 30.0f;
	

	// ==================== 地块配置 ================

	// 地块每秒水分流失
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land Cell",
		meta = (DisplayName = "地块每秒水分流失", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float NaturalMoistureLossPerSecond = 0.001f;

	// 地块每秒肥力流失
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land Cell",
		meta = (DisplayName = "地块每秒肥力流失", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float NaturalFertilityLossPerSecond = 0.001f;

	// ============== 作物配置 ==============
	// 作物z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crop",
			  meta = (DisplayName = "作物z偏移"))
	FVector CropOffset;
	
	// ============== 杂草配置 ==============
	// 杂草模型
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weeds", 
			  meta = (DisplayName = "杂草模型"))
	TObjectPtr<UStaticMesh> WeedsMesh;

	// 地块杂草数量
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weeds", 
			  meta = (DisplayName = "地块杂草数量"))
	int32 WeedNum = 9;

	// 杂草缩放
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weeds", 
			  meta = (DisplayName = "杂草缩放"))
	float WeedsScale = 1.f;

	// 杂草z偏移
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weeds", 
			  meta = (DisplayName = "杂草z偏移"))
	float WeedsOffsetZ = 10.f;
	
	// ============== 虫害配置 ==============
	// 虫害粒子系统
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pest", 
			  meta = (DisplayName = "虫害粒子系统"))
	TObjectPtr<UNiagaraSystem> PestNiagaraSystem;

	// 虫害高度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pest", 
			  meta = (DisplayName = "虫害高度"))
	float PestHeight = 100.f;
	
	// 虫害粒子半径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pest", 
			  meta = (DisplayName = "虫害粒子半径"))
	float PestRadius = 50.f;

	// ==================== 生长倍率系数 ====================
	// 当作物处于相应状态时，生长速度乘以该系数（0~1）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Rate Modifiers",
			  meta = (DisplayName = "浇水过多倍率", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float GrowthRate_OverWatered = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Rate Modifiers",
			  meta = (DisplayName = "肥料烧伤倍率", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float GrowthRate_NutrientBurn = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Rate Modifiers",
			  meta = (DisplayName = "有杂草倍率", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float GrowthRate_WeedInfested = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Rate Modifiers",
			  meta = (DisplayName = "有虫害倍率", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float GrowthRate_PestInfested = 0.6f;

	// ==================== 作物售价倍率 ====================

	// 劣质作物价格倍率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale Price Modifiers",
			  meta = (DisplayName = "劣质作物价格倍率", ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
	float SalePriceMultiplier_Poor = 0.1f;
	
	// 普通作物价格倍率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale Price Modifiers",
			  meta = (DisplayName = "普通作物价格倍率", ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
	float SalePriceMultiplier_Normal = 1.f;

	// 良好作物价格倍率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale Price Modifiers",
			  meta = (DisplayName = "良好作物价格倍率", ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
	float SalePriceMultiplier_Good = 1.5f;

	// 优质作物价格倍率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sale Price Modifiers",
			  meta = (DisplayName = "优质作物价格倍率", ClampMin = "0.0", ClampMax = "5.0", UIMin = "0.0", UIMax = "5.0"))
	float SalePriceMultiplier_Excellent = 2.f;

	// ==================== 操作配置 ====================

	// 每次灌溉湿度增加
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions Configuration",
			  meta = (DisplayName = "每次灌溉湿度增加", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IncreasedMoistureWithEachIrrigation = 0.6f;

	// 每次施肥肥力增加
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions Configuration",
			  meta = (DisplayName = "每次施肥肥力增加", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float IncreasedFertilityWithEachFertilization = 0.6f;

	// ============== 杂草扩散配置 ==============
	
	// 杂草扩散检测间隔（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Weed")
	float WeedSpreadInterval = 5.0f;

	// 每个感染地块向邻居传播的概率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Weed")
	float WeedSpreadProbability = 0.1f;

	// 杂草是否可以传播到未耕地？若false则只传播到已耕地
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Weed")
	bool bWeedCanSpreadToEmptyPlots = true;

	// ============== 虫害扩散配置 ==============
	
	// 虫害扩散检测间隔（秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Pest")
	float PestSpreadInterval = 5.0f;

	// 每个感染地块向邻居传播的概率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Pest")
	float PestSpreadProbability = 0.05f;

	// 虫害是否可以传播到无作物的地块？通常虫害需要作物
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread|Pest")
	bool bPestCanSpreadToEmptyPlots = false;
	
	// ==================== 选中配置 ====================
	// 高亮材质
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", 
			  meta = (DisplayName = "高亮材质"))
	TObjectPtr<UMaterialInstance> OutlineMaterial;

	// 高亮材质偏移Z
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", 
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