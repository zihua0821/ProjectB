// SatelliteOrbitPlanProcessor.h
// 轨道全局规划处理器：在 Mass 帧序内驱动卫星管理器的规划节拍
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteOrbitPlanProcessor.generated.h"

/**
 * 轨道全局规划处理器（服务型）
 * 每帧调用 SatelliteManager->TickOrbitPlanning()（内部 1s 节流）：
 * 抄账 → 求解 → 发单——时点固定在"槽推进 / 卫星推进 / 渲染同步"之前，
 * 消除"组件 Tick 与 Mass 帧序先后不定"的跨驱动源时序问题。
 */
UCLASS()
class PROJECTB_API USatelliteOrbitPlanProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteOrbitPlanProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
