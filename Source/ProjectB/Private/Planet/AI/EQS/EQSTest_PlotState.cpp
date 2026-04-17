// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/EQS/EQSTest_PlotState.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Crops/CropData.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetInterface.h"

UEQSTest_PlotState::UEQSTest_PlotState()
{
	ValidItemType = UEnvQueryItemType_Point::StaticClass();
	TestPurpose = EEnvTestPurpose::Filter;
	FilterType = EEnvTestFilterType::Match;
}

void UEQSTest_PlotState::RunTest(FEnvQueryInstance& QueryInstance) const
{
	AActor* OwnerActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!OwnerActor) return;

	// 1. 获取黑板
	const UBlackboardComponent* Blackboard = UAIBlueprintHelperLibrary::GetBlackboard(OwnerActor);
	if (!Blackboard) return;

	const APlanetActor* PlanetActor = Cast<APlanetActor>(Blackboard->GetValueAsObject(TEXT("PlanetActor")));
	
	// 构建期望的掩码
	FPlotStateData PlotStateData;
	if (bMatchPlowed)       PlotStateData.AddState(EPlotState::Plowed);
	if (bMatchCropPlanted)  PlotStateData.AddState(EPlotState::CropPlanted);
	if (bMatchWatered)      PlotStateData.AddState(EPlotState::Watered);
	if (bMatchFertilized)   PlotStateData.AddState(EPlotState::Fertilized);
	if (bMatchWeedInfested) PlotStateData.AddState(EPlotState::WeedInfested);
	if (bMatchPestInfested) PlotStateData.AddState(EPlotState::PestInfested);

	// 如果没有勾选任何位，默认认为所有项目都匹配
	if (PlotStateData.IsEmpty())
	{
		// 如果没有指定任何过滤条件，默认全部通过
		return;
	}

	// 2. 遍历所有候选点
	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		// 获取候选点的世界位置
		FIntPoint CellCoord = UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(
			GetItemLocation(QueryInstance, It.GetIndex()),
			PlanetActor->PlanetConfig,
			true,
			PlanetActor->GetActorTransform()
			);
		const FFarmLandPlotInfo PlotInfo = PlanetActor->GetPlotInfo(CellCoord);

		It.SetScore(TestPurpose, FilterType, PlotInfo.StateData != PlotStateData, true);
	}
}
