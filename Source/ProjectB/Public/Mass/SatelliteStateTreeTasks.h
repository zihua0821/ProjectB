// SatelliteStateTreeTasks.h
// 卫星 StateTree 任务集：选点（评分）/ 移动等待 / 执行作业（状态机由 ST_Satellite 资产编排）
#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"   // FMassStateTreeTaskBase
#include "SatelliteStateTreeTasks.generated.h"

/**
 * 卫星 StateTree 任务说明（数据访问约定）：
 * - Task 通过 FMassStateTreeExecutionContext 访问卫星实体 fragment / ConstSharedFragment
 * - 状态编排（Cruise → MoveToTarget → Execute → Cruise）与转换条件在 StateTree 资产中配置
 * - Task 只负责"一个状态内的职责"，跨任务数据经 FMassSatelliteTaskFragment 传递
 */

USTRUCT()
struct FSatelliteSelectTargetTaskInstanceData
{
	GENERATED_BODY()

	/** 上次选点时间（秒；巡航态节流：每 0.2s 选一次，避免每帧遍历候选集） */
	double LastSelectTime = 0.0;
};

/**
 * 选点任务（Cruise 状态）
 * 巡航态节流选点：遍历活跃农田（轨道覆盖带几何过滤）→ 缺口评分（状态权重 + 距离）
 * → 最高分按操作类型认领对应 Slot → 写入目标坐标/操作类型/移动意图
 * @return Succeeded=已选目标（转往目标）；Running=无合适目标（继续巡航）
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
struct FSatelliteExecuteActionTaskInstanceData
{
	GENERATED_BODY()

	/** 作业开始时间（秒） */
	double ActionStartTime = 0.0;

	/** 作业时长（秒；取自智能对象行为定义，缺省用默认值） */
	float ActionDuration = 0.f;
};

/**
 * 执行作业任务（Execute 状态）
 * EnterState：读行为定义 Duration（DA_FarmCell；无认领则用缺省）→ 记录开始时间 → Running（作业停留）
 * Tick：倒计时结束 → 执行操作（CellManager::ExecuteFarmAction）→ 释放认领 → 清理任务数据 → Succeeded
 */
USTRUCT(meta = (DisplayName = "Satellite Execute Action"))
struct PROJECTB_API FSatelliteExecuteActionTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteExecuteActionTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
