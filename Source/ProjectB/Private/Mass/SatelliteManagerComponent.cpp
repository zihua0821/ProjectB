// SatelliteManagerComponent.cpp
#include "Mass/SatelliteManagerComponent.h"

#include "Farm/CellManagerComponent.h"    // 意图委托订阅（活跃农田集合维护）
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeFragments.h"   // FMassStateTreeInstanceFragment / FMassStateTreeSharedFragment
#include "Mass/SatelliteMassFragments.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "StateTree.h"
#include "Subsystem/ISMManager.h"

namespace
{
	/** 智能对象 Slot 索引约定：0=Plot（地块），1=Crop（作物） */
	constexpr int32 CropSlotIndex = 1;
}

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

	// 订阅格子 Slot 启停意图：维护活跃农田集合（Crop Slot=1 启用即播种/重播成功）
	if (UCellManagerComponent* CellManager = PlanetActor->GetCellManagerComponent())
	{
		CellManager->OnCellSlotStateChanged.AddUObject(this, &USatelliteManagerComponent::HandleCellSlotStateChanged);
	}

	// 1. 解析目标轨道定义
	FOrbitDefinition OrbitDefinition;
	if (!ResolveOrbitDefinition(OrbitDefinition))
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: no valid orbit definition"));
		return;
	}

	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 2. 构建共享数据（ConstSharedFragment：轨道几何 + Mesh + 缩放 + 依赖引用，同值共享）
	FMassSatelliteSharedFragment SatelliteShared;
	SatelliteShared.OrbitTransform = OrbitDefinition.Transform;
	SatelliteShared.Radius = OrbitDefinition.Radius;
	SatelliteShared.SatelliteMesh = SatelliteMesh;
	SatelliteShared.SatelliteScale = SatelliteScale;
	SatelliteShared.PlanetConfig = PlanetActor->PlanetConfig;
	SatelliteShared.CellManager = PlanetActor->GetCellManagerComponent();
	SatelliteShared.SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent();
	SatelliteShared.SatelliteManager = this;
	const FConstSharedStruct SatelliteSharedStruct = EntityManager.GetOrCreateConstSharedFragment(SatelliteShared);

	// 3. 建原型（手写路线，与格子创建模式一致）
	TArray<const UScriptStruct*> FragmentTypes;
	FragmentTypes.Add(FMassOrbitFragment::StaticStruct());
	FragmentTypes.Add(FMassSatelliteTaskFragment::StaticStruct());
	FragmentTypes.Add(FSatelliteTag::StaticStruct());
	FragmentTypes.Add(FSatelliteRenderFragment::StaticStruct());
	if (StateTreeAsset)
	{
		FragmentTypes.Add(FMassStateTreeInstanceFragment::StaticStruct());
	}
	const FMassArchetypeHandle ArchetypeHandle = EntityManager.CreateArchetype(FragmentTypes);

	// 4. 批量创建实体
	TArray<FMassEntityHandle> Entities;
	EntityManager.BatchCreateEntities(ArchetypeHandle, SatelliteCount, Entities);

	// 5. 写轨道运动数据 + 组装初始位置（角度沿轨道均分）
	TArray<FTransform> InitialTransforms;
	InitialTransforms.Reserve(Entities.Num());
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		FMassOrbitFragment& OrbitFragment = EntityManager.GetFragmentDataChecked<FMassOrbitFragment>(Entities[Index]);
		OrbitFragment.OrbitName = OrbitDefinition.OrbitName;
		OrbitFragment.CurrentAngle = 360.0f * static_cast<float>(Index) / static_cast<float>(SatelliteCount);
		OrbitFragment.TargetAngle = OrbitFragment.CurrentAngle;
		OrbitFragment.AngularVelocity = DefaultAngularVelocity;
		OrbitFragment.bClockwise = bDefaultClockwise;
		OrbitFragment.MoveState = ESatelliteMoveState::Idle;

		// 初始世界变换：轨道位置 + 卫星缩放（朝向暂为零旋转）
		const FVector WorldLocation = ComputeOrbitWorldPosition(SatelliteShared.OrbitTransform, SatelliteShared.Radius, OrbitFragment.CurrentAngle);
		InitialTransforms.Add(FTransform(FRotator::ZeroRotator, WorldLocation, FVector(SatelliteScale)));
	}

	// 6. 挂共享数据（轨道几何 + StateTree；StateTree 实例分配与激活由引擎 Processor 自动完成）
	for (const FMassEntityHandle& Entity : Entities)
	{
		EntityManager.AddConstSharedFragmentToEntity(Entity, SatelliteSharedStruct);
	}
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

	// 7. 注册 ISM 实例（单桶路径：空 GridCoords + bDisableBuckets，与格子分桶完全隔离）
	if (SatelliteMesh)
	{
		if (UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>())
		{
			FISMComponentParams ISMParams;
			ISMParams.AttachParent = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
			ISMParams.bDisableBuckets = true;

			const TArray<FGlobalInstanceId> InstanceIds = ISMManager->BatchAddInstances(
				SatelliteMesh,
				InitialTransforms,
				TArray<FIntPoint>(),   // 空坐标 → 单桶路径
				0,                     // 无 CustomData
				ISMParams,
				/*bWorldSpace=*/true);

			for (int32 Index = 0; Index < Entities.Num() && Index < InstanceIds.Num(); ++Index)
			{
				EntityManager.GetFragmentDataChecked<FSatelliteRenderFragment>(Entities[Index]).InstanceId = InstanceIds[Index];
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] SatelliteMesh is not set - satellites will be invisible"));
	}

	SatelliteEntities = MoveTemp(Entities);

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Created %d satellites on orbit '%s' (StateTree: %s, Mesh: %s)"),
		SatelliteEntities.Num(),
		*OrbitDefinition.OrbitName.ToString(),
		StateTreeAsset ? *StateTreeAsset->GetName() : TEXT("none"),
		SatelliteMesh ? *SatelliteMesh->GetName() : TEXT("none"));
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

void USatelliteManagerComponent::HandleCellSlotStateChanged(const FIntPoint Coords, const int32 SlotIndex, const bool bEnabled)
{
	// 仅关心作物 Slot（1）：启用=播种/重播成功入集合，禁用=移除作物出集合
	if (SlotIndex != CropSlotIndex)
	{
		return;
	}

	if (bEnabled)
	{
		ActiveFarmlandCoords.Add(Coords);
	}
	else
	{
		ActiveFarmlandCoords.Remove(Coords);
	}
}
