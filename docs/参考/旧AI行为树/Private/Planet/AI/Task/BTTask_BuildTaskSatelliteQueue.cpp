// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_BuildTaskSatelliteQueue.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Crops/CropBase.h"
#include "Planet/AutomationSatellite.h"
#include "Planet/PlanetActor.h"
#include "StructUtils/StructView.h"

UBTTask_BuildTaskSatelliteQueue::UBTTask_BuildTaskSatelliteQueue()
{
	NodeName = "Build Task Satellite Queue";
}

EBTNodeResult::Type UBTTask_BuildTaskSatelliteQueue::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TargetCellCoordKey.SelectedKeyName));
	if (!TargetCoordStruct.IsValid()) return EBTNodeResult::Failed;
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	UObject* PlanetObject = Blackboard->GetValueAsObject(PlanetActorKey.SelectedKeyName);
	if (!PlanetObject) return EBTNodeResult::Failed;
	APlanetActor* Planet = Cast<APlanetActor>(PlanetObject);
	if (!Planet) return EBTNodeResult::Failed;

	UObject* SatelliteObject = Blackboard->GetValueAsObject(SatelliteActorKey.SelectedKeyName);
	if (!SatelliteObject) return EBTNodeResult::Failed;
	AAutomationSatellite* Satellite = Cast<AAutomationSatellite>(SatelliteObject);
	if (!Satellite) return EBTNodeResult::Failed;

	FFarmLandPlotInfo PlotInfo = Planet->GetPlotInfo(TargetCoord);
	FPlotCropInfo CropInfo = Planet->GetPlotCropInfo(TargetCoord);
	FPlotStateData StateData = PlotInfo.StateData;
	TQueue<EEditMode>& SatelliteTaskQueue = Satellite->GetSatelliteTaskQueue();

	if (!StateData.HasState(EPlotState::Plowed))
	{
		SatelliteTaskQueue.Enqueue(EEditMode::Plow);
		SatelliteTaskQueue.Enqueue(EEditMode::PlantCrop);
		SatelliteTaskQueue.Enqueue(EEditMode::Wet);
		SatelliteTaskQueue.Enqueue(EEditMode::Fertilize);
	}
	else if (!StateData.HasState(EPlotState::CropPlanted))
	{
		SatelliteTaskQueue.Enqueue(EEditMode::PlantCrop);
		SatelliteTaskQueue.Enqueue(EEditMode::Wet);
		SatelliteTaskQueue.Enqueue(EEditMode::Fertilize);
	}
	else
	{
		if (StateData.HasState(EPlotState::WeedInfested)) SatelliteTaskQueue.Enqueue(EEditMode::UnWeed);
		if (StateData.HasState(EPlotState::PestInfested)) SatelliteTaskQueue.Enqueue(EEditMode::UnPest);
		if (CropInfo.Crop)
		{
			const FCropData& CropData = CropInfo.Crop->GetCropData();
			if (CropData.CropStateData.HasState(ECropState::NeedsWater)) SatelliteTaskQueue.Enqueue(EEditMode::Wet);
			if (CropData.CropStateData.HasState(ECropState::NeedsFertilizer)) SatelliteTaskQueue.Enqueue(EEditMode::Fertilize);
			if (CropData.CropStateData.HasState(ECropState::ReadyForHarvest)) SatelliteTaskQueue.Enqueue(EEditMode::HarvestCrop);
		}
	}
	
	return EBTNodeResult::Succeeded;
}
