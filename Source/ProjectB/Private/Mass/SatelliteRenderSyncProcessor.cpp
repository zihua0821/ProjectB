// SatelliteRenderSyncProcessor.cpp
#include "Mass/SatelliteRenderSyncProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"                // 槽位世界位置（卫星轨道锚点）
#include "Planet/PlanetBlueprintFunctionLibrary.h"   // ComputeOrbitWorldPosition（轨道几何，Planet 域）
#include "Subsystem/ISMManager.h"

USatelliteRenderSyncProcessor::USatelliteRenderSyncProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段（PrePhysics / DuringPhysics 等自动调用，与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// ISM 增删改仅限游戏线程（见 ISMManager 线程模型约定）——GT 外框（UObject 访问收敛至
	// 预快照/批提交两段）+ 内部 chunk 并行（ParallelForEachEntityChunk，见 Execute）
	bRequiresGameThreadExecution = true;
	// 排在本帧相位推进之后（SatelliteMovement），保证渲染使用最新相位与同帧槽缓存
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteRender");
	ExecutionOrder.ExecuteAfter.Add(TEXT("SatelliteMovement"));
}

void USatelliteRenderSyncProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassSatelliteOrbitFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FSatelliteRenderFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();
}

void USatelliteRenderSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// ---- ① GT 预快照：槽世界位置（唯一 UObject 访问段——收敛至并行区之外）----
	// 槽池数据属 SmartObjectManager（UObject）——不能进并行区；先串行取快照
	// （按实体句柄索引，无遍历顺序依赖），并行段只读纯数据
	TMap<FMassEntityHandle, FVector> SlotWorldSnapshot;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&SlotWorldSnapshot](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassSatelliteOrbitFragment> OrbitFragments = ChunkContext.GetFragmentView<FMassSatelliteOrbitFragment>();
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		if (!Shared.SmartObjectManager)
		{
			return;
		}
		const int32 NumEntities = ChunkContext.GetNumEntities();
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			const FMassSatelliteOrbitFragment& Orbit = OrbitFragments[Index];
			if (Orbit.bOverrideLocation)
			{
				continue;   // 覆盖位置优先，无需槽查询
			}
			FVector SlotWorld;
			if (Shared.SmartObjectManager->GetOrbitSlotWorldPositionByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, SlotWorld))
			{
				SlotWorldSnapshot.Add(ChunkContext.GetEntity(Index), SlotWorld);
			}
		}
	});

	// ---- ② 并行：位置计算 + 收集（局部缓冲 + 锁合并——样板：CropVisualSyncProcessor）----
	TArray<FGlobalInstanceId> InstanceIds;
	TArray<FTransform> NewTransforms;
	UStaticMesh* SatelliteMesh = nullptr;
	FCriticalSection MergeLock;

	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [&InstanceIds, &NewTransforms, &SatelliteMesh, &SlotWorldSnapshot, &MergeLock](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassSatelliteOrbitFragment> OrbitFragments = ChunkContext.GetFragmentView<FMassSatelliteOrbitFragment>();
		const TArrayView<FSatelliteRenderFragment> RenderFragments = ChunkContext.GetMutableFragmentView<FSatelliteRenderFragment>();
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		// 本 chunk 局部缓冲（worker 并行——无竞争）
		TArray<FGlobalInstanceId> LocalInstanceIds;
		TArray<FTransform> LocalTransforms;
		LocalInstanceIds.Reserve(NumEntities);
		LocalTransforms.Reserve(NumEntities);

		const FVector ScaleVector(Shared.SatelliteScale);
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			if (!RenderFragments[Index].InstanceId.IsValid())
			{
				continue;
			}
		
			// 位置来源（优先级）：① 离轨/悬停的覆盖位置；② 【归属槽】世界位置（预快照——纯数据查找）；③ 按相位现算（无槽回退）
			const FMassSatelliteOrbitFragment& Orbit = OrbitFragments[Index];
			FVector WorldLocation = Orbit.OverrideLocation;
			if (!Orbit.bOverrideLocation)
			{
				WorldLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.VirtualPhase);
				if (const FVector* SlotWorld = SlotWorldSnapshot.Find(ChunkContext.GetEntity(Index)))
				{
					WorldLocation = *SlotWorld;
				}
			}
			LocalInstanceIds.Add(RenderFragments[Index].InstanceId);
			LocalTransforms.Add(FTransform(FRotator::ZeroRotator, WorldLocation, ScaleVector));
		}

		// 合并到共享缓冲（跨 worker 并发写——锁保护；配对不破：Ids[i] 与 Transforms[i] 同实体）
		FScopeLock Lock(&MergeLock);
		if (SatelliteMesh == nullptr)
		{
			SatelliteMesh = Shared.SatelliteMesh;
		}
		InstanceIds.Append(LocalInstanceIds);
		NewTransforms.Append(LocalTransforms);
	});

	if (InstanceIds.Num() == 0 || SatelliteMesh == nullptr)
	{
		return;
	}

	if (UISMManager* ISMManager = EntityManager.GetWorld()->GetSubsystem<UISMManager>())
	{
		ISMManager->BatchUpdateInstancesTransforms(SatelliteMesh, InstanceIds, NewTransforms, /*bWorldSpace=*/true, /*bTeleport=*/true);
	}
}
