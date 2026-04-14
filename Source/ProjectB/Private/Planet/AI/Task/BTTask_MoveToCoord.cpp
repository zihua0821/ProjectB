// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_MoveToCoord.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/OrbitMovementComponent.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetInterface.h"
#include "StructUtils/StructView.h"

UBTTask_MoveToCoord::UBTTask_MoveToCoord()
{
	NodeName = "Move To Cell Coordinate";
	bNotifyTick = false;
}

EBTNodeResult::Type UBTTask_MoveToCoord::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CachedOwnerComp = &OwnerComp;
	// 1. 获取黑板
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;
	
	// 2. 读取目标坐标
	const FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TargetCoordKey.GetSelectedKeyID()));
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	// 3. 获取 AI Controller 和 Pawn
	const AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return EBTNodeResult::Failed;
	const APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn) return EBTNodeResult::Failed;

	// 4. 获取轨道移动组件
	UOrbitMovementComponent* OrbitMovement = ControlledPawn->FindComponentByClass<UOrbitMovementComponent>();
	if (!OrbitMovement) return EBTNodeResult::Failed;
	
	// 5. 计算目标角度
	const FInstancedStruct OrbitDataStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(OrbitDataKey.GetSelectedKeyID()));
	const FOrbitData& OrbitData = OrbitDataStruct.Get<FOrbitData>();
	const UObject* PlanetActor = Blackboard->GetValueAsObject(PlanetActorKey.SelectedKeyName);
	if (!PlanetActor) return EBTNodeResult::Failed;
	const float TargetAngleDeg = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(TargetCoord, OrbitData, PlanetActor);
	
	// 6. 添加委托
	OrbitMovement->OnMoveToTargetCompleted.AddUObject(this, &ThisClass::OnMoveCompleted);

	// 7. 开始移动
	OrbitMovement->StartMoveToTargetAngle(TargetAngleDeg);
	
	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToCoord::OnMoveCompleted() const
{
	if (UBehaviorTreeComponent* OwnerCompPtr = CachedOwnerComp.Get())
	{
		FinishLatentTask(*OwnerCompPtr, EBTNodeResult::Succeeded);
	}
}
