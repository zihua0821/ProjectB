#include "Subsystem/ISMManager.h"
#include "Components/InstancedStaticMeshComponent.h"

void UISMManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UISMManager::Deinitialize()
{
    MeshToBucketGroups.Empty();
    DirtyBuckets.Empty();
    Super::Deinitialize();
}

UInstancedStaticMeshComponent* UISMManager::CreateISMComponent(
    UStaticMesh* Mesh,
    const int32 NumCustomDataFloats,
    const FISMComponentParams& Params)
{
    if (!Mesh || !Params.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMManager::CreateISMComponent - Invalid Mesh or AttachParent"));
        return nullptr;
    }

    AActor* Owner = Params.AttachParent->GetOwner();
    if (!Owner) return nullptr;

    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    NewISM->SetStaticMesh(Mesh);
    NewISM->SetNumCustomDataFloats(NumCustomDataFloats);
    NewISM->SetCollisionEnabled(Params.CollisionEnabled);
    NewISM->SetCollisionObjectType(Params.CollisionObjectType);
    NewISM->SetCollisionResponseToAllChannels(Params.CollisionResponseToAllChannels);
    NewISM->SetCanEverAffectNavigation(Params.bCanEverAffectNavigation);
    NewISM->bDisallowNanite = Params.bDisallowNanite;
    NewISM->SetCastShadow(Params.bCastShadow);
    // NewISM->SetReceivesDecals(false);
    // NewISM->SetVisibleInRayTracing(false);

    if (Params.MinDrawDistance > 0.0f || Params.MaxDrawDistance > 0.0f)
    {
        NewISM->SetCullDistances(Params.MinDrawDistance, Params.MaxDrawDistance);
    }

    NewISM->SetupAttachment(Params.AttachParent);
    NewISM->RegisterComponent();
    return NewISM;
}

UInstancedStaticMeshComponent* UISMManager::GetOrCreateISMComponent(
    UStaticMesh* Mesh,
    const int32 NumCustomDataFloats,
    const FISMComponentParams& Params)
{
    if (!Mesh) return nullptr;

    const FISMMeshKey Key(Mesh);
    
    // 1. 先尝试在已有的桶组中查找（桶0），如果已有则直接返回（快速路径，线程安全）
    if (const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key))
    {
        if (Group->Buckets.Num() > 0 && Group->Buckets[0])
        {
            return Group->Buckets[0];
        }
    }

    // 2. 组件尚未创建，必须创建新的。
    // 如果在游戏线程，直接创建并注册（安全）
    if (IsInGameThread())
    {
        FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
        Group.NumCustomDataFloats = NumCustomDataFloats;
        Group.bDisableBuckets = Params.bDisableBuckets;
        if (Group.Buckets.Num() == 0) Group.Buckets.SetNum(1);
        UInstancedStaticMeshComponent* NewISM = CreateISMComponent(Mesh, NumCustomDataFloats, Params);
        Group.Buckets[0] = NewISM;
        return NewISM;
    }
    
    // 3. 在工作线程：将创建任务提交到游戏线程，并等待完成
    // 注意：Params 必须拷贝，因为其生命周期可能短于任务执行
    FISMComponentParams ParamsCopy = Params;
    const FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [this, Mesh, NumCustomDataFloats, ParamsCopy]()
        {
            // 在游戏线程执行，此时 IsInGameThread() == true，会进入上面 if 分支
            GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ParamsCopy);
        },
        TStatId(), nullptr, ENamedThreads::GameThread);

    // 等待游戏线程完成创建
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

    // 此时组件一定已经创建，直接返回即可
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (Group && Group->Buckets.Num() > 0 && Group->Buckets[0])
    {
        return Group->Buckets[0];
    }
    return nullptr; 
}

UInstancedStaticMeshComponent* UISMManager::GetISMComponent(UStaticMesh* Mesh) const
{
    if (!Mesh) return nullptr;
    const FISMMeshKey Key(Mesh);
    if (const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key))
    {
        if (Group->Buckets.Num() > 0 && Group->Buckets[0])
        {
            return Group->Buckets[0];
        }
    }
    return nullptr;
}

void UISMManager::UpdateInstanceCustomData(
    UStaticMesh* Mesh,
    const FGlobalInstanceId InstanceId,
    const int32 CustomDataIndex,
    const float Value)
{
    if (!Mesh || !InstanceId.IsValid()) return;

    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(InstanceId.BucketIndex)) return;

    UInstancedStaticMeshComponent* Bucket = Group->Buckets[InstanceId.BucketIndex];
    if (!Bucket) return;

    Bucket->SetCustomDataValueById(InstanceId.LocalId, CustomDataIndex, Value);
    DirtyBuckets.Add(Bucket);
}

DECLARE_CYCLE_STAT(TEXT("BatchAddInstances"), STAT_BatchAddInstances, STATGROUP_Game);
TArray<FGlobalInstanceId> UISMManager::BatchAddInstances(
    UStaticMesh* Mesh,
    const TArray<FTransform>& Transforms,
    const int32 NumCustomDataFloats,
    const FISMComponentParams& ISMParams,
    const TArray<float>& CustomData)
{
    SCOPE_CYCLE_COUNTER(STAT_BatchAddInstances);
    TArray<FGlobalInstanceId> AllGlobalIds;
    if (!Mesh || Transforms.Num() == 0) return AllGlobalIds;

    const int32 AddCount = Transforms.Num();
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    Group.NumCustomDataFloats = NumCustomDataFloats;

    // 如果禁用分桶，始终使用单桶一次性添加全部
    if (Group.bDisableBuckets)
    {
        UInstancedStaticMeshComponent* ISMComp = GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ISMParams);
        if (!ISMComp) return AllGlobalIds;

        TArray<FPrimitiveInstanceId> Ids = ISMComp->AddInstancesById(Transforms, true, false); 
        for (const FPrimitiveInstanceId& Id : Ids)
        {
            AllGlobalIds.Emplace(0, Id);   // 桶编号永远为 0
        }
        if (CustomData.Num() > 0)
        {
            const int32 Expected = Ids.Num() * NumCustomDataFloats;
            if (CustomData.Num() == Expected)
                ISMComp->SetCustomDataById(Ids, CustomData);
        }
        Group.TotalInstanceCount += AddCount;
        DirtyBuckets.Add(ISMComp);   // 添加脏桶，稍后统一刷新
        return AllGlobalIds;
    }
    
    const bool bHasBuckets = Group.Buckets.Num() > 0;
    const bool bShouldUseBuckets = (Group.TotalInstanceCount + AddCount) > INSTANCES_PER_BUCKET;

    // 场景1：尚未分桶且总数不超过一个桶 → 使用桶0
    if (!bHasBuckets && !bShouldUseBuckets)
    {
        UInstancedStaticMeshComponent* ISMComp = GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ISMParams); // 已安全
        if (!ISMComp) return AllGlobalIds;

        TArray<FPrimitiveInstanceId> Ids = ISMComp->AddInstancesById(Transforms, true);
        for (const FPrimitiveInstanceId& Id : Ids)
        {
            AllGlobalIds.Emplace(0, Id);
        }
        if (CustomData.Num() > 0)
        {
            const int32 Expected = Ids.Num() * NumCustomDataFloats;
            if (CustomData.Num() == Expected)
                ISMComp->SetCustomDataById(Ids, CustomData);
        }
        Group.TotalInstanceCount += AddCount;
        return AllGlobalIds;
    }

    // 场景2：启用分桶逻辑
    // 确保至少有桶0
    if (!GetOrCreateBucket(Mesh, 0, NumCustomDataFloats, ISMParams))
    {
        return AllGlobalIds;
    }

    const int32 StartGlobalIndex = Group.TotalInstanceCount;
    int32 CurrentGlobalIdx = StartGlobalIndex;
    int32 Processed = 0;

    while (Processed < AddCount)
    {
        int32 BucketIdx = CurrentGlobalIdx / INSTANCES_PER_BUCKET;
        const int32 LocalOffset = CurrentGlobalIdx % INSTANCES_PER_BUCKET;
        const int32 Remain = INSTANCES_PER_BUCKET - LocalOffset;
        const int32 SliceCount = FMath::Min(Remain, AddCount - Processed);

        // 安全获取或创建桶
        UInstancedStaticMeshComponent* Bucket = GetOrCreateBucket(Mesh, BucketIdx, NumCustomDataFloats, ISMParams);
        if (!Bucket) break; 

        TArray<FTransform> SliceTransforms(&Transforms[Processed], SliceCount);
        TArray<FPrimitiveInstanceId> SliceIds = Bucket->AddInstancesById(SliceTransforms, true);

        for (const FPrimitiveInstanceId& Id : SliceIds)
        {
            AllGlobalIds.Emplace(BucketIdx, Id);
        }

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

DECLARE_CYCLE_STAT(TEXT("BatchRemoveInstances"), STAT_BatchRemoveInstances, STATGROUP_Game);
void UISMManager::BatchRemoveInstances(
    UStaticMesh* Mesh,
    const TArray<FGlobalInstanceId>& InstanceIds)
{
    SCOPE_CYCLE_COUNTER(STAT_BatchRemoveInstances);
    if (!Mesh || InstanceIds.Num() == 0) return;

    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group) return;

    // 按桶分组
    TMap<int32, TArray<FPrimitiveInstanceId>> BucketMap;
    for (const FGlobalInstanceId& Gid : InstanceIds)
    {
        if (Gid.IsValid() && Group->Buckets.IsValidIndex(Gid.BucketIndex))
        {
            BucketMap.FindOrAdd(Gid.BucketIndex).Add(Gid.LocalId);
        }
    }

    for (auto& Pair : BucketMap)
    {
        if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[Pair.Key])
        {
            Bucket->RemoveInstancesById(Pair.Value);
        }
    }
}

void UISMManager::BatchSetInstanceCustomData(
    UStaticMesh* Mesh,
    const TArray<FGlobalInstanceId>& InstanceIds,
    const TArray<float>& CustomData) const
{
    if (!Mesh || InstanceIds.Num() == 0 || CustomData.Num() == 0) return;

    const FISMMeshKey Key(Mesh);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group) return;

    // 获取每个实例的自定义数据数量
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

    // 按桶分组
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
        {
            Data.Add(CustomData[Base + f]);
        }
    }

    for (auto& IdPair : IdGroups)
    {
        if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[IdPair.Key])
        {
            Bucket->SetCustomDataById(IdPair.Value, DataGroups[IdPair.Key]);
        }
    }
}

void UISMManager::FlushDirtyBuckets()
{
    for (UInstancedStaticMeshComponent* Bucket : DirtyBuckets)
    {
        if (Bucket)
        {
            Bucket->MarkRenderStateDirty();
        }
    }
    DirtyBuckets.Empty();
}

UInstancedStaticMeshComponent* UISMManager::GetOrCreateBucket(
    UStaticMesh* Mesh,
    int32 BucketIndex,
    int32 NumCustomDataFloats,
    const FISMComponentParams& Params)
{
    if (!Mesh) return nullptr;

    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    Group.NumCustomDataFloats = NumCustomDataFloats;

    // 确保桶数组容量足够
    if (BucketIndex >= Group.Buckets.Num())
    {
        // 在工作线程中可修改数组大小（TArray 的 SetNum 线程安全，但需注意）
        Group.Buckets.SetNum(BucketIndex + 1);
    }

    // 如果桶已经存在，直接返回
    if (Group.Buckets[BucketIndex])
    {
        return Group.Buckets[BucketIndex];
    }

    // 需要在游戏线程创建组件
    if (IsInGameThread())
    {
        UInstancedStaticMeshComponent* NewBucket = CreateISMComponent(Mesh, NumCustomDataFloats, Params);
        Group.Buckets[BucketIndex] = NewBucket;
        return NewBucket;
    }
    // 在工作线程：委托到游戏线程创建组件并等待完成
    const FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [this, Mesh, Key, BucketIndex, NumCustomDataFloats, Params]()
        {
            // 在游戏线程中调用自身，此时 IsInGameThread() 为 true
            GetOrCreateBucket(Mesh, BucketIndex, NumCustomDataFloats, Params);
        },
        TStatId(), nullptr, ENamedThreads::GameThread);
    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

    // 此时桶一定已经创建完成
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
        for (const auto& Bucket : Group.Buckets)
        {
            if (Bucket) ++ActiveBuckets;
        }
        TotalBuckets += ActiveBuckets;
        UE_LOG(LogTemp, Warning, TEXT("Mesh: %s, Total Instances: %d, Buckets: %d (Active)"),
            *Pair.Key.MeshPath.ToString(), Group.TotalInstanceCount, ActiveBuckets);
    }
    UE_LOG(LogTemp, Warning, TEXT("Total Active Buckets: %d"), TotalBuckets);
}