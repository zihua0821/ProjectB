// SatelliteStateTreeTasks.h
// 卫星 StateTree 任务集：选点 / 移动等待 / 收获（状态机由 ST_Satellite 资产编排）
#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"   // FMassStateTreeTaskBase
#include "SatelliteStateTreeTasks.generated.h"

/**
 * 卫星 StateTree 任务说明（数据访问约定）：
 * - Task 通过 FMassStateTreeExecutionContext 访问卫星实体 fragment / ConstSharedFragment
 * - 状态编排（Cruise → MoveToTarget → Harvest → Cruise）与转换条件在 StateTree 资产中配置
 * - Task 只负责"一个状态内的职责"，跨任务数据经 FMassSatelliteTaskFragment 传递
 */

USTRUCT()
struct FSatelliteSelectTargetTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 选点任务（Cruise 状态）
 * 每帧检测卫星最近格子：作物成熟 → 立即认领（移动前占位）→ 写入目标与移动意图
 * @return Succeeded=认领成功（转往目标）；Running=无合适目标（继续巡航）
 */
USTRUCT(meta = (DisplayName = "Satellite Select Target"))
struct PROJECTB_API FSatelliteSelectTargetTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteSelectTargetTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteMoveToTargetTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 移动等待任务（MoveToTarget 状态）
 * 等待移动处理器到达目标（MoveState=Paused）
 * @return Succeeded=已到达；Running=移动中
 */
USTRUCT(meta = (DisplayName = "Satellite Move To Target"))
struct PROJECTB_API FSatelliteMoveToTargetTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteMoveToTargetTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteHarvestTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 收获任务（Harvest 状态）
 * EnterState 一次性完成：执行收获（复用玩家操作入口）→ 释放认领 → 清理任务数据 → 恢复巡航
 */
USTRUCT(meta = (DisplayName = "Satellite Harvest"))
struct PROJECTB_API FSatelliteHarvestTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteHarvestTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
