// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BasePlayerController.generated.h"

class UTimeSyncComponent;
/**
 * 
 */
UCLASS()
class PROJECTB_API ABasePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABasePlayerController();
protected:
	// 时间同步组件
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Time Sync")
	TObjectPtr<UTimeSyncComponent> TimeSyncComponent;
};
