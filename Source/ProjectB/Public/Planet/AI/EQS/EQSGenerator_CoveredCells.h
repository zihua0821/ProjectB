// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EQSGenerator_CoveredCells.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UEQSGenerator_CoveredCells : public UEnvQueryGenerator
{
	GENERATED_BODY()

public:
	UEQSGenerator_CoveredCells();

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
};
