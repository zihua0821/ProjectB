// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/EQS/EQSTest_PlotScore.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Crops/CropBase.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetInterface.h"

UEQSTest_PlotScore::UEQSTest_PlotScore()
{
	ValidItemType = UEnvQueryItemType_Point::StaticClass();
	TestPurpose = EEnvTestPurpose::Score;
}

void UEQSTest_PlotScore::RunTest(FEnvQueryInstance& QueryInstance) const
{
	AActor* OwnerActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!OwnerActor) return;

	// 1. 获取黑板
	const UBlackboardComponent* Blackboard = UAIBlueprintHelperLibrary::GetBlackboard(OwnerActor);
	if (!Blackboard) return;

	const APlanetActor* PlanetActor = Cast<APlanetActor>(Blackboard->GetValueAsObject(TEXT("PlanetActor")));
	
	// 2. 遍历所有候选点
	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		float Score = 0.f;
		// 获取候选点的世界位置
		FIntPoint CellCoord = UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(
			GetItemLocation(QueryInstance, It.GetIndex()),
			PlanetActor->PlanetConfig,
			true,
			PlanetActor->GetActorTransform()
			);
		const FFarmLandPlotInfo PlotInfo = PlanetActor->GetPlotInfo(CellCoord);
		FPlotStateData StateData = PlotInfo.StateData;
		const FPlotCropInfo CropInfo = PlanetActor->GetPlotCropInfo(CellCoord);

		Score += (StateData.HasState(EPlotState::Plowed) ? 1.f : 0.f) * PlowedWeight;
		Score += (StateData.HasState(EPlotState::CropPlanted) ? 1.f : 0.f) * PlantedWeight;
		Score += FMath::Clamp(1.f - PlotInfo.MoisturePercentage, 0.f, 1.f) * MoistureWeight;
		Score += FMath::Clamp(1.f - PlotInfo.FertilityPercentage, 0.f, 1.f) * FertilityWeight;
		if (CropInfo.Crop)
		{
			Score += (CropInfo.Crop->GetCropData().CropStateData.HasState(ECropState::ReadyForHarvest) ? 1.f : 0.f) * HarvestWeight;
		}
		Score += (StateData.HasState(EPlotState::WeedInfested) ? 1.f : 0.f) * WeedWeight;
		Score += (StateData.HasState(EPlotState::PestInfested) ? 1.f : 0.f) * PestWeight;
		
		It.SetScore(TestPurpose, FilterType, Score, 0.f, 0.f);
	}
}
