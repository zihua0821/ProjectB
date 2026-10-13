// SatelliteOrbitMovementProcessor.cpp
#include "Mass/SatelliteOrbitMovementProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"
#include "Subsystem/SimulationControlSubsystem.h"   // 选定暂停（白名单挂载）

namespace
{
	/**
	 * 单卫星名义相位推进（设计见《卫星离轨作业_需求方案.md》）
	 * 统一行为：相位始终按 ω 推进（巡航与离轨期间一致——离轨时仅渲染位置被覆盖）
	 * 相位归一化到 [0, 360)
	 */
	void UpdateOrbitPhase(FMassSatelliteOrbitFragment& Orbit, const float DeltaTime)
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
	// 帧序：SatelliteSlot → 【SatelliteMovement】→ SatelliteRender（槽推进已迁至专职 GT 处理器）
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteMovement");
	ExecutionOrder.ExecuteAfter.Add(TEXT("SatelliteSlot"));
}

void USatelliteOrbitMovementProcessor::ConfigureQueries()
{
	// 相位推进只依赖自身 fragment（卫星实体集合由 FMassSatelliteOrbitFragment 唯一确定）
	EntityQuery.AddRequirement<FMassSatelliteOrbitFragment>(EMassFragmentAccess::ReadWrite);
}

void USatelliteOrbitMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 选定暂停（白名单）：编辑模式中冻结卫星相位
	SIMULATION_PAUSE_RETURN(Context.GetWorld());

	const float DeltaTime = Context.GetDeltaTimeSeconds();

	// 纯相位推进（槽位相位推进已迁至 USatelliteSlotSyncProcessor——本处理器不触碰 UObject；
	// 各实体独立写自身 fragment → chunk 级并行安全：ParallelForEachEntityChunk）
	EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [DeltaTime](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FMassSatelliteOrbitFragment> OrbitFragments = ChunkContext.GetMutableFragmentView<FMassSatelliteOrbitFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			UpdateOrbitPhase(OrbitFragments[Index], DeltaTime);
		}
	});
}
