// SmartObjectManagerComponent.h
// 智能对象管理组件：全量注册格子为智能对象实例 + Slot 启停控制 + B 路径选点查询
// 资产（USmartObjectDefinition）在蓝图/细节面板配置；Slot 索引约定：Plot=0, Crop=1
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Crops/CropData.h"          // EEditMode（操作模式 → Slot 映射）
#include "SmartObjectRuntime.h"       // FSmartObjectClaimHandle（认领句柄）
#include "SmartObjectTypes.h"        // FSmartObjectHandle / FSmartObjectSlotHandle
#include "SmartObjectManagerComponent.generated.h"

class USmartObjectDefinition;
class USmartObjectSubsystem;
class UCellManagerComponent;
struct FRuntimeOrbitData;

/**
 * 智能对象管理组件
 * 挂载在星球 Actor 上，负责：
 * - 全量注册格子为智能对象实例（分批，避免单帧卡顿）
 * - 维护 坐标 → (对象/Slot) 句柄映射（B 路径选点用，O(1) 查询）
 * - Slot 启停控制（SetSlotEnabled 封装）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECTB_API USmartObjectManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USmartObjectManagerComponent();

	/** 初始化：绑定 CellManager（需在其 CreateMassCell 完成后调用） */
	void Initialize(UCellManagerComponent* InCellManager);

	/** 全量注册是否完成 */
	bool IsRegistrationDone() const { return bRegistrationDone; }

	// ---- B 路径选点接口 ----

	/** 获取指定格子的智能对象句柄 */
	bool GetObjectHandle(const FIntPoint Coords, FSmartObjectHandle& OutHandle) const;

	/** 获取指定格子的 Slot 句柄（SlotIndex: 0=Plot, 1=Crop） */
	bool GetSlotHandle(const FIntPoint Coords, const int32 SlotIndex, FSmartObjectSlotHandle& OutHandle) const;

	/** 获取 Plot Slot（0）句柄 */
	bool GetPlotSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const;

	/** 获取 Crop Slot（1）句柄 */
	bool GetCropSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const;

	// ---- Slot 启停控制 ----

	/** 设置 Plot Slot（0）启停（耕地 Enable / 移除耕地 Disable） */
	bool SetPlotSlotEnabled(const FIntPoint Coords, const bool bEnabled) const;

	/** 设置 Crop Slot（1）启停（播种 Enable / 移除作物 Disable） */
	bool SetCropSlotEnabled(const FIntPoint Coords, const bool bEnabled) const;

	// ---- 认领/释放（B 路径：坐标直连，不走引擎查询） ----

	/**
	 * 尝试认领指定格子的 Slot（坐标 → SlotHandle → MarkSlotAsClaimed）
	 * @param Coords     格子坐标
	 * @param SlotIndex  0=Plot, 1=Crop
	 * @param Priority   认领优先级（卫星 Normal / 玩家 High）
	 * @return 认领句柄（无效 = 失败：Slot 未启用或已被占用）
	 */
	FSmartObjectClaimHandle TryClaimSlot(const FIntPoint Coords, const int32 SlotIndex, const ESmartObjectClaimPriority Priority) const;

	/** 释放认领（MarkSlotAsFree） */
	bool ReleaseClaim(const FSmartObjectClaimHandle& ClaimHandle) const;

	/** 指定坐标 Slot 是否已被任何星认领（账本查询；扫描端排除"正在被服务"的格子，防幽灵认领吸引） */
	bool IsSlotClaimed(const FIntPoint Coords, const int32 SlotIndex) const;

	/**
	 * 认领是否仍归本句柄所有（所有权检查：抢占/失权检测）
	 * 基于管理器账本（所有认领入口统一走本组件），不依赖引擎查询
	 * @return true=本句柄仍是该 Slot 的当前认领者
	 */
	bool IsClaimValid(const FIntPoint Coords, const int32 SlotIndex, const FSmartObjectClaimHandle& ClaimHandle) const;

	/** 指定坐标 Slot 是否启用（启用 = 对象存在，才参与认领协议） */
	bool IsSlotEnabled(const FIntPoint Coords, const int32 SlotIndex) const;

	// ---- 轨道槽位（回轨槽位：与格子同构的智能对象实例） ----

	/**
	 * 注册轨道槽位（每轨 N = 卫星数 × SlotMultiplier；均分相位固定点）
	 * 由卫星重建流程调用；重复调用先清理旧槽
	 */
	void RegisterOrbitSlots(const TArray<FRuntimeOrbitData>& Orbits, const int32 SlotMultiplier);

	/** 注册单条轨道的槽位（重建/新增共用：先销毁该轨旧实例再建新；不影响其他轨道） */
	void RegisterSingleOrbitSlots(const FRuntimeOrbitData& Orbit, const int32 SlotMultiplier);

	/** 注销单条轨道的槽位（删除轨道用：销毁该轨全部实例并移除条目；不影响其他轨道） */
	void UnregisterOrbitSlots(const FName OrbitName);

	/** 槽位数（未注册返回 0） */
	int32 GetOrbitSlotCount(const FName OrbitName) const;

	/** 槽位世界位置（按轨道名 + 槽索引） */
	bool GetOrbitSlotWorldPosition(const FName OrbitName, const int32 SlotIndex, FVector& OutWorldPosition) const;

	/** 槽位当前相位（度；= 基准相位 + 360×i/N，随轨道旋转——选槽判据用） */
	bool GetOrbitSlotPhase(const FName OrbitName, const int32 SlotIndex, float& OutPhaseDeg) const;

	// ---- 按【认领句柄】查询（已认领后的唯一查询入口；避免索引与句柄双字段不同步） ----

	/** 按认领句柄查槽位世界位置 */
	bool GetOrbitSlotWorldPositionByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle, FVector& OutWorldPosition) const;

	/** 按认领句柄查槽位相位（度） */
	bool GetOrbitSlotPhaseByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle, float& OutPhaseDeg) const;

	/** 按认领句柄反查槽索引（调试/日志可读性用；未找到返回 -1） */
	int32 ResolveSlotIndexByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle) const;

	/** 认领轨道槽（成功返回句柄；失败返回无效句柄） */
	FSmartObjectClaimHandle TryClaimOrbitSlot(const FName OrbitName, const int32 SlotIndex, const ESmartObjectClaimPriority Priority) const;

	/** 槽位随相位更新（由 SatelliteOrbitMovementProcessor 在 Mass 帧序内驱动：先槽后星，渲染同步读到同帧缓存） */
	void UpdateOrbitSlots(const float DeltaSeconds);

	/**
	 * 操作模式 → 智能对象 Slot 索引映射
	 * @return 0=Plot（地块操作）/ 1=Crop（作物操作）/ -1=不适用认领（None/Count 等）
	 */
	static int32 GetSlotIndexForEditMode(const EEditMode EditMode);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** CellManager 意图委托回调：收到 Slot 启停意图后执行 */
	void HandleCellSlotStateChanged(const FIntPoint Coords, const int32 SlotIndex, const bool bEnabled);
	/** 单格注册：CreateSmartObject + 微查询捕获 Slot 句柄 + 默认禁用 */
	bool RegisterCell(const FIntPoint Coords);

	/** 从已注册实例捕获两个 Slot 句柄（Plot=0, Crop=1） */
	bool CaptureSlotHandles(const FSmartObjectHandle ObjectHandle, const FTransform& WorldTransform, FSmartObjectSlotHandle& OutPlotSlot, FSmartObjectSlotHandle& OutCropSlot);

	/** 单个格子对应的智能对象句柄组 */
	struct FCellSmartObjectEntry
	{
		FSmartObjectHandle ObjectHandle;
		FSmartObjectSlotHandle PlotSlotHandle;  // Slot 0
		FSmartObjectSlotHandle CropSlotHandle;  // Slot 1
	};

	// ---- 配置（蓝图/细节面板） ----

	/** 智能对象定义资产（DA_FarmCell：Slot[0]=Plot, Slot[1]=Crop） */
	UPROPERTY(EditDefaultsOnly, Category = "SmartObject")
	TObjectPtr<USmartObjectDefinition> SmartObjectDefinition;

	/** 轨道槽位定义资产（DA_OrbitSlot：单 Slot；与格子定义并列） */
	UPROPERTY(EditDefaultsOnly, Category = "SmartObject")
	TObjectPtr<USmartObjectDefinition> OrbitSlotDefinition;

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<UCellManagerComponent> CellManager;

	UPROPERTY()
	TObjectPtr<USmartObjectSubsystem> SmartObjectSubsystem;

	/** 坐标 → 句柄组 映射 */
	TMap<FIntPoint, FCellSmartObjectEntry> CoordToEntry;

	/** 轨道槽位条目（每轨一份：轨道几何 + 槽位句柄/位置，索引对齐） */
	struct FOrbitSlotEntry
	{
		// ---- 轨道几何（槽位位置随相位推进：槽 i 相位 = CurrentPhaseDeg + 360×i/N） ----
		FTransform OrbitTransform;
		float OrbitRadius = 0.f;
		float AngularVelocity = 0.f;   // 度/秒（与卫星同源；槽位与卫星同速旋转）
		bool bClockwise = false;
		float CurrentPhaseDeg = 0.f;   // 槽 0 当前相位（随 ω 推进）

		// ---- 智能对象实例 ----
		TArray<FSmartObjectHandle> ObjectHandles;   // 供 UpdateSmartObject 用
		TArray<FVector> SlotWorldPositions;
		TArray<FSmartObjectSlotHandle> SlotHandles;
	};

	/** 轨道名 → 槽位条目 */
	TMap<FName, FOrbitSlotEntry> OrbitSlots;

	/**
	 * 认领账本：每个 Slot 一本（索引 = SlotIndex：0=Plot, 1=Crop；坐标 → 当前认领句柄）
	 * 抢占时被新句柄覆盖——旧持有者据此检测失权（IsClaimValid）
	 */
	mutable TArray<TMap<FIntPoint, FSmartObjectClaimHandle>> ClaimLedger;

	/** 待注册坐标队列（分批处理） */
	TArray<FIntPoint> PendingCoords;

	/** 当前批次处理到哪 */
	int32 PendingIndex = 0;

	/** 注册是否完成 */
	bool bRegistrationDone = false;

	/** 累计注册成功数（统计用，跨帧累计） */
	int32 RegistrationSuccessCount = 0;

	/** 每帧注册数量 */
	static constexpr int32 BatchSize = 5000;
};
