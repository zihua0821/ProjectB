// SatelliteManagerComponent.cpp
#include "Mass/SatelliteManagerComponent.h"

#include "Farm/CellManagerComponent.h"    // 格子管理器（共享数据引用）
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeFragments.h"   // FMassStateTreeInstanceFragment / FMassStateTreeSharedFragment
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"   // 轨道槽位注册/认领（智能对象）
#include "Planet/PlanetActor.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"   // ComputeOrbitWorldPosition（轨道几何，Planet 域）
#include "Planet/PlanetConfigDataAsset.h"
#include "DrawDebugHelpers.h"             // 槽位球调试绘制（槽池）
#include "StateTree.h"
#include "Subsystem/GlobalConfigSubsystem.h"   // 配置注入全局入口
#include "Subsystem/ISMManager.h"

namespace
{
	// ---- 轨道全局规划（群体决策）求解辅助 ----

	/**
	 * 槽位等待角（度；"槽还要转多少度才到该悬停点的正下方"）——规划选座用
	 * 将到达（沿运动方向未到）→ 剩余角；已过 ≤ 容差 → 0；已过较多 → 360−Δ（必超上限自然被滤）
	 */
	float ComputeSlotWaitAngleDeg(const FOrbitSnapshot& Snapshot, const FVector& HoverLocation, const int32 SlotIndex, const float SlotLeadToleranceDeg)
	{
		float ThetaHoverDeg = 0.f;
		float RadiusInPlane = 0.f;
		float VerticalOffset = 0.f;
		UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Snapshot.OrbitTransform, HoverLocation, ThetaHoverDeg, RadiusInPlane, VerticalOffset);

		const float SlotPhaseDeg = Snapshot.SlotPhaseBaseDeg + 360.f * static_cast<float>(SlotIndex) / static_cast<float>(Snapshot.SlotCount);
		const float Direction = Snapshot.bClockwise ? -1.f : 1.f;
		const float DeltaDeg = FMath::FindDeltaAngleDegrees(UPlanetBlueprintFunctionLibrary::NormalizeAngleDegrees360(ThetaHoverDeg), UPlanetBlueprintFunctionLibrary::NormalizeAngleDegrees360(SlotPhaseDeg)) * Direction;
		if (DeltaDeg > SlotLeadToleranceDeg)
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
	if (!PlanetActor.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: PlanetActor is null"));
		return;
	}

	// 配置注入全局入口（消费方统一经 GlobalConfigSubsystem 读取；资产空则全默认）
	if (UGlobalConfigSubsystem* GlobalConfig = GetWorld()->GetSubsystem<UGlobalConfigSubsystem>())
	{
		GlobalConfig->SetSatelliteConfig(SatelliteConfig);
	}
	UStaticMesh* Mesh = SatelliteConfig ? SatelliteConfig->SatelliteMesh : nullptr;
	UStateTree* ConfigStateTree = SatelliteConfig ? SatelliteConfig->StateTreeAsset : nullptr;
	const int32 SlotMultiplier = SatelliteConfig ? SatelliteConfig->OrbitSlotMultiplier : 5;

	// 注册卫星网格 ISM 参数（单一事实来源；单桶路径，父级 = 星球根组件）
	if (Mesh)
	{
		if (UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>())
		{
			FISMComponentParams SatelliteParams;
			SatelliteParams.AttachParent = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
			SatelliteParams.bDisableBuckets = true;
			ISMManager->RegisterMeshParams(Mesh, SatelliteParams);
		}
	}

	// 1. 构建运行时轨道数据（默认单轨——参数读卫星配置；空则硬编码兜底）
	BuildRuntimeOrbits();
	if (RuntimeOrbits.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[SatelliteManager] Initialize failed: no valid runtime orbit"));
		return;
	}

	// 2. 先注册轨道槽位（自建槽池；卫星创建时做初始认领），再为全部轨道创建卫星（含渲染实例注册）
	OrbitSlotPool = NewObject<USatelliteSlotPool>(this);
	OrbitSlotPool->RegisterOrbitSlots(RuntimeOrbits, SlotMultiplier);

	// 3. 构建选点扫描缓存（性能治本——格静态表 + 每轨覆盖索引；选点改清单遍历）
	BuildCellScanTable();
	for (const FOrbitDefinition& Orbit : RuntimeOrbits)
	{
		RebuildOrbitCoverageIndices(Orbit);
	}
	CreateAllSatellites();

	if (Mesh == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] SatelliteMesh is not set - satellites will be invisible"));
	}

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Created %d satellites on %d orbit(s) (StateTree: %s, Mesh: %s)"),
		SatelliteEntities.Num(),
		RuntimeOrbits.Num(),
		ConfigStateTree ? *ConfigStateTree->GetName() : TEXT("none"),
		Mesh ? *Mesh->GetName() : TEXT("none"));
}

void USatelliteManagerComponent::BuildRuntimeOrbits()
{
	RuntimeOrbits.Reset();

	// 直接构造【默认初始轨道】（单轨；参数来自卫星配置资产/硬编码兜底——不使用资产轨道定义）
	FOrbitDefinition Data;
	Data.OrbitName = (SatelliteConfig && SatelliteConfig->DefaultOrbitName != NAME_None) ? SatelliteConfig->DefaultOrbitName : FName("Default");
	Data.Transform = FTransform::Identity;
	Data.Transform.SetRotation(FRotator(0.f, 180.f, 0.f).Quaternion());
	Data.Radius = SatelliteConfig ? SatelliteConfig->DefaultOrbitRadius : 55000.f;
	Data.ServiceDistance = SatelliteConfig ? SatelliteConfig->DefaultServiceDistance : 1000.f;
	Data.SatelliteCount = SatelliteConfig ? SatelliteConfig->SatelliteCount : 10;
	Data.AngularVelocity = SatelliteConfig ? SatelliteConfig->DefaultAngularVelocity : 10.f;
	Data.bClockwise = SatelliteConfig ? SatelliteConfig->bDefaultClockwise : false;
	RuntimeOrbits.Add(MoveTemp(Data));
}

void USatelliteManagerComponent::BuildCellScanTable()
{
	CellScanTable.Reset();

	UCellManagerComponent* CellManager = PlanetActor.IsValid() ? PlanetActor->GetCellManagerComponent() : nullptr;
	if (CellManager == nullptr)
	{
		return;
	}

	// 全格一次性建表（格静态数据——性能治本：选点不再每次全扫 GetAllCoords）
	const TArray<FIntPoint> AllCoords = CellManager->GetAllCoords();
	CellScanTable.Reserve(AllCoords.Num());
	for (const FIntPoint& Coord : AllCoords)
	{
		FTransform CellTransform;
		if (CellManager->GetCellWorldTransform(Coord, CellTransform))
		{
			CellScanTable.Add({ Coord, CellTransform.GetLocation() });
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Cell scan table built: %d cells"), CellScanTable.Num());
}

void USatelliteManagerComponent::RebuildOrbitCoverageIndices(const FOrbitDefinition& Orbit)
{
	const UPlanetConfigDataAsset* PlanetConfig = PlanetActor.IsValid() ? PlanetActor->PlanetConfig : nullptr;
	if (PlanetConfig == nullptr || CellScanTable.Num() == 0)
	{
		OrbitCoverageIndices.Remove(Orbit.OrbitName);
		return;
	}

	const FVector OrbitCenter = Orbit.Transform.GetLocation();
	const FVector OrbitNormal = Orbit.Transform.GetRotation().GetAxisZ();

	// 覆盖带预滤（与选点原硬过滤同源公式：格心到轨道平面距离 ≤ 服务半径）
	TArray<int32> Indices;
	Indices.Reserve(CellScanTable.Num() / 8);
	for (int32 EntryIndex = 0; EntryIndex < CellScanTable.Num(); ++EntryIndex)
	{
		const FVector CellCenter = CellScanTable[EntryIndex].Location.GetSafeNormal() * PlanetConfig->Radius;
		if (UPlanetBlueprintFunctionLibrary::IsPointInOrbitCoverage(CellCenter, OrbitCenter, OrbitNormal, Orbit.ServiceDistance))
		{
			Indices.Add(EntryIndex);
		}
	}
	OrbitCoverageIndices.Add(Orbit.OrbitName, MoveTemp(Indices));
}

const TArray<int32>* USatelliteManagerComponent::FindOrbitCoverageIndices(const FName OrbitName) const
{
	return OrbitCoverageIndices.Find(OrbitName);
}

void USatelliteManagerComponent::CreateAllSatellites()
{
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 注：槽位注册在 RebuildOrbitEntities（重建入口）与 Initialize 路径中负责；
	//     卫星创建时仅做初始认领（依赖槽位已注册）

	TArray<FMassEntityHandle> AllEntities;

	for (const FOrbitDefinition& Orbit : RuntimeOrbits)
	{
		// 一轨一桶：逐轨创建 + 按轨注册渲染实例（逻辑组键 = 轨道序号 + 1）
		TArray<FMassEntityHandle> OrbitEntities;
		TArray<FTransform> OrbitTransforms;
		CreateSatellitesForOrbit(Orbit, EntityManager, OrbitEntities, OrbitTransforms);
		RegisterRenderInstances(OrbitEntities, OrbitTransforms, GetOrbitGroupKey(Orbit.OrbitName));
		AllEntities.Append(OrbitEntities);
	}

	SatelliteEntities = MoveTemp(AllEntities);
}

void USatelliteManagerComponent::CreateSatellitesForOrbit(const FOrbitDefinition& Orbit, FMassEntityManager& EntityManager, TArray<FMassEntityHandle>& OutEntities, TArray<FTransform>& OutTransforms)
{
	if (Orbit.SatelliteCount <= 0)
	{
		return;
	}

	// 配置读取（本函数一次取定——资产空则默认）
	UStaticMesh* Mesh = SatelliteConfig ? SatelliteConfig->SatelliteMesh : nullptr;
	const float Scale = SatelliteConfig ? SatelliteConfig->SatelliteScale : 10.0f;
	UStateTree* ConfigStateTree = SatelliteConfig ? SatelliteConfig->StateTreeAsset : nullptr;
	const int32 SlotMultiplier = SatelliteConfig ? SatelliteConfig->OrbitSlotMultiplier : 5;

	// 共享数据（每条轨道一份：轨道几何 + 服务半径 + Mesh + 依赖引用；同值共享）
	FMassSatelliteSharedFragment SatelliteShared;
	SatelliteShared.OrbitTransform = Orbit.Transform;
	SatelliteShared.Radius = Orbit.Radius;
	SatelliteShared.ServiceDistance = Orbit.ServiceDistance;
	SatelliteShared.SatelliteMesh = Mesh;
	SatelliteShared.SatelliteScale = Scale;
	SatelliteShared.PlanetConfig = PlanetActor->PlanetConfig;
	SatelliteShared.CellManager = PlanetActor->GetCellManagerComponent();
	SatelliteShared.SmartObjectManager = PlanetActor->GetSmartObjectManagerComponent();
	SatelliteShared.SatelliteManager = this;
	const FConstSharedStruct SatelliteSharedStruct = EntityManager.GetOrCreateConstSharedFragment(SatelliteShared);

	// 建原型（手写路线，与格子创建模式一致）
	TArray<const UScriptStruct*> FragmentTypes;
	FragmentTypes.Add(FMassSatelliteOrbitFragment::StaticStruct());
	FragmentTypes.Add(FMassSatelliteTaskFragment::StaticStruct());
	FragmentTypes.Add(FSatelliteTag::StaticStruct());
	FragmentTypes.Add(FSatelliteRenderFragment::StaticStruct());
	if (ConfigStateTree)
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
		FMassSatelliteOrbitFragment& OrbitFragment = EntityManager.GetFragmentDataChecked<FMassSatelliteOrbitFragment>(Entities[Index]);
		OrbitFragment.OrbitName = Orbit.OrbitName;
		OrbitFragment.VirtualPhase = 360.0f * static_cast<float>(Index) / static_cast<float>(Entities.Num());   // 均分相位
		OrbitFragment.AngularVelocity = Orbit.AngularVelocity;
		OrbitFragment.bClockwise = Orbit.bClockwise;

		// 初始槽位认领（槽 Index = 卫星序 × 倍数——与槽网格均分相位对齐；槽位由槽池注册）
		if (OrbitSlotPool)
		{
			OrbitFragment.SlotClaimHandle = OrbitSlotPool->TryClaimOrbitSlot(Orbit.OrbitName, Index * SlotMultiplier);
			// 归属槽由句柄唯一标识（查询位置/相位一律走 GetOrbitSlot*ByClaim）
			// 异常可见：同次重建中槽位全新 → 认领必然成功；失败 = 槽注册不完整（"无槽星"信号）
			if (!OrbitFragment.SlotClaimHandle.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] Initial slot claim failed: orbit '%s', star index %d (slot registration incomplete?)"),
					*Orbit.OrbitName.ToString(), Index);
			}
		}

		const FVector WorldLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Orbit.Transform, Orbit.Radius, OrbitFragment.VirtualPhase);
		OutTransforms.Add(FTransform(FRotator::ZeroRotator, WorldLocation, FVector(Scale)));
	}

	// 挂共享数据（轨道共享 + StateTree 共享；实例分配与激活由引擎 Processor 自动完成）
	for (const FMassEntityHandle& Entity : Entities)
	{
		EntityManager.AddConstSharedFragmentToEntity(Entity, SatelliteSharedStruct);
	}
	if (ConfigStateTree)
	{
		FMassStateTreeSharedFragment SharedStateTree;
		SharedStateTree.StateTree = ConfigStateTree;
		const FConstSharedStruct StateTreeFragment = EntityManager.GetOrCreateConstSharedFragment(SharedStateTree);
		for (const FMassEntityHandle& Entity : Entities)
		{
			EntityManager.AddConstSharedFragmentToEntity(Entity, StateTreeFragment);
		}
	}

	OutEntities.Append(Entities);
}

int32 USatelliteManagerComponent::GetOrbitGroupKey(const FName OrbitName) const
{
	// 逻辑组键 = 轨道在 RuntimeOrbits 中的序号 + 1（0 保留给默认组）
	const int32 OrbitIndex = RuntimeOrbits.IndexOfByPredicate(
		[OrbitName](const FOrbitDefinition& Data) { return Data.OrbitName == OrbitName; });
	return (OrbitIndex >= 0) ? (OrbitIndex + 1) : 0;
}

void USatelliteManagerComponent::RegisterRenderInstances(const TArray<FMassEntityHandle>& Entities, const TArray<FTransform>& Transforms, const int32 GroupKey)
{
	UStaticMesh* Mesh = SatelliteConfig ? SatelliteConfig->SatelliteMesh : nullptr;
	if (Mesh == nullptr || Entities.Num() == 0)
	{
		return;
	}

	UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>();
	if (ISMManager == nullptr)
	{
		return;
	}

	// 双维度分桶参数：空间分桶依据传空（不启用）；逻辑分桶依据 = 轨道逻辑组键——
	// 每组独立 ISM 组件（一轨一桶）；注册参数自默认组 (Mesh, 0) 种子继承（含 bDisableBuckets）
	FISMComponentParams ISMParams;

	const TArray<FGlobalInstanceId> InstanceIds = ISMManager->BatchAddInstances(
		Mesh,
		Transforms,
		TArray<FIntPoint>(),   // 空间分桶依据：空 → 不启用
		GroupKey,              // 逻辑分桶依据：轨道序号 + 1
		0,                     // 无 CustomData
		ISMParams,
		/*bWorldSpace=*/true,
		TArray<float>());      // 无自定义数据

	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	for (int32 Index = 0; Index < Entities.Num() && Index < InstanceIds.Num(); ++Index)
	{
		EntityManager.GetFragmentDataChecked<FSatelliteRenderFragment>(Entities[Index]).InstanceId = InstanceIds[Index];
	}

	// 一轨一桶验证日志（每轨每次注册一条，低频）：ISM 逻辑组键 = 轨道序号 + 1——
	// 多轨场景下应看到 group 逐轨递增且各自独立注册
	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Rendered %d instances -> ISM group %d"), InstanceIds.Num(), GroupKey);
}

void USatelliteManagerComponent::DestroySatellitesForOrbit(const FName OrbitName)
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

	// 配置读取（资产空则默认）
	UStaticMesh* Mesh = SatelliteConfig ? SatelliteConfig->SatelliteMesh : nullptr;

	// 筛选：目标轨 → 待毁；其余 → 保留（其他轨道零触碰——星、槽、句柄全部原样）
	TArray<FMassEntityHandle> ToDestroy;
	TArray<FMassEntityHandle> ToKeep;
	ToDestroy.Reserve(SatelliteEntities.Num());
	ToKeep.Reserve(SatelliteEntities.Num());
	for (const FMassEntityHandle& Entity : SatelliteEntities)
	{
		const FMassSatelliteOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteOrbitFragment>(Entity);
		if (OrbitFragment && OrbitFragment->OrbitName == OrbitName)
		{
			ToDestroy.Add(Entity);
		}
		else
		{
			ToKeep.Add(Entity);
		}
	}

	// 1. 归还一切认领（认领是"借"，销毁前必"还"）：
	//    · 格子认领：Task 单句柄（经 SmartObjectManager）
	//    · 轨道槽认领：SlotClaimHandle（自建槽池）
	//    保证重建后"账本 / 槽池"两处无残留（否则格子永久不可用、槽位幽灵占用）
	if (ToDestroy.Num() > 0)
	{
		USmartObjectManagerComponent* SmartObjectManager = PlanetActor.IsValid() ? PlanetActor->GetSmartObjectManagerComponent() : nullptr;
		for (const FMassEntityHandle& Entity : ToDestroy)
		{
			if (SmartObjectManager)
			{
				if (const FMassSatelliteTaskFragment* TaskFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteTaskFragment>(Entity))
				{
					if (TaskFragment->CellClaimHandle.IsValid())
					{
						SmartObjectManager->ReleaseClaim(TaskFragment->CellClaimHandle);
					}
				}
			}
			if (OrbitSlotPool)
			{
				if (const FMassSatelliteOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteOrbitFragment>(Entity))
				{
					if (OrbitFragment->SlotClaimHandle.IsValid())
					{
						OrbitSlotPool->ReleaseOrbitSlot(OrbitFragment->SlotClaimHandle);
					}
				}
			}
		}
	}

	// 2. 移除渲染实例（收集稳定句柄 → 批量移除）
	if (ToDestroy.Num() > 0 && Mesh)
	{
		if (UISMManager* ISMManager = World->GetSubsystem<UISMManager>())
		{
			TArray<FGlobalInstanceId> InstanceIds;
			InstanceIds.Reserve(ToDestroy.Num());
			for (const FMassEntityHandle& Entity : ToDestroy)
			{
				if (const FSatelliteRenderFragment* RenderFragment = EntityManager.GetFragmentDataPtr<FSatelliteRenderFragment>(Entity))
				{
					InstanceIds.Add(RenderFragment->InstanceId);
				}
			}
			if (InstanceIds.Num() > 0)
			{
				ISMManager->BatchRemoveInstances(Mesh, InstanceIds);
			}
		}
	}

	// 3. 销毁实体（保留其余）
	if (ToDestroy.Num() > 0)
	{
		EntityManager.BatchDestroyEntities(ToDestroy);
	}
	SatelliteEntities = MoveTemp(ToKeep);
}

void USatelliteManagerComponent::RebuildOrbitEntities(const FName OrbitName)
{
	if (!PlanetActor.IsValid())
	{
		return;
	}
	const FOrbitDefinition* OrbitData = RuntimeOrbits.FindByPredicate(
		[OrbitName](const FOrbitDefinition& Data) { return Data.OrbitName == OrbitName; });
	if (OrbitData == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] RebuildOrbitEntities: orbit '%s' not found"), *OrbitName.ToString());
		return;
	}

	// ① 销毁该轨旧卫星（含"归还一切认领"——先撤人再拆设施：归还发生在旧槽实例仍存活时，释放真实生效）
	DestroySatellitesForOrbit(OrbitName);

	// ② 重建该轨槽位（清空 + 代际 ++ + 重建数组——供初始认领；其他轨道零触碰）
	if (OrbitSlotPool == nullptr)
	{
		return;
	}
	OrbitSlotPool->RegisterSingleOrbitSlots(*OrbitData, SatelliteConfig ? SatelliteConfig->OrbitSlotMultiplier : 5);

	// 选点覆盖索引（性能治本——与槽/星重建同步重算；O(总格数) 一次性，暂停态执行）
	RebuildOrbitCoverageIndices(*OrbitData);

	// ③ 重建该轨卫星（初始认领新槽）+ 渲染实例 + 入列
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	TArray<FMassEntityHandle> NewEntities;
	TArray<FTransform> NewTransforms;
	CreateSatellitesForOrbit(*OrbitData, EntityManager, NewEntities, NewTransforms);
	RegisterRenderInstances(NewEntities, NewTransforms, GetOrbitGroupKey(OrbitName));
	SatelliteEntities.Append(NewEntities);

	UE_LOG(LogTemp, Log, TEXT("[SatelliteManager] Orbit '%s' rebuilt: %d satellites (total %d)"),
		*OrbitName.ToString(), NewEntities.Num(), SatelliteEntities.Num());
}

void USatelliteManagerComponent::ResetSatelliteStateTreeTimeBase()
{
	UWorld* World = GetWorld();
	if (!World || SatelliteEntities.Num() == 0)
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	const double NowSeconds = World->GetTimeSeconds();

	// StateTree 任务 dt = 当前墙钟 − LastUpdateTimeInSeconds（引擎的时间补偿设计）：
	// 选定暂停期间心跳停发（无 tick），若不重置，恢复后第一帧的 dt = 整个暂停窗口
	// → 任务 PhaseElapsed 跃增 → Alpha 越过 1 → "恢复即瞬移到目标"。重置基准即可抹掉暂停窗口。
	for (const FMassEntityHandle& Entity : SatelliteEntities)
	{
		if (FMassStateTreeInstanceFragment* StateTreeFragment = EntityManager.GetFragmentDataPtr<FMassStateTreeInstanceFragment>(Entity))
		{
			StateTreeFragment->LastUpdateTimeInSeconds = NowSeconds;
		}
	}
}

bool USatelliteManagerComponent::UpdateOrbitData(const FName OrbitName, const FOrbitDefinition& NewData)
{
	for (FOrbitDefinition& Data : RuntimeOrbits)
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

bool USatelliteManagerComponent::AddOrbit(const FOrbitDefinition& NewOrbit)
{
	if (NewOrbit.OrbitName == NAME_None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] AddOrbit failed: OrbitName is None"));
		return false;
	}
	for (const FOrbitDefinition& Data : RuntimeOrbits)
	{
		if (Data.OrbitName == NewOrbit.OrbitName)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SatelliteManager] AddOrbit failed: orbit '%s' already exists"), *NewOrbit.OrbitName.ToString());
			return false;
		}
	}

	// 兜底：服务距离未设置时取默认（半格高）——保证新轨道有有效覆盖（如删空后重建）
	FOrbitDefinition SanitizedOrbit = NewOrbit;
	if (SanitizedOrbit.ServiceDistance <= 0.f)
	{
		SanitizedOrbit.ServiceDistance = SatelliteConfig ? SatelliteConfig->DefaultServiceDistance : 1000.f;
	}

	RuntimeOrbits.Add(SanitizedOrbit);
	RebuildOrbitEntities(SanitizedOrbit.OrbitName);   // 只建新轨实体（槽 + 星）；其他轨道零触碰
	OnRuntimeOrbitChanged.Broadcast(NewOrbit.OrbitName);
	return true;
}

void USatelliteManagerComponent::RemoveOrbit(const FName OrbitName)
{
	const int32 RemovedCount = RuntimeOrbits.RemoveAll(
		[OrbitName](const FOrbitDefinition& Data) { return Data.OrbitName == OrbitName; });

	if (RemovedCount == 0)
	{
		return;
	}

	// 只销毁该轨：卫星（含归还认领）+ 槽位；其他轨道零触碰
	DestroySatellitesForOrbit(OrbitName);
	if (OrbitSlotPool)
	{
		OrbitSlotPool->UnregisterOrbitSlots(OrbitName);
	}
	OrbitCoverageIndices.Remove(OrbitName);   // 选点覆盖索引同步清除
	OnRuntimeOrbitChanged.Broadcast(OrbitName);
}

void USatelliteManagerComponent::TickOrbitPlanning()
{
	if (SatelliteEntities.Num() == 0)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastOrbitPlanTime < (SatelliteConfig ? SatelliteConfig->OrbitPlanIntervalSeconds : 1.0f))
	{
		return;
	}
	LastOrbitPlanTime = Now;

	// 滚动节拍：抄账 → 求解（每轨独立）→ 发单（不等执行完成；在途按目标槽计入快照）
	TArray<FOrbitSnapshot> Snapshots;
	CollectOrbitSnapshots(Snapshots);

	// 求解参数（配置 → 纯函数输入；资产空则默认）
	FOrbitPlanParams PlanParams;
	PlanParams.WaitAngleLimitDeg = SatelliteConfig ? SatelliteConfig->OrbitPlanWaitAngleLimitDeg : 45.0f;
	PlanParams.RebalanceRatio = SatelliteConfig ? SatelliteConfig->OrbitPlanRebalanceRatio : 0.6f;
	PlanParams.MoveMinDeg = SatelliteConfig ? SatelliteConfig->PlanMoveMinDeg : 3.0f;
	PlanParams.SlotLeadToleranceDeg = SatelliteConfig ? SatelliteConfig->PlanSlotLeadToleranceDeg : 2.0f;
	// 阶段②（均匀化）独立节流：5s 一窗（落座阶段①仍按主节拍滚动）
	PlanParams.bSkipRebalance = (Now - LastRebalanceTime) < (SatelliteConfig ? SatelliteConfig->OrbitRebalanceIntervalSeconds : 5.0f);
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
	if (!World || !PlanetActor.IsValid())
	{
		return;
	}
	FMassEntityManager& EntityManager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 按轨道分组；同一帧内一口气读完（"同一张照片"）
	TMap<FName, int32> OrbitNameToSnapshotIndex;
	for (int32 SatelliteIndex = 0; SatelliteIndex < SatelliteEntities.Num(); ++SatelliteIndex)
	{
		const FMassEntityHandle& Entity = SatelliteEntities[SatelliteIndex];
		const FMassSatelliteOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteOrbitFragment>(Entity);
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
			if (const FOrbitDefinition* OrbitData = RuntimeOrbits.FindByPredicate(
				[OrbitFragment](const FOrbitDefinition& Data) { return Data.OrbitName == OrbitFragment->OrbitName; }))
			{
				Snapshot.OrbitTransform = OrbitData->Transform;
			}
			Snapshot.AngularVelocity = OrbitFragment->AngularVelocity;
			Snapshot.bClockwise = OrbitFragment->bClockwise;
			if (OrbitSlotPool)
			{
				Snapshot.SlotCount = OrbitSlotPool->GetOrbitSlotCount(OrbitFragment->OrbitName);
				OrbitSlotPool->GetOrbitSlotPhase(OrbitFragment->OrbitName, 0, Snapshot.SlotPhaseBaseDeg);
			}
			SnapshotIndex = OutSnapshots.Add(MoveTemp(Snapshot));
		}

		// 占位 = 槽句柄所指的槽（在途/返航星=目标槽——"在途当已到"）
		FOrbitSnapshotEntry Entry;
		Entry.Entity = Entity;
		Entry.Ordinal = SatelliteIndex;
		Entry.bOccupiesSlot = OrbitFragment->SlotClaimHandle.IsValid();
		if (OrbitSlotPool && Entry.bOccupiesSlot)
		{
			Entry.SlotIndex = OrbitSlotPool->ResolveSlotIndexByClaim(OrbitFragment->SlotClaimHandle);
			Entry.bOccupiesSlot = Entry.SlotIndex != INDEX_NONE;   // 句柄有效但反查失败：视为不占位（防御）
		}
		Entry.bAwaitingReturn = (OrbitFragment->State == ESatelliteState::ReturnWait) && !Entry.bOccupiesSlot;
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

	// 落座求解：候选 = 空槽中"等待角 ≤ 上限"者；
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
			const float WaitAngleDeg = ComputeSlotWaitAngleDeg(Snapshot, Star->HoverLocation, SlotIndex, Params.SlotLeadToleranceDeg);
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

		// 弧段（与 Drift 任务同构：总相位扫过量 = （ω·T + Δs）× 方向——把"沿运动方向量"统一换算为相位增量；
		// 原式 ω·T·dir + Δs 在顺时针时位移项符号反——2026-10-04 修复）
		// T = clamp(|Δs| / 滑动速率上限, 2, 6)——后移速率上限 0.75ω（保底绝对速度 0.25ω，不停住），前移 1ω
		const float ArcAngularSpeed = FMath::Max(Snapshot.AngularVelocity, 1.f);
		const float SlideRateDegPerSec = (DeltaMoveDeg < 0.f) ? (ArcAngularSpeed * 0.75f) : ArcAngularSpeed;
		const float Duration = FMath::Clamp(FMath::Abs(DeltaMoveDeg) / SlideRateDegPerSec, 2.0f, 6.0f);
		const float TotalArcDeg = (Snapshot.AngularVelocity * Duration + DeltaMoveDeg) * Direction;

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
	if (!World || !OrbitSlotPool)
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
		FMassSatelliteOrbitFragment* OrbitFragment = EntityManager.GetFragmentDataPtr<FMassSatelliteOrbitFragment>(PlanEntry.Entity);
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
			const FOrbitSlotHandle ClaimHandle = OrbitSlotPool->TryClaimOrbitSlot(
				OrbitFragment->OrbitName, PlanEntry.TargetSlotIndex);
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
		const FOrbitSlotHandle OldHandle = OrbitFragment->SlotClaimHandle;
		if (!OldHandle.IsValid())
		{
			continue;   // 可动星必有旧槽；无则异常，跳过
		}
		FVector OldSlotWorld;
		if (!OrbitSlotPool->GetOrbitSlotWorldPositionByClaim(OldHandle, OldSlotWorld))
		{
			continue;
		}
		const FOrbitSlotHandle NewHandle = OrbitSlotPool->TryClaimOrbitSlot(
			OrbitFragment->OrbitName, PlanEntry.TargetSlotIndex);
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
		OrbitSlotPool->ReleaseOrbitSlot(OldHandle);
		UE_LOG(LogTemp, Log, TEXT("[OrbitPlan] Entity[%d] drift to slot %d (%s)"),
			PlanEntry.Entity.Index, PlanEntry.TargetSlotIndex, *OrbitFragment->OrbitName.ToString());
	}
}

// ---- 配置 getter 实现（读卫星配置；资产空则内置默认） ----

const FSatelliteScoringWeights& USatelliteManagerComponent::GetScoringWeights() const
{
	static const FSatelliteScoringWeights DefaultWeights;
	return SatelliteConfig ? SatelliteConfig->ScoringWeights : DefaultWeights;
}

// ---- 轨道槽池转发（对外接口——内部方法由 Manager 直调槽池） ----

void USatelliteManagerComponent::UpdateOrbitSlots(const float DeltaSeconds)
{
	if (OrbitSlotPool)
	{
		OrbitSlotPool->UpdateOrbitSlots(DeltaSeconds);
	}
}

void USatelliteManagerComponent::ReleaseOrbitSlot(const FOrbitSlotHandle& Handle)
{
	if (OrbitSlotPool)
	{
		OrbitSlotPool->ReleaseOrbitSlot(Handle);
	}
}

bool USatelliteManagerComponent::GetOrbitSlotWorldPositionByClaim(const FOrbitSlotHandle& ClaimHandle, FVector& OutWorldPosition) const
{
	return OrbitSlotPool && OrbitSlotPool->GetOrbitSlotWorldPositionByClaim(ClaimHandle, OutWorldPosition);
}

bool USatelliteManagerComponent::GetOrbitSlotPhaseByClaim(const FOrbitSlotHandle& ClaimHandle, float& OutPhaseDeg) const
{
	return OrbitSlotPool && OrbitSlotPool->GetOrbitSlotPhaseByClaim(ClaimHandle, OutPhaseDeg);
}

void USatelliteManagerComponent::SetOrbitSlotMarkersVisible(const FName OrbitName, const bool bVisible)
{
	if (OrbitSlotPool)
	{
		OrbitSlotPool->SetOrbitSlotMarkersVisible(OrbitName, bVisible);
	}
}

bool USatelliteManagerComponent::IsOrbitSlotMarkersVisible(const FName OrbitName) const
{
	return OrbitSlotPool && OrbitSlotPool->IsOrbitSlotMarkersVisible(OrbitName);
}

void USatelliteManagerComponent::SetOrbitTrajectoryVisible(const FName OrbitName, const bool bVisible)
{
	if (OrbitSlotPool)
	{
		OrbitSlotPool->SetOrbitTrajectoryVisible(OrbitName, bVisible);
	}
}

bool USatelliteManagerComponent::IsOrbitTrajectoryVisible(const FName OrbitName) const
{
	return OrbitSlotPool && OrbitSlotPool->IsOrbitTrajectoryVisible(OrbitName);
}

// ---- USatelliteSlotPool（自建槽池实现） ----

void USatelliteSlotPool::RegisterOrbitSlots(const TArray<FOrbitDefinition>& Orbits, const int32 SlotMultiplier)
{
	for (const FOrbitDefinition& Orbit : Orbits)
	{
		RegisterSingleOrbitSlots(Orbit, SlotMultiplier);
	}

	UE_LOG(LogTemp, Log, TEXT("[SatelliteSlotPool] Orbit slots registered: %d orbit(s)"), OrbitTracks.Num());
}

void USatelliteSlotPool::RegisterSingleOrbitSlots(const FOrbitDefinition& Orbit, const int32 SlotMultiplier)
{
	// 重建该轨 = 清空旧数据 + 代际 ++ + 重建数组（O(1)——替代逐个销毁/创建智能对象实例）
	FOrbitSlotTrack& Track = OrbitTracks.FindOrAdd(Orbit.OrbitName);
	Track.Generation = FMath::Max(1u, Track.Generation + 1u);   // 代际 ++（0 保留为无效哨兵——旧句柄全部失效）
	Track.OrbitTransform = Orbit.Transform;
	Track.OrbitRadius = Orbit.Radius;
	Track.AngularVelocity = Orbit.AngularVelocity;
	Track.bClockwise = Orbit.bClockwise;
	Track.CurrentPhaseDeg = 0.f;   // 槽 0 起始相位（星 i 认领槽 i×K → 相位恰好均分）

	const int32 SafeMultiplier = FMath::Max(1, SlotMultiplier);
	const int32 SlotCount = FMath::Max(0, Orbit.SatelliteCount) * SafeMultiplier;
	Track.bClaimed.Init(false, SlotCount);
	Track.SlotWorldPositions.SetNum(SlotCount);

	// 初始槽位世界位置（均分相位固定点——与卫星初始相位分配一致；此后随推进更新）
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const float SlotPhaseDeg = 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount);
		Track.SlotWorldPositions[SlotIndex] = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
			Track.OrbitTransform, Track.OrbitRadius, SlotPhaseDeg);
	}
}

void USatelliteSlotPool::UnregisterOrbitSlots(const FName OrbitName)
{
	OrbitTracks.Remove(OrbitName);
}

void USatelliteSlotPool::UpdateOrbitSlots(const float DeltaSeconds)
{
	for (TPair<FName, FOrbitSlotTrack>& Pair : OrbitTracks)
	{
		FOrbitSlotTrack& Track = Pair.Value;
		const int32 SlotCount = Track.SlotWorldPositions.Num();
		if (SlotCount == 0)
		{
			continue;
		}

		// 相位推进（与卫星同速：槽位随轨道旋转）
		Track.CurrentPhaseDeg += (Track.bClockwise ? -1.0f : 1.0f) * Track.AngularVelocity * DeltaSeconds;
		Track.CurrentPhaseDeg = FMath::Fmod(Track.CurrentPhaseDeg, 360.0f);
		if (Track.CurrentPhaseDeg < 0.0f)
		{
			Track.CurrentPhaseDeg += 360.0f;
		}

		// 同步槽位世界位置（查询/渲染/判据同源数据——自建后"引擎位置撒谎"议题消除）
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			const float SlotPhaseDeg = Track.CurrentPhaseDeg + 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount);
			Track.SlotWorldPositions[SlotIndex] = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
				Track.OrbitTransform, Track.OrbitRadius, SlotPhaseDeg);
		}
	}

	// 调试绘制（数据推进与绘制拆分——CVar 门控，默认关）
	DrawOrbitSlotMarkers(GetWorld());
}

void USatelliteSlotPool::DrawOrbitSlotMarkers(UWorld* World) const
{
	if (!World)
	{
		return;
	}

	for (const TPair<FName, FOrbitSlotTrack>& Pair : OrbitTracks)
	{
		if (!Pair.Value.bDebugDrawSlotMarkers)
		{
			continue;   // 该轨未开启（编辑面板勾选）
		}
		for (const FVector& SlotWorldPosition : Pair.Value.SlotWorldPositions)
		{
			DrawDebugSphere(World, SlotWorldPosition, 60.f, 8, FColor::Cyan,
				/*bPersistent=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/0.f);
		}
	}
}

void USatelliteSlotPool::SetOrbitSlotMarkersVisible(const FName OrbitName, const bool bVisible)
{
	if (FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName))
	{
		Track->bDebugDrawSlotMarkers = bVisible;
	}
}

void USatelliteSlotPool::SetOrbitTrajectoryVisible(const FName OrbitName, const bool bVisible)
{
	if (FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName))
	{
		Track->bDebugDrawTrajectory = bVisible;
	}
}

bool USatelliteSlotPool::IsOrbitSlotMarkersVisible(const FName OrbitName) const
{
	const FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName);
	return Track && Track->bDebugDrawSlotMarkers;
}

bool USatelliteSlotPool::IsOrbitTrajectoryVisible(const FName OrbitName) const
{
	const FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName);
	return Track && Track->bDebugDrawTrajectory;
}

int32 USatelliteSlotPool::GetOrbitSlotCount(const FName OrbitName) const
{
	const FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName);
	return Track ? Track->SlotWorldPositions.Num() : 0;
}

bool USatelliteSlotPool::GetOrbitSlotPhase(const FName OrbitName, const int32 SlotIndex, float& OutPhaseDeg) const
{
	const FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName);
	if (!Track || SlotIndex < 0 || SlotIndex >= Track->SlotWorldPositions.Num())
	{
		return false;
	}
	OutPhaseDeg = Track->CurrentPhaseDeg + 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(Track->SlotWorldPositions.Num());
	return true;
}

FOrbitSlotHandle USatelliteSlotPool::TryClaimOrbitSlot(const FName OrbitName, const int32 SlotIndex)
{
	FOrbitSlotTrack* Track = OrbitTracks.Find(OrbitName);
	if (!Track || !Track->bClaimed.IsValidIndex(SlotIndex) || Track->bClaimed[SlotIndex])
	{
		return FOrbitSlotHandle();   // 未注册 / 越界 / 已被占用（默认构造 = 无效哨兵）
	}
	Track->bClaimed[SlotIndex] = true;

	FOrbitSlotHandle Handle;
	Handle.OrbitName = OrbitName;
	Handle.SlotIndex = SlotIndex;
	Handle.Generation = Track->Generation;   // 快照代际（重建后比对失效——防幽灵认领）
	return Handle;
}

void USatelliteSlotPool::ReleaseOrbitSlot(const FOrbitSlotHandle& Handle)
{
	FOrbitSlotTrack* Track = OrbitTracks.Find(Handle.OrbitName);
	if (!Track || !Handle.IsValid() || Handle.Generation != Track->Generation || !Track->bClaimed.IsValidIndex(Handle.SlotIndex))
	{
		return;   // 过期/无效句柄：静默忽略（幽灵防护）
	}
	Track->bClaimed[Handle.SlotIndex] = false;
}

bool USatelliteSlotPool::GetOrbitSlotWorldPositionByClaim(const FOrbitSlotHandle& ClaimHandle, FVector& OutWorldPosition) const
{
	const int32 SlotIndex = ResolveSlotIndexByClaim(ClaimHandle);
	const FOrbitSlotTrack* Track = OrbitTracks.Find(ClaimHandle.OrbitName);
	if (!Track || !Track->SlotWorldPositions.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutWorldPosition = Track->SlotWorldPositions[SlotIndex];
	return true;
}

bool USatelliteSlotPool::GetOrbitSlotPhaseByClaim(const FOrbitSlotHandle& ClaimHandle, float& OutPhaseDeg) const
{
	const int32 SlotIndex = ResolveSlotIndexByClaim(ClaimHandle);
	return GetOrbitSlotPhase(ClaimHandle.OrbitName, SlotIndex, OutPhaseDeg);
}

int32 USatelliteSlotPool::ResolveSlotIndexByClaim(const FOrbitSlotHandle& ClaimHandle) const
{
	const FOrbitSlotTrack* Track = OrbitTracks.Find(ClaimHandle.OrbitName);
	if (!Track || !ClaimHandle.IsValid() || ClaimHandle.Generation != Track->Generation)
	{
		return INDEX_NONE;   // 无效/过期句柄
	}
	return Track->bClaimed.IsValidIndex(ClaimHandle.SlotIndex) ? ClaimHandle.SlotIndex : INDEX_NONE;
}
