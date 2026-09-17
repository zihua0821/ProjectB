// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/EQS/EQSGenerator_CoveredCells.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Planet/OrbitManagerComponent.h"
#include "Planet/PlanetInterface.h"
#include "StructUtils/StructView.h"

UEQSGenerator_CoveredCells::UEQSGenerator_CoveredCells()
{
	ItemType = UEnvQueryItemType_Point::StaticClass();
}

void UEQSGenerator_CoveredCells::GenerateItems(FEnvQueryInstance& QueryInstance) const
{
	AActor* OwnerActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!OwnerActor) return;

	// 1. 获取黑板
	const UBlackboardComponent* Blackboard = UAIBlueprintHelperLibrary::GetBlackboard(OwnerActor);
	if (!Blackboard) return;
	
	// 2. 读取 OrbitData 结构体
	const FInstancedStruct OrbitDataStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TEXT("OrbitData")));
	const FOrbitData& OrbitData = OrbitDataStruct.Get<FOrbitData>();

	// 3. 获取 PlanetActor
	const UObject* PlanetObject = Blackboard->GetValueAsObject(TEXT("PlanetActor"));
	
	// 4. 收集所有候选点的世界位置
	for (const auto& Pair : OrbitData.CoveredCoordsMap)
	{
		FTransform CellTransform = IPlanetInterface::Execute_GetCellWorldTransform(PlanetObject, Pair.Key);
		const FVector CellLocation = CellTransform.GetLocation();
		QueryInstance.AddItemData<UEnvQueryItemType_Point>(CellLocation);
	}
}
