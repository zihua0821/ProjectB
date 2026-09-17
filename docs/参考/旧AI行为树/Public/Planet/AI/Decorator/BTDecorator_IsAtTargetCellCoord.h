// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_IsAtTargetCellCoord.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTDecorator_IsAtTargetCellCoord : public UBTDecorator
{
	GENERATED_BODY()
	
public:
	UBTDecorator_IsAtTargetCellCoord();

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	// 目标单元格坐标（Struct: FIntPoint）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetCoordKey;

	// 当前单元格坐标（Struct: FIntPoint）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CurrentCoordKey;

	// PlanetActor
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlanetActorKey;

	// 轨道数据（Struct: FOrbitData）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector OrbitDataKey;

	// 误差容限
	UPROPERTY(EditAnywhere, Category = "Config")
	float ErrorTolerance = 10.f;
	
};
