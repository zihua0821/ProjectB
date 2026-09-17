// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "Planet/OrbitMovementComponent.h"
#include "BTDecorator_IsSatelliteMoveState.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTDecorator_IsSatelliteMoveState : public UBTDecorator
{
	GENERATED_BODY()
	
public:
	UBTDecorator_IsSatelliteMoveState();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	ESatelliteMoveState MoveState = ESatelliteMoveState::Idle;
};
