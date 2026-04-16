// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Decorator/BTDecorator_IsAtTargetCellCoord.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/OrbitMovementComponent.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "StructUtils/StructView.h"

UBTDecorator_IsAtTargetCellCoord::UBTDecorator_IsAtTargetCellCoord()
{
	NodeName = "Is At Target Cell Coord";
}

bool UBTDecorator_IsAtTargetCellCoord::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	// 1. 获取黑板
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return false;
	
	// 2. 读取目标坐标
	const FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TargetCoordKey.SelectedKeyName));
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	// 3. 读取当前坐标
	const FInstancedStruct CurrentCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(CurrentCoordKey.SelectedKeyName));
	const FIntPoint CurrentCoord = CurrentCoordStruct.Get<FIntPoint>();

	// 4. 获取 AI Controller 和 Pawn
	const AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return false;

	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return false;

	// 5. 读取当前角度
	const UOrbitMovementComponent* OrbitMovement = ControlledPawn->FindComponentByClass<UOrbitMovementComponent>();
	if (!OrbitMovement) return false;
	const float CurrentAngleDeg = OrbitMovement->GetCurrentAngle();
	
	// 6. 计算目标角度
	const FInstancedStruct OrbitDataStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(OrbitDataKey.SelectedKeyName));
	const FOrbitData& OrbitData = OrbitDataStruct.Get<FOrbitData>();
	const UObject* PlanetActor = Blackboard->GetValueAsObject(PlanetActorKey.SelectedKeyName);
	if (!PlanetActor) return false;
	const float TargetAngleDeg = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(TargetCoord, OrbitData, PlanetActor);

	const bool bIsAtTargetCoord = CurrentCoord == TargetCoord || FMath::IsNearlyEqual(CurrentAngleDeg, TargetAngleDeg, ErrorTolerance);
	return IsInversed() ? !bIsAtTargetCoord : bIsAtTargetCoord;
}
