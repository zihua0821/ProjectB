// SmartObjectManagerComponent.h
// 智能对象管理组件：全量注册格子为智能对象实例（单 Slot）+ 坐标直连选点查询（不经引擎空间查询）
// 资产（USmartObjectDefinition）在蓝图/细节面板配置；DA_FarmCell 单 Slot 挂全部 7 行为类（单 Slot 化）
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SmartObjectRuntime.h"       // FSmartObjectClaimHandle（认领句柄）
#include "SmartObjectTypes.h"        // FSmartObjectHandle / FSmartObjectSlotHandle
#include "SmartObjectManagerComponent.generated.h"

class USmartObjectDefinition;
class USmartObjectSubsystem;
class UCellManagerComponent;

/**
 * 智能对象管理组件
 * 挂载在星球 Actor 上，负责：
 * - 全量注册格子为智能对象实例（分批，避免单帧卡顿；单 Slot 恒启用——一实例一作业席位）
 * - 维护 坐标 → (对象/Slot) 句柄映射（选点直查用，O(1)——不经引擎空间查询）
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

	// ---- 选点查询接口（坐标直连——不经引擎空间查询） ----

	/** 获取指定格子的 Slot 句柄（单 Slot——一实例一作业席位） */
	bool GetCellSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const;

	// ---- 认领/释放（坐标直连——不走引擎空间查询） ----

	/**
	 * 尝试认领指定格子的 Slot（坐标 → SlotHandle → MarkSlotAsClaimed）
	 * @param Coords     格子坐标
	 * @param Priority   认领优先级（卫星 Normal / 玩家 High）
	 * @return 认领句柄（无效 = 失败：已被占用）
	 */
	FSmartObjectClaimHandle TryClaimSlot(const FIntPoint Coords, const ESmartObjectClaimPriority Priority) const;

	/** 释放认领（MarkSlotAsFree） */
	bool ReleaseClaim(const FSmartObjectClaimHandle& ClaimHandle) const;

	/** 指定坐标 Slot 是否已被任何星认领（账本查询；扫描端排除"正在被服务"的格子，防幽灵认领吸引） */
	bool IsSlotClaimed(const FIntPoint Coords) const;

	/**
	 * 认领是否仍归本句柄所有（所有权检查：抢占/失权检测）
	 * 基于管理器账本（所有认领入口统一走本组件），不依赖引擎查询
	 * @return true=本句柄仍是该格子的当前认领者
	 */
	bool IsClaimValid(const FIntPoint Coords, const FSmartObjectClaimHandle& ClaimHandle) const;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 单格注册：CreateSmartObject + 微查询捕获 Slot 句柄 */
	bool RegisterCell(const FIntPoint Coords);

	/** 从已注册实例捕获 Slot 句柄（单 Slot——Slot 0） */
	bool CaptureSlotHandle(const FSmartObjectHandle ObjectHandle, const FTransform& WorldTransform, FSmartObjectSlotHandle& OutSlot);

	/** 单个格子对应的智能对象句柄 */
	struct FCellSmartObjectEntry
	{
		FSmartObjectHandle ObjectHandle;
		FSmartObjectSlotHandle SlotHandle;   // 单 Slot（一实例一作业席位）
	};

	// ---- 配置（蓝图/细节面板） ----

	/** 智能对象定义资产（DA_FarmCell：单 Slot 挂全部 7 行为类） */
	UPROPERTY(EditDefaultsOnly, Category = "SmartObject")
	TObjectPtr<USmartObjectDefinition> SmartObjectDefinition;

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<UCellManagerComponent> CellManager;

	UPROPERTY()
	TObjectPtr<USmartObjectSubsystem> SmartObjectSubsystem;

	/** 坐标 → 句柄组 映射 */
	TMap<FIntPoint, FCellSmartObjectEntry> CoordToEntry;

	/**
	 * 认领账本：坐标 → 当前认领句柄（单账本——单 Slot 化）
	 * 抢占时被新句柄覆盖——旧持有者据此检测失权（IsClaimValid）
	 */
	mutable TMap<FIntPoint, FSmartObjectClaimHandle> ClaimLedger;

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
