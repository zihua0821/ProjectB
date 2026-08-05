// Fill out your copyright notice in the Description page of Project Settings.

#include "Component/IncrementalInstancedStaticMeshComponent.h"
#include "PrimitiveSceneInfo.h"// ============================================================================
#include "ProjectB/ProjectB.h"
// 辅助函数 — 消费脏队列并刷新 GPU
// ============================================================================
namespace IncrementalInstancedStaticMeshComponent
{
    void ProcessDirtyInstances_RenderThread(
        const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& InstanceDataSceneProxy,
        const FPrimitiveSceneProxy& SceneProxy,
        TQueue<FDirtyInstance, EQueueMode::Mpsc>& DirtyInstances
    )
    {
        if (!InstanceDataSceneProxy.IsValid())
        {
            return;
        }

        // 获取实例场景数据缓冲区
        const FInstanceSceneDataBuffers* Buffers = InstanceDataSceneProxy->GeInstanceSceneDataBuffers();
        if (!Buffers)
        {
            UE_LOG(LogTemp, Error, TEXT("[IncrementalISM] ProcessDirtyInstances_RenderThread: InstanceSceneDataBuffers is NULL! Owner=%s"),
                *SceneProxy.GetOwnerName().ToString());
            return;
        }

        FInstanceSceneDataBuffers* MutableBuffers = const_cast<FInstanceSceneDataBuffers*>(Buffers);

        const FMatrix PrimitiveWorldToLocal = SceneProxy.GetLocalToWorld().Inverse();

        const FInstanceSceneDataBuffers::FWriteView WriteView = MutableBuffers->BeginWriteAccess(
            FInstanceSceneDataBuffers::FAccessTag(1));

        FDirtyInstance Dirty;
        while (DirtyInstances.Dequeue(Dirty))
        {
            if (Dirty.InstanceIndex < 0 || Dirty.InstanceIndex >= WriteView.InstanceToPrimitiveRelative.Num())
            {
                continue;
            }

            if (Dirty.bUpdateTransform)
            {
                // 将世界变换转换为局部变换并更新
                const FMatrix LocalMatrix = Dirty.NewTransform.ToMatrixWithScale() * PrimitiveWorldToLocal;
                FRenderTransform RenderTransform;
                RenderTransform.TransformRows[0] = FVector3f(LocalMatrix.M[0][0], LocalMatrix.M[0][1],
                                                             LocalMatrix.M[0][2]);
                RenderTransform.TransformRows[1] = FVector3f(LocalMatrix.M[1][0], LocalMatrix.M[1][1],
                                                             LocalMatrix.M[1][2]);
                RenderTransform.TransformRows[2] = FVector3f(LocalMatrix.M[2][0], LocalMatrix.M[2][1],
                                                             LocalMatrix.M[2][2]);
                RenderTransform.Origin = FVector3f(LocalMatrix.M[3][0], LocalMatrix.M[3][1], LocalMatrix.M[3][2]);
                WriteView.InstanceToPrimitiveRelative[Dirty.InstanceIndex] = RenderTransform;
            }

            if (Dirty.bUpdateCustomData)
            {
                const int32 NumFloats = WriteView.NumCustomDataFloats;
                const int32 Count = FMath::Min(Dirty.NewCustomData.Num(), NumFloats);
                const int32 StartOffset = Dirty.InstanceIndex * NumFloats;
                for (int32 i = 0; i < Count; ++i)
                {
                    WriteView.InstanceCustomData[StartOffset + i] = Dirty.NewCustomData[i];
                }
            }
        }

        MutableBuffers->EndWriteAccess(FInstanceSceneDataBuffers::FAccessTag(1));

        // 标记该组件的实例数据已脏，驱动 GPUScene 更新
        if (FPrimitiveSceneInfo* SceneInfo = SceneProxy.GetPrimitiveSceneInfo())
        {
            SceneInfo->MarkGPUStateDirty();
        }
    }
}

// ============================================================================
// FIncrementalSceneProxy 实现
// ============================================================================

FIncrementalSceneProxy::FIncrementalSceneProxy(UIncrementalInstancedStaticMeshComponent* InComponent,
        const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe>& InInstanceDataProxy)
    : FInstancedStaticMeshSceneProxy(InComponent, InComponent->GetWorld()->GetFeatureLevel()),
    CustomInstanceDataProxy(InInstanceDataProxy)
{
    // 父类构造函数已完成所有 ISM 初始化（材质、LOD、包围盒、InstanceDataSceneProxy 等）
}

void FIncrementalSceneProxy::UpdateInstances_RenderThread(
    FRHICommandListBase& RHICmdList,
    const FBoxSphereBounds& InBounds,
    const FBoxSphereBounds& InLocalBounds,
    const FBoxSphereBounds& InStaticMeshBounds)
{
    // FInstancedStaticMeshSceneProxy::UpdateInstances_RenderThread(RHICmdList, InBounds, InLocalBounds, InStaticMeshBounds);
    
    IncrementalInstancedStaticMeshComponent::ProcessDirtyInstances_RenderThread(CustomInstanceDataProxy, *this, DirtyQueue.DirtyInstances);
}

void FIncrementalSceneProxy::AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform)
{
    DirtyQueue.AddDirtyInstance(InstanceIndex, NewTransform);
}

void FIncrementalSceneProxy::AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData)
{
    DirtyQueue.AddDirtyInstanceCustomData(InstanceIndex, NewCustomData);
}

void FIncrementalSceneProxy::TriggerIncrementalUpdate(FRHICommandListBase& RHICmdList)
{
    if (DirtyQueue.IsEmpty()) return;
    UpdateInstances_RenderThread(RHICmdList, GetBounds(), GetLocalBounds(), FBoxSphereBounds());
}

// ============================================================================
// FIncrementalNaniteSceneProxy 实现
// ============================================================================

FIncrementalNaniteSceneProxy::FIncrementalNaniteSceneProxy(
    const Nanite::FMaterialAudit& MaterialAudit,
    UIncrementalInstancedStaticMeshComponent* InComponent)
    : Nanite::FSceneProxy(MaterialAudit, InComponent)
{
    // 父类构造函数已完成所有 Nanite 初始化
}

void FIncrementalNaniteSceneProxy::UpdateInstances_RenderThread(
    FRHICommandListBase& RHICmdList,
    const FBoxSphereBounds& InBounds,
    const FBoxSphereBounds& InLocalBounds,
    const FBoxSphereBounds& InStaticMeshBounds)
{
    // Super::UpdateInstances_RenderThread(RHICmdList, InBounds, InLocalBounds, InStaticMeshBounds);

    IncrementalInstancedStaticMeshComponent::ProcessDirtyInstances_RenderThread(InstanceDataSceneProxy, *this, DirtyQueue.DirtyInstances);
}

void FIncrementalNaniteSceneProxy::AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform)
{
    DirtyQueue.AddDirtyInstance(InstanceIndex, NewTransform);
}

void FIncrementalNaniteSceneProxy::AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData)
{
    DirtyQueue.AddDirtyInstanceCustomData(InstanceIndex, NewCustomData);
}

void FIncrementalNaniteSceneProxy::TriggerIncrementalUpdate(FRHICommandListBase& RHICmdList)
{
    if (DirtyQueue.IsEmpty()) return;
    UpdateInstances_RenderThread(RHICmdList, GetBounds(), GetLocalBounds(), FBoxSphereBounds());
}

FPrimitiveSceneProxy* UIncrementalInstancedStaticMeshComponent::CreateStaticMeshSceneProxy(
    Nanite::FMaterialAudit& NaniteMaterials, bool bCreateNanite)
{
    if (bCreateNanite)
    {
        return ::new FIncrementalNaniteSceneProxy(NaniteMaterials, this);
    }

    const TSharedPtr<FInstanceDataSceneProxy, ESPMode::ThreadSafe> InstanceDataProxy =
        StaticCastSharedPtr<FInstanceDataSceneProxy>(GetOrCreateInstanceDataSceneProxy());
    return ::new FIncrementalSceneProxy(this, InstanceDataProxy);
}

void UIncrementalInstancedStaticMeshComponent::SendRenderInstanceData_Concurrent()
{
    // 主动切断全量同步路径。
    // 增量更新模式下，实例数据的初始同步由 CreateSceneProxy 内部的 FlushChanges 完成，
    // 后续更新通过增量通道推送，无需在此处触发 MarkRenderStateDirty 全量重建。
}

// ============================================================================
// UIncrementalInstancedStaticMeshComponent 的公开增量接口
// ============================================================================

void UIncrementalInstancedStaticMeshComponent::UpdateInstanceTransformIncremental(
    int32 InstanceIndex, const FTransform& NewTransform)
{
    // 1. 有效性检查
    if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
    {
        UE_LOG(LogStaticMesh, Warning, TEXT("UpdateInstanceTransformIncremental: Invalid instance index %d"), InstanceIndex);
        return;
    }

    // ★ 将世界空间变换转为组件局部空间（PerInstanceSMData 要求局部空间）
    const FTransform LocalTransform = NewTransform.GetRelativeTransform(GetComponentTransform());

    FInstancedStaticMeshInstanceData& InstanceData = PerInstanceSMData[InstanceIndex];
    InstanceData.Transform = LocalTransform.ToMatrixWithScale();
    
    // 同步更新物理碰撞体
    if (bPhysicsStateCreated && InstanceBodies.IsValidIndex(InstanceIndex))
    {
        UpdateIncrementalInstanceBodyTransform(InstanceIndex, NewTransform, false);
    }

    // 3. 如果 SceneProxy 已创建，通过增量通道提交给渲染线程
    if (SceneProxy)
    {
        FDirtyInstance Dirty;
        Dirty.InstanceIndex = InstanceIndex;
        Dirty.NewTransform = NewTransform;
        Dirty.bUpdateTransform = true;

        // 根据代理类型（Nanite 或非 Nanite）发送到正确的队列
        if (SceneProxy->IsNaniteMesh())
        {
            FIncrementalNaniteSceneProxy* NaniteProxy = static_cast<FIncrementalNaniteSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncrementalTransformNanite)(
                [NaniteProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    NaniteProxy->AddDirtyInstance(Dirty.InstanceIndex, Dirty.NewTransform);
                });
        }
        else
        {
            FIncrementalSceneProxy* ISMProxy = static_cast<FIncrementalSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncrementalTransform)(
                [ISMProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    ISMProxy->AddDirtyInstance(Dirty.InstanceIndex, Dirty.NewTransform);
                });
        }
    }
    
    // UpdateBounds();
    bNeedsBoundsUpdate = true;
}

void UIncrementalInstancedStaticMeshComponent::UpdateInstanceCustomDataIncremental(
    int32 InstanceIndex, TArrayView<const float> NewCustomData)
{
    // 1. 有效性检查
    if (!PerInstanceSMData.IsValidIndex(InstanceIndex) || NewCustomData.Num() != NumCustomDataFloats)
    {
        UE_LOG(LogStaticMesh, Warning, TEXT("UpdateInstanceCustomDataIncremental: Invalid index %d or data size mismatch (expected %d, got %d)"), 
            InstanceIndex, NumCustomDataFloats, NewCustomData.Num());
        return;
    }

    // 2. 直接修改本地 CustomData 数组
    const int32 DataOffset = InstanceIndex * NumCustomDataFloats;
    FMemory::Memcpy(&PerInstanceSMCustomData[DataOffset], NewCustomData.GetData(), NumCustomDataFloats * sizeof(float));

    // 3. 通过增量通道提交
    if (SceneProxy)
    {
        FDirtyInstance Dirty;
        Dirty.InstanceIndex = InstanceIndex;
        Dirty.NewCustomData.Append(NewCustomData.GetData(), NewCustomData.Num());
        Dirty.bUpdateCustomData = true;

        if (SceneProxy->IsNaniteMesh())
        {
            FIncrementalNaniteSceneProxy* NaniteProxy = static_cast<FIncrementalNaniteSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncrementalCustomDataNanite)(
                [NaniteProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    NaniteProxy->AddDirtyInstanceCustomData(Dirty.InstanceIndex, Dirty.NewCustomData);
                });
        }
        else
        {
            FIncrementalSceneProxy* ISMProxy = static_cast<FIncrementalSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncrementalCustomData)(
                [ISMProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    ISMProxy->AddDirtyInstanceCustomData(Dirty.InstanceIndex, Dirty.NewCustomData);
                });
        }
    }
}

void UIncrementalInstancedStaticMeshComponent::UpdateInstanceCustomDataValueIncremental(int32 InstanceIndex,
    int32 DataIndex, float Value)
{
    if (!PerInstanceSMData.IsValidIndex(InstanceIndex) || 
        DataIndex < 0 || 
        DataIndex >= NumCustomDataFloats)
    {
        UE_LOG(LogStaticMesh, Warning, TEXT("UpdateInstanceCustomDataValueIncremental: Invalid index %d or data index %d"), 
            InstanceIndex, DataIndex);
        return;
    }

    // 读取当前完整的 CustomData
    TArray<float> FullCustomData;
    FullCustomData.SetNum(NumCustomDataFloats);
    const int32 Offset = InstanceIndex * NumCustomDataFloats;
    FMemory::Memcpy(FullCustomData.GetData(), &PerInstanceSMCustomData[Offset], NumCustomDataFloats * sizeof(float));

    // 修改指定索引
    FullCustomData[DataIndex] = Value;

    // 通过已有的全量增量接口提交（内部会拷贝并发送完整数组）
    UpdateInstanceCustomDataIncremental(InstanceIndex, FullCustomData);
}

void UIncrementalInstancedStaticMeshComponent::UpdateInstanceIncremental(
    int32 InstanceIndex, const FTransform& NewTransform, TArrayView<const float> NewCustomData)
{
    // 1. 有效性检查
    if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
    {
        UE_LOG(LogStaticMesh, Warning, TEXT("UpdateInstanceIncremental: Invalid instance index %d"), InstanceIndex);
        return;
    }

    // 2. 修改 Transform（★ 注意：此处输入的 NewTransform 是组件局部空间，非世界空间）
    FInstancedStaticMeshInstanceData& InstanceData = PerInstanceSMData[InstanceIndex];
    InstanceData.Transform = NewTransform.ToMatrixWithScale();
    
    // 同步更新物理碰撞体
    if (bPhysicsStateCreated && InstanceBodies.IsValidIndex(InstanceIndex))
    {
        UpdateIncrementalInstanceBodyTransform(InstanceIndex, NewTransform, false);
    }

    // 3. 修改 CustomData（如果提供了有效数据）
    bool bCustomDataChanged = false;
    if (NewCustomData.Num() > 0 && NewCustomData.Num() == NumCustomDataFloats)
    {
        const int32 DataOffset = InstanceIndex * NumCustomDataFloats;
        FMemory::Memcpy(&PerInstanceSMCustomData[DataOffset], NewCustomData.GetData(), NumCustomDataFloats * sizeof(float));
        bCustomDataChanged = true;
    }

    // 4. 通过增量通道提交
    if (SceneProxy)
    {
        FDirtyInstance Dirty;
        Dirty.InstanceIndex = InstanceIndex;
        Dirty.NewTransform = NewTransform;
        Dirty.bUpdateTransform = true;
        if (bCustomDataChanged)
        {
            Dirty.NewCustomData.Append(NewCustomData.GetData(), NewCustomData.Num());
            Dirty.bUpdateCustomData = true;
        }

        if (SceneProxy->IsNaniteMesh())
        {
            FIncrementalNaniteSceneProxy* NaniteProxy = static_cast<FIncrementalNaniteSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncrementalNanite)(
                [NaniteProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    NaniteProxy->AddDirtyInstance(Dirty.InstanceIndex, Dirty.NewTransform);
                    if (Dirty.bUpdateCustomData)
                    {
                        NaniteProxy->AddDirtyInstanceCustomData(Dirty.InstanceIndex, Dirty.NewCustomData);
                    }
                });
        }
        else
        {
            FIncrementalSceneProxy* ISMProxy = static_cast<FIncrementalSceneProxy*>(SceneProxy);
            ENQUEUE_RENDER_COMMAND(AddIncremental)(
                [ISMProxy, Dirty = MoveTemp(Dirty)](FRHICommandListImmediate& RHICmdList) mutable
                {
                    ISMProxy->AddDirtyInstance(Dirty.InstanceIndex, Dirty.NewTransform);
                    if (Dirty.bUpdateCustomData)
                    {
                        ISMProxy->AddDirtyInstanceCustomData(Dirty.InstanceIndex, Dirty.NewCustomData);
                    }
                });
        }
    }
}

// ============================================================================
// 刷新 / 辅助方法
// ============================================================================

void UIncrementalInstancedStaticMeshComponent::FlushIncrementalUpdates()
{
    if (!SceneProxy) return;

    ENQUEUE_RENDER_COMMAND(FlushIncrementalInstances)(
        [this](FRHICommandListImmediate& RHICmdList) mutable
        {
            if (!SceneProxy) return;
            if (SceneProxy->IsNaniteMesh())
            {
                static_cast<FIncrementalNaniteSceneProxy*>(SceneProxy)->TriggerIncrementalUpdate(RHICmdList);
            }
            else
            {
                static_cast<FIncrementalSceneProxy*>(SceneProxy)->TriggerIncrementalUpdate(RHICmdList);
            }
        });

    if (!SceneProxy->IsNaniteMesh() && bNeedsBoundsUpdate && GetScene())
    {
        UpdateBounds();
        GetScene()->UpdatePrimitiveInstances(this);
        bNeedsBoundsUpdate = false;
    }
}

// ============================================================================
// 物理碰撞体辅助方法
// ============================================================================

void UIncrementalInstancedStaticMeshComponent::UpdateIncrementalInstanceBodyTransform(int32 InstanceIndex, const FTransform& WorldSpaceInstanceTransform, bool bTeleport)
{
    check(bPhysicsStateCreated);
    if (!InstanceBodies.IsValidIndex(InstanceIndex))
    {
        return;
    }

    FBodyInstance*& InstanceBodyInstance = InstanceBodies[InstanceIndex];

    if (WorldSpaceInstanceTransform.GetScale3D().IsNearlyZero())
    {
        if (InstanceBodyInstance)
        {
            // delete BodyInstance
            InstanceBodyInstance->TermBody();
            delete InstanceBodyInstance;
            InstanceBodyInstance = nullptr;
        }
    }
    else
    {
        if (InstanceBodyInstance)
        {
            // Update existing BodyInstance
            InstanceBodyInstance->SetBodyTransform(WorldSpaceInstanceTransform, TeleportFlagToEnum(bTeleport));
            InstanceBodyInstance->UpdateBodyScale(WorldSpaceInstanceTransform.GetScale3D());
        }
        else
        {
            // create new BodyInstance
            InstanceBodyInstance = new FBodyInstance();
            InitInstanceBody(InstanceIndex, InstanceBodyInstance);
        }
    }
}

void UIncrementalInstancedStaticMeshComponent::SetInstanceVisibility(int32 InstanceIndex, bool bNewVisible)
{
    if (!PerInstanceSMData.IsValidIndex(InstanceIndex))
    {
        return;
    }

    if (bNewVisible)
    {
        // 显示：仅设 CustomData 不透明度为 1（Transform 由 AcquireSlot 设置）
        if (NumCustomDataFloats > 0)
        {
            UpdateInstanceCustomDataValueIncremental(InstanceIndex, 0, 1.0f);
        }
    }
    else
    {
        // 隐藏：仅移到远处 / 原点（不碰 CustomData，避免渲染线程竞态）
        const bool bIsNanite = GetStaticMesh()->IsNaniteEnabled();
        const FVector TargetLocation = bIsNanite
            ? ParkedLocation
            : FVector::ZeroVector;
        const FTransform TargetTransform(FQuat::Identity, TargetLocation, FVector::ZeroVector);
        UpdateInstanceTransformIncremental(InstanceIndex, TargetTransform);
    }
}