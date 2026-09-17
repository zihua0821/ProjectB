// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_IsTaskSatelliteQueueEmpty.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTDecorator_IsTaskSatelliteQueueEmpty : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_IsTaskSatelliteQueueEmpty();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	// SelfActorKey
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SelfActorKey;
};
