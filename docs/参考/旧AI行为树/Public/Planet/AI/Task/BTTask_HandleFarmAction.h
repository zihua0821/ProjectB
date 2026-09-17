// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Crops/CropData.h"
#include "BTTask_HandleFarmAction.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_HandleFarmAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_HandleFarmAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 目标单元格坐标（Struct: FIntPoint）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetCoordKey;

	// CurrentAction
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CurrentActionKey;
	
	// PlanetActor
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlanetActorKey;
	
};
