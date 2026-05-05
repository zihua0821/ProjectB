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