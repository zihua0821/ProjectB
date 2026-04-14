// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Decorator/BTDecorator_IsTaskSatelliteQueueEmpty.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Planet/AutomationSatellite.h"

UBTDecorator_IsTaskSatelliteQueueEmpty::UBTDecorator_IsTaskSatelliteQueueEmpty()
{
	NodeName = "Is Task Satellite Queue Empty";
}

bool UBTDecorator_IsTaskSatelliteQueueEmpty::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	const UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return false;
	UObject* SatelliteObject = Blackboard->GetValueAsObject(SelfActorKey.SelectedKeyName);
	if (!SatelliteObject) return false;
	AAutomationSatellite* Satellite = Cast<AAutomationSatellite>(SatelliteObject);
	if (!Satellite) return false;

	const TQueue<EEditMode>& SatelliteTaskQueue = Satellite->GetSatelliteTaskQueue();
	return IsInversed() ? !SatelliteTaskQueue.IsEmpty() : SatelliteTaskQueue.IsEmpty();
}
