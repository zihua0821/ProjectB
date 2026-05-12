// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CellVisualSyncProcessor.generated.h"

class UFarmlandConfigDataAsset;
class UPlanetConfigDataAsset;
/**
 * 
 */
UCLASS()
class PROJECTB_API UCellVisualSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCellVisualSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;

	static TArray<FVector> CalculateCellVertexPositions(const UPlanetConfigDataAsset* PlanetConfig, int32 LatIdx, int32 LonIdx);

	static FTransform CalculateOutlineInstanceTransform(const UPlanetConfigDataAsset* PlanetConfig,
	const UFarmlandConfigDataAsset* FarmlandConfig, const FTransform& CellTransform, const float LatIdx);
};
