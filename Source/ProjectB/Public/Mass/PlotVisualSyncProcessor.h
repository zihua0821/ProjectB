// Fill out your copyright notice in the Description page of Project Settings.

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
