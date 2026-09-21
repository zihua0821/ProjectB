// SatelliteRenderSyncProcessor.cpp
#include "Mass/SatelliteRenderSyncProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Planet/PlanetBlueprintFunctionLibrary.h"   // ComputeOrbitWorldPosition（轨道几何，Planet 域）
#include "Subsystem/ISMManager.h"

USatelliteRenderSyncProcessor::USatelliteRenderSyncProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段（PrePhysics / DuringPhysics 等自动调用，与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// ISM 增删改仅限游戏线程（见 ISMManager 线程模型约定）
	bRequiresGameThreadExecution = true;
	// 排在本帧移动推进之后，保证渲染使用最新角度
	ExecutionOrder.ExecuteAfter.Add(TEXT("SatelliteOrbitMovement"));
}

void USatelliteRenderSyncProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassOrbitFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FSatelliteRenderFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();
}

void USatelliteRenderSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 收集本帧所有卫星的最新变换，末尾一次批量提交（引擎帧末聚合脏实例）
	TArray<FGlobalInstanceId> InstanceIds;
	TArray<FTransform> NewTransforms;
	UStaticMesh* SatelliteMesh = nullptr;

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&InstanceIds, &NewTransforms, &SatelliteMesh](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FMassOrbitFragment> OrbitFragments = ChunkContext.GetFragmentView<FMassOrbitFragment>();
		const TArrayView<FSatelliteRenderFragment> RenderFragments = ChunkContext.GetMutableFragmentView<FSatelliteRenderFragment>();
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		if (SatelliteMesh == nullptr)
		{
			SatelliteMesh = Shared.SatelliteMesh;
		}

		const FVector ScaleVector(Shared.SatelliteScale);
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			if (!RenderFragments[Index].InstanceId.IsValid())
			{
				continue;
			}

			// 角度 → 世界位置（与注册时同一公式）
			const FVector WorldLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, OrbitFragments[Index].CurrentAngle);
			InstanceIds.Add(RenderFragments[Index].InstanceId);
			NewTransforms.Add(FTransform(FRotator::ZeroRotator, WorldLocation, ScaleVector));
		}
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
