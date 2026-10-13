// CropGrowthProcessor.h
// 作物生长推进处理器：阶段计时/转换 + 品质累加（Harvested 阶段为重播延迟计时段）
#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CropGrowthProcessor.generated.h"

UCLASS()
class PROJECTB_API UCropGrowthProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCropGrowthProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};