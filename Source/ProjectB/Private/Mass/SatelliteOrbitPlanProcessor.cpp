// SatelliteOrbitPlanProcessor.cpp
#include "Mass/SatelliteOrbitPlanProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SatelliteManagerComponent.h"
#include "Subsystem/SimulationControlSubsystem.h"   // 选定暂停（白名单挂载）

USatelliteOrbitPlanProcessor::USatelliteOrbitPlanProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段自动调用（与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 线程说明（T2 核实）：保持 worker——访问面 = 只读稳定数据（fragment / 运行时轨道数据）+
	// 引擎允许的 Subsystem 调用 + 两个仅供本处理器自用的节流时间戳（单写者单读者），无并行风险
	// 帧序：SatellitePlan → SatelliteSlot → SatelliteMovement → SatelliteRender（顺序由后继处理器声明）
	ExecutionOrder.ExecuteInGroup = TEXT("SatellitePlan");
}

void USatelliteOrbitPlanProcessor::ConfigureQueries()
{
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();   // 取 SatelliteManager（驱动规划节拍）
}

void USatelliteOrbitPlanProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 选定暂停（白名单）：编辑模式中冻结规划（不发单、不整理）
	SIMULATION_PAUSE_RETURN(Context.GetWorld());

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
