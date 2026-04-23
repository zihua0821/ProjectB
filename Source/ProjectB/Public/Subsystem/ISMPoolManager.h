// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ISMPoolManager.generated.h"
DECLARE_CYCLE_STAT(TEXT("UISMPoolManager - AcquireInstance"), STAT_ISMPoolManager_AcquireInstance, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("UISMPoolManager - ReleaseInstance"), STAT_ISMPoolManager_ReleaseInstance, STATGROUP_Game);
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
 * ISM 实例对象池管理器（WorldSubsystem）
 * 
 * 功能：
 * - 随 UWorld 自动创建和销毁，提供全局统一的 ISM 实例管理。
 * - 按静态网格体（UStaticMesh*）自动创建或复用 ISM 组件。
 * - 提供实例的申请、释放、变换更新及自定义数据更新接口。
 * - 采用“零缩放隐藏”策略实现实例释放，避免频繁增删导致的 ISM 内部数组重排。
 * - 通过量化位置（Location）建立 Transform 到内部槽位的映射，对外屏蔽物理索引变化。
 * 
 * 使用方式：
 *    UISMPoolManager* Manager = GetWorld()->GetSubsystem<UISMPoolManager>();
 *    Manager->AcquireInstance(Mesh, AttachParent, Transform, NumCustomDataFloats, CustomData);
 *    Manager->ReleaseInstance(Mesh, Transform);
 */
UCLASS()
class PROJECTB_API UISMPoolManager : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End USubsystem Interface
	
	/**
	 * 申请一个实例。
	 * 
	 * 若指定 Mesh 对应的 ISM 组件尚不存在，则自动创建并挂载到 AttachParent 下。
	 * 实例初始以给定的 Transform 显示。如果该位置已存在同 Mesh 的有效实例，则操作被忽略并输出警告。
	 * 
	 * @param Mesh                静态网格体指针（必须非空）。
	 * @param AttachParent        用于挂载新 ISM 组件的父级组件（当需要创建 ISM 时使用）。
	 * @param Transform           实例的初始变换（仅 Location 用于内部定位，Rotation 和 Scale 正常应用）。
	 * @param NumCustomDataFloats 每个实例的自定义浮点数据数量（仅在首次创建 ISM 组件时生效）。
	 * @param CustomData          可选的自定义数据数组，长度应为 NumCustomDataFloats。
	 */
	void AcquireInstance(UStaticMesh* Mesh, USceneComponent* AttachParent, const FTransform& Transform, int32 NumCustomDataFloats, TArrayView<const float> CustomData = TArrayView<const float>());
	
    /**
     * 释放一个实例。
     * 
     * 采用“零缩放隐藏”策略：将实例的缩放设为零使其不可见，内部槽位标记为空闲，供后续复用。
     * 注意：实例并未从 ISM 组件中真正移除，因此 Transform 位置仍被占用，不可重复申请。
     * 
     * @param Mesh         静态网格体指针（必须与申请时一致）。
     * @param Transform    用于定位实例的变换（仅 Location 参与查找，需与申请时的 Location 量化后一致）。
     */
	void ReleaseInstance(UStaticMesh* Mesh, const FTransform& Transform);

    /**
     * 更新实例的变换。
     * 
     * 通过旧变换定位实例，将其变换更新为新变换。若新旧位置不同，内部映射表会同步更新。
     * 
     * @param Mesh          静态网格体指针。
     * @param OldTransform  用于定位实例的旧变换（仅 Location 参与查找）。
     * @param NewTransform  新的变换。
     */
	void UpdateInstanceTransform(UStaticMesh* Mesh, const FTransform& OldTransform, const FTransform& NewTransform);
	
    /**
     * 更新实例的单个自定义数据值。
     * 
     * @param Mesh            静态网格体指针。
     * @param Transform       用于定位实例的变换（仅 Location 参与查找）。
     * @param CustomDataIndex 自定义数据索引（从 0 开始，需小于 NumCustomDataFloats）。
     * @param Value           新的浮点值。
     */
	void UpdateInstanceCustomData(UStaticMesh* Mesh, const FTransform& Transform, int32 CustomDataIndex, float Value);

    /**
     * 检查指定位置是否存在有效（非空闲）的实例。
     * 
     * @param Mesh          静态网格体指针。
     * @param Transform     用于定位实例的变换（仅 Location 参与查找）。
     * @return              true 表示存在有效实例。
     */
	bool HasInstance(UStaticMesh* Mesh, const FTransform& Transform);

protected:
    /**
     * 实例槽位信息。
     * 每个槽位代表一个逻辑实例，可被复用。
     */
    struct FISMInstanceSlot
    {
        /** 所属的 ISM 组件 */
        TObjectPtr<UInstancedStaticMeshComponent> ISMComponent = nullptr;

        /** ISM 内部返回的稳定物理句柄，用于后续操作 */
        FPrimitiveInstanceId PhysicalId;

        /** 最后一次设置的完整变换（用于隐藏后恢复，或避免查询 ISM） */
        FTransform LastTransform;

        /** 自定义数据副本，用于恢复时还原 */
        TArray<float> SavedCustomData;

        /** 是否空闲 */
        bool bIsFree = true;
    };

    /**
     * 每个 MeshKey 对应的数据。
     * 管理一个 ISM 组件及其上所有实例槽位的映射。
     */
    struct FISMPerMeshData
    {
        /** 实际的 ISM 组件实例 */
        TObjectPtr<UInstancedStaticMeshComponent> ISMComponent = nullptr;

        /** 创建时指定的自定义数据数量，用于一致性检查 */
    	int32 NumCustomDataFloats = 0;

        /**
         * 量化后的位置 → 槽位索引的映射。
         * 用于通过 Transform 快速定位实例。
         */
        TMap<FVector, int32> LocationToSlotIndex;
    };

    /**
     * 获取或创建指定 Mesh 对应的 FISMPerMeshData。
     * 若不存在，则调用 CreateISMComponent 创建 ISM 组件并初始化数据。
     */
    FISMPerMeshData& GetOrCreatePerMeshData(UStaticMesh* Mesh, USceneComponent* AttachParent, int32 NumCustomDataFloats);

    /**
     * 创建并初始化一个 ISM 组件。
     * 
     * @param Mesh              静态网格体。
     * @param AttachParent      挂载父级。
     * @param NumCustomDataFloats 自定义数据数量。
     * @return                  已注册的 ISM 组件，失败返回 nullptr。
     */
	static UInstancedStaticMeshComponent* CreateISMComponent(UStaticMesh* Mesh, USceneComponent* AttachParent, int32 NumCustomDataFloats);

    /**
     * 分配一个空闲槽位。
     * 优先从 FreeSlotIndices 中取用，若无空闲则新建槽位。
     * @return 槽位索引。
     */
    int32 AllocateSlot();

    /**
     * 显示实例（恢复变换或首次创建物理实例）。
     * 
     * @param Slot        目标槽位。
     * @param Transform   要应用的变换。
     * @param CustomData  自定义数据。
     */
	void ShowInstance(FISMInstanceSlot& Slot, const FTransform& Transform, TArrayView<const float> CustomData);

    /**
     * 隐藏实例（零缩放隐藏）。
     * 将实例的缩放设为零，并依赖 LastTransform 保存原变换。
     * 
     * @param Slot 目标槽位（只读，但会通过 ISMComponent 修改物理实例）。
     */
	void HideInstance(const FISMInstanceSlot& Slot);

    /**
     * 位置量化，用于消除浮点数精度误差导致的键值不匹配。
     * 
     * @param Loc       原始位置。
     * @param GridSize  量化网格大小（默认 0.1 单位，即厘米精度）。
     * @return          量化后的位置。
     */
    static FVector QuantizeLocation(const FVector& Loc, float GridSize = 0.1f);
	
	/**
	 * 批量提交所有待处理的实例变换
	 */
	void FlushPendingTransforms();
private:
    /** MeshKey → 对应数据（ISM 组件及槽位映射） */
    TMap<FISMMeshKey, FISMPerMeshData> MeshKeyToData;

    /** 所有实例槽位数组，索引即为逻辑实例 ID（对外不可见） */
    TArray<FISMInstanceSlot> Slots;

    /** 空闲槽位索引池，用于快速分配 */
    TArray<int32> FreeSlotIndices;

	/** 待处理的变换更新请求 */
	struct FPendingTransform
	{
		TObjectPtr<UInstancedStaticMeshComponent> ISMComponent;
		FPrimitiveInstanceId InstanceId;
		FTransform NewTransform;
	};
	TArray<FPendingTransform> PendingTransforms;

	/** 是否有未处理的变换请求 */
	bool bHasPendingTransforms = false;
};