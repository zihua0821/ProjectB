// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DataTools.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UDataTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Data Tools")
	static FString PrintPlotState(const FPlotStateData& PlotState);

	UFUNCTION(BlueprintPure, Category = "Data Tools")
	static FString PrintCropState(const FCropStateData& CropState);
};
