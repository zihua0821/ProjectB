// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Decorator/BTDecorator_IsCellInOrbitCoverage.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/OrbitManagerComponent.h"
#include "StructUtils/StructView.h"

UBTDecorator_IsCellInOrbitCoverage::UBTDecorator_IsCellInOrbitCoverage()
{
	NodeName = "Is Cell In Orbit Coverage";
}

bool UBTDecorator_IsCellInOrbitCoverage::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory) const
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return  false;
	FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(GetSelectedBlackboardKey()));
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	FInstancedStruct OrbitDataStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TEXT("OrbitData")));
	const FOrbitData OrbitData = OrbitDataStruct.Get<FOrbitData>();
	return OrbitData.CoveredCoordsMap.Contains(TargetCoord);
}
