#include "Subsystem/ISMManager.h"
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

    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    // 启用引擎原生增量渲染（AddInstancesById/RemoveInstancesById 增量路径）
    NewISM->bUseIncrementalISM = Params.bUseIncrementalISM;

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

    // 引擎原生增量接口（SetCustomDataValueById 本身即增量路径，无需额外分支）
    Bucket->SetCustomDataValueById(InstanceId.LocalId, CustomDataIndex, Value);
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