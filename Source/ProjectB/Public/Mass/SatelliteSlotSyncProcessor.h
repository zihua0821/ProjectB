// SatelliteSlotSyncProcessor.h
// 卫星槽位同步处理器（专职游戏线程）：槽位相位推进的唯一驱动点
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteSlotSyncProcessor.generated.h"

/**
 * 卫星槽位同步处理器（专职 GT）
 * 槽位相位推进自 USatelliteOrbitMovementProcessor（worker）迁出——组件级访问归游戏线程（T1/T2 修复）：
 * - 唯一职责：调用 SatelliteManager->UpdateOrbitSlots(dt)（转发至自建槽池；含槽位球调试绘制——面板每轨开关门控）
 * - 帧序链：SatellitePlan → 【SatelliteSlot】→ SatelliteMovement → SatelliteRender（保持"先槽后星"，
 *   渲染同步读到同帧槽缓存；规划仍读到推进前槽相位）
 * - 选定暂停挂载：编辑模式冻结槽推进（与卫星相位推进一致）
 */
UCLASS()
class PROJECTB_API USatelliteSlotSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteSlotSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
