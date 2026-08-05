// PlotVisualSyncProcessor.cpp
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
    
    // 如果需要确保在某个处理器之后执行，可以使用 ExecuteAfter
    // ExecutionOrder.ExecuteAfter.Add(TEXT("PlotInitProcessor"));
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
    UPlanetConfigDataAsset* PlanetConfig = ConfigSub->GetPlanetConfig();

    EntityQuery.ForEachEntityChunk(EntityManager, Context, [ISMManager, FarmlandConfig, PlanetConfig](FMassExecutionContext& InContext)
    {
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
                    // ISMManager->UpdateInstanceCustomData(FarmlandConfig->LandCellMesh, PlotRender.InstanceId, PlotCustomData::Opacity, Opacity);
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
                    // ISMManager->UpdateInstanceCustomData(FarmlandConfig->LandCellMesh, PlotRender.InstanceId, PlotCustomData::Wetness, Wetness);
                }

                // 肥力可视化
                if (State.StateData.HasState(EPlotState::Fertilized) != State.PreStateData.HasState(EPlotState::Fertilized))
                {
                    const float Fertility = State.StateData.HasState(EPlotState::Fertilized) ? 1.0f : 0.0f;
                    // ISMManager->UpdateInstanceCustomData(FarmlandConfig->LandCellMesh, PlotRender.InstanceId, PlotCustomData::Fertility, Fertility);
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
    });
}