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