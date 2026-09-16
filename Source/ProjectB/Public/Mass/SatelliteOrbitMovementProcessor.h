// SatelliteOrbitMovementProcessor.h
// 卫星轨道移动处理器：角度推进（Idle / MovingToTarget / Paused）
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteOrbitMovementProcessor.generated.h"

/**
 * 卫星轨道移动处理器
 * 迁移自 UOrbitMovementComponent::UpdateOrbitMovement（1D 角度推进）：
 * - Idle：按角速度推进（bClockwise 决定方向）
 * - MovingToTarget：最短弧向目标推进，到达（≤0.1°）转 Paused
 * - Paused：不移动
 * - 角度归一化到 [0, 360)
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
