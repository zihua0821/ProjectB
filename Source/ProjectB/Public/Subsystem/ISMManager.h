// ISMManager.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ISMManager.generated.h"

// 定义每个桶的实例容量
#define INSTANCES_PER_BUCKET 2048

/**
 * 用于唯一标识一种 ISM 组件配置的键。
 * 基于 SoftObjectPath，保证在资源重定向后仍能正确匹配。
 */
USTRUCT(BlueprintType)
struct FISMMeshKey
{
    GENERATED_BODY()
    
    UPROPERTY()
    FSoftObjectPath MeshPath;
    
    FISMMeshKey() = default;
    explicit FISMMeshKey(const FSoftObjectPath& InMeshPath) : MeshPath(InMeshPath) {}
    explicit FISMMeshKey(UStaticMesh* InMesh) : MeshPath(FSoftObjectPath(InMesh)) {}
    bool operator==(const FISMMeshKey& Other) const { return MeshPath == Other.MeshPath; }
};

FORCEINLINE uint32 GetTypeHash(const FISMMeshKey& Key) { return GetTypeHash(Key.MeshPath); }

/**
 * 全局唯一的 ISM 实例句柄
 */
USTRUCT(BlueprintType)
struct FGlobalInstanceId
{
    GENERATED_BODY()

    UPROPERTY()
    int32 BucketIndex = INDEX_NONE;

    FPrimitiveInstanceId LocalId;

    FGlobalInstanceId() = default;
    FGlobalInstanceId(const int32 InBucketIndex, const FPrimitiveInstanceId InId)
        : BucketIndex(InBucketIndex), LocalId(InId) {}

    bool IsValid() const { return BucketIndex != INDEX_NONE && LocalId.IsValid(); }
    bool operator==(const FGlobalInstanceId& Other) const { return BucketIndex == Other.BucketIndex && LocalId == Other.LocalId; }
};

FORCEINLINE uint32 GetTypeHash(const FGlobalInstanceId& Key) { return HashCombine(GetTypeHash(Key.BucketIndex), GetTypeHash(Key.LocalId.Id)); }

/**
 * ISM 组件参数结构体。
 */
USTRUCT(BlueprintType)
struct FISMComponentParams
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<USceneComponent> AttachParent = nullptr;
    
    ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
    ECollisionChannel CollisionObjectType = ECC_WorldDynamic;
    ECollisionResponse CollisionResponseToAllChannels = ECR_Ignore;
    bool bCanEverAffectNavigation = false;
    bool bDisallowNanite = false;
    bool bCastShadow = true;
    float MinDrawDistance = 0.f;
    float MaxDrawDistance = 0.f;

    // 强制不分桶
    UPROPERTY()
    bool bDisableBuckets = false;

    // 是否创建增量 ISM 组件（UIncrementalInstancedStaticMeshComponent）
    UPROPERTY()
    bool bUseIncrementalISM = false;
};

/**
 * 用于管理一个Mesh下的所有ISM桶
 */
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
    GENERATED_BODY()
    
    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Buckets;

    int32 TotalInstanceCount = 0;
    int32 NumCustomDataFloats = 0;
    bool bDisableBuckets = false;

    // 缓存组件创建参数，用于扩容等
    FISMComponentParams CachedParams;

    // 自动分桶阈值，当实例总数超过此值且未禁用分桶时，自动启用分桶。0 表示使用默认 INSTANCES_PER_BUCKET 逻辑
    int32 BucketingThreshold = 0;
};

/**
 * ISM 组件管理器
 */
UCLASS()
class PROJECTB_API UISMManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ---- 原有接口（保持不变） ----
    UInstancedStaticMeshComponent* GetOrCreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);
    UInstancedStaticMeshComponent* GetISMComponent(UStaticMesh* Mesh) const;
    void UpdateInstanceCustomData(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, int32 CustomDataIndex, float Value);

    TArray<FGlobalInstanceId> BatchAddInstances(
        UStaticMesh* Mesh, const TArray<FTransform>& Transforms, const int32 NumCustomDataFloats,
        const FISMComponentParams& ISMParams, const TArray<float>& CustomData = TArray<float>());
    void BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds);
    void BatchSetInstanceCustomData(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<float>& CustomData) const;
    UFUNCTION(BlueprintCallable)
    void DebugPrintBucketStats();

    // ---- 对象池接口（仅支持 bUseIncrementalISM = true 的组件） ----
    FGlobalInstanceId AcquireSlot(UStaticMesh* Mesh, const FTransform& WorldTransform, TArrayView<const float> CustomData = TArrayView<const float>());
    void ReleaseSlot(UStaticMesh* Mesh, const FGlobalInstanceId& InstanceId);
    void SetBatchExpandSize(int32 NewSize) { BatchExpandSize = FMath::Max(1, NewSize); }
    void SetBucketingThreshold(int32 Threshold) { BucketingThreshold = FMath::Max(0, Threshold); }
    void PreAllocateSlots(UStaticMesh* Mesh, int32 Count);
    void FlushAllIncrementalComponents();

private:
    static UInstancedStaticMeshComponent* CreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);
    UInstancedStaticMeshComponent* GetOrCreateBucket(UStaticMesh* Mesh, int32 BucketIndex, int32 NumCustomDataFloats, const FISMComponentParams& Params);
    void ExpandCapacity(UStaticMesh* Mesh, int32 AdditionalCount);

    UPROPERTY()
    TMap<FISMMeshKey, FISMBucketGroup> MeshToBucketGroups;

    // 本帧被触碰过的增量 ISM 组件（用于精准 Flush）
    TSet<TObjectPtr<UInstancedStaticMeshComponent>> DirtyIncrementalComponents;

    // 空闲槽位池：MeshKey → 空闲的 FGlobalInstanceId 队列
    TMap<FISMMeshKey, TUniquePtr<TQueue<FGlobalInstanceId, EQueueMode::Mpsc>>> FreeSlotIndices;

    int32 BatchExpandSize = 512;
    int32 BucketingThreshold = 4096;
};
