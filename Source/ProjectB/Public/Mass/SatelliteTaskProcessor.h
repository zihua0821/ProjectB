// SatelliteTaskProcessor.h
// 卫星任务处理器：巡航检测 → 认领 → 前往目标 → 执行收获 → 释放
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteTaskProcessor.generated.h"

/**
 * 卫星任务处理器（认领→移动→执行→释放 闭环）
 * - 巡航：球面几何反推最近格子 → 作物成熟检测 → 立即认领（移动前占位防抢）
 * - 前往目标：移动处理器到达后（MoveState=Paused）执行收获（复用玩家操作入口）→ 释放 → 恢复巡航
 * - 游戏线程执行（访问 SmartObjectSubsystem / CellManager）
 */
UCLASS()
class PROJECTB_API USatelliteTaskProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteTaskProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
