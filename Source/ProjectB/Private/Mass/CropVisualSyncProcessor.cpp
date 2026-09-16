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
    if (!CropParams.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UCropVisualSyncProcessor: ISMAttachParent not set in GlobalConfigSubsystem"));
        return;
    }

    const FVector CropOffset = FarmConfig->CropOffset;

    // 合批累积缓冲（高频路径：单帧累积后一次批量，BatchAddInstances 按 Mesh 分组）：
    // - 移除按“当前 Mesh”归组；添加按“目标 Mesh”归组，并保留实体句柄用于批量后回填
    struct FPendingCropAdd
    {
        FTransform Tf;
        FIntPoint Coord;      // 分桶输入：作物所属格子坐标（业务寻址语言）
        FMassEntityHandle Entity;
    };
    TMap<UStaticMesh*, TArray<FGlobalInstanceId>> PendingRemovesByMesh;
    TMap<UStaticMesh*, TArray<FPendingCropAdd>> PendingAddsByMesh;
    // 并行收集：各 chunk 在 worker 线程并行处理 fragment，局部缓冲合并到共享缓冲（统一批量提交）用同一把锁保护
    FCriticalSection PendingLock;

    EntityQuery.ParallelForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& InContext)
    {
        // 当前 chunk 的局部收集缓冲（worker 线程并行，无竞争）
        TMap<UStaticMesh*, TArray<FGlobalInstanceId>> LocalRemovesByMesh;
        TMap<UStaticMesh*, TArray<FPendingCropAdd>> LocalAddsByMesh;
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
            CropTransform.SetScale3D(FVector::One());
            // 作物偏移以格子局部坐标系（球面切空间：Z=法线方向）为参考，经格子旋转转到世界空间后再平移
            CropTransform.AddToTranslation(Cell.Transform.TransformVectorNoScale(CropOffset));

            // 移除旧阶段实例：按当前 Mesh 归组累积（BatchRemoveInstances 需按 Mesh 分组）
            if (CropRender.InstanceId.IsValid() && CropRender.CurrentMesh)
            {
                LocalRemovesByMesh.FindOrAdd(CropRender.CurrentMesh).Add(CropRender.InstanceId);
                CropRender.InstanceId = FGlobalInstanceId(); // 标记为空
            }

            // 添加新阶段实例：按目标 Mesh 归组累积（BatchAddInstances 需按 Mesh 分组，句柄顺序回填）
            if (DesiredMesh)
            {
                LocalAddsByMesh.FindOrAdd(DesiredMesh).Add(
                    FPendingCropAdd{ CropTransform, Cell.Coords, InContext.GetEntity(i) }); // 分桶输入：格子坐标（业务寻址语言）
            }

            // 更新 CurrentMesh 快照
            CropRender.CurrentMesh = DesiredMesh;

            // 更新阶段快照
            CropState.LastRenderedStage = CurrentStage;
        }

        // 将本 chunk 的局部缓冲合并到共享缓冲（统一批量提交；加锁保护跨 worker 并发写）
        FScopeLock Lock(&PendingLock);
        for (auto& Pair : LocalRemovesByMesh)
            PendingRemovesByMesh.FindOrAdd(Pair.Key).Append(Pair.Value);
        for (auto& Pair : LocalAddsByMesh)
            PendingAddsByMesh.FindOrAdd(Pair.Key).Append(Pair.Value);
        Lock.Unlock();
    });

    // ---- 批量移除（每 Mesh 一次；原生增量 Remove，计数归零事件驱动停用由管理器处理）----
    for (auto& RemovePair : PendingRemovesByMesh)
    {
        if (RemovePair.Value.Num() > 0)
            ISMManager->BatchRemoveInstances(RemovePair.Key, RemovePair.Value);
    }

    // ---- 批量添加（每 Mesh 一次；分桶输入 GridCoords 非空，CustomData 固定 3 floats）----
    for (auto& AddPair : PendingAddsByMesh)
    {
        UStaticMesh* Mesh = AddPair.Key;
        const TArray<FPendingCropAdd>& PendingAdds = AddPair.Value;
        if (PendingAdds.Num() == 0) continue;

        TArray<FTransform> Transforms;
        TArray<FIntPoint> GridCoords;
        TArray<FMassEntityHandle> Entities;
        TArray<float> CustomData;
        Transforms.Reserve(PendingAdds.Num());
        GridCoords.Reserve(PendingAdds.Num());
        Entities.Reserve(PendingAdds.Num());
        CustomData.Reserve(PendingAdds.Num() * 3);
        for (const FPendingCropAdd& Add : PendingAdds)
        {
            Transforms.Add(Add.Tf);
            GridCoords.Add(Add.Coord);
            Entities.Add(Add.Entity);
            CustomData.Append({1.0f, 0.0f, 0.0f});
        }

        const TArray<FGlobalInstanceId> NewIds = ISMManager->BatchAddInstances(
            Mesh, Transforms, GridCoords, 3, CropParams, /*bWorldSpace=*/true, CustomData);
        // 返回与输入一一对应（保序），按实体回填
        for (int32 j = 0; j < NewIds.Num() && j < Entities.Num(); ++j)
        {
            if (EntityManager.IsEntityActive(Entities[j]))
                EntityManager.GetFragmentDataChecked<FCropRenderFragment>(Entities[j]).InstanceId = NewIds[j];
        }
    }
}