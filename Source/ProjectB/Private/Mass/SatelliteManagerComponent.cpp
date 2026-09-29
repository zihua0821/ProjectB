// SatelliteManagerComponent.cpp
#include "Mass/SatelliteManagerComponent.h"

#include "Farm/CellManagerComponent.h"    // 意图委托订阅（活跃农田集合维护）
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeFragments.h"   // FMassStateTreeInstanceFragment / FMassStateTreeSharedFragment
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"   // 轨道槽位注册/认领（智能对象）
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

	// ---- 轨道全局规划（群体决策）求解辅助 ----

	/** 规划：槽位"已过"重合容差（度；已过 ≤ 此角视为立即可用——与任务侧起飞对齐窗口一致） */
	constexpr float PlanSlotLeadToleranceDeg = 2.0f;

	/** 角度归一化到 [0, 360)（消除长时运行累积圈数，保证 FindDeltaAngleDegrees 单次修正准确） */
	float NormalizeAngleDegrees360(const float AngleDeg)
	{
		const float Remainder = FMath::Fmod(AngleDeg, 360.f);
		return Remainder < 0.f ? Remainder + 360.f : Remainder;
	}

	/**
	 * 槽位等待角（度；"槽还要转多少度才到该悬停点的正下方"）——规划选座用
	 * 将到达（沿运动方向未到）→ 剩余角；已过 ≤ 容差 → 0；已过较多 → 360−Δ（必超上限自然被滤）
	 */
	float ComputeSlotWaitAngleDeg(const FOrbitSnapshot& Snapshot, const FVector& HoverLocation, const int32 SlotIndex)
	{
		float ThetaHoverDeg = 0.f;
		float RadiusInPlane = 0.f;
		float VerticalOffset = 0.f;
		UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Snapshot.OrbitTransform, HoverLocation, ThetaHoverDeg, RadiusInPlane, VerticalOffset);

		const float SlotPhaseDeg = Snapshot.SlotPhaseBaseDeg + 360.f * static_cast<float>(SlotIndex) / static_cast<float>(Snapshot.SlotCount);
		const float Direction = Snapshot.bClockwise ? -1.f : 1.f;
		const float DeltaDeg = FMath::FindDeltaAngleDegrees(NormalizeAngleDegrees360(ThetaHoverDeg), NormalizeAngleDegrees360(SlotPhaseDeg)) * Direction;
		if (DeltaDeg > PlanSlotLeadToleranceDeg)
		{
			return 360.f - DeltaDeg;   // 已过较多（需绕一整圈）
		}
		return FMath::Max(0.f, -DeltaDeg);
	}

	/** 占位集合（+可选试插槽、−可选排除槽）的"最小相邻间距"（度；环上取相邻槽索引差 × 槽间隔） */
	float ComputeMinAdjacentGapDeg(const TArray<bool>& SlotOccupied, const int32 SlotCount, const int32 ExtraSlotIndex, const int32 ExcludeSlotIndex = INDEX_NONE)
	{
		TArray<int32> OccupiedIndices;
		OccupiedIndices.Reserve(SlotOccupied.Num() + 1);
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			if (SlotIndex != ExcludeSlotIndex && SlotOccupied[SlotIndex])
			{
				OccupiedIndices.Add(SlotIndex);
			}
		}
		if (ExtraSlotIndex != INDEX_NONE)
		{
			OccupiedIndices.Add(ExtraSlotIndex);
		}
		if (OccupiedIndices.Num() <= 1)
		{
			return 360.f;   // 单点：视作无限间距
		}

		OccupiedIndices.Sort();
		const float DegPerSlot = 360.f / static_cast<float>(SlotCount);
		float MinGapDeg = 360.f;
		for (int32 Index = 0; Index < OccupiedIndices.Num(); ++Index)
		{
			const int32 NextIndex = OccupiedIndices[(Index + 1) % OccupiedIndices.Num()];
			const int32 SlotDiff = (Index + 1 < OccupiedIndices.Num())
				? (NextIndex - OccupiedIndices[Index])
				: (NextIndex + SlotCount - OccupiedIndices[Index]);
			MinGapDeg = FMath::Min(MinGapDeg, SlotDiff * DegPerSlot);
		}
		return MinGapDeg;
	}

	/** 某槽与最近占位邻居的间距（度；环向取第一个命中的步距；无邻居返回 360）——挤星判定用 */
	float ComputeNearestNeighborGapDeg(const TArray<bool>& SlotOccupied, const int32 SlotCount, const int32 SlotIndex)
	{
		const float DegPerSlot = 360.f / static_cast<float>(SlotCount);
		for (int32 Step = 1; Step < SlotCount; ++Step)
		{
			if (SlotOccupied[(SlotIndex - Step + SlotCount) % SlotCount] || SlotOccupied[(SlotIndex + Step) % SlotCount])
			{
				return Step * DegPerSlot;
			}
		}
		return 360.f;
	}

	/** 规划：漂移位移下限（度；低于不漂——构建求解参数时填入） */
	constexpr float PlanMoveMinDeg = 3.0f;

	/** 有向弧（起点方位 + 有符号扫过量）展开为 [0,360) 的连续角度区间（最多 2 段） */
	void AppendArcIntervals(const float StartDeg, const float TotalDeg, TArray<TPair<float, float>>& OutIntervals)
	{
		if (FMath::IsNearlyZero(TotalDeg))
		{
			return;
		}
		float NormalizedStart = FMath::Fmod(StartDeg, 360.f);
		if (NormalizedStart < 0.f)
		{
			NormalizedStart += 360.f;
		}
		if (TotalDeg > 0.f)
		{
			const float EndDeg = NormalizedStart + TotalDeg;
			if (EndDeg <= 360.f)
			{
				OutIntervals.Add(TPair<float, float>(NormalizedStart, EndDeg));
			}
			else
			{
				OutIntervals.Add(TPair<float, float>(NormalizedStart, 360.f));
				OutIntervals.Add(TPair<float, float>(0.f, EndDeg - 360.f));
			}
		}
		else
		{
			const float EndDeg = NormalizedStart + TotalDeg;   // TotalDeg 为负
			if (EndDeg >= 0.f)
			{
				OutIntervals.Add(TPair<float, float>(EndDeg, NormalizedStart));
			}
			else
			{
				OutIntervals.Add(TPair<float, float>(0.f, NormalizedStart));
				OutIntervals.Add(TPair<float, float>(360.f + EndDeg, 360.f));
			}
		}
	}

	/** 两段有向弧是否重叠（防撞·合并版：重叠即视为冲突；端点相接也算重叠） */
	bool AreDriftArcsOverlapping(const float StartA, const float TotalA, const float StartB, const float TotalB)
	{
		TArray<TPair<float, float>> IntervalsA;
		TArray<TPair<float, float>> IntervalsB;
		AppendArcIntervals(StartA, TotalA, IntervalsA);
		AppendArcIntervals(StartB, TotalB, IntervalsB);

		for (const TPair<float, float>& IntervalA : IntervalsA)
		{
			for (const TPair<float, float>& IntervalB : IntervalsB)
			{
				if (IntervalA.Key <= IntervalB.Value && IntervalB.Key <= IntervalA.Value)
				{
					return true;
				}
			}
		}
		return false;
	}
}

USatelliteManagerComponent::USatelliteManagerComponent()
{
	// 规划层已迁移至 Mass 帧序（由 USatelliteOrbitPlanProcessor 驱动 TickOrbitPlanning）——组件 Tick 不再使用
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

	// 2. 先注册轨道槽位（智能对象；卫星创建时做初始认领），再为全部轨道创建卫星（含渲染实例注册）
	if (PlanetActor)
	{
		if (USmartObjectManagerComponent* SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent())
		{
			SmartObjectManager->RegisterOrbitSlots(RuntimeOrbits, OrbitSlotMultiplier);
		}
	}
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

	// 注：槽位注册在 ApplyOrbitChanges（重建入口）与 Initialize 路径中负责；
	//     卫星创建时仅做初始认领（依赖槽位已注册）

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
		OrbitFragment.VirtualPhase = 360.0f * static_cast<float>(Index) / static_cast<float>(Entities.Num());   // 均分相位
		OrbitFragment.AngularVelocity = Orbit.AngularVelocity;
		OrbitFragment.bClockwise = Orbit.bClockwise;

		// 初始槽位认领（槽 Index = 卫星序 × 倍数——与槽网格均分相位对齐；槽位由 SmartObjectManager 注册）
		if (PlanetActor)
		{
			if (USmartObjectManagerComponent* SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent())
			{
				OrbitFragment.SlotClaimHandle = SmartObjectManager->TryClaimOrbitSlot(Orbit.OrbitName, Index * OrbitSlotMultiplier, ESmartObjectClaimPriority::Normal);
				// 归属槽由句柄唯一标识（查询位置/相位一律走 GetOrbitSlot*ByClaim）
			}
		}

		const FVector WorldLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Orbit.Transform, Orbit.Radius, OrbitFragment.VirtualPhase);
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
	// ① 先重建轨道槽位（智能对象；轨道数据变更 → 槽位与卫星同步重建，先清旧槽）
	if (PlanetActor)
	{
		if (USmartObjectManagerComponent* SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent())
		{
			SmartObjectManager->RegisterOrbitSlots(RuntimeOrbits, OrbitSlotMultiplier);
		}
	}

	// ② 销毁旧卫星 → ③ 重建卫星（内部不再注册槽位）
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

void USatelliteManagerComponent::TickOrbitPlanning()
{
	if (SatelliteEntities.Num() == 0)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastOrbitPlanTime < OrbitPlanIntervalSeconds)
	{
		return;
	}
	LastOrbitPlanTime = Now;

	// 滚动节拍：抄账 → 求解（每轨独立）→ 发单（不等执行完成；在途按目标槽计入快照）
	TArray<FOrbitSnapshot> Snapshots;
	CollectOrbitSnapshots(Snapshots);

	// 求解参数（配置 → 纯函数输入）
	FOrbitPlanParams PlanParams;
	PlanParams.WaitAngleLimitDeg = OrbitPlanWaitAngleLimitDeg;
	PlanParams.RebalanceRatio = OrbitPlanRebalanceRatio;
	PlanParams.MoveMinDeg = PlanMoveMinDeg;
	// 阶段②（均匀化）独立节流：5s 一窗（落座阶段①仍按主节拍滚动）
	PlanParams.bSkipRebalance = (Now - LastRebalanceTime) < OrbitRebalanceIntervalSeconds;
	if (!PlanParams.bSkipRebalance)
	{
		LastRebalanceTime = Now;
	}

	FOrbitPlan Plan;
	for (const FOrbitSnapshot& Snapshot : Snapshots)
	{
		BuildOrbitPlan(Snapshot, PlanParams, Plan);
	}

	DispatchOrbitPlan(Plan);
}

void USatelliteManagerComponent::CollectOrbitSnapshots(TArray<FOrbitSnapshot>& OutSnapshots)
{
	OutSnapshots.Reset();

	UWorld* World = GetWorld();
	if (!World || !PlanetActor)
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	const USmartObjectManagerComponent* SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent();

	// 按轨道分组；同一帧内一口气读完（"同一张照片"）
	TMap<FName, int32> OrbitNameToSnapshotIndex;
	for (int32 SatelliteIndex = 0; SatelliteIndex < SatelliteEntities.Num(); ++SatelliteIndex)
	{
		const FMassEntityHandle& Entity = SatelliteEntities[SatelliteIndex];
		const FMassOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassOrbitFragment>(Entity);
		if (!OrbitFragment)
		{
			continue;
		}

		int32& SnapshotIndex = OrbitNameToSnapshotIndex.FindOrAdd(OrbitFragment->OrbitName, INDEX_NONE);
		if (SnapshotIndex == INDEX_NONE)
		{
			// 新建该轨快照（槽网格参数同时定格）
			FOrbitSnapshot Snapshot;
			Snapshot.OrbitName = OrbitFragment->OrbitName;
			if (const FRuntimeOrbitData* OrbitData = RuntimeOrbits.FindByPredicate(
				[OrbitFragment](const FRuntimeOrbitData& Data) { return Data.OrbitName == OrbitFragment->OrbitName; }))
			{
				Snapshot.OrbitTransform = OrbitData->Transform;
			}
			Snapshot.AngularVelocity = OrbitFragment->AngularVelocity;
			Snapshot.bClockwise = OrbitFragment->bClockwise;
			if (SmartObjectManager)
			{
				Snapshot.SlotCount = SmartObjectManager->GetOrbitSlotCount(OrbitFragment->OrbitName);
				SmartObjectManager->GetOrbitSlotPhase(OrbitFragment->OrbitName, 0, Snapshot.SlotPhaseBaseDeg);
			}
			SnapshotIndex = OutSnapshots.Add(MoveTemp(Snapshot));
		}

		// 占位 = 槽句柄所指的槽（在途/返航星=目标槽——"在途当已到"）
		FOrbitSnapshotEntry Entry;
		Entry.Entity = Entity;
		Entry.Ordinal = SatelliteIndex;
		Entry.bOccupiesSlot = OrbitFragment->SlotClaimHandle.IsValid();
		if (SmartObjectManager && Entry.bOccupiesSlot)
		{
			Entry.SlotIndex = SmartObjectManager->ResolveSlotIndexByClaim(OrbitFragment->OrbitName, OrbitFragment->SlotClaimHandle);
			Entry.bOccupiesSlot = Entry.SlotIndex != INDEX_NONE;   // 句柄有效但反查失败：视为不占位（防御）
		}
		Entry.bAwaitingReturn = (OrbitFragment->State == ESatelliteState::AwaitReturn) && !Entry.bOccupiesSlot;
		if (Entry.bAwaitingReturn && OrbitFragment->bOverrideLocation)
		{
			Entry.HoverLocation = OrbitFragment->OverrideLocation;
		}

		// 均匀化对象（可动星）：在轨巡航 + 占位 + 无在途漂移指令
		Entry.bDriftEligible = (OrbitFragment->State == ESatelliteState::Cruise) && Entry.bOccupiesSlot && !OrbitFragment->bDriftRequested;

		// 在途漂移星：记录路径弧段（防撞禁区）
		Entry.bInTransitDrift = (OrbitFragment->State == ESatelliteState::Drift);
		if (Entry.bInTransitDrift)
		{
			if (const FMassSatelliteTaskFragment* TaskFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteTaskFragment>(Entity))
			{
				Entry.DriftArcStartDeg = TaskFragment->PhaseThetaStartDeg;
				Entry.DriftArcTotalDeg = TaskFragment->PhaseThetaDeltaDeg;
			}
		}
		OutSnapshots[SnapshotIndex].Entries.Add(Entry);
	}
}

void USatelliteManagerComponent::BuildOrbitPlan(const FOrbitSnapshot& Snapshot, const FOrbitPlanParams& Params, FOrbitPlan& OutPlan)
{
	if (Snapshot.SlotCount <= 0 || FMath::IsNearlyZero(Snapshot.AngularVelocity))
	{
		return;   // 无槽位 / 静止轨道（ω≈0 不参与作业体系）
	}

	// 占位集合（既有占位 + 本轮已分配）
	TArray<bool> SlotOccupied;
	SlotOccupied.Init(false, Snapshot.SlotCount);
	for (const FOrbitSnapshotEntry& Entry : Snapshot.Entries)
	{
		if (Entry.bOccupiesSlot && SlotOccupied.IsValidIndex(Entry.SlotIndex))
		{
			SlotOccupied[Entry.SlotIndex] = true;
		}
	}

	// 待返星：按创建序号升序处理（确定性）
	TArray<const FOrbitSnapshotEntry*> AwaitingReturnStars;
	for (const FOrbitSnapshotEntry& Entry : Snapshot.Entries)
	{
		if (Entry.bAwaitingReturn && !Entry.HoverLocation.IsNearlyZero())
		{
			AwaitingReturnStars.Add(&Entry);
		}
	}
	// 注：UE 对指针数组的 Sort 会自动解引用（TDereferenceWrapper）——谓词参数为引用
	AwaitingReturnStars.Sort([](const FOrbitSnapshotEntry& A, const FOrbitSnapshotEntry& B) { return A.Ordinal < B.Ordinal; });

	// 乙案落座：候选 = 空槽中"等待角 ≤ 上限"者；
	// 择选 = 插入后最小相邻间距最大 → 平局等待角小 → 再平局索引小（升序遍历天然保留小索引）
	for (const FOrbitSnapshotEntry* Star : AwaitingReturnStars)
	{
		int32 BestSlotIndex = INDEX_NONE;
		float BestMinGapDeg = -1.f;
		float BestWaitAngleDeg = 0.f;
		for (int32 SlotIndex = 0; SlotIndex < Snapshot.SlotCount; ++SlotIndex)
		{
			if (SlotOccupied[SlotIndex])
			{
				continue;
			}
			const float WaitAngleDeg = ComputeSlotWaitAngleDeg(Snapshot, Star->HoverLocation, SlotIndex);
			if (WaitAngleDeg > Params.WaitAngleLimitDeg)
			{
				continue;
			}
			const float MinGapDeg = ComputeMinAdjacentGapDeg(SlotOccupied, Snapshot.SlotCount, SlotIndex);
			const bool bBetter = (MinGapDeg > BestMinGapDeg + KINDA_SMALL_NUMBER)
				|| (FMath::IsNearlyEqual(MinGapDeg, BestMinGapDeg) && WaitAngleDeg < BestWaitAngleDeg - KINDA_SMALL_NUMBER);
			if (bBetter)
			{
				BestSlotIndex = SlotIndex;
				BestMinGapDeg = MinGapDeg;
				BestWaitAngleDeg = WaitAngleDeg;
			}
		}

		if (BestSlotIndex != INDEX_NONE)
		{
			FOrbitPlanEntry PlanEntry;
			PlanEntry.Entity = Star->Entity;
			PlanEntry.Action = EOrbitPlanAction::AssignReturnSlot;
			PlanEntry.TargetSlotIndex = BestSlotIndex;
			OutPlan.Entries.Add(PlanEntry);
			SlotOccupied[BestSlotIndex] = true;   // 本轮已分配 → 并入占位（后续待返星不重复占）
		}
	}

	// ---- 阶段②：在轨均匀化（"消除过近"：只动挤对中可动的一方；独立节流见 bSkipRebalance）----

	if (Params.bSkipRebalance)
	{
		return;   // 均匀化独立节流（落座阶段①不受影响）
	}

	// 触发：最小相邻间距 < 均匀间距 × 触发线
	int32 OccupiedCount = 0;
	for (int32 SlotIndex = 0; SlotIndex < Snapshot.SlotCount; ++SlotIndex)
	{
		if (SlotOccupied[SlotIndex])
		{
			++OccupiedCount;
		}
	}
	if (OccupiedCount < 2)
	{
		return;   // 单星无间距可言
	}
	const float UniformStepDeg = 360.f / static_cast<float>(OccupiedCount);
	const float MinGapLineDeg = UniformStepDeg * Params.RebalanceRatio;
	if (ComputeMinAdjacentGapDeg(SlotOccupied, Snapshot.SlotCount, INDEX_NONE) >= MinGapLineDeg)
	{
		return;   // 未到触发线
	}

	const float SlotStepDeg = 360.f / static_cast<float>(Snapshot.SlotCount);
	// 注：原"试算锚 + 全体对齐网格"已移除——目标从"追求等距"收敛为"消除过近"：
	//     已达标星不再产生伪位移（"陷入整理"死循环的根治点）

	// 可动星按序号升序（确定性）；仅处理"与最近邻居过近"者——只动挤对中可动的一方
	TArray<const FOrbitSnapshotEntry*> MovableStars;
	for (const FOrbitSnapshotEntry& Entry : Snapshot.Entries)
	{
		if (Entry.bDriftEligible)
		{
			MovableStars.Add(&Entry);
		}
	}
	MovableStars.Sort([](const FOrbitSnapshotEntry& A, const FOrbitSnapshotEntry& B) { return A.Ordinal < B.Ordinal; });

	struct FIssuedDriftArc
	{
		float StartDeg = 0.f;
		float TotalDeg = 0.f;
	};
	TArray<FIssuedDriftArc> IssuedArcs;
	TArray<bool> SlotReserved;
	SlotReserved.Init(false, Snapshot.SlotCount);

	const float Direction = Snapshot.bClockwise ? -1.f : 1.f;
	for (const FOrbitSnapshotEntry* Star : MovableStars)
	{
		// 已达标（与最近邻居 ≥ 触发线）：免动——防止对"已经没问题的星"产出伪位移（死循环根治点之一）
		if (ComputeNearestNeighborGapDeg(SlotOccupied, Snapshot.SlotCount, Star->SlotIndex) >= MinGapLineDeg)
		{
			continue;
		}

		const float SlotDeg = Snapshot.SlotPhaseBaseDeg + static_cast<float>(Star->SlotIndex) * SlotStepDeg;

		// 目标槽：合规候选（到达后全场最小间距 ≥ 触发线；排除本星旧槽——将空出）中择选：
		// ① 前移（沿运动方向，加速调整——“停住被穿”的规避倾向）② 位移幅度小 ③ 小索引
		// 无合规则退取"到达后全场最小间距最大"者（尽量改善；平局优先前移 → 位移小 → 小索引）
		int32 TargetSlotIndex = INDEX_NONE;
		{
			bool bBestForward = false;   // 当前最优是否前移
			float BestMoveDeg = TNumericLimits<float>::Max();
			for (int32 SlotIndex = 0; SlotIndex < Snapshot.SlotCount; ++SlotIndex)
			{
				if (SlotOccupied[SlotIndex] || SlotReserved[SlotIndex] || SlotIndex == Star->SlotIndex)
				{
					continue;
				}
				if (ComputeMinAdjacentGapDeg(SlotOccupied, Snapshot.SlotCount, SlotIndex, Star->SlotIndex) < MinGapLineDeg)
				{
					continue;   // 不合规（到达后仍有过近对）
				}
				const float CandidateDeg = Snapshot.SlotPhaseBaseDeg + static_cast<float>(SlotIndex) * SlotStepDeg;
				const float MoveSignedDeg = FMath::FindDeltaAngleDegrees(SlotDeg, CandidateDeg) * Direction;
				const bool bForward = MoveSignedDeg > 0.f;
				const float MoveDeg = FMath::Abs(MoveSignedDeg);
				const bool bBetter = (bForward && !bBestForward)
					|| (bForward == bBestForward && MoveDeg < BestMoveDeg - KINDA_SMALL_NUMBER);
				if (bBetter)
				{
					bBestForward = bForward;
					BestMoveDeg = MoveDeg;
					TargetSlotIndex = SlotIndex;
				}
			}
		}
		if (TargetSlotIndex == INDEX_NONE)
		{
			// 无合规：退取"到达后全场最小间距最大"者（平局优先前移 → 位移小 → 小索引）
			float BestArrivalGapDeg = -1.f;
			bool bFallbackForward = false;
			float BestFallbackMoveDeg = TNumericLimits<float>::Max();
			for (int32 SlotIndex = 0; SlotIndex < Snapshot.SlotCount; ++SlotIndex)
			{
				if (SlotOccupied[SlotIndex] || SlotReserved[SlotIndex] || SlotIndex == Star->SlotIndex)
				{
					continue;
				}
				const float ArrivalGapDeg = ComputeMinAdjacentGapDeg(SlotOccupied, Snapshot.SlotCount, SlotIndex, Star->SlotIndex);
				const float CandidateDeg = Snapshot.SlotPhaseBaseDeg + static_cast<float>(SlotIndex) * SlotStepDeg;
				const float MoveSignedDeg = FMath::FindDeltaAngleDegrees(SlotDeg, CandidateDeg) * Direction;
				const bool bForward = MoveSignedDeg > 0.f;
				const float MoveDeg = FMath::Abs(MoveSignedDeg);
				const bool bBetterFallback =
					(ArrivalGapDeg > BestArrivalGapDeg + KINDA_SMALL_NUMBER)
					|| (FMath::IsNearlyEqual(ArrivalGapDeg, BestArrivalGapDeg)
						&& ((bForward && !bFallbackForward)
							|| (bForward == bFallbackForward && MoveDeg < BestFallbackMoveDeg - KINDA_SMALL_NUMBER)));
				if (bBetterFallback)
				{
					BestArrivalGapDeg = ArrivalGapDeg;
					bFallbackForward = bForward;
					BestFallbackMoveDeg = MoveDeg;
					TargetSlotIndex = SlotIndex;
				}
			}
			if (TargetSlotIndex == INDEX_NONE)
			{
				continue;   // 无空槽可用（K≥2 有余量；异常时下轮再议）
			}
		}

		// 位移量（沿运动方向带符号）→ 过滤 [MoveMin, 上限]（上限与等待角上限统一）
		const float TargetSlotDeg = Snapshot.SlotPhaseBaseDeg + static_cast<float>(TargetSlotIndex) * SlotStepDeg;
		const float DeltaMoveDeg = FMath::FindDeltaAngleDegrees(SlotDeg, TargetSlotDeg) * Direction;
		if (FMath::Abs(DeltaMoveDeg) < Params.MoveMinDeg || FMath::Abs(DeltaMoveDeg) > Params.WaitAngleLimitDeg)
		{
			continue;
		}

		// 弧段（与 Drift 任务同构：总扫过量 = ω·T·dir + Δs；
		// T = clamp(|Δs| / 滑动速率上限, 2, 6)——后移速率上限 0.75ω（保底绝对速度 0.25ω，不停住），前移 1ω）
		const float ArcAngularSpeed = FMath::Max(Snapshot.AngularVelocity, 1.f);
		const float SlideRateDegPerSec = (DeltaMoveDeg < 0.f) ? (ArcAngularSpeed * 0.75f) : ArcAngularSpeed;
		const float Duration = FMath::Clamp(FMath::Abs(DeltaMoveDeg) / SlideRateDegPerSec, 2.0f, 6.0f);
		const float TotalArcDeg = Snapshot.AngularVelocity * Duration * Direction + DeltaMoveDeg;

		// 防撞（合并版）：与在途漂移弧段、与本轮已发弧段，重叠即跳过（下轮再议）
		bool bBlocked = false;
		for (const FOrbitSnapshotEntry& Entry : Snapshot.Entries)
		{
			if (Entry.bInTransitDrift
				&& AreDriftArcsOverlapping(SlotDeg, TotalArcDeg, Entry.DriftArcStartDeg, Entry.DriftArcTotalDeg))
			{
				bBlocked = true;
				break;
			}
		}
		if (!bBlocked)
		{
			for (const FIssuedDriftArc& Arc : IssuedArcs)
			{
				if (AreDriftArcsOverlapping(SlotDeg, TotalArcDeg, Arc.StartDeg, Arc.TotalDeg))
				{
					bBlocked = true;
					break;
				}
			}
		}
		if (bBlocked)
		{
			continue;
		}

		FOrbitPlanEntry PlanEntry;
		PlanEntry.Entity = Star->Entity;
		PlanEntry.Action = EOrbitPlanAction::DriftToSlot;
		PlanEntry.TargetSlotIndex = TargetSlotIndex;
		OutPlan.Entries.Add(PlanEntry);

		SlotReserved[TargetSlotIndex] = true;   // 本轮预占（后续星不再选它）
		FIssuedDriftArc NewArc;
		NewArc.StartDeg = SlotDeg;
		NewArc.TotalDeg = TotalArcDeg;
		IssuedArcs.Add(NewArc);
	}
}

void USatelliteManagerComponent::DispatchOrbitPlan(const FOrbitPlan& Plan)
{
	if (Plan.Entries.Num() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	USmartObjectManagerComponent* SmartObjectManager = PlanetActor ? PlanetActor->GetSmartObjectManagerComponent() : nullptr;
	if (!World || !SmartObjectManager)
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	for (const FOrbitPlanEntry& PlanEntry : Plan.Entries)
	{
		if (PlanEntry.TargetSlotIndex == INDEX_NONE)
		{
			continue;
		}
		FMassOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassOrbitFragment>(PlanEntry.Entity);
		if (!OrbitFragment)
		{
			continue;
		}

		if (PlanEntry.Action == EOrbitPlanAction::AssignReturnSlot)
		{
			// 落座：认领目标槽 + 写回句柄（待返星原无槽）
			if (OrbitFragment->SlotClaimHandle.IsValid())
			{
				continue;   // 已被分配：跳过（下轮快照会再评估）
			}
			const FSmartObjectClaimHandle ClaimHandle = SmartObjectManager->TryClaimOrbitSlot(
				OrbitFragment->OrbitName, PlanEntry.TargetSlotIndex, ESmartObjectClaimPriority::Normal);
			if (ClaimHandle.IsValid())
			{
				OrbitFragment->SlotClaimHandle = ClaimHandle;
				UE_LOG(LogTemp, Log, TEXT("[OrbitPlan] Entity[%d] assign return slot %d (%s)"),
					PlanEntry.Entity.Index, PlanEntry.TargetSlotIndex, *OrbitFragment->OrbitName.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[OrbitPlan] Entity[%d] return slot claim failed (slot %d, %s)"),
					PlanEntry.Entity.Index, PlanEntry.TargetSlotIndex, *OrbitFragment->OrbitName.ToString());
			}
			continue;
		}

		// 漂移发单三件套：认领目标槽 → 写单（句柄=B、请求、冻结显示）→ 释放旧槽
		const FSmartObjectClaimHandle OldHandle = OrbitFragment->SlotClaimHandle;
		if (!OldHandle.IsValid())
		{
			continue;   // 可动星必有旧槽；无则异常，跳过
		}
		FVector OldSlotWorld;
		if (!SmartObjectManager->GetOrbitSlotWorldPositionByClaim(OrbitFragment->OrbitName, OldHandle, OldSlotWorld))
		{
			continue;
		}
		const FSmartObjectClaimHandle NewHandle = SmartObjectManager->TryClaimOrbitSlot(
			OrbitFragment->OrbitName, PlanEntry.TargetSlotIndex, ESmartObjectClaimPriority::Normal);
		if (!NewHandle.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrbitPlan] Entity[%d] drift claim failed (slot %d, %s)"),
				PlanEntry.Entity.Index, PlanEntry.TargetSlotIndex, *OrbitFragment->OrbitName.ToString());
			continue;
		}
		OrbitFragment->SlotClaimHandle = NewHandle;
		OrbitFragment->bDriftRequested = true;
		if (!OrbitFragment->bOverrideLocation)
		{
			// 冻结显示在旧槽点（防"句柄切换帧"渲染瞬移；Drift 任务启动后接管覆盖位置）
			OrbitFragment->OverrideLocation = OldSlotWorld;
			OrbitFragment->bOverrideLocation = true;
		}
		SmartObjectManager->ReleaseClaim(OldHandle);
		UE_LOG(LogTemp, Log, TEXT("[OrbitPlan] Entity[%d] drift to slot %d (%s)"),
			PlanEntry.Entity.Index, PlanEntry.TargetSlotIndex, *OrbitFragment->OrbitName.ToString());
	}
}
