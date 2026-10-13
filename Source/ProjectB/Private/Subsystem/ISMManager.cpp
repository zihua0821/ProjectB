//=============================================================================
//  ISMManager.cpp - 球面分桶 + 大规模实例化渲染的核心管理器实现
//  
//  职责：组件管理（按 Mesh 懒创建/复用 ISM 桶）、空间分桶映射、
//        5.4+ ById 增量增删改、事件驱动停用与调试可视化。
//  架构说明见 docs/ISMManager技术文档.md。
//=============================================================================
#include "Subsystem/ISMManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"                  // DrawDebugBox 所在模块 + GEngine 默认字体（Engine.h 不传递包含 DrawDebugHelpers.h）
#include "DrawDebugHelpers.h"                // DrawDebugBox 声明（Engine 模块公共头）
#include "Kismet/KismetSystemLibrary.h"     // FlushPersistentDebugLines
#include "Engine/Canvas.h"                   // UCanvas（画面列表绘制回调）
#include "CanvasItem.h"                      // FCanvasTextItem（画布文本项）
#include "Debug/DebugDrawService.h"          // UDebugDrawService（每帧画布绘制钩子）
#include "GameFramework/PlayerController.h"  // WasInputKeyJustPressed / GetMousePosition
#include "InputCoreTypes.h"                  // EKeys

// ---- AABB 画面列表布局（绘制与鼠标命中共用同一坐标系：视口左上原点） ----
namespace
{
    constexpr float DebugListStartY = 60.0f;    // 列表起始 Y（避开左上角系统提示区）
    constexpr float DebugListRowHeight = 18.0f; // 行高
}

// ---- ISM 网格参数解析（Mesh → Params 单一事实来源） ----
namespace
{
    /** 两套 ISM 参数是否等价（比对影响组件创建的字段） */
    bool AreISMParamsEquivalent(const FISMComponentParams& A, const FISMComponentParams& B)
    {
        return A.AttachParent == B.AttachParent
            && A.bDisallowNanite == B.bDisallowNanite
            && A.bCastShadow == B.bCastShadow
            && A.bDisableBuckets == B.bDisableBuckets;
    }

}

// ============================================================================
// 生命周期
// ============================================================================

void UISMManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // 注册画布绘制回调（每帧随视口渲染调用；bDebugAABB 关闭时早退，近似零开销）
    DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"),
        FDebugDrawDelegate::CreateUObject(this, &UISMManager::OnDebugCanvasDraw));
}

void UISMManager::Deinitialize()
{
    // 注销画布回调（必须先于桶组释放：绘制回调遍历 MeshToBucketGroups）
    if (DebugDrawHandle.IsValid())
    {
        UDebugDrawService::Unregister(DebugDrawHandle);
        DebugDrawHandle.Reset();
    }
    // 释放全部桶组（组件由 RF_Transient + Owner 托管，随 World 销毁；此处清引用即可）
    MeshToBucketGroups.Empty();
    Super::Deinitialize();
}

// ============================================================================
// 组件管理
// ============================================================================

UInstancedStaticMeshComponent* UISMManager::CreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh || !Params.AttachParent) return nullptr;
    AActor* Owner = Params.AttachParent->GetOwner();
    if (!Owner) return nullptr;

    // 始终创建普通 ISM 组件：增量路径由 5.4+ ById API 原生保证（无开关属性）
    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
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

    return NewISM;
}


UInstancedStaticMeshComponent* UISMManager::GetBucketComponent(UStaticMesh* Mesh, int32 BucketIndex, const int32 GroupKey) const
{
    if (!Mesh) return nullptr;
    // 线程约定：仅限游戏线程（无锁读与游戏线程写操作并发即数据竞争，且返回组件增量用法亦要求游戏线程）。
    checkf(IsInGameThread(), TEXT("GetBucketComponent 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    const FISMMeshKey Key(Mesh, GroupKey);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (Group && Group->Buckets.IsValidIndex(BucketIndex)) return Group->Buckets[BucketIndex];
    return nullptr;
}

FISMComponentParams UISMManager::ResolveEffectiveParams(FISMBucketGroup& Group, UStaticMesh* Mesh, const FISMComponentParams& Incoming)
{
    if (!Group.bParamsRegistered)
    {
        // 两级表（种子）：先取 Mesh 级注册（默认组 (Mesh, 0) 条目）——卫星等分组组由此继承 Mesh 参数
        if (const FISMBucketGroup* MeshLevel = MeshToBucketGroups.Find(FISMMeshKey(Mesh)))
        {
            if (MeshLevel->bParamsRegistered)
            {
                Group.CachedParams = MeshLevel->CachedParams;
                Group.bParamsRegistered = true;
                Group.bParamMismatchWarned = MeshLevel->bParamMismatchWarned;   // 跨组继承告警状态（避免同类漂移重复告警）
                return Group.CachedParams;
            }
        }
        // 未注册 + 传入非空 → 以传入值首次定格（兼容旧调用）
        if (Incoming.AttachParent)
        {
            Group.CachedParams = Incoming;
            Group.bParamsRegistered = true;
            return Group.CachedParams;
        }
        // 未注册 + 传入为空 → 返回默认（调用方判空处理）
        return FISMComponentParams();
    }

    // 已注册 → 以组内缓存为准；传入非空且不一致 → 每网格一次性告警（暴露漂移）
    if (Incoming.AttachParent && !Group.bParamMismatchWarned && !AreISMParamsEquivalent(Group.CachedParams, Incoming))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("ISMManager: mesh '%s' 传入的 ISM 参数与注册值不一致——以注册值为准（请统一走 RegisterMeshParams）"),
            *FSoftObjectPath(Mesh).ToString());
        Group.bParamMismatchWarned = true;
    }
    return Group.CachedParams;
}

// ============================================================================
// 球面分桶配置与内部映射
// ============================================================================

void UISMManager::ConfigureSphereBuckets(const FISMSphereBucketConfig& Config)
{
    // 线程约定：仅限游戏线程（写共享分桶配置状态，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("ConfigureSphereBuckets 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    SphereConfig = Config;
    bSphereBucketsConfigured = (Config.LatCells > 0 && Config.LonCells > 0);
    if (!bSphereBucketsConfigured) return;

    // 块大小推导（§3.3：块面积 ≤ InstancesPerBucket 且接近正方形）
    const int32 LatCells = Config.LatCells;
    const int32 LonCells = Config.LonCells;
    const int32 S = FMath::Max(1, Config.InstancesPerBucket);

    if (LatCells * LonCells <= S)
    {
        // 总格子 ≤ 每桶上限 → 单块（桶数 = 1，随网格规模自适应）
        LatBlockSize = LatCells;
        LonBlockSize = LonCells;
    }
    else
    {
        // 按网格纵横比分配边长
        const float fLat = FMath::Sqrt(static_cast<float>(S) * LatCells / LonCells);
        const float fLon = FMath::Sqrt(static_cast<float>(S) * LonCells / LatCells);
        LatBlockSize = FMath::Max(1, FMath::RoundToInt(fLat));
        LonBlockSize = FMath::Max(1, FMath::RoundToInt(fLon));

        // 面积校验：确保每块覆盖 ≤ S 格（密铺时每桶 ≤ 2048 实例，容量保险）
        while (LatBlockSize * LonBlockSize > S)
        {
            if (LatBlockSize >= LonBlockSize) --LatBlockSize; else --LonBlockSize;
            if (LatBlockSize <= 1 && LonBlockSize <= 1) break;
        }
    }

    // 角跨度约束：限制桶最大角跨度，压缩球面弧高（∝α²）与 AABB 尺寸；
    // 经度恒按 360° 计（经度方向是膨胀主源）；min 在面积校验之后只会缩小块，永不超面积上限；
    // ≤ 0 跳过（平面测试台不传字段，行为不变）。
    if (Config.TotalLatSpanDeg > 0.f && Config.MaxBucketAngularSpanDeg > 0.f)
    {
        const float DegPerLatCell = Config.TotalLatSpanDeg / LatCells;
        const float DegPerLonCell = 360.0f / LonCells;
        LatBlockSize = FMath::Min(LatBlockSize, FMath::Max(1, FMath::FloorToInt(Config.MaxBucketAngularSpanDeg / DegPerLatCell)));
        LonBlockSize = FMath::Min(LonBlockSize, FMath::Max(1, FMath::FloorToInt(Config.MaxBucketAngularSpanDeg / DegPerLonCell)));
    }

    // 钳制与桶数
    LatBlockSize = FMath::Min(LatBlockSize, LatCells);
    LonBlockSize = FMath::Min(LonBlockSize, LonCells);
    LatBlockNum = FMath::DivideAndRoundUp(LatCells, LatBlockSize);
    LonBlockNum = FMath::DivideAndRoundUp(LonCells, LonBlockSize);
}

int32 UISMManager::GetBucketKey(const FIntPoint& GridCoords) const
{
    if (!bSphereBucketsConfigured) return 0;

    // 越界 clamp 保证确定性，实例不丢失（包围盒略扩展）
    const int32 LatIdx = FMath::Clamp(GridCoords.X, 0, SphereConfig.LatCells - 1);
    const int32 LonIdx = FMath::Clamp(GridCoords.Y, 0, SphereConfig.LonCells - 1);

    // 经纬块公式：桶列边界对齐 0° 经线（LonIdx 从 0 起，天然满足 C-7 回绕规避）
    return (LatIdx / LatBlockSize) * LonBlockNum + (LonIdx / LonBlockSize);
}

// ============================================================================
// 批量操作
// ============================================================================

FGlobalInstanceId UISMManager::AddInstance(
    UStaticMesh* Mesh, const FTransform& Transform, const FIntPoint& GridCoord,
    const int32 GroupKey, const int32 NumCustomDataFloats, const FISMComponentParams& ISMParams,
    const bool bWorldSpace, const TArray<float>& CustomData)
{
    const TArray<FGlobalInstanceId> Ids = BatchAddInstances(
        Mesh,
        TArray<FTransform>{Transform},
        TArray<FIntPoint>{GridCoord},
        GroupKey, NumCustomDataFloats, ISMParams, bWorldSpace, CustomData);
    return Ids.Num() > 0 ? Ids[0] : FGlobalInstanceId();
}

TArray<FGlobalInstanceId> UISMManager::BatchAddInstances(
    UStaticMesh* Mesh, const TArray<FTransform>& Transforms, const TArray<FIntPoint>& GridCoords,
    const int32 GroupKey, const int32 NumCustomDataFloats, const FISMComponentParams& ISMParams,
    const bool bWorldSpace, const TArray<float>& CustomData)
{
    TArray<FGlobalInstanceId> AllGlobalIds;
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("BatchAddInstances 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || Transforms.Num() == 0) return AllGlobalIds;
    // 前置校验：GridCoords 为空或长度 == Transforms 长度
    if (!GridCoords.IsEmpty() && GridCoords.Num() != Transforms.Num()) return AllGlobalIds;

    const int32 AddCount = Transforms.Num();
    const FISMMeshKey Key(Mesh, GroupKey);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    // 参数解析（两级表：Mesh 级注册种子 → 组级缓存；空参数自动取注册值）
    const FISMComponentParams EffectiveParams = ResolveEffectiveParams(Group, Mesh, ISMParams);
    if (EffectiveParams.AttachParent == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMManager: mesh '%s' 无可用 ISM 参数（未注册且未传入——请先 RegisterMeshParams）"), *Key.MeshPath.ToString());
        return AllGlobalIds;
    }
    // 新组：创建即继承全局分桶配置（分桶判定在此之后立即执行，不能在 GetOrCreateBucket 才继承——鸡生蛋）；预创建组不覆盖
    if (Group.Buckets.Num() == 0)
    {
        Group.bDisableBuckets = EffectiveParams.bDisableBuckets;
        Group.BucketingThreshold = BucketingThreshold;
        Group.NumCustomDataFloats = NumCustomDataFloats;
    }

    // 分桶判定：强制单桶 / 未配置球面分桶 / 未传格子坐标 → 单桶路径
    const bool bUseSphereBucketing = !Group.bDisableBuckets && Group.BucketingThreshold > 0
        && bSphereBucketsConfigured && !GridCoords.IsEmpty();

    if (!bUseSphereBucketing)
    {
        // 单桶路径（Buckets[0]）
        UInstancedStaticMeshComponent* ISMComp = GetOrCreateBucket(Mesh, 0, NumCustomDataFloats, EffectiveParams, GroupKey);
        if (!ISMComp) return AllGlobalIds;
        TArray<FPrimitiveInstanceId> Ids = ISMComp->AddInstancesById(Transforms, bWorldSpace);
        AllGlobalIds.Reserve(Ids.Num());
        for (const FPrimitiveInstanceId& Id : Ids)
            AllGlobalIds.Emplace(GroupKey, 0, Id);
        if (CustomData.Num() > 0)
        {
            const int32 Expected = Ids.Num() * NumCustomDataFloats;
            if (CustomData.Num() == Expected) ISMComp->SetCustomDataById(Ids, CustomData);
        }
        const bool bWasEmpty = (Group.BucketInstanceCounts[0] == 0);
        Group.TotalInstanceCount += AddCount;
        Group.BucketInstanceCounts[0] += AddCount;
        // 空桶 → 非空：显式重算 Bounds——引擎增量 API 不会内联刷新 Bounds（帧末补偿存在但有多重条件，
        // 重建分支可能吞掉）；陈旧 Bounds 双重失效：①位置错位 → 视锥误剔除（该显示的消失）
        // ②膨胀残留 → 过度提交（性能浪费）。注：此机制仅对非 Nanite 组件生效（Nanite 不依赖组件 Bounds 剔除）
        if (bWasEmpty)
        {
            ISMComp->UpdateBounds();
        }
        return AllGlobalIds;
    }

    // 空间分桶路径：按格子坐标分组（桶 → 实例下标集合，一次遍历）
    TMap<int32, TArray<int32>> BucketToIndices;
    for (int32 i = 0; i < AddCount; ++i)
        BucketToIndices.FindOrAdd(GetBucketKey(GridCoords[i])).Add(i);

    // 返回与输入一一对应的句柄数组（保序：按输入下标填充）
    AllGlobalIds.SetNum(AddCount);
    for (const auto& Pair : BucketToIndices)
    {
        const int32 BucketIdx = Pair.Key;
        const TArray<int32>& Indices = Pair.Value;
        UInstancedStaticMeshComponent* Bucket = GetOrCreateBucket(Mesh, BucketIdx, NumCustomDataFloats, EffectiveParams, GroupKey);
        if (!Bucket) continue;
        TArray<FTransform> SliceTransforms;
        SliceTransforms.Reserve(Indices.Num());
        for (const int32 Idx : Indices) SliceTransforms.Add(Transforms[Idx]);

        TArray<FPrimitiveInstanceId> SliceIds = Bucket->AddInstancesById(SliceTransforms, bWorldSpace);
        for (int32 k = 0; k < SliceIds.Num() && k < Indices.Num(); ++k)
            AllGlobalIds[Indices[k]] = FGlobalInstanceId(GroupKey, BucketIdx, SliceIds[k]);

        if (CustomData.Num() > 0)
        {
            const int32 Floats = NumCustomDataFloats;
            TArray<float> SliceData;
            SliceData.Reserve(Indices.Num() * Floats);
            for (const int32 Idx : Indices)
                for (int32 f = 0; f < Floats; ++f)
                    SliceData.Add(CustomData[Idx * Floats + f]);
            Bucket->SetCustomDataById(SliceIds, SliceData);
        }
        const bool bWasEmpty = (Group.BucketInstanceCounts[BucketIdx] == 0);
        Group.TotalInstanceCount += Indices.Num();
        Group.BucketInstanceCounts[BucketIdx] += Indices.Num();
        // 空桶 → 非空：显式重算 Bounds——引擎增量 API 不会内联刷新 Bounds（帧末补偿存在但有多重条件，
        // 重建分支可能吞掉）；陈旧 Bounds 双重失效：①位置错位 → 视锥误剔除（该显示的消失）
        // ②膨胀残留 → 过度提交（性能浪费）。注：此机制仅对非 Nanite 组件生效（Nanite 不依赖组件 Bounds 剔除）
        if (bWasEmpty)
        {
            Bucket->UpdateBounds();
        }
    }
    return AllGlobalIds;
}

void UISMManager::BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds)
{
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("BatchRemoveInstances 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || InstanceIds.Num() == 0) return;

    // 按【逻辑组键】分组（句柄自解析；跨组句柄数组亦正确——如全轨汇总销毁场景）
    TMap<int32, TMap<int32, TArray<FPrimitiveInstanceId>>> GroupBucketMap;
    for (const FGlobalInstanceId& Gid : InstanceIds)
    {
        if (Gid.IsValid())
            GroupBucketMap.FindOrAdd(Gid.GroupKey).FindOrAdd(Gid.BucketIndex).Add(Gid.LocalId);
    }

    for (auto& GroupPair : GroupBucketMap)
    {
        FISMBucketGroup* Group = MeshToBucketGroups.Find(FISMMeshKey(Mesh, GroupPair.Key));
        if (!Group) continue;
        // 计数对齐：确保每桶计数数组就绪（兼容预创建/历史桶组）
        if (Group->BucketInstanceCounts.Num() < Group->Buckets.Num())
            Group->BucketInstanceCounts.SetNum(Group->Buckets.Num());

        for (auto& BucketPair : GroupPair.Value)
        {
            if (!Group->Buckets.IsValidIndex(BucketPair.Key)) continue;
            if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[BucketPair.Key])
            {
                Bucket->RemoveInstancesById(BucketPair.Value);
                Group->TotalInstanceCount = FMath::Max(0, Group->TotalInstanceCount - BucketPair.Value.Num());
                // 事件驱动停用（PR-6）：Remove 归零即停用，无每帧轮询
                Group->BucketInstanceCounts[BucketPair.Key] = FMath::Max(0, Group->BucketInstanceCounts[BucketPair.Key] - BucketPair.Value.Num());
                if (Group->BucketInstanceCounts[BucketPair.Key] == 0)
                {
                    Bucket->SetHiddenInGame(true);   // 轻量停用，组件常驻（组件级池化 D-4）
                }
            }
        }
    }
}

void UISMManager::RemoveInstance(UStaticMesh* Mesh, FGlobalInstanceId InstanceId)
{
    BatchRemoveInstances(Mesh, TArray<FGlobalInstanceId>{InstanceId});
}

void UISMManager::BatchUpdateInstanceCustomData(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<float>& CustomData) const
{
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("BatchUpdateInstanceCustomData 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || InstanceIds.Num() == 0 || CustomData.Num() == 0) return;

    // FloatsPerInstance：取首个有效句柄所在组（同 Mesh 各组 NumCustomDataFloats 一致）
    int32 FloatsPerInstance = 0;
    for (const FGlobalInstanceId& Gid : InstanceIds)
    {
        if (Gid.IsValid())
        {
            if (const FISMBucketGroup* Group = MeshToBucketGroups.Find(FISMMeshKey(Mesh, Gid.GroupKey)))
            {
                FloatsPerInstance = Group->NumCustomDataFloats;
            }
            break;
        }
    }
    if (FloatsPerInstance == 0) return;
    const int32 Expected = InstanceIds.Num() * FloatsPerInstance;
    if (CustomData.Num() != Expected) return;

    // 按（组标签 → 桶索引）分组批量设置（句柄自解析；跨组句柄数组亦正确）
    TMap<int32, TMap<int32, TArray<FPrimitiveInstanceId>>> GroupBucketIds;
    TMap<int32, TMap<int32, TArray<float>>> GroupBucketData;
    for (int32 i = 0; i < InstanceIds.Num(); ++i)
    {
        const FGlobalInstanceId& Gid = InstanceIds[i];
        if (!Gid.IsValid()) continue;
        GroupBucketIds.FindOrAdd(Gid.GroupKey).FindOrAdd(Gid.BucketIndex).Add(Gid.LocalId);
        TArray<float>& Data = GroupBucketData.FindOrAdd(Gid.GroupKey).FindOrAdd(Gid.BucketIndex);
        const int32 Base = i * FloatsPerInstance;
        for (int32 f = 0; f < FloatsPerInstance; ++f)
            Data.Add(CustomData[Base + f]);
    }
    for (auto& GroupPair : GroupBucketIds)
    {
        const FISMBucketGroup* Group = MeshToBucketGroups.Find(FISMMeshKey(Mesh, GroupPair.Key));
        if (!Group) continue;
        for (auto& IdPair : GroupPair.Value)
        {
            if (!Group->Buckets.IsValidIndex(IdPair.Key)) continue;
            if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[IdPair.Key])
                Bucket->SetCustomDataById(IdPair.Value, GroupBucketData[GroupPair.Key][IdPair.Key]);
        }
    }
}

void UISMManager::UpdateInstanceCustomData(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, int32 CustomDataIndex, float Value)
{
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("UpdateInstanceCustomData 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || !InstanceId.IsValid()) return;
    const FISMMeshKey Key(Mesh, InstanceId.GroupKey);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(InstanceId.BucketIndex)) return;

    UInstancedStaticMeshComponent* Bucket = Group->Buckets[InstanceId.BucketIndex];
    if (!Bucket) return;

    // 引擎原生增量接口（SetCustomDataValueById 本身即增量路径）
    Bucket->SetCustomDataValueById(InstanceId.LocalId, CustomDataIndex, Value);
}

void UISMManager::UpdateInstanceTransform(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, const FTransform& NewTransform, const bool bWorldSpace, const bool bTeleport)
{
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("UpdateInstanceTransform 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || !InstanceId.IsValid()) return;
    const FISMMeshKey Key(Mesh, InstanceId.GroupKey);
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(Key);
    if (!Group || !Group->Buckets.IsValidIndex(InstanceId.BucketIndex)) return;

    UInstancedStaticMeshComponent* Bucket = Group->Buckets[InstanceId.BucketIndex];
    if (!Bucket) return;

    Bucket->UpdateInstanceTransformById(InstanceId.LocalId, NewTransform, bWorldSpace, bTeleport);
}

void UISMManager::BatchUpdateInstancesTransforms(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<FTransform>& NewTransforms, const bool bWorldSpace, const bool bTeleport)
{
    // 线程约定：仅限游戏线程（ISM 组件写接口游戏线程专属，非游戏线程断言）。
    checkf(IsInGameThread(), TEXT("BatchUpdateInstancesTransforms 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));
    if (!Mesh || InstanceIds.Num() == 0 || NewTransforms.Num() != InstanceIds.Num()) return;

    // 逐实例：句柄自解析组与桶 → ById 更新（引擎帧末统一聚合脏标记，无需调用层分组；
    // 跨组句柄数组（多轨卫星汇总）亦正确）
    for (int32 i = 0; i < InstanceIds.Num(); ++i)
    {
        const FGlobalInstanceId& Gid = InstanceIds[i];
        if (!Gid.IsValid()) continue;
        const FISMBucketGroup* Group = MeshToBucketGroups.Find(FISMMeshKey(Mesh, Gid.GroupKey));
        if (!Group || !Group->Buckets.IsValidIndex(Gid.BucketIndex)) continue;
        if (UInstancedStaticMeshComponent* Bucket = Group->Buckets[Gid.BucketIndex])
            Bucket->UpdateInstanceTransformById(Gid.LocalId, NewTransforms[i], bWorldSpace, bTeleport);
    }
}

// ============================================================================
// 桶管理
// ============================================================================

UInstancedStaticMeshComponent* UISMManager::GetOrCreateBucket(UStaticMesh* Mesh, int32 BucketIndex, int32 NumCustomDataFloats, const FISMComponentParams& Params, const int32 GroupKey)
{
    if (!Mesh) return nullptr;

    // 线程约定：仅限游戏线程。非游戏线程直接断言（不再派发——调用端自行保证游戏线程，见文档 §3.3）。
    checkf(IsInGameThread(), TEXT("GetOrCreateBucket 仅限游戏线程调用（ISM 组件创建/写操作须在游戏线程，见 ISMManager 技术文档 §3.3）"));

    const FISMMeshKey Key(Mesh, GroupKey);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);
    // 参数解析（Mesh → Params 单一事实来源；空参数自动取注册值）
    const FISMComponentParams EffectiveParams = ResolveEffectiveParams(Group, Mesh, Params);
    if (EffectiveParams.AttachParent == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("ISMManager: mesh '%s' 无可用 ISM 参数（未注册且未传入）"), *Key.MeshPath.ToString());
        return nullptr;
    }
    // 新组：继承全局分桶配置（否则 BucketingThreshold=0 → 永远单桶）
    if (Group.Buckets.Num() == 0)
    {
        Group.bDisableBuckets = EffectiveParams.bDisableBuckets;
        Group.BucketingThreshold = BucketingThreshold;
        Group.NumCustomDataFloats = NumCustomDataFloats;
    }
    if (BucketIndex >= Group.Buckets.Num())
        Group.Buckets.SetNum(BucketIndex + 1);
    // 计数数组无条件对齐（兼容预创建/历史桶组，防越界）
    if (Group.BucketInstanceCounts.Num() < Group.Buckets.Num())
        Group.BucketInstanceCounts.SetNum(Group.Buckets.Num());
    if (Group.Buckets[BucketIndex])
    {
        // 复活停用桶（PR-6：重新 Add 时解除停用；SetHiddenInGame 幂等，先复活后 Add 无窗口）
        Group.Buckets[BucketIndex]->SetHiddenInGame(false);
        return Group.Buckets[BucketIndex];
    }
    UInstancedStaticMeshComponent* NewBucket = CreateISMComponent(Mesh, NumCustomDataFloats, EffectiveParams);
    Group.Buckets[BucketIndex] = NewBucket;
    return NewBucket;
}

// ============================================================================
// 网格参数注册表
// ============================================================================

void UISMManager::RegisterMeshParams(UStaticMesh* Mesh, const FISMComponentParams& Params)
{
    if (!Mesh) return;
    // 线程约定：仅限游戏线程（写共享参数状态，非游戏线程断言）
    checkf(IsInGameThread(), TEXT("RegisterMeshParams 仅限游戏线程调用（见 ISMManager 技术文档 §3.3）"));

    // 注册载体 = 默认组 (Mesh, 0) 条目（Mesh 级单一事实来源；分组组（如卫星一轨一桶）
    // 由 ResolveEffectiveParams 从本条目取种子初始化组级参数）
    const FISMMeshKey Key(Mesh);
    FISMBucketGroup& Group = MeshToBucketGroups.FindOrAdd(Key);

    if (Group.bParamsRegistered)
    {
        if (!AreISMParamsEquivalent(Group.CachedParams, Params))
        {
            UE_LOG(LogTemp, Warning, TEXT("ISMManager: mesh '%s' 重复注册参数与首次不一致——保留首次注册值"), *Key.MeshPath.ToString());
        }
        return;
    }

    Group.CachedParams = Params;
    Group.bParamsRegistered = true;
}

FISMComponentParams UISMManager::GetMeshParams(UStaticMesh* Mesh) const
{
    if (!Mesh) return FISMComponentParams();
    // 线程约定：无锁读，仅限游戏线程
    checkf(IsInGameThread(), TEXT("GetMeshParams 仅限游戏线程调用（无锁读，见 ISMManager 技术文档 §3.3）"));

    // 读默认组 (Mesh, 0) 注册条目（Mesh 级注册表）
    const FISMBucketGroup* Group = MeshToBucketGroups.Find(FISMMeshKey(Mesh));
    return (Group && Group->bParamsRegistered) ? Group->CachedParams : FISMComponentParams();
}

// ============================================================================
// 调试
// ============================================================================

void UISMManager::DebugPrintBucketStats()
{
    int32 TotalBuckets = 0;
    UE_LOG(LogTemp, Warning, TEXT("----- ISM Bucket Stats -----"));
    UE_LOG(LogTemp, Warning, TEXT("SphereConfig: %s, Block: %d x %d, BucketNum: %d x %d = %d"),
        bSphereBucketsConfigured ? TEXT("Configured") : TEXT("Disabled"),
        LatBlockSize, LonBlockSize, LatBlockNum, LonBlockNum, bSphereBucketsConfigured ? LatBlockNum * LonBlockNum : 1);
    for (auto& Pair : MeshToBucketGroups)
    {
        const FISMBucketGroup& Group = Pair.Value;
        int32 ActiveBuckets = 0;
        // 以自维护计数为准（PR-6 事件驱动状态，不依赖引擎查询 API）
        for (int32 i = 0; i < Group.Buckets.Num(); ++i)
            if (Group.Buckets[i] && Group.BucketInstanceCounts.IsValidIndex(i) && Group.BucketInstanceCounts[i] > 0)
                ++ActiveBuckets;
        TotalBuckets += ActiveBuckets;
        UE_LOG(LogTemp, Warning, TEXT("Mesh: %s (group=%d), Total Instances: %d, Buckets: %d (Active %d / Disabled %d)"),
            *Pair.Key.MeshPath.ToString(), Pair.Key.GroupKey, Group.TotalInstanceCount, Group.Buckets.Num(), ActiveBuckets,
            Group.Buckets.Num() - ActiveBuckets);
        // 每桶包围盒尺寸与停用状态（NFR-5 诊断扩展）
        for (int32 i = 0; i < Group.Buckets.Num(); ++i)
        {
            const UInstancedStaticMeshComponent* Bucket = Group.Buckets[i];
            if (!Bucket) continue;
            UE_LOG(LogTemp, Warning, TEXT("  Bucket[%d]: Instances=%d, %s, BoundsExtent=(%.1f, %.1f, %.1f)"),
                i, Group.BucketInstanceCounts.IsValidIndex(i) ? Group.BucketInstanceCounts[i] : 0,
                (Group.BucketInstanceCounts.IsValidIndex(i) && Group.BucketInstanceCounts[i] > 0) ? TEXT("Active") : TEXT("Disabled"),
                Bucket->Bounds.BoxExtent.X, Bucket->Bounds.BoxExtent.Y, Bucket->Bounds.BoxExtent.Z);
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("Total Active Buckets: %d"), TotalBuckets);
}

// ============================================================================
// 调试：桶包围盒可视化（线框 + 桶索引配色 + 内缩 1cm 防共面重合）
// ============================================================================

void UISMManager::DrawBucketBounds(UStaticMesh* Mesh, float LineThickness)
{
    if (!Mesh) return;
    DrawBucketBoundsByPath(FSoftObjectPath(Mesh).ToString(), LineThickness);
}

void UISMManager::DrawBucketBoundsByPath(const FString& MeshPath, float LineThickness, bool bPersistent)
{
    UWorld* World = GetWorld();
    if (!World || MeshPath.IsEmpty()) return;

    // 收集同 MeshPath 的全部组（一轨一桶后同一 Mesh 可能对应多个分组组）
    const FSoftObjectPath TargetPath(MeshPath);
    TArray<const FISMBucketGroup*> MatchedGroups;
    for (const auto& Pair : MeshToBucketGroups)
    {
        if (Pair.Key.MeshPath == TargetPath && Pair.Value.Buckets.Num() > 0)
            MatchedGroups.Add(&Pair.Value);
    }
    if (MatchedGroups.Num() == 0) return;

    // 一次性持久绘制（bPersistent=true）：先清全部持久调试线再画，重复调用自动替换不叠加；
    // 每帧非持久绘制（bPersistent=false，bDebugAABB 路径）：不清理，线框按帧自动覆盖无残留
    if (bPersistent)
        UKismetSystemLibrary::FlushPersistentDebugLines(World);

    for (const FISMBucketGroup* Group : MatchedGroups)
    {
        const int32 NumBuckets = Group->Buckets.Num();
        for (int32 i = 0; i < NumBuckets; ++i)
        {
            const UInstancedStaticMeshComponent* Bucket = Group->Buckets[i];
            if (!Bucket) continue;

            // 活动桶按索引取 HSV 渐变（相邻桶可区分）；停用桶灰色（PR-6 状态一目了然）
            const bool bActive = Group->BucketInstanceCounts.IsValidIndex(i) && Group->BucketInstanceCounts[i] > 0;
            FColor Color;
            if (bActive)
            {
                const float Hue = (float)(i + 1) / (float)FMath::Max(1, NumBuckets) * 360.0f;
                // HSV → RGB：成员函数 HSVToLinearRGB()（FLinearColor 无静态 HSVToLinearColor）
                Color = FLinearColor(Hue, 1.0f, 1.0f).HSVToLinearRGB().ToFColor(true);
            }
            else
            {
                Color = FColor(96, 96, 96);
            }

            const FBoxSphereBounds& B = Bucket->Bounds;
            // 内缩 1cm：相邻桶共面边界线不重合（防重叠核心）
            const FVector Extent = B.BoxExtent - FVector(1.0f);
            DrawDebugBox(World, B.Origin, Extent, FQuat::Identity, Color,
                /*bPersistentLines=*/bPersistent, /*LifeTime=*/bPersistent ? -1.0f : 0.0f,
                /*DepthPriority=*/0, LineThickness);
        }
    }
}

void UISMManager::ClearBucketBoundsDebug()
{
    if (UWorld* World = GetWorld())
        UKismetSystemLibrary::FlushPersistentDebugLines(World);
}

// ============================================================================
// 调试：AABB 线框封装（bDebugAABB：画面列表 + 每帧线框绘制 + 键盘/鼠标交互）
// ============================================================================

void UISMManager::Tick(float DeltaTime)
{
    if (!bDebugAABB) return;

    HandleDebugAABBInput();

    // 每帧非持久重绘：线框随实例变换/桶状态实时刷新，关闭时无残留（无需 Flush）
    if (!DebugAABBMeshPath.IsEmpty())
        DrawBucketBoundsByPath(DebugAABBMeshPath, /*LineThickness=*/4.0f, /*bPersistent=*/false);
}

TStatId UISMManager::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UISMManager, STATGROUP_Tickables);
}

void UISMManager::SetDebugAABB(bool bEnable, const FString& MeshPath)
{
    if (!bEnable)
    {
        bDebugAABB = false;
        DebugAABBMeshPath.Reset();
        ClearBucketBoundsDebug();
        return;
    }

    bDebugAABB = true;
    if (!MeshPath.IsEmpty())
    {
        SetDebugAABBTarget(MeshPath);
    }
    else if (DebugAABBMeshPath.IsEmpty())
    {
        // 未指定目标：自动选中并绘制第一个 Mesh（与画面列表高亮一致）
        TArray<FISMDebugEntry> Entries;
        BuildDebugMeshEntries(Entries);
        if (Entries.Num() > 0)
            SetDebugAABBTarget(Entries[0].PathName);
    }
}

void UISMManager::SetDebugAABBTarget(const FString& MeshPath)
{
    DebugAABBMeshPath = MeshPath;
    // 线框绘制由 Tick 每帧驱动，此处仅更新目标，下一帧自动生效
}

void UISMManager::BuildDebugMeshEntries(TArray<FISMDebugEntry>& OutEntries) const
{
    OutEntries.Reset();
    // 按 Mesh 聚合（一轨一桶后同 Mesh 多个分组组：实例/桶求和，组数单列）
    TMap<FString, int32> PathToEntryIndex;
    for (const auto& Pair : MeshToBucketGroups)
    {
        const FString PathName = Pair.Key.MeshPath.ToString();
        int32& EntryIndex = PathToEntryIndex.FindOrAdd(PathName, INDEX_NONE);
        if (EntryIndex == INDEX_NONE)
        {
            FISMDebugEntry Entry;
            Entry.PathName = PathName;
            Entry.DisplayName = Pair.Key.MeshPath.GetAssetName();
            EntryIndex = OutEntries.Add(MoveTemp(Entry));
        }
        FISMDebugEntry& Entry = OutEntries[EntryIndex];
        Entry.Instances += Pair.Value.TotalInstanceCount;
        Entry.Buckets += Pair.Value.Buckets.Num();
        ++Entry.Groups;
    }
    // DisplayName 升序（与原 Slate 列表一致；绘制/导航/命中共用同一序列）
    OutEntries.Sort([](const FISMDebugEntry& A, const FISMDebugEntry& B)
        { return A.DisplayName < B.DisplayName; });
}

void UISMManager::HandleDebugAABBInput()
{
    UWorld* World = GetWorld();
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    if (!PC) return;

    TArray<FISMDebugEntry> Entries;
    BuildDebugMeshEntries(Entries);
    if (Entries.Num() == 0) return;

    // Esc：关闭调试（停止绘制 + 清线）
    if (PC->WasInputKeyJustPressed(EKeys::Escape))
    {
        SetDebugAABB(false);
        return;
    }

    // ↑/↓：循环切换绘制的 Mesh（贴合原 Slate"点击条目即切换绘制"，移动即生效）
    int32 CurIdx = Entries.IndexOfByPredicate(
        [this](const FISMDebugEntry& E) { return E.PathName == DebugAABBMeshPath; });
    if (PC->WasInputKeyJustPressed(EKeys::Up))
    {
        CurIdx = (CurIdx < 0) ? 0 : (CurIdx - 1 + Entries.Num()) % Entries.Num();
        SetDebugAABBTarget(Entries[CurIdx].PathName);
    }
    else if (PC->WasInputKeyJustPressed(EKeys::Down))
    {
        CurIdx = (CurIdx + 1) % Entries.Num();
        SetDebugAABBTarget(Entries[CurIdx].PathName);
    }

    // 鼠标左键点击列表行 → 直接切换绘制（与原 Slate 点击行为一致；行布局与绘制共用常量）
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        float MX = 0.f, MY = 0.f;
        if (PC->GetMousePosition(MX, MY) && MY >= DebugListStartY)
        {
            const int32 Row = FMath::FloorToInt((MY - DebugListStartY) / DebugListRowHeight);
            if (Entries.IsValidIndex(Row))
                SetDebugAABBTarget(Entries[Row].PathName);
        }
    }
}

void UISMManager::OnDebugCanvasDraw(UCanvas* Canvas, APlayerController* PC)
{
    if (!bDebugAABB || !Canvas) return;
    // 多 World（编辑器 PIE）防护：仅绘制本子系统所属 World 的视口
    if (PC && PC->GetWorld() != GetWorld()) return;

    TArray<FISMDebugEntry> Entries;
    BuildDebugMeshEntries(Entries);
    if (Entries.Num() == 0) return;

    UFont* Font = GEngine->GetMediumFont();
    float Y = DebugListStartY;

    // 标题 + 操作提示
    FCanvasTextItem TitleItem(FVector2D(10.0f, Y),
        FText::FromString(TEXT("ISM Bucket Bounds Debug  (↑/↓ 切换 · 左键点击 · Esc 关闭)")),
        Font, FLinearColor::Yellow);
    Canvas->DrawItem(TitleItem);
    Y += DebugListRowHeight + 8.0f;

    // Mesh 列表：当前绘制目标高亮 + 前导箭头（实例数/桶数实时刷新）
    for (const FISMDebugEntry& Entry : Entries)
    {
        const bool bSelected = (Entry.PathName == DebugAABBMeshPath);
        const FString Line = FString::Printf(TEXT("%s %s  (instances=%d, buckets=%d, groups=%d)"),
            bSelected ? TEXT("▶") : TEXT("  "), *Entry.DisplayName, Entry.Instances, Entry.Buckets, Entry.Groups);
        FCanvasTextItem Item(FVector2D(10.0f, Y), FText::FromString(Line), Font,
            bSelected ? FLinearColor(0.2f, 1.0f, 0.4f) : FLinearColor::White);
        Canvas->DrawItem(Item);
        Y += DebugListRowHeight;
    }

    // 底部：当前绘制目标完整路径（小字）
    if (!DebugAABBMeshPath.IsEmpty())
    {
        FCanvasTextItem InfoItem(FVector2D(10.0f, Y + 8.0f),
            FText::FromString(FString::Printf(TEXT("当前绘制: %s"), *DebugAABBMeshPath)),
            GEngine->GetSmallFont(), FLinearColor(0.65f, 0.65f, 0.65f));
        Canvas->DrawItem(InfoItem);
    }
}

