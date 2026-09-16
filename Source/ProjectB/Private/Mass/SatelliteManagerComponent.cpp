// SatelliteManagerComponent.cpp
#include "Mass/SatelliteManagerComponent.h"

#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeFragments.h"   // FMassStateTreeInstanceFragment / FMassStateTreeSharedFragment
#include "Mass/SatelliteMassFragments.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "StateTree.h"

USatelliteManagerComponent::USatelliteManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USatelliteManagerComponent::Initialize(APlanetActor* InPlanetActor)
{
	PlanetActor = InPlanetActor;
	if (!PlanetActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: PlanetActor is null"));
		return;
	}
	if (SatelliteCount <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] Initialize skipped: SatelliteCount <= 0"));
		return;
	}

	// 1. 解析目标轨道定义
	FOrbitDefinition OrbitDefinition;
	if (!ResolveOrbitDefinition(OrbitDefinition))
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: no valid orbit definition"));
		return;
	}

	// 2. 建原型（手写路线，与格子创建模式一致）
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	TArray<const UScriptStruct*> FragmentTypes;
	FragmentTypes.Add(FMassOrbitFragment::StaticStruct());
	FragmentTypes.Add(FSatelliteTag::StaticStruct());
	if (StateTreeAsset)
	{
		FragmentTypes.Add(FMassStateTreeInstanceFragment::StaticStruct());
	}
	const FMassArchetypeHandle ArchetypeHandle = EntityManager.CreateArchetype(FragmentTypes);

	// 3. 批量创建实体
	TArray<FMassEntityHandle> Entities;
	EntityManager.BatchCreateEntities(ArchetypeHandle, SatelliteCount, Entities);

	// 4. 写轨道运动数据（初始角度沿轨道均分）
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		FMassOrbitFragment& OrbitFragment = EntityManager.GetFragmentDataChecked<FMassOrbitFragment>(Entities[Index]);
		OrbitFragment.OrbitName = OrbitDefinition.OrbitName;
		OrbitFragment.CurrentAngle = 360.0f * static_cast<float>(Index) / static_cast<float>(SatelliteCount);
		OrbitFragment.TargetAngle = OrbitFragment.CurrentAngle;
		OrbitFragment.AngularVelocity = DefaultAngularVelocity;
		OrbitFragment.bClockwise = bDefaultClockwise;
		OrbitFragment.MoveState = ESatelliteMoveState::Idle;
	}

	// 5. 挂 StateTree（ConstSharedFragment 携带资产；实例分配与激活由引擎 Processor 自动完成）
	if (StateTreeAsset)
	{
		FMassStateTreeSharedFragment SharedStateTree;
		SharedStateTree.StateTree = StateTreeAsset;
		const FConstSharedStruct StateTreeFragment = EntityManager.GetOrCreateConstSharedFragment(SharedStateTree);
		for (const FMassEntityHandle& Entity : Entities)
		{
			EntityManager.AddConstSharedFragmentToEntity(Entity, StateTreeFragment);
		}
	}

	SatelliteEntities = MoveTemp(Entities);

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Created %d satellites on orbit '%s' (StateTree: %s)"),
		SatelliteEntities.Num(),
		*OrbitDefinition.OrbitName.ToString(),
		StateTreeAsset ? *StateTreeAsset->GetName() : TEXT("none"));
}

bool USatelliteManagerComponent::ResolveOrbitDefinition(FOrbitDefinition& OutDefinition) const
{
	const UPlanetConfigDataAsset* Config = PlanetActor ? PlanetActor->PlanetConfig : nullptr;
	if (Config && Config->OrbitDefinitions.Num() > 0)
	{
		// 指定轨道名优先
		if (DefaultOrbitName != NAME_None)
		{
			for (const FOrbitDefinition& Definition : Config->OrbitDefinitions)
			{
				if (Definition.OrbitName == DefaultOrbitName)
				{
					OutDefinition = Definition;
					return true;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] Orbit '%s' not found, fallback to first definition"),
				*DefaultOrbitName.ToString());
		}

		OutDefinition = Config->OrbitDefinitions[0];
		return OutDefinition.OrbitName != NAME_None;
	}

	// 兜底：与 OrbitManager 未配置时一致的默认轨道
	OutDefinition.OrbitName = FName("Default");
	OutDefinition.Transform = FTransform::Identity;
	OutDefinition.Transform.SetRotation(FRotator(0.f, 180.f, 0.f).Quaternion());
	OutDefinition.Radius = 30000.f;
	return true;
}
