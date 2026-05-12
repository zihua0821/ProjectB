#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Crops/CropData.h"
#include "Subsystem/ISMManager.h"
#include "CellMassFragments.generated.h"

// ========== 单元格基础 Fragment ==========

/**
 * 单元格静态数据（初始化后几乎不变）
 * 所有单元格实体必带此 Fragment
 */
USTRUCT(BlueprintType)
struct FCellStaticFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 网格坐标（稳定索引） */
	UPROPERTY()
	FIntPoint Coords;

	/** 世界空间变换（球面位置、朝向、缩放） */
	UPROPERTY()
	FTransform Transform;

	/** 单元格类型（空地 / 农场 / 工业 / 商业 / 仓库 / 传送带 / ...） */
	UPROPERTY()
	ECellType CellType = ECellType::Empty;
};

/**
 * 高亮状态 Fragment
 * 由 UCelManagerComponent 修改，由 UPlotVisualSyncProcessor 消费
 */
USTRUCT(BlueprintType)
struct FHighlightFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 当前 ISM 实例稳定 ID */
	FGlobalInstanceId InstanceId;
	
	/** 是否需要高亮 */
	UPROPERTY()
	bool bHighlighted = false;

	/** 上次高亮状态 */
	UPROPERTY()
	bool LastHighlightedState = false;

	/** 高亮颜色 */
	UPROPERTY()
	FLinearColor HighlightColor = FLinearColor::White;

	/** 上次高亮颜色 */
	UPROPERTY()
	FLinearColor LastHighlightColor = FLinearColor::White;
};

// ========== 农场地块 Fragment & Tag ==========

/**
 * 农场地块渲染状态
 */
USTRUCT(BlueprintType)
struct FPlotRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 当前 ISM 实例稳定 ID */
	FGlobalInstanceId InstanceId;
};

/**
 * 农场地块动态状态（湿度、肥力、状态位）
 */
USTRUCT(BlueprintType)
struct FPlotStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 耕地、浇水、施肥、杂草、虫害等状态位 */
	UPROPERTY()
	FPlotStateData StateData;

	UPROPERTY()
	FPlotStateData PreStateData;

	/** 水分百分比 [0,1] */
	UPROPERTY()
	float MoisturePercentage = 0.0f;

	/** 肥力百分比 [0,1] */
	UPROPERTY()
	float FertilityPercentage = 0.0f;
};

// ========== 作物相关 Fragment ==========

/**
 * 标记实体上有种植的作物
 */
USTRUCT(BlueprintType)
struct FCropTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * 作物静态标识（种植后不变）
 */
USTRUCT(BlueprintType)
struct FCropStaticConfigFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 对应数据表中的 CropID */
	UPROPERTY()
	FName CropID;

	/** 种子阶段所需时间 */
	UPROPERTY()
	float GrowthSeedTime = 0.0f;

	/** 幼苗阶段所需时间 */
	UPROPERTY()
	float GrowthSeedlingTime = 0.0f;

	/** 每秒水分消耗 */
	UPROPERTY()
	float MoistureConsumePerSecond = 0.0f;

	/** 每秒肥力消耗 */
	UPROPERTY()
	float FertilityConsumePerSecond = 0.0f;

	// 各生长阶段的网格体
	UPROPERTY()
	TObjectPtr<UStaticMesh> SeedMesh = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SeedlingMesh = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMesh> MatureMesh = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HarvestedMesh = nullptr;

	UPROPERTY()
	int32 CropNumCustomDataFloats = 0;
};

/**
 * 作物生长阶段与状态（低频变化）
 */
USTRUCT(BlueprintType)
struct FCropStateFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 当前生长阶段 */
	UPROPERTY()
	ECropGrowthStage CropGrowthStage = ECropGrowthStage::Seed;

	/** 需水、需肥、病害、可收获等状态位 */
	UPROPERTY()
	FCropStateData CropStateData;

	/** 上次渲染的生长阶段，用于判断是否需要切换 Mesh */
	UPROPERTY()
	ECropGrowthStage LastRenderedStage = ECropGrowthStage::None;
};

/**
 * 作物生长计时（高频变化，每 Tick 更新）
 */
USTRUCT(BlueprintType)
struct FCropGrowthTimerFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 已累计的生长总时间 */
	UPROPERTY()
	float TotalGrowthTime = 0.0f;

	/** 当前阶段已用时间 */
	UPROPERTY()
	float CurrentStageGrowthTime = 0.0f;
};

/**
 * 作物品质数据（中频变化）
 */
USTRUCT(BlueprintType)
struct FCropQualityFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 品质累加时间、事件计数、得分、等级 */
	UPROPERTY()
	FCropQualityData QualityData;
};

/**
 * 作物渲染状态（由渲染处理器维护）
 */
USTRUCT(BlueprintType)
struct FCropRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 当前 ISM 实例稳定 ID */
	FGlobalInstanceId InstanceId;

	/** 当前显示的网格体 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> CurrentMesh = nullptr;
};