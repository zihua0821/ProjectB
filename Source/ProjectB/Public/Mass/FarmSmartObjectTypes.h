// FarmSmartObjectTypes.h
// 农场智能对象类型定义：坐标荷载、操作行为定义
#pragma once

#include "CoreMinimal.h"
#include "SmartObjectTypes.h"        // FSmartObjectDefinitionData（定义数据基类）
#include "SmartObjectDefinition.h"   // USmartObjectBehaviorDefinition（行为定义基类）
#include "FarmSmartObjectTypes.generated.h"

// ============================================================================
// 格子坐标荷载（定义数据：实例化时传入，标记该智能对象对应哪个格子）
// 继承 FSmartObjectDefinitionData 以支持 Slot 级 DefinitionData 存取
// ============================================================================
USTRUCT(BlueprintType)
struct PROJECTB_API FCoordinatePayload : public FSmartObjectDefinitionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Farm")
	FIntPoint Coords = FIntPoint::ZeroValue;

	FCoordinatePayload() = default;
	FCoordinatePayload(const FIntPoint& InCoords) : Coords(InCoords) {}
};

// ============================================================================
// 农场操作行为定义
// 基类统一声明操作耗时（Duration）；派生类只留各自的特殊参数。
// 每个操作一个轻量 UCLASS，挂到智能对象资产 Slot 的 BehaviorDefinitions 数组。
// 引擎按类匹配取参数（GetBehaviorDefinitionByType 用 IsChildOf 查找），
// 同一 Slot 可挂多个不同操作类；行为挂哪个 Slot 由资产配置决定。
// ============================================================================

/** 农场操作基类：统一声明操作耗时 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, Abstract)
class PROJECTB_API UFarmActionBehaviorBase : public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()

public:
	/** 操作耗时（秒），执行期间 Slot 保持锁定 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Farm Action")
	float Duration = 1.0f;
};

/** 收获 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmHarvestBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()
};

/** 灌溉：单次增加的水分比例 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmIrrigationBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()

public:
	/** 单次灌溉增加的水分比例 [0,1]（读 FarmlandConfig 可覆盖） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Farm Action")
	float WaterAmount = 0.1f;
};

/** 施肥：单次增加的肥力比例 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmFertilizeBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()

public:
	/** 单次施肥增加的肥力比例 [0,1] */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Farm Action")
	float FertilityAmount = 0.1f;
};

/** 耕地 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmPlowBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()
};

/** 种植：作物 ID */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmPlantBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()

public:
	/** 作物 ID（若为空则按当前选择的种子/默认） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Farm Action")
	FName CropID = NAME_None;
};

/** 除草（跨 Plot/Crop：需要双锁） */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmWeedingBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()
};

/** 除虫（跨 Plot/Crop：需要双锁） */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced)
class PROJECTB_API UFarmPestBehavior : public UFarmActionBehaviorBase
{
	GENERATED_BODY()
};
