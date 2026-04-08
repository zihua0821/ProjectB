// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/OrbitManagerComponent.h"

#include "Engine/OverlapResult.h"
#include "Farm/FarmlandComponent.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "ProjectB/ProjectB.h"


UOrbitManagerComponent::UOrbitManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

void UOrbitManagerComponent::AddOrbit(FName OrbitName, const FOrbitData& Definition)
{
	if (OrbitName == NAME_None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UOrbitManagerComponent] Attempted to add orbit with NAME_None"));
		return;
	}
	Orbits.Add(OrbitName, Definition);
	// DrawCircle(
	// 	GetWorld(),
	// 	Definition.Transform.GetLocation(),
	// 	Definition.Transform.GetRotation().GetForwardVector(),
	// 	Definition.Transform.GetRotation().GetRightVector(),
	// 	FColor::Red,
	// 	Definition.Radius,
	// 	128,
	// 	true
	// 	);
	UpdateOrbitCoveredCoords(OrbitName);
}

void UOrbitManagerComponent::RemoveOrbit(FName OrbitName)
{
	if (Orbits.Contains(OrbitName))
	{
		Orbits.Remove(OrbitName);
	}
}

FOrbitData UOrbitManagerComponent::GetOrbitData(FName OrbitName) const
{
	if (const FOrbitData* FoundOrbit = Orbits.Find(OrbitName))
	{
		return *FoundOrbit;
	}
	return FOrbitData();
}

bool UOrbitManagerComponent::HasOrbit(FName OrbitName) const
{
	return Orbits.Contains(OrbitName);
}

TArray<FName> UOrbitManagerComponent::GetAllOrbitNames() const
{
	TArray<FName> Keys;
	Orbits.GetKeys(Keys);
	return Keys;
}


void UOrbitManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	
	if (CellManager.IsValid())
	{
		if (CellManager.Get()->bIsInstancesReady)
		{
			InitOrbit();
		}
		else
		{
			CellManager.Get()->OnInstancesReady.BindRaw(this, &ThisClass::InitOrbit);
		}
	}
	
}

void UOrbitManagerComponent::UpdateOrbitCoveredCoords(FName OrbitName)
{
	if (!CellManager.IsValid() || !PlanetConfig.IsValid())
	{
		return;
	}
	if (!Orbits.Contains(OrbitName))
	{
		return;
	}
	FOrbitData& OrbitData = Orbits[OrbitName];
	TSet<FIntPoint> CoveredCoords;

	const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
	const float LatStepRad = FMath::DegreesToRadians(LatStep);
	const float CellHeight = PlanetConfig->Radius * LatStepRad;                    
	
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SphereOverlapComponents), false);
	Params.bDebugQuery = true;
	TArray<FOverlapResult> Overlaps;

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Cell);
	
	UWorld* World = GetWorld();
	if(World != nullptr)
	{
		World->OverlapMultiByObjectType(
			Overlaps,
			OrbitData.Transform.GetLocation(),
			OrbitData.Transform.GetRotation(),
			ObjectParams,
			FCollisionShape::MakeBox(FVector(OrbitData.Radius,OrbitData.Radius , CellHeight / 2.f)),
			Params
			);
	}

	for (const FOverlapResult& Overlap : Overlaps)
	{
		CoveredCoords.Add(CellManager.Get()->GetCellCoordsByIndex(Overlap.ItemIndex));
	}
	OrbitData.CoveredCoords = CoveredCoords;
}

void UOrbitManagerComponent::InitOrbit()
{
	FOrbitData DefaultOrbitData;
	DefaultOrbitData.Transform = FTransform::Identity;
	DefaultOrbitData.Radius = 30000.f;

	AddOrbit(FName("Default"), DefaultOrbitData);
}

