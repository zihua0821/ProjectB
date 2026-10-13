// SatelliteOrbitMovementProcessor.h
// 卫星轨道移动处理器：名义相位推进（VirtualPhase 按 ω 匀速——任务链只覆盖渲染位置，不影响相位）
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteOrbitMovementProcessor.generated.h"

/**
 * 卫星轨道移动处理器
 * 统一行为：名义相位（VirtualPhase）始终按 ω 推进（bClockwise 决定方向；归一化 [0, 360)）——
 * 巡航/离轨/作业/漂移全程一致（离轨时仅渲染位置被覆盖——相位照走保证时间轴连续）
 * 纯数据修改（不触碰 ISM/世界对象），允许并行 chunk 执行；
 * 渲染跟随由 USatelliteRenderSyncProcessor 完成。
 */
UCLASS()
class PROJECTB_API USatelliteOrbitMovementProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteOrbitMovementProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
