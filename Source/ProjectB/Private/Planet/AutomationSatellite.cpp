// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AutomationSatellite.h"

#include "Planet/OrbitMovementComponent.h"


AAutomationSatellite::AAutomationSatellite()
{
	PrimaryActorTick.bCanEverTick = true;

	// RootComponent
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	// OrbitMovementComponent
	OrbitMovementComponent = CreateDefaultSubobject<UOrbitMovementComponent>(TEXT("OrbitMovementComponent"));

	// SatelliteMeshComponent
	SatelliteMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SatelliteMeshComponent"));
	SatelliteMeshComponent->SetupAttachment(RootComponent);
}

void AAutomationSatellite::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SatelliteMeshComponent->SetStaticMesh(Mesh);
	SatelliteMeshComponent->SetWorldScale3D(FVector(10.f));

	if (OrbitMovementComponent)
	{
		OrbitMovementComponent->OrbitData = OrbitData;
		OrbitMovementComponent->AngularVelocity = AngularVelocity;
		OrbitMovementComponent->bClockwise = bClockwise;
	}
}

void AAutomationSatellite::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AAutomationSatellite::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

