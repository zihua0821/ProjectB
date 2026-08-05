// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NaniteSceneProxy.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/InstancedStaticMesh.h"
#include "IncrementalInstancedStaticMeshComponent.generated.h"

class UIncrementalInstancedStaticMeshComponent;
// ============================================================================
// 脏实例数据结构
// ============================================================================
struct FDirtyInstance
{
    int32 InstanceIndex = INDEX_NONE;
    FTransform NewTransform;
    TArray<float> NewCustomData;
    bool bUpdateTransform = false;
    bool bUpdateCustomData = false;
};

// ============================================================================
// 脏实例队列 — 聚合两个 SceneProxy 中重复的队列操作
// ============================================================================
struct FDirtyInstanceQueue
{
    TQueue<FDirtyInstance, EQueueMode::Mpsc> DirtyInstances;

    void AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform)
    {
        FDirtyInstance Dirty;
        Dirty.InstanceIndex = InstanceIndex;
        Dirty.NewTransform = NewTransform;
        Dirty.bUpdateTransform = true;
        DirtyInstances.Enqueue(Dirty);
    }

    void AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData)
    {
        FDirtyInstance Dirty;
        Dirty.InstanceIndex = InstanceIndex;
        Dirty.NewCustomData.Append(NewCustomData.GetData(), NewCustomData.Num());
        Dirty.bUpdateCustomData = true;
        DirtyInstances.Enqueue(Dirty);
    }

    bool IsEmpty() const { return DirtyInstances.IsEmpty(); }
};

// ============================================================================
// 自定义非 Nanite SceneProxy
// ============================================================================
class FIncrementalSceneProxy : public FInstancedStaticMeshSceneProxy
{
public:
    FIncrementalSceneProxy(UIncrementalInstancedStaticMeshComponent* InComponent,
        const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& InInstanceDataProxy);

    virtual void UpdateInstances_RenderThread(
        FRHICommandListBase& RHICmdList,
        const FBoxSphereBounds& InBounds,
        const FBoxSphereBounds& InLocalBounds,
        const FBoxSphereBounds& InStaticMeshBounds) override;

    void AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform);

    void AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData);

    void TriggerIncrementalUpdate(FRHICommandListBase& RHICmdList);

private:
    FDirtyInstanceQueue DirtyQueue;

    TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> CustomInstanceDataProxy;
};

// ============================================================================
// 自定义 Nanite SceneProxy
// ============================================================================
class FIncrementalNaniteSceneProxy : public Nanite::FSceneProxy
{
public:
    FIncrementalNaniteSceneProxy(
        const Nanite::FMaterialAudit& MaterialAudit,
        UIncrementalInstancedStaticMeshComponent* InComponent);

    virtual void UpdateInstances_RenderThread(
        FRHICommandListBase& RHICmdList,
        const FBoxSphereBounds& InBounds,
        const FBoxSphereBounds& InLocalBounds,
        const FBoxSphereBounds& InStaticMeshBounds) override;

    void AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform);
    
    void AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData);

    void TriggerIncrementalUpdate(FRHICommandListBase& RHICmdList);

private:
    FDirtyInstanceQueue DirtyQueue;
};

/**
 * 支持增量式更新的 ISM 组件。
 *
 * 常规的 UInstancedStaticMeshComponent 在每次修改实例数据时需调用 MarkRenderStateDirty()
 * 触发全量 GPU 场景重建。此类通过自定义 SceneProxy 和渲染线程脏队列实现增量路径，
 * 仅推送实际发生变化的实例数据到 GPU，避免全量重建开销。
 */
UCLASS()
class PROJECTB_API UIncrementalInstancedStaticMeshComponent : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()

public:
    /**
     * 更新实例变换
     * @param InstanceIndex 组件本地实例索引（非全局 ID）
     * @param NewTransform 新的实例变换
     */
    void UpdateInstanceTransformIncremental(int32 InstanceIndex, const FTransform& NewTransform);

    /**
     * 更新实例 CustomData
     * @param InstanceIndex 组件本地实例索引（非全局 ID）
     * @param NewCustomData 新的 CustomData
     */
    void UpdateInstanceCustomDataIncremental(int32 InstanceIndex, TArrayView<const float> NewCustomData);

    /**
     * 更新实例 CustomData 的单个值
     * @param InstanceIndex 组件本地实例索引（非全局 ID）
     * @param DataIndex     CustomData 的索引
     * @param Value         新的 CustomData 值
     */
    void UpdateInstanceCustomDataValueIncremental(int32 InstanceIndex, int32 DataIndex, float Value);
    
    /**
     * 更新实例
     * @param InstanceIndex 组件本地实例索引（非全局 ID）
     * @param NewTransform  新的实例变换
     * @param NewCustomData 新的 CustomData
     */
    void UpdateInstanceIncremental(int32 InstanceIndex, const FTransform& NewTransform,
                                   TArrayView<const float> NewCustomData);

    /**
     * 设置实例可见性
     * @param InstanceIndex 组件本地实例索引（非全局 ID）
     * @param bNewVisible 是否可见
     */
    void SetInstanceVisibility(int32 InstanceIndex, bool bNewVisible);

    /**
     * 刷新所有暂挂的增量更新到渲染线程。
     * 将所有已 Enqueue 的脏实例数据通过 UpdateInstances_RenderThread 推送到 GPU Scene。
     * 在批量修改后调用此方法确保修改立即在下一帧生效。
     */
    void FlushIncrementalUpdates();

protected:
    //~ Begin UInstancedStaticMeshComponent Interface
    virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite) override;
    virtual void SendRenderInstanceData_Concurrent() override;
    //~ End UInstancedStaticMeshComponent Interface

    /**
     * 增量更新单个实例的物理碰撞体。
     * @param InstanceIndex                  组件本地实例索引
     * @param WorldSpaceInstanceTransform    世界空间变换
     * @param bTeleport                      是否物理瞬移（跳过 CCD）
     */
    void UpdateIncrementalInstanceBodyTransform(int32 InstanceIndex, const FTransform& WorldSpaceInstanceTransform,
                                                bool bTeleport);

private:
    /** 标记是否需要在下一帧冲刷时更新包围盒 */
    bool bNeedsBoundsUpdate = false;
};
