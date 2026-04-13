// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EQSTest_PlotState.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UEQSTest_PlotState : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEQSTest_PlotState();

	// ========== 状态位勾选 ==========
	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "已耕地 (Plowed)"))
	bool bMatchPlowed = false;

	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "已种植 (CropPlanted)"))
	bool bMatchCropPlanted = false;

	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "已灌溉 (Watered)"))
	bool bMatchWatered = false;

	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "已施肥 (Fertilized)"))
	bool bMatchFertilized = false;

	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "有杂草 (WeedInfested)"))
	bool bMatchWeedInfested = false;

	UPROPERTY(EditAnywhere, Category = "State Filter", meta = (DisplayName = "有虫害 (PestInfested)"))
	bool bMatchPestInfested = false;

protected:
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
};
