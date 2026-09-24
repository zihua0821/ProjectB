// SatelliteStateTreeTasks.h
// 卫星 StateTree 任务集：选点（评分）/ 移动等待 / 执行作业（状态机由 ST_Satellite 资产编排）
#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"   // FMassStateTreeTaskBase
#include "SmartObjectRuntime.h"    // FSmartObjectClaimHandle（认领句柄）
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
struct FSatelliteApproachTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 离轨飞行任务（Approach 状态）
 * EnterState：路径参数一次算好（起点/终点/控制点/时长；贝塞尔）→ 开启位置覆盖
 * Tick：每帧曲线求值 → 写覆盖位置；到达检查所有权（失权 → Failed，供状态树跳 AwaitReturn）
 * @return Succeeded=到达悬停点（且所有权有效）；Failed=失权/异常；Running=飞行中
 * 注：替代旧名 Move To Target——状态树资产中的任务引用需重新指定
 */
USTRUCT(meta = (DisplayName = "Satellite Approach"))
struct PROJECTB_API FSatelliteApproachTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteApproachTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteDepartWaitTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 待发任务（Depart 状态）
 * 等待“最近点”时机（相位方向与悬停点方位在轨道平面内对齐）→ Succeeded
 * 每帧做所有权检查（Depart → Approach 转换点）：失权 → Failed（供状态树回 Cruise）
 * 不控制运动（卫星照常巡航）
 */
USTRUCT(meta = (DisplayName = "Satellite Depart Wait"))
struct PROJECTB_API FSatelliteDepartWaitTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteDepartWaitTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteReturnWaitTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 待回任务（AwaitReturn 状态）
 * 保持悬停（位置覆盖由作业阶段持有）；等到"虚拟相位方向与悬停点方位对齐"（最近点）→ Succeeded
 */
USTRUCT(meta = (DisplayName = "Satellite Return Wait"))
struct PROJECTB_API FSatelliteReturnWaitTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteReturnWaitTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteReturnTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 回轨飞行任务（Return 状态）
 * EnterState：路径参数（起点=悬停点；终点=入轨点初值；控制点；时长估算）
 * Tick：动态终点（每帧 = 虚拟相位点，保证入轨连续无跳变）→ 贝塞尔求值；
 *       到达（Alpha≥1）：清位置覆盖 + State=Cruise → Succeeded
 */
USTRUCT(meta = (DisplayName = "Satellite Return"))
struct PROJECTB_API FSatelliteReturnTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteReturnTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteExecuteActionTaskInstanceData
{
	GENERATED_BODY()

	/** 当前项开始时间（秒） */
	double ActionStartTime = 0.0;

	/** 当前项作业时长（秒；取自该项认领的行为定义，缺省用默认值） */
	float ActionDuration = 0.f;

	/** 本次出勤认领句柄快照（去重；队列完成时统一释放） */
	TArray<FSmartObjectClaimHandle> UniqueClaimHandles;
};

/**
 * 悬停作业任务（Work 状态）
 * EnterState：写悬停点（位置覆盖）→ 快照认领句柄 → 开始首项（读行为时长）
 * Tick：倒计时 → 执行队首操作 → 出队 → 队列空则释放全部认领、清理并 Succeeded；
 *       否则所有权检查（失权 → Failed）后开始下一项
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
