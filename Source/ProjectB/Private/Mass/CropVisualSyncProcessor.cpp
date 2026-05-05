// UCropVisualSyncProcessor.cpp
#include "Mass/CropVisualSyncProcessor.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Mass/CellMassFragments.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Crops/CropData.h"
#include "Engine/DataTable.h"
#include "Subsystem/ISMManager.h"

UCropVisualSyncProcessor::UCropVisualSyncProcessor()
{
    bAutoRegisterWithProcessingPhases = true;
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
    if (!CropParams.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UCropVisualSyncProcessor: ISMAttachParent not set in GlobalConfigSubsystem"));
        return;
    }

    const FVector CropOffset = FarmConfig->CropOffset;

     // 收集待移除的实例
    TArray<TPair<UStaticMesh*, FPrimitiveInstanceId>> PendingRemoves;
    
    // 键：(Mesh, NumFloats) 值：(Transform, Entity)
    TArray<TPair<TPair<UStaticMesh*, int32>, TPair<FTransform, FMassEntityHandle>>> PendingAdds;

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

            if (!DesiredMesh) continue;

            FTransform CropTransform = Cell.Transform;
            CropTransform.AddToTranslation(CropOffset);

            // 记录需要移除的旧实例
            if (CropRender.InstanceId.IsValid() && CropRender.CurrentMesh)
            {
                PendingRemoves.Emplace(CropRender.CurrentMesh, CropRender.InstanceId);
            }
            
            // 填充待添加列表，包含分组键
            PendingAdds.Emplace(
                TPair<UStaticMesh*, int32>{DesiredMesh, CropStaticConfig.CropNumCustomDataFloats},
                TPair<FTransform, FMassEntityHandle>{CropTransform, Context.GetEntity(i)}
            );

            // 更新 CurrentMesh 快照
            CropRender.CurrentMesh = DesiredMesh;

            // 更新阶段快照
            CropState.LastRenderedStage = CurrentStage;
        }
    });

    // ---- 批量移除 ----
    if (PendingRemoves.Num() > 0)
    {
        PendingRemoves.Sort([](const auto& A, const auto& B)
        {
            return A.Key < B.Key;
        });
        
        for (int32 i = 0; i < PendingRemoves.Num(); )
        {
            UStaticMesh* Mesh = PendingRemoves[i].Key;
            TArray<FPrimitiveInstanceId> Ids;
            while (i < PendingRemoves.Num() && PendingRemoves[i].Key == Mesh)
            {
                Ids.Add(PendingRemoves[i].Value);
                ++i;
            }
            ISMManager->BatchRemoveInstances(Mesh, Ids);
        }
    }

    // ---- 批量添加 ----
    if (PendingAdds.Num() > 0)
    {
        PendingAdds.Sort([](const auto& A, const auto& B)
        {
            return A.Key.Key < B.Key.Key;
        });

        for (int32 i = 0; i < PendingAdds.Num(); )
        {
            const TPair<UStaticMesh*, int32>& Key = PendingAdds[i].Key;  
            UStaticMesh* Mesh = Key.Key;
            const int32 NumFloats = Key.Value;

            TArray<FTransform> Transforms;
            TArray<FMassEntityHandle> Entities;
            while (i < PendingAdds.Num() &&
                   PendingAdds[i].Key.Key == Mesh &&
                   PendingAdds[i].Key.Value == NumFloats)
            {
                Transforms.Add(PendingAdds[i].Value.Key);
                Entities.Add(PendingAdds[i].Value.Value);
                ++i;
            }

            TArray<FPrimitiveInstanceId> NewIds = ISMManager->BatchAddInstances(
                Mesh,
                Transforms,
                NumFloats,
                CropParams
                );
            check(NewIds.Num() == Entities.Num());

            // 写回实例 ID
            for (int32 j = 0; j < NewIds.Num(); ++j)
            {
                if (EntityManager.IsEntityActive(Entities[j]))
                {
                    EntityManager.GetFragmentDataChecked<FCropRenderFragment>(Entities[j]).InstanceId = NewIds[j];
                }
            }
        }
    }
}