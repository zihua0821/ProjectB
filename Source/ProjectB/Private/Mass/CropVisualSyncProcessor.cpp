// UCropVisualSyncProcessor.cpp
#include "Mass/CropVisualSyncProcessor.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Mass/CellMassFragments.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Crops/CropData.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Subsystem/ISMManager.h"

UCropVisualSyncProcessor::UCropVisualSyncProcessor()
{
    bAutoRegisterWithProcessingPhases = true;
    ExecutionOrder.ExecuteBefore.Add(TEXT("UCropGrowthProcessor"));
    bRequiresGameThreadExecution = true;
}

void UCropVisualSyncProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FCellStaticFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FCropStaticConfigFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FCropStateFragment>(EMassFragmentAccess::ReadWrite); 
    EntityQuery.AddRequirement<FCropRenderFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
}

void UCropVisualSyncProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    const UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    UISMManager* ISMManager = World->GetSubsystem<UISMManager>();
    const UGlobalConfigSubsystem* ConfigSub = World->GetSubsystem<UGlobalConfigSubsystem>();
    if (!ISMManager || !ConfigSub) return;

    const UFarmlandConfigDataAsset* FarmConfig = ConfigSub->GetFarmlandConfig();
    if (!FarmConfig) return;

    FISMComponentParams CropParams;
    CropParams.AttachParent = ConfigSub->GetISMAttachParent();
    CropParams.bUseIncrementalISM = true; // 引擎原生增量渲染
    if (!CropParams.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UCropVisualSyncProcessor: ISMAttachParent not set in GlobalConfigSubsystem"));
        return;
    }

    const FVector CropOffset = FarmConfig->CropOffset;

    EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& InContext)
    {
        const TConstArrayView<FCellStaticFragment>& Cells = InContext.GetFragmentView<FCellStaticFragment>();
        const TConstArrayView<FCropStaticConfigFragment>& CropStaticConfigs = InContext.GetFragmentView<FCropStaticConfigFragment>();
        const TArrayView<FCropStateFragment> CropStates = InContext.GetMutableFragmentView<FCropStateFragment>();
        const TArrayView<FCropRenderFragment> CropRenders = InContext.GetMutableFragmentView<FCropRenderFragment>();
        const int32 Num = InContext.GetNumEntities();

        for (int32 i = 0; i < Num; ++i)
        {
            const FCellStaticFragment& Cell = Cells[i];
            const FCropStaticConfigFragment& CropStaticConfig = CropStaticConfigs[i];
            FCropStateFragment& CropState = CropStates[i];
            FCropRenderFragment& CropRender = CropRenders[i];

            const ECropGrowthStage CurrentStage = CropState.CropGrowthStage;

            // 如果阶段未变化且作物可见状态未变，则跳过
            if (CurrentStage == CropState.LastRenderedStage) continue;

            UStaticMesh* DesiredMesh = nullptr;
            switch (CurrentStage)
            {
            case ECropGrowthStage::Seed:      DesiredMesh = CropStaticConfig.SeedMesh; break;
            case ECropGrowthStage::Seedling:  DesiredMesh = CropStaticConfig.SeedlingMesh; break;
            case ECropGrowthStage::Mature:    DesiredMesh = CropStaticConfig.MatureMesh; break;
            case ECropGrowthStage::Harvested: DesiredMesh = CropStaticConfig.HarvestedMesh; break;
            default: break;
            }
            
            FTransform CropTransform = Cell.Transform;
            CropTransform.AddToTranslation(CropOffset);

            // 移除旧阶段实例（原生增量 Remove）
            if (CropRender.InstanceId.IsValid() && CropRender.CurrentMesh)
            {
                ISMManager->BatchRemoveInstances(CropRender.CurrentMesh, {CropRender.InstanceId});
                CropRender.InstanceId = FGlobalInstanceId(); // 标记为空
            }

            // 添加新阶段实例（原生增量 Add）
            if (DesiredMesh)
            {
                const TArray<FGlobalInstanceId> Ids = ISMManager->BatchAddInstances(
                    DesiredMesh,
                    {CropTransform},
                    3,
                    CropParams,
                    {1.0f, 0.0f, 0.0f});
                if (Ids.Num() > 0) CropRender.InstanceId = Ids[0];
            }

            // 更新 CurrentMesh 快照
            CropRender.CurrentMesh = DesiredMesh;

            // 更新阶段快照
            CropState.LastRenderedStage = CurrentStage;
        }
    });
}