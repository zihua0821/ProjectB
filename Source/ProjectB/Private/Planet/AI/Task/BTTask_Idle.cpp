// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_Idle.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Planet/OrbitMovementComponent.h"

UBTTask_Idle::UBTTask_Idle()
{
	NodeName = "Idle";
	bNotifyTick = false;
}

EBTNodeResult::Type UBTTask_Idle::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return EBTNodeResult::Failed;

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn) return EBTNodeResult::Failed;

	UOrbitMovementComponent* OrbitMovement = Pawn->FindComponentByClass<UOrbitMovementComponent>();
	if (!OrbitMovement) return EBTNodeResult::Failed;

	// 恢复 Idle 状态
	OrbitMovement->SetMoveState(ESatelliteMoveState::Idle);  
	
	return EBTNodeResult::Succeeded;
}
