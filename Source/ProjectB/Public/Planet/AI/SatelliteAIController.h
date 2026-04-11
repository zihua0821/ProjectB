// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/AIModule/Classes/AIController.h"
#include "SatelliteAIController.generated.h"

UCLASS()
class PROJECTB_API ASatelliteAIController : public AAIController
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASatelliteAIController();

	// 卫星行为树
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite")
	TObjectPtr<UBehaviorTree> SatelliteBehaviorTree;
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
