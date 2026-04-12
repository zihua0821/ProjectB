// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_MoveToCoord.generated.h"

class UOrbitMovementComponent;
/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_MoveToCoord : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToCoord();
	
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	// 缓存 BehaviorTreeComponent
	UPROPERTY()
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	void OnMoveCompleted() const;
};
