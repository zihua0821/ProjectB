#include "Subsystem/ISMManager.h"
#include "Component/IncrementalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProjectB/ProjectB.h"

// ============================================================================
// 生命周期
// ============================================================================

void UISMManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UISMManager::Deinitialize()
{
    MeshToBucketGroups.Empty();
    DirtyIncrementalComponents.Empty();
    FreeSlotIndices.Empty();
    Super::Deinitialize();
}

// ============================================================================
// 组件创建
// ============================================================================

UInstancedStaticMeshComponent* UISMManager::CreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh || !Params.AttachParent) return nullptr;
    AActor* Owner = Params.AttachParent->GetOwner();
    if (!Owner) return nullptr;

    UInstancedStaticMeshComponent* NewISM = nullptr;
    if (Params.bUseIncrementalISM)
        NewISM = NewObject<UIncrementalInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    else
        NewISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);

    NewISM->SetStaticMesh(Mesh);
    NewISM->SetNumCustomDataFloats(NumCustomDataFloats);
    NewISM->SetCollisionEnabled(Params.CollisionEnabled);
    NewISM->SetCollisionObjectType(Params.CollisionObjectType);
    NewISM->SetCollisionResponseToAllChannels(Params.CollisionResponseToAllChannels);
    NewISM->SetCanEverAffectNavigation(Params.bCanEverAffectNavigation);
    NewISM->bDisallowNanite = Params.bDisallowNanite;
    NewISM->SetCastShadow(Params.bCastShadow);
    if (Params.MinDrawDistance > 0.0f || Params.MaxDrawDistance > 0.0f)
        NewISM->SetCullDistances(Params.MinDrawDistance, Params.MaxDrawDistance);
    NewISM->SetupAttachment(Params.AttachParent);
    NewISM->RegisterComponent();

    NewISM->MarkRenderStateDirty();
    return NewISM;
}

// ============================================================================
// 基本接口
// ============================================================================

UInstancedStaticMeshComponent* UISMManager::GetOrCreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh) return nullptr;
    const FISMMeshKey Key(Mesh);
    if (const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key))
    {
        if (Group->Buckets.Num() > 0 && Group->Buckets[0])
            return Group->Buckets[0];
    }

    if (IsInGameThread())
    {
        FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
        Group.NumCustomDataFloats = NumCustomDataFloats;
        Group.bDisableBuckets = Params.bDisableBuckets;
        Group.CachedParams = Params;
        Group.BucketingThreshold = BucketingThreshold; // 使用当前全局阈值
        if (Group.Buckets.Num() == 0) Group.Buckets.SetNum(1);
        UInstancedStaticMeshComponent* NewISM = CreateISMComponent(Mesh, NumCustomDataFloats, Params);
        Group.Buckets[0] = NewISM;
        return NewISM;
    }

    FISMComponentParams ParamsCopy = Params;
    const FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [this, Mesh, NumCustomDataFloats, ParamsCopy]() { GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ParamsCopy); },
        TStatId(), nullptr, ENamedThreads::GameThread);
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (Group && Group->Buckets.Num() > 0 && Group->Buckets[0]) return Group->Buckets[0];
    return nullptr;
}

UInstancedStaticMeshComponent* UISMManager::GetISMComponent(UStaticMesh* Mesh) const
{
    if (!Mesh) return nullptr;
    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (Group && Group->Buckets.Num() > 0 && Group->Buckets[0]) return Group->Buckets[0];
    return nullptr;
}

// ============================================================================
// 单个实例更新
// ============================================================================

void UISMManager::UpdateInstanceCustomData(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, int32 CustomDataIndex, float Value)
{
    if (!Mesh || !InstanceId.IsValid()) return;
    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(InstanceId.BucketIndex)) return;

    UInstancedStaticMeshComponent* Bucket = Group->Buckets[InstanceId.BucketIndex];
    if (!Bucket) return;

    // 优先走增量接口（避免全量 MarkRenderStateDirty）
    if (UIncrementalInstancedStaticMeshComponent* IncComp = Cast<UIncrementalInstancedStaticMeshComponent>(Bucket))
    {
        int32 InstanceIndex = IncComp->GetInstanceIndexForId(InstanceId.LocalId);
        if (InstanceIndex != INDEX_NONE)
            IncComp->UpdateInstanceCustomDataIncremental(InstanceIndex, MakeArrayView(&Value, 1));
    }
    else
    {
        Bucket->SetCustomDataValueById(InstanceId.LocalId, CustomDataIndex, Value);
    }
}

// ============================================================================
// 批量操作
// ============================================================================

DECLARE_CYCLE_STAT(TEXT("BatchAddInstances"), STAT_BatchAddInstances, STATGROUP_Game);
TArray<FGlobalInstanceId> UISMManager::BatchAddInstances(
    UStaticMesh* Mesh, const TArray<FTransform>& Transforms, const int32 NumCustomDataFloats,
    const FISMComponentParams& ISMParams, const TArray<float>& CustomData)
{
    SCOPE_CYCLE_COUNTER(STAT_BatchAddInstances);
    TArray<FGlobalInstanceId> AllGlobalIds;
    if (!Mesh || Transforms.Num() == 0) return AllGlobalIds;

    const int32 AddCount = Transforms.Num();
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    Group.NumCustomDataFloats = NumCustomDataFloats;

    // 根据阈值决定是否启用分桶
    const bool bForceSingleBucket = Group.bDisableBuckets || Group.BucketingThreshold <= 0;
    const bool bShouldStartBucketing = (!bForceSingleBucket) && (Group.TotalInstanceCount + AddCount > Group.BucketingThreshold);

    // 场景1：未分桶且不需要分桶 → 使用单桶
    if ((Group.Buckets.Num() == 0) && !bShouldStartBucketing)
    {
        UInstancedStaticMeshComponent* ISMComp = GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ISMParams);
        if (!ISMComp) return AllGlobalIds;
        TArray<FPrimitiveInstanceId> Ids = ISMComp->AddInstancesById(Transforms, true);
        for (const FPrimitiveInstanceId& Id : Ids)
            AllGlobalIds.Emplace(0, Id);
        if (CustomData.Num() > 0)
        {
            const int32 Expected = Ids.Num() * NumCustomDataFloats;
            if (CustomData.Num() == Expected) ISMComp->SetCustomDataById(Ids, CustomData);
        }
        Group.TotalInstanceCount += AddCount;
        return AllGlobalIds;
    }

    // 场景2：需要分桶或已经分桶
    if (!GetOrCreateBucket(Mesh, 0, NumCustomDataFloats, ISMParams)) return AllGlobalIds;

    const int32 StartGlobalIndex = Group.TotalInstanceCount;
    int32 CurrentGlobalIdx = StartGlobalIndex;
    int32 Processed = 0;

    while (Processed < AddCount)
    {
        int32 BucketIdx = CurrentGlobalIdx / INSTANCES_PER_BUCKET;
        const int32 LocalOffset = CurrentGlobalIdx % INSTANCES_PER_BUCKET;
        const int32 Remain = INSTANCES_PER_BUCKET - LocalOffset;
        const int32 SliceCount = FMath::Min(Remain, AddCount - Processed);

        UInstancedStaticMeshComponent* Bucket = GetOrCreateBucket(Mesh, BucketIdx, NumCustomDataFloats, ISMParams);
        if (!Bucket) break;

        TArray<FTransform> SliceTransforms(&Transforms[Processed], SliceCount);
        TArray<FPrimitiveInstanceId> SliceIds = Bucket->AddInstancesById(SliceTransforms, true);
        for (const FPrimitiveInstanceId& Id : SliceIds)
            AllGlobalIds.Emplace(BucketIdx, Id);

        if (CustomData.Num() > 0)
        {
            const int32 Floats = NumCustomDataFloats;
            const int32 DataStart = Processed * Floats;
            TArray<float> SliceData(&CustomData[DataStart], SliceCount * Floats);
            Bucket->SetCustomDataById(SliceIds, SliceData);
        }
        Processed += SliceCount;
        CurrentGlobalIdx += SliceCount;
    }
    Group.TotalInstanceCount += AddCount;
    return AllGlobalIds;
}

void UISMManager::BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds)
{
    if (!Mesh || InstanceIds.Num() == 0) return;
    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group) return;
    TMap<int32, TArray<FPrimitiveInstanceId>> BucketMap;
    for (const FGlobalInstanceId& Gid : InstanceIds)
    {
        if (Gid.IsValid() && Group->Buckets.IsValidIndex(Gid.BucketIndex))
            BucketMap.FindOrAdd(Gid.BucketIndex).Add(Gid.LocalId);
    }
    for (auto& Pair : BucketMap)
    {
        if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[Pair.Key])
            Bucket->RemoveInstancesById(Pair.Value);
    }
}

void UISMManager::BatchSetInstanceCustomData(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<float>& CustomData) const
{
    if (!Mesh || InstanceIds.Num() == 0 || CustomData.Num() == 0) return;
    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group) return;
    int32 FloatsPerInstance = 0;
    for (const auto& Gid : InstanceIds)
    {
        if (Gid.IsValid() && Group->Buckets.IsValidIndex(Gid.BucketIndex))
        {
            FloatsPerInstance = Group->NumCustomDataFloats;
            break;
        }
    }
    if (FloatsPerInstance == 0) return;
    const int32 Expected = InstanceIds.Num() * FloatsPerInstance;
    if (CustomData.Num() != Expected) return;
    TMap<int32, TArray<FPrimitiveInstanceId>> IdGroups;
    TMap<int32, TArray<float>> DataGroups;
    for (int32 i = 0; i < InstanceIds.Num(); ++i)
    {
        const FGlobalInstanceId& Gid = InstanceIds[i];
        if (!Gid.IsValid() || !Group->Buckets.IsValidIndex(Gid.BucketIndex)) continue;
        IdGroups.FindOrAdd(Gid.BucketIndex).Add(Gid.LocalId);
        TArray<float>& Data = DataGroups.FindOrAdd(Gid.BucketIndex);
        const int32 Base = i * FloatsPerInstance;
        for (int32 f = 0; f < FloatsPerInstance; ++f)
            Data.Add(CustomData[Base + f]);
    }
    for (auto& IdPair : IdGroups)
    {
        if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[IdPair.Key])
            Bucket->SetCustomDataById(IdPair.Value, DataGroups[IdPair.Key]);
    }
}

// ============================================================================
// 桶管理
// ============================================================================

UInstancedStaticMeshComponent* UISMManager::GetOrCreateBucket(UStaticMesh* Mesh, int32 BucketIndex, int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh) return nullptr;
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    Group.NumCustomDataFloats = NumCustomDataFloats;
    if (BucketIndex >= Group.Buckets.Num())
        Group.Buckets.SetNum(BucketIndex + 1);
    if (Group.Buckets[BucketIndex]) return Group.Buckets[BucketIndex];

    if (IsInGameThread())
    {
        UInstancedStaticMeshComponent* NewBucket = CreateISMComponent(Mesh, NumCustomDataFloats, Params);
        Group.Buckets[BucketIndex] = NewBucket;
        return NewBucket;
    }
    const FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [this, Mesh, Key, BucketIndex, NumCustomDataFloats, Params]() { GetOrCreateBucket(Mesh, BucketIndex, NumCustomDataFloats, Params); },
        TStatId(), nullptr, ENamedThreads::GameThread);
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
    return Group.Buckets[BucketIndex];
}

void UISMManager::DebugPrintBucketStats()
{
    int32 TotalBuckets = 0;
    UE_LOG(LogTemp, Warning, TEXT("----- ISM Bucket Stats -----"));
    for (auto& Pair : MeshToBucketGroups)
    {
        const FISMBucketGroup& Group = Pair.Value;
        int32 ActiveBuckets = 0;
        for (const auto& Bucket : Group.Buckets) if (Bucket) ++ActiveBuckets;
        TotalBuckets += ActiveBuckets;
        UE_LOG(LogTemp, Warning, TEXT("Mesh: %s, Total Instances: %d, Buckets: %d (Active)"), *Pair.Key.MeshPath.ToString(), Group.TotalInstanceCount, ActiveBuckets);
    }
    UE_LOG(LogTemp, Warning, TEXT("Total Active Buckets: %d"), TotalBuckets);
}

// ---- 对象池实现 ----
// 注意：仅支持 UIncrementalInstancedStaticMeshComponent，不支持普通 ISM
FGlobalInstanceId UISMManager::AcquireSlot(UStaticMesh* Mesh, const FTransform& WorldTransform, TArrayView<const float> CustomData)
{
    if (!Mesh) return FGlobalInstanceId();
    const FISMMeshKey Key(Mesh);
    TUniquePtr<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>& FreeQueue = FreeSlotIndices.FindOrAdd(Key);
    if (!FreeQueue)
    {
        FreeQueue = MakeUnique<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>();
    }

    FGlobalInstanceId AllocatedId;
    if (!FreeQueue->Dequeue(AllocatedId))
    {
        // 池空，扩容
        ExpandCapacity(Mesh, BatchExpandSize);
        // 扩容后队列一定存在且非空
        FreeQueue->Dequeue(AllocatedId);
    }
    
    FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(AllocatedId.BucketIndex)) return FGlobalInstanceId();
    UIncrementalInstancedStaticMeshComponent* IncComp = Cast<UIncrementalInstancedStaticMeshComponent>(Group->Buckets[AllocatedId.BucketIndex]);
    if (!IncComp) return FGlobalInstanceId();

    const int32 InstanceIndex = IncComp->GetInstanceIndexForId(AllocatedId.LocalId);
  
    if (InstanceIndex == INDEX_NONE) return FGlobalInstanceId();

    IncComp->UpdateInstanceTransformIncremental(InstanceIndex, WorldTransform);
    if (CustomData.Num() > 0)
    {
        IncComp->UpdateInstanceCustomDataIncremental(InstanceIndex, CustomData);
    }
    DirtyIncrementalComponents.Add(IncComp);
    return AllocatedId;
}

void UISMManager::ReleaseSlot(UStaticMesh* Mesh, const FGlobalInstanceId& InstanceId)
{
    if (!Mesh || !InstanceId.IsValid()) return;
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(InstanceId.BucketIndex)) return;
    UIncrementalInstancedStaticMeshComponent* IncComp = Cast<UIncrementalInstancedStaticMeshComponent>(Group->Buckets[InstanceId.BucketIndex]);
    if (!IncComp) return;
    int32 InstanceIndex = IncComp->GetInstanceIndexForId(InstanceId.LocalId);
    if (InstanceIndex == INDEX_NONE) return;
    IncComp->SetInstanceVisibility(InstanceIndex, false);
    DirtyIncrementalComponents.Add(IncComp);
    TUniquePtr<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>& FreeQueue = FreeSlotIndices.FindOrAdd(Key);
    if (!FreeQueue)
    {
        FreeQueue = MakeUnique<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>();
    }
    FreeQueue->Enqueue(InstanceId);
}

void UISMManager::ExpandCapacity(UStaticMesh* Mesh, int32 AdditionalCount)
{
    if (!Mesh || AdditionalCount <= 0) return;
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    
    TArray<FTransform> DummyTransforms;
    DummyTransforms.SetNum(AdditionalCount);

    const bool bIsNanite = Group.Buckets[0]->GetStaticMesh()->IsNaniteEnabled();
    const FTransform InitTransform = bIsNanite ? FTransform(ParkedLocation) : FTransform::Identity;
    for (int32 i = 0; i < AdditionalCount; ++i)
    {
        DummyTransforms[i] = InitTransform;
        DummyTransforms[i].SetScale3D(FVector::One());
    }
    // 扩容总是使用缓存的参数
    TArray<FGlobalInstanceId> NewIds = BatchAddInstances(Mesh, DummyTransforms, Group.NumCustomDataFloats, Group.CachedParams);

    // 将新创建的实例全部入空闲队列（所有实例初始为缩放零的隐藏状态）
    TUniquePtr<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>& FreeQueue = FreeSlotIndices.FindOrAdd(Key);
    if (!FreeQueue)
    {
        FreeQueue = MakeUnique<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>();
    }
    for (FGlobalInstanceId& Id : NewIds)
    {
        FreeQueue->Enqueue(Id);
    }
    // for (auto& Bucket : Group.Buckets)
    // {
    //     if (Bucket)
    //     {
    //         Bucket->MarkRenderStateDirty();
    //     }
    // }
}

void UISMManager::PreAllocateSlots(UStaticMesh* Mesh, int32 Count)
{
    if (Count <= 0) return;
    ExpandCapacity(Mesh, Count);
}

void UISMManager::FlushAllIncrementalComponents()
{
    for (auto& Bucket : DirtyIncrementalComponents)
    {
        if (UIncrementalInstancedStaticMeshComponent* IncComp = Cast<UIncrementalInstancedStaticMeshComponent>(Bucket))
        {
            IncComp->FlushIncrementalUpdates();
        }
    }
    DirtyIncrementalComponents.Reset();
}