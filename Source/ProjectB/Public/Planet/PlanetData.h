#pragma once

#include "CoreMinimal.h"
#include "PlanetData.generated.h"

USTRUCT(BlueprintType)
struct FPlanetInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 PlanetPower = 99999999;
};