// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/SatelliteAIController.h"


// Sets default values
ASatelliteAIController::ASatelliteAIController()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void ASatelliteAIController::BeginPlay()
{
	Super::BeginPlay();

	ensure(SatelliteBehaviorTree);
	RunBehaviorTree(SatelliteBehaviorTree);
}

// Called every frame
void ASatelliteAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

