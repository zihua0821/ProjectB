// SatelliteStateTreeTasks.h
// 卫星 StateTree 任务集：选点 / 离轨 / 待发 / 作业 / 待回 / 回轨 / 漂移 + 漂移请求评估器（状态机由 StateTree 资产编排）
#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"   // FMassStateTreeTaskBase
#include "SmartObjectRuntime.h"    // FSmartObjectClaimHandle（认领句柄）
#include "SatelliteStateTreeTasks.generated.h"

/**
 * 卫星 StateTree 任务说明（数据访问约定）：
 * - Task 通过 FMassStateTreeExecutionContext 访问卫星实体 fragment / ConstSharedFragment
 * - 状态编排（Cruise → Depart → Approach → Work → ReturnWait → Return → Cruise 等）与转换条件在 StateTree 资产中配置
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
 * 巡航态节流选点：遍历轨道覆盖清单（覆盖带几何过滤）→ 缺口评分（状态权重 + 距离）
 * → 最高分认领整格席位（单 Slot）→ 写入目标坐标/作业队列
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
 * EnterState：轨迹参数一次算好（起点/落点方位/平面内半径与法线偏移/时长）→ 开启位置覆盖 + 让槽
 * Tick：每帧参数方程求值（含垂直分量）→ 写覆盖位置；到达检查所有权（失权 → Failed）
 * @return Succeeded=到达悬停点（且所有权有效）；Failed=失权/异常；Running=飞行中
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

	/** 送达探针：等待起点（秒；DepartWait 首帧记录；漂移中断星在送达时重置） */
	double WaitStartTimeSeconds = 0.0;

	/** 长等待警告是否已打（>10s 只报一次） */
	bool bLongWaitWarned = false;
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
	// 选槽已移至群体决策（卫星管理器规划层）——本任务不再维护实例数据
};

USTRUCT()
struct FSatelliteReturnTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * 待回任务（ReturnWait 状态）
 * 保持悬停（位置覆盖由作业阶段持有）；槽位由群体决策分配（规划器认领后写回句柄）
 * → 等到"目标槽位实时位置与悬停投影重合"（≤ 容差）→ Succeeded
 */
USTRUCT(meta = (DisplayName = "Satellite Return Wait"))
struct PROJECTB_API FSatelliteReturnWaitTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteReturnWaitTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

/**
 * 回轨飞行任务（Return 状态）
 * EnterState：起点=悬停点（权威）；落点 = 槽位相位 + ω·T（含 δ0 补偿）；垂直偏移按需插值；
 *             起飞瞬间释放格子认领（唯一释放点——互斥期至起飞为止）
 * Tick：参数方程求值 → 写覆盖；到达（Alpha≥1）：写落点 + 清覆盖 + 账面相位对齐槽位 → Succeeded
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

	/** 当前项作业时长（秒；按操作类查行为定义，缺省用默认值） */
	float ActionDuration = 0.f;
};

/**
 * 悬停作业任务（Work 状态）
 * EnterState：写悬停点（位置覆盖）→ 开始首项（按操作类读取行为时长）
 * Tick：倒计时 → 执行队首操作 → 出队 → 队列空：不释放认领（保留至回轨起飞统一释放）、清理并 Succeeded；
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

USTRUCT()
struct FSatelliteDriftTaskInstanceData
{
	GENERATED_BODY()

	/** 上次选点探测时间（秒；漂移中"掠过工作"检查的节流） */
	double LastProbeTime = 0.0;
};

/**
 * 漂移任务（Drift 状态；同轨道加速前移 / 减速后移——无变轨）
 * EnterState：起点=显示位置（发单冻结）；目标=已认领槽（句柄）；Δs 与时长一次算好 → 消费请求
 * Tick：参数方程求值 → 写覆盖；到达（Alpha≥1）：写落点 + 账面相位对齐目标槽 + 清覆盖 → Succeeded
 * 漂移中探测（节流）：发现值得接的工作 → 当场认领（ClaimWorkTarget）→ 就地交还显示（释放目标槽）
 *   → Succeeded——统一走"状态已成功时 → Depart"；滑完（无认领）经 DepartWait"无目标"自愈回 Cruise
 */
USTRUCT(meta = (DisplayName = "Satellite Drift"))
struct PROJECTB_API FSatelliteDriftTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteDriftTaskInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT()
struct FSatelliteDriftRequestEvaluatorInstanceData
{
	GENERATED_BODY()

	/** 漂移请求（每帧从卫星 fragment 同步；供状态树转移条件绑定——由规划器发单置位） */
	UPROPERTY(EditAnywhere, Category = "Drift")
	bool bDriftRequested = false;
};

/**
 * 漂移请求评估器（挂 Cruise 状态；每帧把 bDriftRequested 照进实例数据）
 * 无副作用、只读——供"Tick时 + 条件（本字段 == true）→ Drift"的转移消费
 */
USTRUCT(meta = (DisplayName = "Satellite Drift Request"))
struct PROJECTB_API FSatelliteDriftRequestEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSatelliteDriftRequestEvaluatorInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};
