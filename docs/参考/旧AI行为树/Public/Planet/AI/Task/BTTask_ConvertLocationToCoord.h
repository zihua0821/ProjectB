// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ConvertLocationToCoord.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UBTTask_ConvertLocationToCoord : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ConvertLocationToCoord();
	
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	
private:
	// 输入：存储 EQS 结果的黑板键（Vector）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector InputLocationKey;

	// 输出：目标单元格坐标（Struct: FIntPoint）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector OutputCellCoordKey;

	// 网格管理器引用（从黑板获取）
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PlanetActorKey;
};
