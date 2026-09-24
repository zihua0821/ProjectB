// SatelliteRenderSyncProcessor.h
// 卫星渲染同步处理器：角度 → 轨道世界位置 → 批量更新 ISM 实例变换
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteRenderSyncProcessor.generated.h"

/**
 * 卫星渲染同步处理器
 * 每帧同步卫星实体状态到 ISM 渲染：
 * - 读 FMassOrbitFragment.VirtualPhase / 位置覆盖 → 计算世界位置 → BatchUpdateInstancesTransforms
 * - 轨道几何 / Mesh / 缩放来自 FMassSatelliteSharedFragment（ConstShared）
 * - 游戏线程执行（ISM 增删改仅限游戏线程，见 ISMManager 线程模型约定）
 */
UCLASS()
class PROJECTB_API USatelliteRenderSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteRenderSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
