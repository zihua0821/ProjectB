// OrbitPlanningTypes.h
// 轨道全局规划（群体决策）数据结构：快照（抄账）+ 参数 + 计划（发单）——纯 C++ 结构，无反射、无状态
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"

/** 快照行：一颗卫星的当前情况（占位按槽句柄；待返星按悬停点；漂移星带路径弧段） */
struct FOrbitSnapshotEntry
{
	FMassEntityHandle Entity;
	int32 Ordinal = 0;                               // 创建序号（确定性平局键）
	bool bOccupiesSlot = false;                      // 有槽句柄 = 占位（含在途/返航星——"在途当已到"）
	int32 SlotIndex = INDEX_NONE;                    // 占位槽索引
	bool bAwaitingReturn = false;                    // 待返（悬停等分配）
	FVector HoverLocation = FVector::ZeroVector;     // 待返星悬停点（等待角计算用）
	bool bDriftEligible = false;                     // 可动星（占位 + 巡航 + 无在途指令）——均匀化对象
	bool bInTransitDrift = false;                    // 在途漂移中（其弧段为防撞禁区）
	float DriftArcStartDeg = 0.f;                    // 在途漂移弧段：起点方位（度，轨道局部）
	float DriftArcTotalDeg = 0.f;                    // 在途漂移弧段：有符号扫过量（度）
};

/** 快照：一条轨道的全部卫星 + 槽网格参数（同一帧一次性采集——"同一张照片"） */
struct FOrbitSnapshot
{
	FName OrbitName;
	FTransform OrbitTransform = FTransform::Identity;
	float AngularVelocity = 0.f;
	bool bClockwise = false;
	int32 SlotCount = 0;
	float SlotPhaseBaseDeg = 0.f;                    // 槽 0 当前相位（度）
	TArray<FOrbitSnapshotEntry> Entries;
};

/** 规划参数（配置 → 求解输入；求解为纯函数，只依赖快照与本参数） */
struct FOrbitPlanParams
{
	float WaitAngleLimitDeg = 45.f;                  // 阶段①排座：等待角上限（同口径充当位移上限——统一 45°）
	float RebalanceRatio = 0.6f;                     // 阶段②触发：最小相邻间距 < 均匀间距 × 此比例
	float MoveMinDeg = 3.f;                          // 阶段②过滤：位移下限（低于不漂）
	bool bSkipRebalance = false;                     // 本轮跳过阶段②均匀化（独立节流；落座阶段①不受影响）
};

/** 计划动作类型 */
enum class EOrbitPlanAction : uint8
{
	AssignReturnSlot,   // 待返星落座（认领目标槽 + 写回句柄）
	DriftToSlot         // 漂移发单（认领目标槽 + 释放旧槽 + 冻结显示覆盖）
};

/** 计划行：给某颗星的行动指令 */
struct FOrbitPlanEntry
{
	FMassEntityHandle Entity;
	EOrbitPlanAction Action = EOrbitPlanAction::AssignReturnSlot;
	int32 TargetSlotIndex = INDEX_NONE;              // 目标槽（INDEX_NONE = 无动作）
};

/** 计划：本轮全部轨道的发单汇总 */
struct FOrbitPlan
{
	TArray<FOrbitPlanEntry> Entries;
};
