// PlanetInterface.cpp
// 实现：星球接口默认实现（业务覆写见 APlanetActor）

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

FTransform IPlanetInterface::GetCellWorldTransform_Implementation(FIntPoint CellCoord) const
{
	return FTransform();
}
