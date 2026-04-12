// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "Planet/OrbitManagerComponent.h"
#include "BTService_UpdateSatelliteData.generated.h"

class AAutomationSatellite;
/**
 * 更新卫星数据
 */
UCLASS()
class PROJECTB_API UBTService_UpdateSatelliteData : public UBTService_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTService_UpdateSatelliteData();

protected:

	virtual void OnSearchStart(FBehaviorTreeSearchData& SearchData) override;
	
	// Tick
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
private:
	UPROPERTY()
	TObjectPtr<AAutomationSatellite> AutomationSatellite;

	UPROPERTY()
	FOrbitData OrbitData;
};
