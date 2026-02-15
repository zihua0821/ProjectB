// Fill out your copyright notice in the Description page of Project Settings.


#include "Tools/DataTools.h"

FString UDataTools::PrintPlotState(const FPlotStateData& PlotState)
{
	return PlotState.ToString();
}

FString UDataTools::PrintCropState(const FCropStateData& CropState)
{
	return CropState.ToString();
}
