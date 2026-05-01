// Fill out your copyright notice in the Description page of Project Settings.

#include "Subsystem/ISMPoolManager.h"
#include "Components/InstancedStaticMeshComponent.h"

void UISMPoolManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // 预分配容量以减少运行时扩容次数
    Slots.Reserve(2048);
    FreeSlotIndices.Reserve(2048);
}

void UISMPoolManager::Deinitialize()
{
    // 清理所有数据，ISM 组件会随 Owner 销毁而自动清理（因其挂载在外部传入的 AttachParent 所属 Actor 上）
    MeshKeyToData.Empty();
    Slots.Empty();
    FreeSlotIndices.Empty();
    Super::Deinitialize();
}

void UISMPoolManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    FlushPendingTransforms();

    // 每 5 秒检查一次是否需要清理
    TimeSinceLastCompact += DeltaTime;
    if (TimeSinceLastCompact >= 5.0f)
    {
        TimeSinceLastCompact = 0.0f;
        // 当空闲槽位超过总数的 20% 时，触发清理
        if (Slots.Num() > 0 && static_cast<float>(FreeSlotIndices.Num()) / Slots.Num() > 0.2f)
        {
            Compact();
        }
    }
}

TStatId UISMPoolManager::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UISMPoolManager, STATGROUP_Tickables);
}

FVector UISMPoolManager::QuantizeLocation(const FVector& Loc, float GridSize)
{
    // 将位置舍入到指定网格大小，避免浮点误差导致 TMap 查找失败
    return FVector(
        FMath::RoundToFloat(Loc.X / GridSize) * GridSize,
        FMath::RoundToFloat(Loc.Y / GridSize) * GridSize,
        FMath::RoundToFloat(Loc.Z / GridSize) * GridSize
    );
}

void UISMPoolManager::FlushPendingTransforms()
{
     if (PendingTransforms.Num() == 0) return;

    // 按 ISMComponent 分组
    TMap<UInstancedStaticMeshComponent*, TArray<int32>> ComponentToPendingIndices;
    for (int32 i = 0; i < PendingTransforms.Num(); ++i)
    {
        ComponentToPendingIndices.FindOrAdd(PendingTransforms[i].ISMComponent).Add(i);
    }

    for (auto& Pair : ComponentToPendingIndices)
    {
        UInstancedStaticMeshComponent* ISMComp = Pair.Key;
        TArray<int32>& PendingIndices = Pair.Value;

        // 构建待更新的索引和变换对，并排序以便批量提交
        TArray<TPair<int32, FTransform>> SortedUpdates;
        for (const int32 PendingIdx : PendingIndices)
        {
            const FPendingTransform& Update = PendingTransforms[PendingIdx];
            int32 InstanceIndex = ISMComp->GetInstanceIndexForId(Update.InstanceId);
            if (InstanceIndex != INDEX_NONE)
            {
                SortedUpdates.Add(TPair<int32, FTransform>(InstanceIndex, Update.NewTransform));
            }
        }

        // 按索引排序，便于找出连续段
        SortedUpdates.Sort([](const TPair<int32, FTransform>& A, const TPair<int32, FTransform>& B)
        {
            return A.Key < B.Key;
        });

        // 批量提交连续段
        for (int32 i = 0; i < SortedUpdates.Num();)
        {
            const int32 StartIndex = SortedUpdates[i].Key;
            int32 Count = 1;

            // 收集连续变换
            TArray<FTransform> Transforms;
            Transforms.Add(SortedUpdates[i].Value);

            // 尝试向后扩展连续段
            int32 j = i + 1;
            while (j < SortedUpdates.Num() && SortedUpdates[j].Key == StartIndex + Count)
            {
                Transforms.Add(SortedUpdates[j].Value);
                ++Count;
                ++j;
            }

            // 批量更新这个连续段（最后一个段才标记脏，这里统一最后标记）
            ISMComp->BatchUpdateInstancesTransforms(StartIndex, Transforms);

            i = j;
        }
    }

    // 统一标记渲染状态为脏
    for (const auto& Pair : ComponentToPendingIndices)
    {
        Pair.Key->MarkRenderStateDirty();
    }

    PendingTransforms.Empty();
    bHasPendingTransforms = false;
}

void UISMPoolManager::Compact()
{
    for (auto& Pair : MeshKeyToData)
    {
        FISMPerMeshData& Data = Pair.Value;
        UInstancedStaticMeshComponent* ISMComp = Data.ISMComponent;

        // 1. 收集所有属于该 ISM 组件的空闲物理实例 ID
        TArray<FPrimitiveInstanceId> IdsToRemove;
        for (const int32 SlotIndex : FreeSlotIndices)
        {
            FISMInstanceSlot& Slot = Slots[SlotIndex];
            if (Slot.ISMComponent == ISMComp && Slot.PhysicalId.IsValid() && Slot.bIsFree)
            {
                IdsToRemove.Add(Slot.PhysicalId);
                
                // 2. 重置槽位状态，让其可被完全复用
                Slot.PhysicalId = FPrimitiveInstanceId{};
                Slot.LastTransform = FTransform::Identity;
                Slot.SavedCustomData.Empty();
            }
        }

        // 3. 执行真正的移除操作
        if (IdsToRemove.Num() > 0)
        {
            ISMComp->RemoveInstancesById(IdsToRemove);
            UE_LOG(LogTemp, Log, TEXT("[ISMPoolManager] Compact: Removed %d idle instances from [%s]."), IdsToRemove.Num(), *Pair.Key.MeshPath.ToString());
        }
    }
}

UInstancedStaticMeshComponent* UISMPoolManager::CreateISMComponent(UStaticMesh* Mesh, const int32 NumCustomDataFloats, const FISMComponentParams& ISMParams)
{
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMPoolManager::CreateISMComponent - Invalid Mesh"));
        return nullptr;
    }
    if (!ISMParams.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMPoolManager::CreateISMComponent - AttachParent is null"));
        return nullptr;
    }

    // 动态创建 ISM 组件，标记为 Transient 以避免被序列化到关卡中
    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(ISMParams.AttachParent, NAME_None, RF_Transient);
    NewISM->SetStaticMesh(Mesh);
    NewISM->SetCollisionEnabled(ISMParams.CollisionEnabled);
    NewISM->SetCollisionObjectType(ISMParams.CollisionObjectType);
    NewISM->SetCollisionResponseToAllChannels(ISMParams.CollisionResponseToAllChannels);
    NewISM->SetCanEverAffectNavigation(ISMParams.bCanEverAffectNavigation);
    NewISM->bDisallowNanite = ISMParams.bDisallowNanite;
    NewISM->SetCastShadow(ISMParams.bCastShadow);
    if (ISMParams.MinDrawDistance > 0.f || ISMParams.MaxDrawDistance > 0.f)
    {
        NewISM->SetCullDistances(ISMParams.MinDrawDistance, ISMParams.MaxDrawDistance);
    }
    NewISM->SetNumCustomDataFloats(NumCustomDataFloats);
    NewISM->SetupAttachment(ISMParams.AttachParent);
    NewISM->RegisterComponent();

    return NewISM;
}

UISMPoolManager::FISMPerMeshData& UISMPoolManager::GetOrCreatePerMeshData(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& ISMParams)
{
    // 尝试查找已有数据
    if (FISMPerMeshData* Existing = MeshKeyToData.Find(FISMMeshKey(Mesh)))
    {
        return *Existing;
    }

    // 新建数据并插入映射表
    FISMPerMeshData NewData;
    NewData.ISMComponent = CreateISMComponent(Mesh, NumCustomDataFloats, ISMParams);
    NewData.NumCustomDataFloats = NumCustomDataFloats;
    return MeshKeyToData.Add(FISMMeshKey(Mesh), MoveTemp(NewData));
}

int32 UISMPoolManager::AllocateSlot()
{
    // 优先从空闲列表中取用
    if (FreeSlotIndices.Num() > 0)
    {
        return FreeSlotIndices.Pop(EAllowShrinking::No);
    }
    // 否则扩展数组
    return Slots.Emplace();
}

void UISMPoolManager::ShowInstance(FISMInstanceSlot& Slot, const FTransform& Transform, const TArrayView<const float> CustomData)
{
    if (!Slot.ISMComponent) return;

    if (Slot.PhysicalId.IsValid())
    {
        // 延迟提交变换更新
        PendingTransforms.Add({ Slot.ISMComponent, Slot.PhysicalId, Transform });
        bHasPendingTransforms = true;
        if (CustomData.Num() > 0)
        {
            Slot.ISMComponent->SetCustomDataById(Slot.PhysicalId, CustomData);
        }
    }
    else
    {
        // 首次创建物理实例，必须立即执行（无法延迟，因为需要返回 ID）
        const FPrimitiveInstanceId NewId = Slot.ISMComponent->AddInstanceById(Transform);
        Slot.PhysicalId = NewId;
        if (CustomData.Num() > 0)
        {
            Slot.ISMComponent->SetCustomDataById(NewId, CustomData);
        }
    }

    Slot.LastTransform = Transform;
    Slot.SavedCustomData = CustomData;
}

void UISMPoolManager::HideInstance(const FISMInstanceSlot& Slot)
{
    if (Slot.ISMComponent && Slot.PhysicalId.IsValid())
    {
        FTransform HiddenTransform = Slot.LastTransform;
        HiddenTransform.SetScale3D(FVector::ZeroVector);
        
        PendingTransforms.Add({ Slot.ISMComponent, Slot.PhysicalId, HiddenTransform });
        bHasPendingTransforms = true;
    }
}

void UISMPoolManager::AcquireInstance(UStaticMesh* Mesh, USceneComponent* AttachParent, const FTransform& Transform, int32 NumCustomDataFloats, TArrayView<const float> CustomData)
{
    SCOPE_CYCLE_COUNTER(STAT_ISMPoolManager_AcquireInstance);
    if (!Mesh) return;

    FISMPerMeshData& Data = GetOrCreatePerMeshData(Mesh, NumCustomDataFloats, FISMComponentParams());
    const FVector QuantizedLoc = QuantizeLocation(Transform.GetLocation());

    // 防止同一位置重复申请
    if (Data.LocationToSlotIndex.Contains(QuantizedLoc))
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMPoolManager: Instance at location %s already exists for this MeshKey"), *QuantizedLoc.ToString());
        return;
    }

    // 分配槽位
    const int32 SlotIndex = AllocateSlot();
    FISMInstanceSlot& Slot = Slots[SlotIndex];

    if (Slot.ISMComponent)
    {
        if (Slot.ISMComponent->GetStaticMesh() != Mesh)
        {
            // 不同 Mesh，物理 ID 无效，重置相关状态
            Slot.PhysicalId = FPrimitiveInstanceId();
            Slot.LastTransform = FTransform::Identity;
            Slot.SavedCustomData.Empty();
        }
    }
    
    Slot.ISMComponent = Data.ISMComponent;
    Slot.bIsFree = false;

    // 显示实例
    ShowInstance(Slot, Transform, CustomData);

    // 建立位置→槽位映射
    Data.LocationToSlotIndex.Add(QuantizedLoc, SlotIndex);
}

void UISMPoolManager::ReleaseInstance(UStaticMesh* Mesh, const FTransform& Transform)
{
    SCOPE_CYCLE_COUNTER(STAT_ISMPoolManager_ReleaseInstance);
    if (!Mesh) return;

    FISMPerMeshData* Data = MeshKeyToData.Find(FISMMeshKey(Mesh));
    if (!Data) return;

    const FVector QuantizedLoc = QuantizeLocation(Transform.GetLocation());
    const int32* SlotIndexPtr = Data->LocationToSlotIndex.Find(QuantizedLoc);
    if (!SlotIndexPtr) return;

    const int32 SlotIndex = *SlotIndexPtr;
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].bIsFree) return;

    FISMInstanceSlot& Slot = Slots[SlotIndex];

    // 判断是否为“跨 Mesh 释放”：槽位记录的 ISM 组件与本次传入 Mesh 对应的组件不同
    if (Slot.ISMComponent != Data->ISMComponent)
    {
        // 彻底移除物理实例
        if (Slot.ISMComponent && Slot.PhysicalId.IsValid())
        {
            Slot.ISMComponent->RemoveInstanceById(Slot.PhysicalId);
        }
        // 重置槽位状态
        Slot.ISMComponent = nullptr;
        Slot.PhysicalId = FPrimitiveInstanceId();
        Slot.LastTransform = FTransform::Identity;
        Slot.SavedCustomData.Empty();
    }
    else
    {
        // 同 Mesh 复用：零缩放隐藏
        HideInstance(Slot);
    }
    
    Slot.bIsFree = true;
    

    // 回收槽位索引，并移除位置映射
    FreeSlotIndices.Add(SlotIndex);

    Data->LocationToSlotIndex.Remove(QuantizedLoc);
}

void UISMPoolManager::UpdateInstanceTransform(UStaticMesh* Mesh, const FTransform& OldTransform, const FTransform& NewTransform)
{
    if (!Mesh) return;

    FISMPerMeshData* Data = MeshKeyToData.Find(FISMMeshKey(Mesh));
    if (!Data) return;

    const FVector OldLoc = QuantizeLocation(OldTransform.GetLocation());
    const int32* SlotIndexPtr = Data->LocationToSlotIndex.Find(OldLoc);
    if (!SlotIndexPtr) return;

    const int32 SlotIndex = *SlotIndexPtr;
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].bIsFree) return;

    FISMInstanceSlot& Slot = Slots[SlotIndex];
    Slot.ISMComponent->UpdateInstanceTransformById(Slot.PhysicalId, NewTransform);
    Slot.LastTransform = NewTransform;

    // 如果位置发生变化，更新映射表
    const FVector NewLoc = QuantizeLocation(NewTransform.GetLocation());
    if (OldLoc != NewLoc)
    {
        Data->LocationToSlotIndex.Remove(OldLoc);
        Data->LocationToSlotIndex.Add(NewLoc, SlotIndex);
    }
}

void UISMPoolManager::UpdateInstanceCustomData(UStaticMesh* Mesh, const FTransform& Transform, const int32 CustomDataIndex, const float Value)
{
    if (!Mesh) return;

    FISMPerMeshData* Data = MeshKeyToData.Find(FISMMeshKey(Mesh));
    if (!Data) return;

    const FVector QuantizedLoc = QuantizeLocation(Transform.GetLocation());
    const int32* SlotIndexPtr = Data->LocationToSlotIndex.Find(QuantizedLoc);
    if (!SlotIndexPtr) return;

    const int32 SlotIndex = *SlotIndexPtr;
    if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].bIsFree) return;

    FISMInstanceSlot& Slot = Slots[SlotIndex];
    Slot.ISMComponent->SetCustomDataValueById(Slot.PhysicalId, CustomDataIndex, Value);

    // 同步更新本地副本，以便恢复时使用
    if (Slot.SavedCustomData.Num() > CustomDataIndex)
    {
        Slot.SavedCustomData[CustomDataIndex] = Value;
    }
}

void UISMPoolManager::UpdateInstanceCustomData(UStaticMesh* Mesh, int32 InstanceIndex, int32 CustomDataIndex,
    float Value)
{
    if (!Mesh) return;

    const FISMPerMeshData* Data = MeshKeyToData.Find(FISMMeshKey(Mesh));
    if (!Data) return;
    
    Data->ISMComponent->SetCustomDataValue(InstanceIndex, CustomDataIndex, Value);

}

bool UISMPoolManager::HasInstance(UStaticMesh* Mesh, const FTransform& Transform)
{
    if (!Mesh) return false;

    const FISMPerMeshData* Data = MeshKeyToData.Find(FISMMeshKey(Mesh));
    if (!Data) return false;

    const FVector QuantizedLoc = QuantizeLocation(Transform.GetLocation());
    return Data->LocationToSlotIndex.Contains(QuantizedLoc);
}

TArray<FPrimitiveInstanceId> UISMPoolManager::BatchAddInstances(UStaticMesh* Mesh, const TArray<FTransform>& Transforms,
    const int32 NumCustomDataFloats,const FISMComponentParams& ISMParams,
    const TArray<float>& CustomData)
{
    TArray<FPrimitiveInstanceId> InstanceIds;

    const FISMPerMeshData& Data = GetOrCreatePerMeshData(Mesh, NumCustomDataFloats, ISMParams);
    const TObjectPtr<UInstancedStaticMeshComponent> ISMComp = Data.ISMComponent;
    if (!ISMComp || Transforms.Num() == 0) return InstanceIds;

    // 批量创建实例，返回稳定 ID
    InstanceIds = ISMComp->AddInstancesById(Transforms, true);

    // 如果需要设置自定义数据
    if (CustomData.Num() > 0)
    {
        const int32 ExpectedCount = InstanceIds.Num() * NumCustomDataFloats;
        if (CustomData.Num() == ExpectedCount)
        {
            ISMComp->SetCustomDataById(InstanceIds, CustomData);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("BatchAddInstances: CustomData size mismatch. Expected %d, got %d"),
                ExpectedCount, CustomData.Num());
        }
    }

    return InstanceIds;
}

bool UISMPoolManager::LineTraceISMInstances(UStaticMesh* Mesh, FHitResult& OutHit, const FVector& Start,
    const FVector& End, const FCollisionQueryParams& Params)
{
    if (const FISMPerMeshData* Existing = MeshKeyToData.Find(FISMMeshKey(Mesh)))
    {
        const TObjectPtr<UInstancedStaticMeshComponent> ISMComp = Existing->ISMComponent;
        return ISMComp && ISMComp->LineTraceComponent(OutHit, Start, End, Params);
    }
   
    return false;
}
