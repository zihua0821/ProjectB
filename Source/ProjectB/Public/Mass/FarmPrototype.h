// FarmPrototype.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmartObjectComponent.h"
#include "MassEntityTypes.h"        // FMassFragment, FMassEntityHandle
#include "MassProcessor.h"          // UMassProcessor
#include "FarmPrototype.generated.h"

class USmartObjectSubsystem;
struct FMassEntityManager;

// ============================================================================
// 纯数据 Fragment：生长阶段
// ============================================================================
USTRUCT(BlueprintType)
struct FCropGrowthFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Farm")
    int32 GrowthStage = 0;  // 0=种子, 1=幼苗, 2=成熟, 3=已收获
};

// ============================================================================
// 纯数据 Fragment：ISM 渲染状态
// ============================================================================
USTRUCT(BlueprintType)
struct FCropRenderStateFragment : public FMassFragment
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Farm")
    FVector WorldLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "Farm")
    FRotator WorldRotation = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadWrite, Category = "Farm")
    int32 CurrentVisibleStage = -1; // -1 表示全部隐藏

    // 每个生长阶段对应的 ISM 实例索引（0=种子,1=幼苗,2=成熟,3=收获）
    UPROPERTY()
    TArray<int32> PerStageInstanceIndex;
};

// ============================================================================
// 智能对象荷载：用于在 Slot 中携带网格坐标
// ============================================================================
USTRUCT()
struct FCoordinatePayload
{
    GENERATED_BODY()

    UPROPERTY()
    int32 X = 0;

    UPROPERTY()
    int32 Y = 0;

    FCoordinatePayload() {}
    FCoordinatePayload(FIntPoint Coord) : X(Coord.X), Y(Coord.Y) {}
    FIntPoint ToIntPoint() const { return FIntPoint(X, Y); }
};

// ============================================================================
// 智能对象行为定义（可为蓝图子类）
// ============================================================================
UCLASS(Blueprintable, BlueprintType)
class UFarmHarvestBehavior : public USmartObjectBehaviorDefinition
{
    GENERATED_BODY()
};

// ============================================================================
// Mass 处理器：检测生长阶段变化 -> 切换 ISM 图层
// ============================================================================
UCLASS()
class UFarmRenderSyncProcessor : public UMassProcessor
{
    GENERATED_BODY()

public:
    UFarmRenderSyncProcessor();

protected:
    virtual void ConfigureQueries() override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
    FMassEntityQuery EntityQuery;
};

// ============================================================================
// 原型管理器 Actor：初始化 Mass、智能对象，并提供蓝图收获接口
// ============================================================================
UCLASS(Blueprintable)
class AFarmPrototypeManager : public AActor
{
    GENERATED_BODY()

public:
    AFarmPrototypeManager();

    virtual void BeginPlay() override;

    /** 蓝图调用：收获指定网格坐标的作物 */
    UFUNCTION(BlueprintCallable, Category = "Farm")
    void HarvestCrop(FIntPoint Coord);

    /** 蓝图调用：获取当前所有可收获的坐标 */
    UFUNCTION(BlueprintCallable, Category = "Farm")
    TArray<FIntPoint> GetHarvestableSlots() const { return HarvestSlots; }

    UPROPERTY()
    TObjectPtr<UFarmRenderSyncProcessor> FarmRenderSyncProcessor;

    // 存储注册后的槽位句柄，便于后续移除和查找
    UPROPERTY()
    TMap<FIntPoint, FSmartObjectSlotHandle> CoordToSlot;

    // 坐标 → 已创建的 SmartObjectHandle
    TMap<FIntPoint, FSmartObjectHandle> CoordToSmartObjectHandle;

    UPROPERTY(EditAnywhere, Category = "Farm")
    TSoftObjectPtr<USmartObjectDefinition> HarvestDefinition;
protected:
    // 创建三个测试作物
    void SetupMassEntities();
    // 为所有成熟作物注册智能对象插槽
    void RegisterSmartObjectSlots();

private:
    UPROPERTY()
    TObjectPtr<USmartObjectComponent> SmartObjectComp;

    UPROPERTY()
    TObjectPtr<USmartObjectSubsystem> SmartObjectSubsystem;

    TSharedPtr<FMassEntityManager> EntityManager;

    FMassArchetypeHandle CropArchetype;

    // 坐标 -> 实体句柄
    TMap<FIntPoint, FMassEntityHandle> CoordToEntity;

    // 已注册的可收获坐标
    TArray<FIntPoint> HarvestSlots;

    // 网格大小（世界单位）
    static constexpr float GridSize = 200.0f;
};