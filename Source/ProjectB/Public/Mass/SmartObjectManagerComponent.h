// SmartObjectManagerComponent.h
// 智能对象管理组件：全量注册格子为智能对象实例 + Slot 启停控制 + B 路径选点查询
// 资产（USmartObjectDefinition）在蓝图/细节面板配置；Slot 索引约定：Plot=0, Crop=1
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SmartObjectTypes.h"        // FSmartObjectHandle / FSmartObjectSlotHandle
#include "SmartObjectManagerComponent.generated.h"

class USmartObjectDefinition;
class USmartObjectSubsystem;
class UCellManagerComponent;

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

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<UCellManagerComponent> CellManager;

	UPROPERTY()
	TObjectPtr<USmartObjectSubsystem> SmartObjectSubsystem;

	/** 坐标 → 句柄组 映射 */
	TMap<FIntPoint, FCellSmartObjectEntry> CoordToEntry;

	/** 待注册坐标队列（分批处理） */
	TArray<FIntPoint> PendingCoords;

	/** 当前批次处理到哪 */
	int32 PendingIndex = 0;

	/** 注册是否完成 */
	bool bRegistrationDone = false;

	/** 每帧注册数量 */
	static constexpr int32 BatchSize = 5000;
};
