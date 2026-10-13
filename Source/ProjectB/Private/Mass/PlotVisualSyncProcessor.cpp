// PlotVisualSyncProcessor.cpp
// 实现：地块视觉同步（透明度/湿度/肥力 CustomData 增量——GT 外框 + 并行收集 + GT 批提交）
#include "Mass/PlotVisualSyncProcessor.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Mass/CellMassFragments.h"
#include "Crops/CropData.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Subsystem/ISMManager.h"

UPlotVisualSyncProcessor::UPlotVisualSyncProcessor()
{
    bAutoRegisterWithProcessingPhases = true;
    // 渲染同步：Execute 内直接调用 ISMManager 写接口，必须在游戏线程运行（线程约定见 ISMManager 技术文档 §3.3）
    bRequiresGameThreadExecution = true;
}

void UPlotVisualSyncProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FCellStaticFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FPlotRenderFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FPlotStateFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
}

void UPlotVisualSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    const UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    UISMManager* ISMManager = World->GetSubsystem<UISMManager>();
    if (!ISMManager) return;

    const UGlobalConfigSubsystem* ConfigSub = World->GetSubsystem<UGlobalConfigSubsystem>();
    if (!ConfigSub) return;
    
    UFarmlandConfigDataAsset* FarmlandConfig = ConfigSub->GetFarmlandConfig();

    // 单值 CustomData 更新缓冲（worker 线程并行收集，锁合并，游戏线程统一提交，见 ISMManager 技术文档 §3.3）
    struct FPendingCustomDataUpdate
    {
        FGlobalInstanceId InstanceId;
        int32 Index;
        float Value;
    };
    TArray<FPendingCustomDataUpdate> PendingUpdates;
    FCriticalSection PendingLock;

    EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [&PendingUpdates, &PendingLock](FMassExecutionContext& InContext)
    {
        // 当前 chunk 的局部收集缓冲（worker 线程并行，无竞争）
        TArray<FPendingCustomDataUpdate> LocalUpdates;
        const TConstArrayView<FPlotRenderFragment> PlotRenderList = InContext.GetFragmentView<FPlotRenderFragment>();
        const TArrayView<FPlotStateFragment> StateList = InContext.GetMutableFragmentView<FPlotStateFragment>();
        const int32 NumEntities = InContext.GetNumEntities();
        
        for (int32 i = 0; i < NumEntities; ++i)
        {
            const FPlotRenderFragment& PlotRender = PlotRenderList[i];
            FPlotStateFragment& State = StateList[i];
            if (State.StateData != State.PreStateData)
            {
                // 透明度：有耕地或作物时地块可见
                if (State.StateData.HasState(EPlotState::Plowed) != State.PreStateData.HasState(EPlotState::Plowed))
                {
                    const float Opacity = State.StateData.HasState(EPlotState::Plowed) ? 1.0f : 0.0f;
                    LocalUpdates.Add({ PlotRender.InstanceId, PlotCustomData::Opacity, Opacity });
                    if (!Opacity)
                    {
                        const FMassEntityHandle Entity = InContext.GetEntity(i);
                        InContext.Defer().RemoveFragment<FPlotStateFragment>(Entity);
                    }
                }

                // 湿润可视化
                if (State.StateData.HasState(EPlotState::Watered) != State.PreStateData.HasState(EPlotState::Watered))
                {
                    const float Wetness = State.StateData.HasState(EPlotState::Watered) ? 1.0f : 0.0f;
                    LocalUpdates.Add({ PlotRender.InstanceId, PlotCustomData::Wetness, Wetness });
                }

                // 肥力可视化
                if (State.StateData.HasState(EPlotState::Fertilized) != State.PreStateData.HasState(EPlotState::Fertilized))
                {
                    const float Fertility = State.StateData.HasState(EPlotState::Fertilized) ? 1.0f : 0.0f;
                    LocalUpdates.Add({ PlotRender.InstanceId, PlotCustomData::Fertility, Fertility });
                }

                if (State.StateData.HasState(EPlotState::WeedInfested) != State.PreStateData.HasState(EPlotState::WeedInfested))
                {
                    const bool bWeedInfested = State.StateData.HasState(EPlotState::WeedInfested);
                    
                }

                if (State.StateData.HasState(EPlotState::PestInfested) != State.PreStateData.HasState(EPlotState::PestInfested))
                {
                    const bool bPestInfested = State.StateData.HasState(EPlotState::PestInfested);
                    
                }
               
                
                State.PreStateData = State.StateData;
            }
        }

        // 将本 chunk 的局部缓冲合并到共享缓冲（统一批量提交；加锁保护跨 worker 并发写）
        if (LocalUpdates.Num() > 0)
        {
            FScopeLock Lock(&PendingLock);
            PendingUpdates.Append(MoveTemp(LocalUpdates));
            Lock.Unlock();
        }
    });

    // ---- 游戏线程统一批量提交（单值增量更新，仅游戏线程调 ISM） ----
    for (const FPendingCustomDataUpdate& Update : PendingUpdates)
    {
        if (Update.InstanceId.IsValid())
            ISMManager->UpdateInstanceCustomData(FarmlandConfig->LandCellMesh, Update.InstanceId, Update.Index, Update.Value);
    }
}