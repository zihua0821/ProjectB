// SatelliteOrbitMovementProcessor.cpp
#include "Mass/SatelliteOrbitMovementProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"   // UpdateOrbitSlots（槽位推进——先槽后星，同帧同序）
#include "Simulation/SimulationControlSubsystem.h"   // 选定暂停（白名单挂载）

namespace
{
	/**
	 * 单卫星相位推进（离轨作业方案 v1.0）
	 * 统一行为：相位始终按 ω 推进（巡航与离轨期间一致——离轨时仅渲染位置被覆盖）
	 * 相位归一化到 [0, 360)
	 */
	void UpdateOrbitPhase(FMassOrbitFragment& Orbit, const float DeltaTime)
	{
		if (FMath::IsNearlyZero(Orbit.AngularVelocity))
		{
			return;
		}

		float NewPhase = Orbit.VirtualPhase + (Orbit.bClockwise ? -1.0f : 1.0f) * Orbit.AngularVelocity * DeltaTime;

		// 归一化到 [0, 360)
		NewPhase = FMath::Fmod(NewPhase, 360.0f);
		if (NewPhase < 0.0f)
		{
			NewPhase += 360.0f;
		}
		Orbit.VirtualPhase = NewPhase;
	}
}

USatelliteOrbitMovementProcessor::USatelliteOrbitMovementProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段自动调用（与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 纯 fragment 数据修改，不触碰 ISM/世界对象，允许并行 chunk 执行
	// 执行组：渲染同步处理器按此组名排在其后（同帧读到最新相位）
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteOrbitMovement");
}

void USatelliteOrbitMovementProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassOrbitFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();   // 取 SmartObjectManager（驱动槽位推进）
}

void USatelliteOrbitMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 选定暂停（白名单）：编辑模式中冻结槽推进与卫星相位
	SIMULATION_PAUSE_RETURN(Context.GetWorld());

	const float DeltaTime = Context.GetDeltaTimeSeconds();

	// 槽位相位推进【先槽后星】：与卫星同帧同序——渲染同步处理器随后读到同帧槽缓存，消除"卫星滞后一帧相位"的恒定错位
	//（此前由 SmartObjectManagerComponent 的组件 Tick 驱动，与 Mass 帧序先后不定）
	bool bSlotsUpdated = false;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [DeltaTime, &bSlotsUpdated](FMassExecutionContext& ChunkContext)
	{
		if (!bSlotsUpdated)
		{
			bSlotsUpdated = true;   // 槽推进为全局行为：仅首个 chunk 执行一次
			const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
			if (Shared.SmartObjectManager)
			{
				Shared.SmartObjectManager->UpdateOrbitSlots(DeltaTime);
			}
		}

		const TArrayView<FMassOrbitFragment> OrbitFragments = ChunkContext.GetMutableFragmentView<FMassOrbitFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			UpdateOrbitPhase(OrbitFragments[Index], DeltaTime);
		}
	});
}
