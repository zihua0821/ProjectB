// SatelliteManagerComponent.cpp
#include "Mass/SatelliteManagerComponent.h"

#include "Farm/CellManagerComponent.h"    // 意图委托订阅（活跃农田集合维护）
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeFragments.h"   // FMassStateTreeInstanceFragment / FMassStateTreeSharedFragment
#include "Mass/SatelliteMassFragments.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"   // ComputeOrbitWorldPosition（轨道几何，Planet 域）
#include "Planet/PlanetConfigDataAsset.h"
#include "StateTree.h"
#include "Subsystem/ISMManager.h"

namespace
{
	/** 智能对象 Slot 索引约定：0=Plot（地块），1=Crop（作物） */
	constexpr int32 CropSlotIndex = 1;

	/** 未配置轨道时的兜底轨道参数 */
	constexpr float FallbackOrbitRadius = 55000.f;

	/** 服务距离默认值（覆盖带半厚 cm；与 FOrbitDefinition::ServiceDistance 默认一致） */
	constexpr float DefaultServiceDistance = 1000.f;
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

	// 订阅格子 Slot 启停意图：维护活跃农田集合（Crop Slot=1 启用即播种/重播成功）
	if (UCellManagerComponent* CellManager = PlanetActor->GetCellManagerComponent())
	{
		CellManager->OnCellSlotStateChanged.AddUObject(this, &USatelliteManagerComponent::HandleCellSlotStateChanged);
	}

	// 注册卫星网格 ISM 参数（单一事实来源；单桶路径，父级 = 星球根组件）
	if (SatelliteMesh)
	{
		if (UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>())
		{
			FISMComponentParams SatelliteParams;
			SatelliteParams.AttachParent = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
			SatelliteParams.bDisableBuckets = true;
			ISMManager->RegisterMeshParams(SatelliteMesh, SatelliteParams);
		}
	}

	// 1. 构建运行时轨道数据（可编辑副本；来自 PlanetConfig->OrbitDefinitions，空则兜底默认）
	BuildRuntimeOrbits();
	if (RuntimeOrbits.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: no valid runtime orbit"));
		return;
	}

	// 2. 为全部轨道创建卫星（含渲染实例注册）
	CreateAllSatellites();

	if (SatelliteMesh == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] SatelliteMesh is not set - satellites will be invisible"));
	}

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Created %d satellites on %d orbit(s) (StateTree: %s, Mesh: %s)"),
		SatelliteEntities.Num(),
		RuntimeOrbits.Num(),
		StateTreeAsset ? *StateTreeAsset->GetName() : TEXT("none"),
		SatelliteMesh ? *SatelliteMesh->GetName() : TEXT("none"));
}

void USatelliteManagerComponent::BuildRuntimeOrbits()
{
	RuntimeOrbits.Reset();

	const UPlanetConfigDataAsset* Config = PlanetActor ? PlanetActor->PlanetConfig : nullptr;

	// 优先读取星球配置中的轨道定义（每个定义生成一条运行时轨道）
	if (Config && Config->OrbitDefinitions.Num() > 0)
	{
		for (const FOrbitDefinition& Definition : Config->OrbitDefinitions)
		{
			if (Definition.OrbitName == NAME_None)
			{
				UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] Skipping orbit definition with NAME_None"));
				continue;
			}

			FRuntimeOrbitData Data;
			Data.OrbitName = Definition.OrbitName;
			Data.Transform = Definition.Transform;
			Data.Radius = Definition.Radius;
			// 服务距离：配置值优先；未配置（0）时用默认值（DefaultServiceDistance）
			Data.ServiceDistance = (Definition.ServiceDistance > 0.f) ? Definition.ServiceDistance : DefaultServiceDistance;
			Data.SatelliteCount = SatelliteCount;
			Data.AngularVelocity = DefaultAngularVelocity;
			Data.bClockwise = bDefaultClockwise;
			RuntimeOrbits.Add(MoveTemp(Data));
		}
	}

	// 兜底：未配置时使用默认轨道（与 OrbitManager 未配置时一致）
	if (RuntimeOrbits.Num() == 0)
	{
		FRuntimeOrbitData Data;
		Data.OrbitName = FName("Default");
		Data.Transform = FTransform::Identity;
		Data.Transform.SetRotation(FRotator(0.f, 180.f, 0.f).Quaternion());
		Data.Radius = FallbackOrbitRadius;
		Data.ServiceDistance = DefaultServiceDistance;
		Data.SatelliteCount = SatelliteCount;
		Data.AngularVelocity = DefaultAngularVelocity;
		Data.bClockwise = bDefaultClockwise;
		RuntimeOrbits.Add(MoveTemp(Data));
	}
}

void USatelliteManagerComponent::CreateAllSatellites()
{
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	TArray<FMassEntityHandle> AllEntities;
	TArray<FTransform> AllTransforms;

	for (const FRuntimeOrbitData& Orbit : RuntimeOrbits)
	{
		CreateSatellitesForOrbit(Orbit, EntityManager, AllEntities, AllTransforms);
	}

	RegisterRenderInstances(AllEntities, AllTransforms);

	SatelliteEntities = MoveTemp(AllEntities);
}

void USatelliteManagerComponent::CreateSatellitesForOrbit(const FRuntimeOrbitData& Orbit, FMassEntityManager& EntityManager, TArray<FMassEntityHandle>& OutEntities, TArray<FTransform>& OutTransforms)
{
	if (Orbit.SatelliteCount <= 0)
	{
		return;
	}

	// 共享数据（每条轨道一份：轨道几何 + 服务半径 + Mesh + 依赖引用；同值共享）
	FMassSatelliteSharedFragment SatelliteShared;
	SatelliteShared.OrbitTransform = Orbit.Transform;
	SatelliteShared.Radius = Orbit.Radius;
	SatelliteShared.ServiceDistance = Orbit.ServiceDistance;
	SatelliteShared.SatelliteMesh = SatelliteMesh;
	SatelliteShared.SatelliteScale = SatelliteScale;
	SatelliteShared.PlanetConfig = PlanetActor->PlanetConfig;
	SatelliteShared.CellManager = PlanetActor->GetCellManagerComponent();
	SatelliteShared.SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent();
	SatelliteShared.SatelliteManager = this;
	const FConstSharedStruct SatelliteSharedStruct = EntityManager.GetOrCreateConstSharedFragment(SatelliteShared);

	// 建原型（手写路线，与格子创建模式一致）
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

	// 批量创建实体
	TArray<FMassEntityHandle> Entities;
	EntityManager.BatchCreateEntities(ArchetypeHandle, Orbit.SatelliteCount, Entities);

	// 写轨道运动数据 + 组装初始世界变换（角度沿该轨道均分）
	for (int32 Index = 0; Index < Entities.Num(); ++Index)
	{
		FMassOrbitFragment& OrbitFragment = EntityManager.GetFragmentDataChecked<FMassOrbitFragment>(Entities[Index]);
		OrbitFragment.OrbitName = Orbit.OrbitName;
		OrbitFragment.CurrentAngle = 360.0f * static_cast<float>(Index) / static_cast<float>(Entities.Num());
		OrbitFragment.TargetAngle = OrbitFragment.CurrentAngle;
		OrbitFragment.AngularVelocity = Orbit.AngularVelocity;
		OrbitFragment.bClockwise = Orbit.bClockwise;
		OrbitFragment.MoveState = ESatelliteMoveState::Idle;

		const FVector WorldLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Orbit.Transform, Orbit.Radius, OrbitFragment.CurrentAngle);
		OutTransforms.Add(FTransform(FRotator::ZeroRotator, WorldLocation, FVector(SatelliteScale)));
	}

	// 挂共享数据（轨道共享 + StateTree 共享；实例分配与激活由引擎 Processor 自动完成）
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

	OutEntities.Append(Entities);
}

void USatelliteManagerComponent::RegisterRenderInstances(const TArray<FMassEntityHandle>& Entities, const TArray<FTransform>& Transforms)
{
	if (SatelliteMesh == nullptr || Entities.Num() == 0)
	{
		return;
	}

	UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>();
	if (ISMManager == nullptr)
	{
		return;
	}

	// 单桶路径：空 GridCoords + 注册参数（Initialize 已 RegisterMeshParams——含 bDisableBuckets）
	FISMComponentParams ISMParams;

	const TArray<FGlobalInstanceId> InstanceIds = ISMManager->BatchAddInstances(
		SatelliteMesh,
		Transforms,
		TArray<FIntPoint>(),   // 空坐标 → 单桶路径
		0,                     // 无 CustomData
		ISMParams,
		/*bWorldSpace=*/true);

	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	for (int32 Index = 0; Index < Entities.Num() && Index < InstanceIds.Num(); ++Index)
	{
		EntityManager.GetFragmentDataChecked<FSatelliteRenderFragment>(Entities[Index]).InstanceId = InstanceIds[Index];
	}
}

void USatelliteManagerComponent::DestroyAllSatellites()
{
	if (SatelliteEntities.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 1. 移除渲染实例（收集稳定句柄 → 批量移除）
	if (SatelliteMesh)
	{
		if (UISMManager* ISMManager = World->GetSubsystem<UISMManager>())
		{
			TArray<FGlobalInstanceId> InstanceIds;
			InstanceIds.Reserve(SatelliteEntities.Num());
			for (const FMassEntityHandle& Entity : SatelliteEntities)
			{
				if (const FSatelliteRenderFragment* RenderFragment = EntityManager.GetFragmentDataPtr<FSatelliteRenderFragment>(Entity))
				{
					InstanceIds.Add(RenderFragment->InstanceId);
				}
			}
			if (InstanceIds.Num() > 0)
			{
				ISMManager->BatchRemoveInstances(SatelliteMesh, InstanceIds);
			}
		}
	}

	// 2. 销毁实体
	EntityManager.BatchDestroyEntities(SatelliteEntities);
	SatelliteEntities.Reset();
}

void USatelliteManagerComponent::ApplyOrbitChanges()
{
	DestroyAllSatellites();
	CreateAllSatellites();

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Applied orbit changes: %d satellites on %d orbit(s)"),
		SatelliteEntities.Num(), RuntimeOrbits.Num());
}

bool USatelliteManagerComponent::UpdateOrbitData(const FName OrbitName, const FRuntimeOrbitData& NewData)
{
	for (FRuntimeOrbitData& Data : RuntimeOrbits)
	{
		if (Data.OrbitName == OrbitName)
		{
			Data = NewData;
			Data.OrbitName = OrbitName;   // 名称不可通过此接口变更
			OnRuntimeOrbitChanged.Broadcast(OrbitName);
			return true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] UpdateOrbitData failed: orbit '%s' not found"), *OrbitName.ToString());
	return false;
}

bool USatelliteManagerComponent::AddOrbit(const FRuntimeOrbitData& NewOrbit)
{
	if (NewOrbit.OrbitName == NAME_None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] AddOrbit failed: OrbitName is None"));
		return false;
	}
	for (const FRuntimeOrbitData& Data : RuntimeOrbits)
	{
		if (Data.OrbitName == NewOrbit.OrbitName)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] AddOrbit failed: orbit '%s' already exists"), *NewOrbit.OrbitName.ToString());
			return false;
		}
	}

	// 兜底：服务距离未设置时取默认（半格高）——保证新轨道有有效覆盖（如删空后重建）
	FRuntimeOrbitData SanitizedOrbit = NewOrbit;
	if (SanitizedOrbit.ServiceDistance <= 0.f)
	{
		SanitizedOrbit.ServiceDistance = DefaultServiceDistance;
	}

	RuntimeOrbits.Add(SanitizedOrbit);
	ApplyOrbitChanges();
	OnRuntimeOrbitChanged.Broadcast(NewOrbit.OrbitName);
	return true;
}

void USatelliteManagerComponent::RemoveOrbit(const FName OrbitName)
{
	const int32 RemovedCount = RuntimeOrbits.RemoveAll(
		[OrbitName](const FRuntimeOrbitData& Data) { return Data.OrbitName == OrbitName; });

	if (RemovedCount == 0)
	{
		return;
	}

	ApplyOrbitChanges();
	OnRuntimeOrbitChanged.Broadcast(OrbitName);
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
