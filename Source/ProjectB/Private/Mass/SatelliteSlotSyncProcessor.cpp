// SatelliteSlotSyncProcessor.cpp
#include "Mass/SatelliteSlotSyncProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"        // UpdateOrbitSlots（槽位相位推进）
#include "Simulation/SimulationControlSubsystem.h"   // 选定暂停（白名单挂载）

USatelliteSlotSyncProcessor::USatelliteSlotSyncProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 槽位推进访问 UObject 组件（SmartObjectManager）——仅游戏线程（T1/T2 修复：从 Movement 迁出的 GT 最小部分）
	bRequiresGameThreadExecution = true;
	// 帧序：Plan → Slot → Movement → Render（先槽后星；本处理器接在规划之后、相位推进之前）
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteSlot");
	ExecutionOrder.ExecuteAfter.Add(TEXT("SatellitePlan"));
}

void USatelliteSlotSyncProcessor::ConfigureQueries()
{
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();   // 取 SmartObjectManager（槽位推进目标）
}

void USatelliteSlotSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 选定暂停（白名单）：编辑模式中冻结槽推进（与卫星相位推进一致）
	SIMULATION_PAUSE_RETURN(Context.GetWorld());

	const float DeltaTime = Context.GetDeltaTimeSeconds();

	// 槽推进为全局行为：仅首个 chunk 调用一次（本处理器为 GT 单线程执行，无并行竞态）
	bool bSlotsUpdated = false;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [DeltaTime, &bSlotsUpdated](FMassExecutionContext& ChunkContext)
	{
		if (bSlotsUpdated)
		{
			return;
		}
		bSlotsUpdated = true;
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		if (Shared.SmartObjectManager)
		{
			Shared.SmartObjectManager->UpdateOrbitSlots(DeltaTime);
		}
	});
}
