// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_HandleFarmAction.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/PlanetActor.h"
#include "StructUtils/StructView.h"


UBTTask_HandleFarmAction::UBTTask_HandleFarmAction()
{
	NodeName = "Handle Farm Action";
	bNotifyTick = false;
}

EBTNodeResult::Type UBTTask_HandleFarmAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	const FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TargetCoordKey.SelectedKeyName));
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	const EEditMode EditMode = static_cast<EEditMode>(Blackboard->GetValueAsEnum(CurrentActionKey.SelectedKeyName));
	
	UObject* PlanetObject = Blackboard->GetValueAsObject(PlanetActorKey.SelectedKeyName);
	const APlanetActor* Planet = Cast<APlanetActor>(PlanetObject);
	if (!Planet) return EBTNodeResult::Failed;
	
	Planet->HandleLeftMouseClicked(EditMode, TargetCoord);
	
	return EBTNodeResult::Succeeded;
}
