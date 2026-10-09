// SatelliteRenderSyncProcessor.h
// 卫星渲染同步处理器：角度 → 轨道世界位置 → 批量更新 ISM 实例变换
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteRenderSyncProcessor.generated.h"

/**
 * 卫星渲染同步处理器
 * 每帧同步卫星实体状态到 ISM 渲染（GT 外框 + 并行收集——样板：CropVisualSyncProcessor）：
 * - 【GT 预快照】串行取槽世界位置快照（唯一 UObject 访问段；按实体句柄索引，无遍历顺序依赖）
 * - 【并行收集】读 FMassSatelliteOrbitFragment.VirtualPhase / 位置覆盖 → 计算世界位置 → 局部缓冲+锁合并
 * - 【GT 批提交】BatchUpdateInstancesTransforms（ISM 增删改仅限游戏线程，见 ISMManager 线程模型约定）
 * - 轨道几何 / Mesh / 缩放来自 FMassSatelliteSharedFragment（ConstShared）
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
