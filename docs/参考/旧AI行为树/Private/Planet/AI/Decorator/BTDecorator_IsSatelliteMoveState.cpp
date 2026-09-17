// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Decorator/BTDecorator_IsSatelliteMoveState.h"

#include "AIController.h"

UBTDecorator_IsSatelliteMoveState::UBTDecorator_IsSatelliteMoveState()
{
	NodeName = "Is Satellite Move State";
}

bool UBTDecorator_IsSatelliteMoveState::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("UBTDecorator_IsSatelliteMoveState: AI Controller is null"));
		return false;
	}

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("UBTDecorator_IsSatelliteMoveState: Pawn is null"));
		return false;
	}

	// 4. 获取轨道移动组件
	const UOrbitMovementComponent* OrbitMovement = ControlledPawn->FindComponentByClass<UOrbitMovementComponent>();
	if (!OrbitMovement)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("UBTDecorator_IsSatelliteMoveState: OrbitMovementComponent not found on pawn"));
		return false;
	}
	return OrbitMovement->GetMoveState() == MoveState;
}
