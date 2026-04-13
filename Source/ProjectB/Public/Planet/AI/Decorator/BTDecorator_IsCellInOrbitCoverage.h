// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BTDecorator_IsCellInOrbitCoverage.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTDecorator_IsCellInOrbitCoverage : public UBTDecorator_Blackboard
{
	GENERATED_BODY()

public:
	UBTDecorator_IsCellInOrbitCoverage();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
