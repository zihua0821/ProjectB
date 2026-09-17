// SatelliteStateTreeTickProcessor.h
// 卫星 StateTree 心跳驱动：Mass StateTree 为信号驱动，需外部信号源驱动持续 Tick
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "SatelliteStateTreeTickProcessor.generated.h"

/**
 * 卫星 StateTree 心跳处理器
 * Mass StateTree 采用信号驱动（无信号不 Tick），持续运行的任务（巡航轮询/等待到达）
 * 需要外部驱动：本处理器每帧为所有卫星实体发送 StateTree 推进信号
 * （复用引擎信号 UE::Mass::Signals::NewStateTreeTaskRequired，已由 UMassStateTreeProcessor 订阅）
 */
UCLASS()
class PROJECTB_API USatelliteStateTreeTickProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USatelliteStateTreeTickProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
