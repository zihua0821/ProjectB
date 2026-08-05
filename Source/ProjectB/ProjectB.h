// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogServer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogClient, Log, All);

constexpr ECollisionChannel ECC_Cell = ECC_GameTraceChannel1;
static const FVector ParkedLocation(1.0e7, 1.0e7, -1.0e7);
