#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ISMManager.generated.h"

// 定义每个桶的实例容量
#define INSTANCES_PER_BUCKET 256

/**
 * 用于唯一标识一种 ISM 组件配置的键。
 * 
 * 设计意图：将相同静态网格体的实例合并到同一个 ISM 组件中渲染。
 * 使用 FSoftObjectPath 而非 UStaticMesh* 作为键，确保在资产重载或存档加载时键值稳定。
 */
USTRUCT(BlueprintType)
struct FISMMeshKey
{
	GENERATED_BODY()
	
	/** 静态网格体的软引用路径，作为唯一标识 */
	UPROPERTY()
	FSoftObjectPath MeshPath;
	
	/** 默认构造函数 */
	FISMMeshKey() = default;

	/** 从 FSoftObjectPath 显式构造 */
	explicit FISMMeshKey(const FSoftObjectPath& InMeshPath) : MeshPath(InMeshPath) {}

	/** 从 UStaticMesh* 显式构造（自动转换为软引用路径） */
	explicit FISMMeshKey(UStaticMesh* InMesh) : MeshPath(FSoftObjectPath(InMesh)) {}

	/** 相等比较，仅比较 MeshPath */
	bool operator==(const FISMMeshKey& Other) const
	{
		return MeshPath == Other.MeshPath;
	}
};

/** 哈希函数，基于 MeshPath 计算 */
FORCEINLINE uint32 GetTypeHash(const FISMMeshKey& Key)
{
	return GetTypeHash(Key.MeshPath);
}

/**
 * 全局唯一的 ISM 实例句柄
 * 由桶编号和桶内实例 ID 组成
 */
USTRUCT(BlueprintType)
struct FGlobalInstanceId
{
	GENERATED_BODY()

	/** 桶在 FISMBucketGroup::Buckets 中的索引 */
	UPROPERTY()
	int32 BucketIndex = INDEX_NONE;

	/** 桶内实例 ID（局部唯一） */
	FPrimitiveInstanceId LocalId;

	FGlobalInstanceId() = default;
	FGlobalInstanceId(const int32 InBucketIndex, const FPrimitiveInstanceId InId)
		: BucketIndex(InBucketIndex), LocalId(InId) {}

	bool IsValid() const { return BucketIndex != INDEX_NONE && LocalId.IsValid(); }

	bool operator==(const FGlobalInstanceId& Other) const
	{
		return BucketIndex == Other.BucketIndex && LocalId == Other.LocalId;
	}
};

FORCEINLINE uint32 GetTypeHash(const FGlobalInstanceId& Key)
{
	return HashCombine(GetTypeHash(Key.BucketIndex), GetTypeHash(Key.LocalId.Id));
}

/**
 * 用于管理一个Mesh下的所有ISM桶
 */
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
	GENERATED_BODY()
	
	/** 存储 ISM 组件的桶数组 */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Buckets;

	/** 总实例数量 */
	int32 TotalInstanceCount = 0;

	/** 记录自定义数据数量，所有桶共享 */
	int32 NumCustomDataFloats = 0;

	/** 禁用分桶，即使实例数超过桶容量也强制使用单一 ISM 组件 */
	bool bDisableBuckets = false;
};

/**
 * ISM 组件参数结构体。
 */
USTRUCT(BlueprintType)
struct FISMComponentParams
{
	GENERATED_BODY()

	/** 挂载父组件（留空则挂载到调用者指定的根） */
	UPROPERTY()
	TObjectPtr<USceneComponent> AttachParent = nullptr;
	
	/** 碰撞启用状态（NoCollision / QueryOnly / PhysicsOnly / QueryAndPhysics） */
	ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;

	/** 碰撞对象类型（当启用了查询碰撞时有效） */
	ECollisionChannel CollisionObjectType = ECC_WorldDynamic;

	/** 对所有通道的默认碰撞响应 */
	ECollisionResponse CollisionResponseToAllChannels = ECR_Ignore;

	/** 是否影响导航网格 */
	bool bCanEverAffectNavigation = false;

	/** 是否禁用 Nanite（轮廓高亮等特殊效果需要） */
	bool bDisallowNanite = false;

	/** 是否投射阴影 */
	bool bCastShadow = true;

	/** 最小绘制距离（0 表示不设置） */
	float MinDrawDistance = 0.f;

	/** 最大绘制距离（0 表示不设置） */
	float MaxDrawDistance = 0.f;

	/** 用于作物等需要大量批量替换实例的 Mesh，强制不分桶 */
	UPROPERTY()
	bool bDisableBuckets = false;
};

/**
 * ISM 组件管理器
 *  - 按 Mesh 获取/创建 ISM 组件
 *  - 提供批量实例操作（添加、删除、更新变换、设置自定义数据）
 */
UCLASS()
class PROJECTB_API UISMManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * 获取或创建指定 Mesh 的 ISM 组件
     * @param Mesh                静态网格体指针
     * @param NumCustomDataFloats 每个实例的自定义数据浮点数量
     * @param Params              ISM 组件参数
     * @return                    ISM 组件实例
     */
    UInstancedStaticMeshComponent* GetOrCreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);

    /**
     * 获取已存在的 ISM 组件（可能为空）
     * @param Mesh  静态网格体指针
     * @return      找到的 ISM 组件，否则为 nullptr
     */
    UInstancedStaticMeshComponent* GetISMComponent(UStaticMesh* Mesh) const;

    /**
     * 更新实例的单个自定义数据值。
     * @param Mesh                静态网格体指针
     * @param InstanceId      实例id
     * @param CustomDataIndex 自定义数据索引（从 0 开始，需小于 NumCustomDataFloats）
     * @param Value           新的浮点值
     */
    void UpdateInstanceCustomData(
		UStaticMesh* Mesh,
    	FGlobalInstanceId InstanceId,
    	int32 CustomDataIndex,
    	float Value
    	);
	
    /**
     * 批量添加实例，返回稳定 ID
     * @param Mesh                静态网格体指针
     * @param Transforms          世界空间变换数组
     * @param NumCustomDataFloats 每个实例的自定义数据浮点数量
     * @param ISMParams           ISM 组件参数（仅在 ISM 组件不存在时用于创建）
     * @param CustomData          扁平自定义数据数组，长度 = Transforms.Num() * NumCustomDataFloats
     * @return                    新创建实例的 ID 数组
     */
    TArray<FGlobalInstanceId> BatchAddInstances(
        UStaticMesh* Mesh,
        const TArray<FTransform>& Transforms,
        const int32 NumCustomDataFloats,
        const FISMComponentParams& ISMParams,
        const TArray<float>& CustomData = TArray<float>()
    );

    /**
     * 批量移除实例
     * @param Mesh                静态网格体指针
     * @param InstanceIds 要移除的实例 ID 数组
     */
    void BatchRemoveInstances(
		UStaticMesh* Mesh,
        const TArray<FGlobalInstanceId>& InstanceIds
    );

    /**
     * 批量设置实例自定义数据
     * @param Mesh        静态网格体指针
     * @param InstanceIds 实例 ID 数组
     * @param CustomData  扁平自定义数据数组，长度 = InstanceIds.Num() * NumCustomDataFloats
     */
    void BatchSetInstanceCustomData(
        UStaticMesh* Mesh,
        const TArray<FGlobalInstanceId>& InstanceIds,
        const TArray<float>& CustomData
    ) const;

	/** 刷新所有被标记为脏的 ISM 桶的渲染状态 */
	void FlushDirtyBuckets();

	UFUNCTION(BlueprintCallable)
	void DebugPrintBucketStats();

private:
    /**
     * 创建并初始化 ISM 组件
     * @param Mesh                静态网格体
     * @param NumCustomDataFloats 自定义数据浮点数量
     * @param Params              组件参数
     * @return                    已注册的 ISM 组件
     */
    static UInstancedStaticMeshComponent* CreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);

	UInstancedStaticMeshComponent* GetOrCreateBucket(UStaticMesh* Mesh, int32 BucketIndex, int32 NumCustomDataFloats,
												 const FISMComponentParams& Params);

    /** Mesh -> ISM 组件映射，使用软引用路径作为键 */
    UPROPERTY()
	TMap<FISMMeshKey, FISMBucketGroup> MeshToBucketGroups;

	/** 用于收集需要刷新渲染状态的桶 */
	UPROPERTY()
	TSet<TObjectPtr<UInstancedStaticMeshComponent>> DirtyBuckets;
};