// SatelliteStateTreeTasks.cpp
#include "Mass/SatelliteStateTreeTasks.h"

#include "Farm/CellManagerComponent.h"
#include "Mass/CellMassFragments.h"     // FCropStateFragment / ECropGrowthStage
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"
#include "MassEntityManager.h"
#include "MassStateTreeExecutionContext.h"
#include "Planet/OrbitManagerComponent.h"          // FOrbitData
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetConfigDataAsset.h"

namespace
{
	/** 智能对象 Slot 索引约定：0=Plot（地块），1=Crop（作物） */
	constexpr int32 CropSlotIndex = 1;

	/** 卫星认领优先级（玩家 High 可抢占，见《智能对象架构与落地.md》7.5） */
	constexpr ESmartObjectClaimPriority SatelliteClaimPriority = ESmartObjectClaimPriority::Normal;

	/** 卫星任务数据聚合（fragment + 共享引用） */
	struct FSattelliteTaskData
	{
		FMassEntityManager* EntityManager = nullptr;
		FMassEntityHandle Entity;
		const FMassSatelliteSharedFragment* Shared = nullptr;
		FMassOrbitFragment* Orbit = nullptr;
		FMassSatelliteTaskFragment* Task = nullptr;

		bool IsValid() const { return EntityManager && Shared && Orbit && Task; }
	};

	/** 读取卫星任务数据（任一缺失时 IsValid() 为 false） */
	FSattelliteTaskData GetSatelliteTaskData(FStateTreeExecutionContext& Context)
	{
		FSattelliteTaskData Data;
		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		Data.EntityManager = &MassContext.GetEntityManager();
		Data.Entity = MassContext.GetEntity();
		Data.Shared = Data.EntityManager->GetConstSharedFragmentDataPtr<FMassSatelliteSharedFragment>(Data.Entity);
		Data.Orbit = Data.EntityManager->GetFragmentDataPtr<FMassOrbitFragment>(Data.Entity);
		Data.Task = Data.EntityManager->GetFragmentDataPtr<FMassSatelliteTaskFragment>(Data.Entity);
		return Data;
	}
}

EStateTreeRunStatus FSatelliteSelectTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] SelectTarget: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SmartObjectManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] SelectTarget: shared references not injected (PlanetConfig/CellManager/SmartObjectManager)"));
		return EStateTreeRunStatus::Failed;
	}

	// 1. 卫星当前位置 → 最近格子
	// 球面几何反推：位置方向投影到星球表面点，复用网格生成公式的逆运算
	const FVector WorldLocation = ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.CurrentAngle);
	const FVector SurfacePoint = WorldLocation.GetSafeNormal() * Shared.PlanetConfig->Radius;
	const FIntPoint CellCoord = UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(
		SurfacePoint, Shared.PlanetConfig, /*IsWorldLocation=*/false);

	// 2. 状态筛选：作物成熟才操作（B 路径：直接读格子实体 fragment，不走引擎查询）
	FMassEntityHandle CellEntity;
	if (!Shared.CellManager->GetCellEntity(CellCoord, CellEntity))
	{
		return EStateTreeRunStatus::Running;
	}
	const FCropStateFragment* CropState = Data.EntityManager->GetFragmentDataPtr<FCropStateFragment>(CellEntity);
	if (CropState == nullptr || CropState->CropGrowthStage != ECropGrowthStage::Mature)
	{
		return EStateTreeRunStatus::Running;
	}

	// 3. 选点后立即认领（移动前占位，避免多卫星同抢同一格子）
	const FSmartObjectClaimHandle ClaimHandle = Shared.SmartObjectManager->TryClaimSlot(
		CellCoord, CropSlotIndex, SatelliteClaimPriority);
	if (!ClaimHandle.IsValid())
	{
		// 已被其它卫星认领 / Slot 未启用：跳过（下一帧最近格子可能已变化）
		return EStateTreeRunStatus::Running;
	}

	// 4. 计算目标角度（格子中心角度）并写入任务与移动意图
	FTransform CellTransform;
	if (!Shared.CellManager->GetCellWorldTransform(CellCoord, CellTransform))
	{
		Shared.SmartObjectManager->ReleaseClaim(ClaimHandle);
		return EStateTreeRunStatus::Running;
	}

	FOrbitData OrbitData;
	OrbitData.Transform = Shared.OrbitTransform;
	OrbitData.Radius = Shared.Radius;

	Task.ClaimHandle = ClaimHandle;
	Task.TargetCoord = CellCoord;
	Orbit.MoveState = ESatelliteMoveState::MovingToTarget;
	Orbit.TargetAngle = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(CellTransform, OrbitData);

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] claimed cell (%d, %d) for harvest"),
		Data.Entity.Index, CellCoord.X, CellCoord.Y);

	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FSatelliteMoveToTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] MoveToTarget: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}

	// 到达检测：移动处理器到达目标后设置 Paused
	return Data.Orbit->MoveState == ESatelliteMoveState::Paused
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteHarvestTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Harvest: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.CellManager || !Shared.SmartObjectManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Harvest: CellManager/SmartObjectManager not injected"));
		return EStateTreeRunStatus::Failed;
	}

	// 执行收获（复用玩家同一操作入口；成功/失败均释放，统一重选出口）
	Shared.CellManager->HandleLeftMouseClicked(EEditMode::HarvestCrop, Task.TargetCoord);

	// 释放认领
	Shared.SmartObjectManager->ReleaseClaim(Task.ClaimHandle);

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] harvested cell (%d, %d)"),
		Data.Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y);

	// 清理任务数据并恢复巡航（下一状态选择将从 Cruise 重新开始）
	Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
	Task.TargetCoord = FIntPoint(-1, -1);
	Orbit.MoveState = ESatelliteMoveState::Idle;

	return EStateTreeRunStatus::Succeeded;
}
