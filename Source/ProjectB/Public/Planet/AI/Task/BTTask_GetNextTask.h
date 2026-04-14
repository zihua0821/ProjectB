// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_GetNextTask.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_GetNextTask : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_GetNextTask();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:

	// SatelliteActor
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SatelliteActorKey;

	// 当前任务
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CurrentActionKey;
};
