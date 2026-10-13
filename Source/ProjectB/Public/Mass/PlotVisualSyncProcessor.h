// PlotVisualSyncProcessor.h
// 地块视觉同步处理器：地块 CustomData（透明度/湿度/肥力）增量更新（GT 外框 + 并行收集 + GT 批提交）

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "PlotVisualSyncProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UPlotVisualSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UPlotVisualSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
