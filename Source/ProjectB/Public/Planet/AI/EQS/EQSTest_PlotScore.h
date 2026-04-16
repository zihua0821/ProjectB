// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EQSTest_PlotScore.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UEQSTest_PlotScore : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEQSTest_PlotScore();
	
	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "耕地权重"))
	float PlowedWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "种植权重"))
	float PlantedWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "水分权重"))
	float MoistureWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "肥力权重"))
	float FertilityWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "收获权重"))
	float HarvestWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "杂草权重"))
	float WeedWeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Config", meta = (DisplayName = "虫害权重"))
	float PestWeight = 20.f;

	
	
protected:
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
};
