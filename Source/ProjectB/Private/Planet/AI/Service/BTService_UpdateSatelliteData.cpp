// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Service/BTService_UpdateSatelliteData.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Kismet/GameplayStatics.h"
#include "Planet/AutomationSatellite.h"
#include "Planet/OrbitMovementComponent.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/AI/SatelliteAIController.h"
#include "StructUtils/StructView.h"

UBTService_UpdateSatelliteData::UBTService_UpdateSatelliteData()
{
	NodeName = TEXT("Update Satellite Data");
	bTickIntervals = true;
	Interval = 0.1f;
}

void UBTService_UpdateSatelliteData::OnSearchStart(FBehaviorTreeSearchData& SearchData)
{
	Super::OnSearchStart(SearchData);
	
	ASatelliteAIController* AIController = Cast<ASatelliteAIController>(SearchData.OwnerComp.GetAIOwner());
	if (!AIController) return;
	
	UBlackboardComponent* Blackboard = SearchData.OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return;

	UObject* SelfActor = Blackboard->GetValueAsObject(TEXT("SelfActor"));
	if (!SelfActor) return;
	AutomationSatellite = Cast<AAutomationSatellite>(SelfActor);
	if (!AutomationSatellite) return;
	
	AActor* PlanetActor = UGameplayStatics::GetActorOfClass(AIController, APlanetActor::StaticClass());
	if (PlanetActor == nullptr) return;
	Blackboard->SetValueAsObject(TEXT("PlanetActor"), PlanetActor);
	
	OrbitData = IPlanetInterface::Execute_GetOrbitData(PlanetActor, "Default");
	Blackboard->SetValue<UBlackboardKeyType_Struct>(TEXT("OrbitData"), FInstancedStruct::Make(OrbitData));
}

void UBTService_UpdateSatelliteData::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	if (!AutomationSatellite) return;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return;
	UOrbitMovementComponent* OrbitMovementComponent = AutomationSatellite->OrbitMovementComponent;
	if (!OrbitMovementComponent) return;

	// 获取当前坐标
	const FIntPoint CurrentCellCoord = UPlanetBlueprintFunctionLibrary::GetNearestCellCoordFromOrbitAngle(OrbitMovementComponent->GetCurrentAngle(), OrbitData);
	Blackboard->SetValue<UBlackboardKeyType_Struct>(TEXT("CurrentCellCoord"), FInstancedStruct::Make(CurrentCellCoord));
}

