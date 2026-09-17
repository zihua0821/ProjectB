// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_GetNextTask.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Planet/AutomationSatellite.h"

UBTTask_GetNextTask::UBTTask_GetNextTask()
{
	NodeName = "GetNextTask";
}

EBTNodeResult::Type UBTTask_GetNextTask::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;
	
	UObject* SatelliteObject = Blackboard->GetValueAsObject(SatelliteActorKey.SelectedKeyName);
	if (!SatelliteObject) return EBTNodeResult::Failed;
	AAutomationSatellite* Satellite = Cast<AAutomationSatellite>(SatelliteObject);
	if (!Satellite) return EBTNodeResult::Failed;

	TQueue<EEditMode>& SatelliteTaskQueue = Satellite->GetSatelliteTaskQueue();
	EEditMode CurrentAction;
	if (!SatelliteTaskQueue.Dequeue(CurrentAction))
	{
		return EBTNodeResult::Failed;
	}
	Blackboard->SetValueAsEnum(CurrentActionKey.SelectedKeyName, CurrentAction);

	return EBTNodeResult::Succeeded;
}
