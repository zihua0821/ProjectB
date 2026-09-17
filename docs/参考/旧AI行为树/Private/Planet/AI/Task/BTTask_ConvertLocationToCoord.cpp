// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_ConvertLocationToCoord.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetInterface.h"
#include "StructUtils/StructView.h"

UBTTask_ConvertLocationToCoord::UBTTask_ConvertLocationToCoord()
{
	NodeName = "Convert Location to Cell Coord";
}

EBTNodeResult::Type UBTTask_ConvertLocationToCoord::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	// 1. 读取 EQS 返回的最佳位置
	const FVector BestLocation = Blackboard->GetValueAsVector(InputLocationKey.SelectedKeyName);
	if (BestLocation.IsNearlyZero()) return EBTNodeResult::Failed;

	// 2. 获取网格管理器
	const APlanetActor* PlanetActor = Cast<APlanetActor>(Blackboard->GetValueAsObject(PlanetActorKey.SelectedKeyName));
	if (!PlanetActor) return EBTNodeResult::Failed;

	// 3. 将世界位置转换为单元格坐标
	const FIntPoint CellCoord = UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(
				BestLocation,
				PlanetActor->PlanetConfig,
				true,
				PlanetActor->GetActorTransform()
				);
	// 4. 写入输出黑板键
	Blackboard->SetValue<UBlackboardKeyType_Struct>(OutputCellCoordKey.SelectedKeyName, FInstancedStruct::Make(CellCoord));
	
	return EBTNodeResult::Succeeded;
}
