// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Crops/CropData.h"
#include "BTTask_HandleFarmAction.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_HandleFarmAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_HandleFarmAction();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FarmAction")
	TEnumAsByte<EEditMode> EditMode = EEditMode::None;
};
