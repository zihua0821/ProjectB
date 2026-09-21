// OrbitManagerComponent.cpp
#include "Planet/OrbitManagerComponent.h"

#include "DrawDebugHelpers.h"
#include "Planet/PlanetConfigDataAsset.h"

UOrbitManagerComponent::UOrbitManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOrbitManagerComponent::SetPlanetConfig(const TObjectPtr<UPlanetConfigDataAsset>& InPlanetConfig)
{
	PlanetConfig = InPlanetConfig;
}

void UOrbitManagerComponent::AddOrbit(FName OrbitName, const FOrbitDefinition& Definition)
{
	if (OrbitName == NAME_None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UOrbitManagerComponent] Attempted to add orbit with NAME_None"));
		return;
	}
	Orbits.Add(OrbitName, Definition);

	// 调试绘制：轨道圆（编辑器/调试观察用）
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
}

void UOrbitManagerComponent::RemoveOrbit(FName OrbitName)
{
	if (Orbits.Contains(OrbitName))
	{
		Orbits.Remove(OrbitName);
	}
}

FOrbitDefinition UOrbitManagerComponent::GetOrbitData(FName OrbitName) const
{
	if (const FOrbitDefinition* FoundOrbit = Orbits.Find(OrbitName))
	{
		return *FoundOrbit;
	}
	return FOrbitDefinition();
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

	// 覆盖集（物理 Overlap）移除后不再依赖 CellManager，直接初始化轨道表
	InitOrbit();
}

void UOrbitManagerComponent::InitOrbit()
{
	// 优先读取星球配置中的轨道定义（数据化，PlanetConfig->OrbitDefinitions）
	if (PlanetConfig.IsValid() && PlanetConfig->OrbitDefinitions.Num() > 0)
	{
		for (const FOrbitDefinition& Definition : PlanetConfig->OrbitDefinitions)
		{
			if (Definition.OrbitName == NAME_None)
			{
				UE_LOG(LogTemp, Warning, TEXT("[UOrbitManagerComponent] Skipping orbit definition with NAME_None"));
				continue;
			}
			AddOrbit(Definition.OrbitName, Definition);
		}
		return;
	}

	// 兜底：未配置时使用默认轨道
	FOrbitDefinition DefaultOrbit;
	DefaultOrbit.OrbitName = FName("Default");
	DefaultOrbit.Transform = FTransform::Identity;
	DefaultOrbit.Transform.SetRotation(FRotator(0.f, 180.f, 0.f).Quaternion());
	DefaultOrbit.Radius = 55000.f;
	AddOrbit(DefaultOrbit.OrbitName, DefaultOrbit);
}
