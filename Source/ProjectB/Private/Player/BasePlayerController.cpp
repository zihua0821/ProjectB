// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Player/BasePlayerController.h"

#include "ProjectB/Public/TimeSync/TimeSyncComponent.h"

ABasePlayerController::ABasePlayerController()
{
	TimeSyncComponent = CreateDefaultSubobject<UTimeSyncComponent>(TEXT("TimeSyncComponent"));
}
