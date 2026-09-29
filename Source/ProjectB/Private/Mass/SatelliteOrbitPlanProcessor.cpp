// SatelliteOrbitPlanProcessor.cpp
#include "Mass/SatelliteOrbitPlanProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SatelliteManagerComponent.h"

USatelliteOrbitPlanProcessor::USatelliteOrbitPlanProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段自动调用（与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 帧序：规划（抄账/发单）→ 槽推进 → 卫星推进 → 渲染同步——顺序显式固定
	ExecutionOrder.ExecuteBefore.Add(TEXT("SatelliteOrbitMovement"));
}

void USatelliteOrbitPlanProcessor::ConfigureQueries()
{
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();   // 取 SatelliteManager（驱动规划节拍）
}

void USatelliteOrbitPlanProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 规划为全局行为：仅首个 chunk 触发一次（1s 节流在 TickOrbitPlanning 内部）
	bool bPlanned = false;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&bPlanned](FMassExecutionContext& ChunkContext)
	{
		if (bPlanned)
		{
			return;
		}
		bPlanned = true;
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		if (Shared.SatelliteManager)
		{
			Shared.SatelliteManager->TickOrbitPlanning();
		}
	});
}
