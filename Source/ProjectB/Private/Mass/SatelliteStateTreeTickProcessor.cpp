// SatelliteStateTreeTickProcessor.cpp
#include "Mass/SatelliteStateTreeTickProcessor.h"

#include "MassExecutionContext.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeTypes.h"     // UE::Mass::Signals::NewStateTreeTaskRequired
#include "Mass/SatelliteMassFragments.h"

USatelliteStateTreeTickProcessor::USatelliteStateTreeTickProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 仅发送信号（线程安全的信号收集），允许并行 chunk 收集
}

void USatelliteStateTreeTickProcessor::ConfigureQueries()
{
	// Tag 需求（FSatelliteTag 继承 FMassTag，需用 AddTagRequirement 而非 AddRequirement）
	EntityQuery.AddTagRequirement<FSatelliteTag>(EMassFragmentPresence::All);
	ProcessorRequirements.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
}

void USatelliteStateTreeTickProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 收集全部卫星实体（chunk 并行，加锁合并；卫星数量少，开销可忽略）
	TArray<FMassEntityHandle> SatelliteEntities;
	FCriticalSection CollectLock;
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&SatelliteEntities, &CollectLock](FMassExecutionContext& ChunkContext)
	{
		const int32 NumEntities = ChunkContext.GetNumEntities();
		FScopeLock Lock(&CollectLock);
		SatelliteEntities.Reserve(SatelliteEntities.Num() + NumEntities);
		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			SatelliteEntities.Add(ChunkContext.GetEntity(Index));
		}
	});

	if (SatelliteEntities.Num() == 0)
	{
		return;
	}

	// 发送 StateTree 推进信号（UMassStateTreeProcessor 订阅此信号并 Tick 对应实体）
	UMassSignalSubsystem& SignalSubsystem = Context.GetMutableSubsystemChecked<UMassSignalSubsystem>();
	SignalSubsystem.SignalEntities(UE::Mass::Signals::NewStateTreeTaskRequired, SatelliteEntities);
}
