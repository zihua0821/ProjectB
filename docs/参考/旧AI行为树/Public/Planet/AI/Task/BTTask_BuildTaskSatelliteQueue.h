// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BuildTaskSatelliteQueue.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_BuildTaskSatelliteQueue : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BuildTaskSatelliteQueue();
	
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	
private:

	// 输出：目标单元格坐标（Struct: FIntPoint）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetCellCoordKey;

	// PlanetActor
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlanetActorKey;

	// SatelliteActor
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector SatelliteActorKey;
};
