// CropVisualSyncProcessor.h
// 作物视觉同步处理器：作物阶段 Mesh 切换（GT 外框 + 并行收集 + GT 批提交）
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CropVisualSyncProcessor.generated.h"

UCLASS()
class PROJECTB_API UCropVisualSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCropVisualSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};