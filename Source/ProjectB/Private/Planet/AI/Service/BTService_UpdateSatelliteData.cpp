// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Service/BTService_UpdateSatelliteData.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Kismet/GameplayStatics.h"
#include "Planet/PlanetActor.h"
#include "Planet/AI/SatelliteAIController.h"
#include "StructUtils/StructView.h"

UBTService_UpdateSatelliteData::UBTService_UpdateSatelliteData()
{
	NodeName = TEXT("Update Satellite Data");
	bTickIntervals = true;
	Interval = 1.f;
}

void UBTService_UpdateSatelliteData::OnSearchStart(FBehaviorTreeSearchData& SearchData)
{
	Super::OnSearchStart(SearchData);

	FName Name = GetSelectedBlackboardKey();
	FString Key = Name.ToString();

	ASatelliteAIController* AIController = Cast<ASatelliteAIController>(SearchData.OwnerComp.GetAIOwner());
	if (!AIController) return;
	
	UBlackboardComponent* Blackboard = SearchData.OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return;

	AActor* PlanetActor = UGameplayStatics::GetActorOfClass(AIController, APlanetActor::StaticClass());
	if (PlanetActor == nullptr) return;
	Blackboard->SetValueAsObject(TEXT("PlanetActor"), PlanetActor);

	FBlackboard::FKey KeyID = Blackboard->GetKeyID("TargetCellCoord");
	FIntPoint MyCoord(10, 20);
	Blackboard->SetValue<UBlackboardKeyType_Struct>(KeyID, FInstancedStruct::Make(MyCoord));
	
	FOrbitData OrbitData = IPlanetInterface::Execute_GetOrbitData(PlanetActor, "Default");
	Blackboard->SetValue<UBlackboardKeyType_Struct>(TEXT("OrbitData"), FInstancedStruct::Make(OrbitData));
}

void UBTService_UpdateSatelliteData::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FName Name = GetSelectedBlackboardKey();
	FString Key = Name.ToString();
}
