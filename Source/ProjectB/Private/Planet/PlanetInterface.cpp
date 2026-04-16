// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/PlanetInterface.h"

#include "Planet/PlanetData.h"


// Add default functionality here for any IPlanetInterface functions that are not pure virtual.
void IPlanetInterface::SetPlanetInfo_Implementation(const FPlanetInfo& InPlanetData)
{
}

FPlanetInfo IPlanetInterface::GetPlanetInfo_Implementation() const
{
	return FPlanetInfo();
}

FPlanetInfo& IPlanetInterface::GetPlanetInfoRef()
{
	static FPlanetInfo PlanetInfo;
	return PlanetInfo;
}

void IPlanetInterface::CallPlanetInfoChanged_Implementation()
{
}

void IPlanetInterface::UpdateRotation_Implementation(float NewAngle)
{
}

FOrbitData IPlanetInterface::GetOrbitData_Implementation(FName OrbitName) const
{
	return FOrbitData();
}

FTransform IPlanetInterface::GetCellWorldTransform_Implementation(FIntPoint CellCoord) const
{
	return FTransform();
}

FIntPoint IPlanetInterface::GetCellCoord_Implementation(FVector CellWorldLocation) const
{
	return FIntPoint();
}

FFarmLandPlotInfo IPlanetInterface::GetCellPlotInfoFromWorldLocation_Implementation(FVector CellWorldLocation) const
{
	return FFarmLandPlotInfo();
}

FPlotCropInfo IPlanetInterface::GetPlotCropInfoFromWorldLocation_Implementation(FVector CellWorldLocation) const
{
	return FPlotCropInfo();
}
