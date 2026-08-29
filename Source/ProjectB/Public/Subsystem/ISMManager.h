//=============================================================================
//  ISMManager.h - 球面分桶 + 大规模实例化渲染的核心管理器声明
//
//  职责：按 StaticMesh 自动创建/复用 ISM 桶组件，内置球面分桶映射，
//        提供 5.4+ ById 增量增删改接口与事件驱动停用（PR-6）；
//        全部组件级操作仅限游戏线程调用（见线程模型约定）。
//  架构说明见 docs/ISMManager技术文档.md。
//=============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ISMManager.generated.h"

class UCanvas;           // 前向声明（画面调试列表绘制回调）
class APlayerController; // 前向声明（画面调试列表输入/绘制回调）

// 唯一分桶参数（FR-7/C-5）：参与空间划分（决定块大小）+ 容量保险
#define INSTANCES_PER_BUCKET 2048

/**
 * 用于唯一标识一种 ISM 组件配置的键。
 * 基于 SoftObjectPath（软引用路径）而非 UStaticMesh* 裸指针：
 * 资产重定向（Move/Rename）后路径仍正确匹配，TMap 键稳定；
 * 同时提供 UStaticMesh* 隐式构造，调用方无需手动转路径。
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
 * 全局唯一的 ISM 实例句柄：(BucketIndex, LocalId)
 * - BucketIndex：桶索引（单桶模式恒为 0），对外屏蔽"实例在哪个组件"的细节；
 * - LocalId：引擎稳定物理实例 ID（FPrimitiveInstanceId，非数组下标），
 *   实例增删导致物理索引重排时仍保持有效——外部只需持有本句柄即可增删改，
 *   无需关心实例在组件数组中的位置。
 * 注意：句柄在阶段切换（Remove+Add）后会更新为新 Add 返回的句柄，不跨操作复用。
 */
USTRUCT(BlueprintType)
struct FGlobalInstanceId
{
    GENERATED_BODY()

    UPROPERTY()
    int32 BucketIndex = INDEX_NONE;

    FPrimitiveInstanceId LocalId; // 引擎原生实例 ID（ById API 用；与桶内实例一一对应）

    FGlobalInstanceId() = default;
    FGlobalInstanceId(const int32 InBucketIndex, const FPrimitiveInstanceId InId)
        : BucketIndex(InBucketIndex), LocalId(InId) {}

    bool IsValid() const { return BucketIndex != INDEX_NONE && LocalId.IsValid(); }
    bool operator==(const FGlobalInstanceId& Other) const { return BucketIndex == Other.BucketIndex && LocalId == Other.LocalId; }
};

FORCEINLINE uint32 GetTypeHash(const FGlobalInstanceId& Key) { return HashCombine(GetTypeHash(Key.BucketIndex), GetTypeHash(Key.LocalId.Id)); }

/**
 * ISM 组件创建参数：透传给 UISMManager 创建桶组件（CreateISMComponent）。
 * 同一 Mesh 的组件创建时固化本参数（FISMBucketGroup::CachedParams），
 * 后续扩容/新桶复用，保证组件配置一致；仅 bDisableBuckets 会影响分桶判定。
 */
USTRUCT(BlueprintType)
struct FISMComponentParams
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<USceneComponent> AttachParent = nullptr;

    // ---- 碰撞/渲染配置（默认：无碰撞、不挡寻路、开阴影） ----
    ECollisionEnabled::Type CollisionEnabled = ECollisionEnabled::NoCollision;
    ECollisionChannel CollisionObjectType = ECC_WorldDynamic;
    ECollisionResponse CollisionResponseToAllChannels = ECR_Ignore;
    bool bCanEverAffectNavigation = false;
    bool bDisallowNanite = false;  // 作物类网格（无 Nanite 支持）置 true，走传统渲染路径
    bool bCastShadow = true;
    float MinDrawDistance = 0.f;
    float MaxDrawDistance = 0.f;   // 距离剔除：>0 时按组件级 CullDistances 生效

    // 强制不分桶（单桶）
    bool bDisableBuckets = false;
};

/**
 * 球面分桶配置（纯数据）：由持有业务配置的调用方（UCellManagerComponent::Initialize）
 * 解析 PlanetConfig 后一次性注入，管理器全局持有（所有 Mesh 共享同一划分）；
 * 注入后内部推导块大小/桶数（ConfigureSphereBuckets），S2/S3 共用同一划分；
 * 未配置（LatCells/LonCells ≤ 0）= 全部单桶（功能正确，无剔除收益）。
 */
USTRUCT(BlueprintType)
struct FISMSphereBucketConfig
{
    GENERATED_BODY()

    // 纬度格子数（NumLat - 1，与 CreateMassCell 同源）
    int32 LatCells = 0;

    // 经度格子数（NumLon）
    int32 LonCells = 0;

    // 唯一参数（FR-7/C-5）：块大小推导 + 容量保险
    int32 InstancesPerBucket = 2048;

    // 角跨度约束（方案 B）：纬度总跨度 = MaxLat - MinLat（球面场景注入）；≤ 0 = 不约束
    float TotalLatSpanDeg = 0.f;

    // 桶最大角跨度上限（度，经度恒按 360° 计）；≤ 0 = 不约束（平面测试台行为不变）
    float MaxBucketAngularSpanDeg = 0.f;
};

/**
 * 一个 Mesh 对应的所有 ISM 桶（桶组）：桶组件数组 + 自维护计数。
 * - Buckets[i] 可空（懒创建）：只创建实际有实例的桶，空桶不预先分配；
 * - BucketInstanceCounts 与 Buckets 对齐，是"活动/停用"的权威状态（PR-6），
 *   不依赖引擎查询 API（DebugPrintBucketStats 亦以它为准）；
 * - 注：球面配置不存于桶组——由管理器全局持有（ConfigureSphereBuckets 注入，S2/S3 共享同一划分）。
 */
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
    GENERATED_BODY()
    
    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Buckets;

    int32 TotalInstanceCount = 0;    // 该 Mesh 的实例总数（跨桶求和，供统计/校验）
    int32 NumCustomDataFloats = 0;   // 每实例 CustomData 数量（组件创建时固化，CustomData 校验依据）
    bool bDisableBuckets = false;    // 强制单桶（Cell 轮廓等无分桶需求的系统）

    // 缓存组件创建参数（扩容时复用）
    FISMComponentParams CachedParams;

    // 分桶阈值兼容开关（创建组件时继承全局值；≤ 0 = 强制单桶）
    int32 BucketingThreshold = 0;

    // 每桶活动实例数（与 Buckets 对齐，事件驱动停用依据，PR-6）
    UPROPERTY()
    TArray<int32> BucketInstanceCounts;
};

/**
 * ISM 组件管理器（WorldSubsystem，全局唯一）
 *
 * 职责（技术文档 v2.0/v2.1）：
 * - 组件管理：按 StaticMesh 自动创建/复用 ISM 组件（懒创建，空桶不预先分配）
 * - 空间分桶：内置球面映射（FISMSphereBucketConfig 数据注入，分桶输入 = 格子坐标，
 *   块大小按 InstancesPerBucket 推导 → 桶数自适应网格规模）
 * - 批量操作：批量添加/移除/设置 CustomData（引擎 5.4+ ById API 原生增量路径，无开关属性）
 * - 增量更新：UpdateInstanceTransform / SetCustomDataValueById 单实例 O(1) 更新
 * - 空桶事件驱动停用：Remove 归零即停用（SetHiddenInGame），重新 Add 自动复活，组件常驻复用
 *
 * 线程模型（v3.0 约定）：**全部组件级操作（创建组件、增删实例、更新 Transform / CustomData）仅限游戏线程**——
 * ISM 组件创建与内部写接口均游戏线程专属（无锁、无线程派发），本管理器直接透传这些引擎接口，因此同样仅限游戏线程；
 * 各写接口在**非游戏线程时断言**（check(IsInGameThread())），不隐式派发，调用端自行保证游戏线程。
 * 跨线程场景的正确姿势：worker 线程只收集、不碰 ISM，回到游戏线程后统一提交（见 ISMManager 技术文档 §3.3）。
 */
UCLASS()
class PROJECTB_API UISMManager : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    // ---- 组件管理（桶组件获取/创建；首桶 = Bucket[0]，统一走 GetOrCreateBucket / GetBucketComponent） ----
    /**
     * 获取或创建指定索引的桶组件（懒创建 + 扩容对齐计数数组）。首桶传 BucketIndex=0。
     * 新组创建时继承全局分桶配置（bDisableBuckets/BucketingThreshold）；
     * 已存在的桶无条件 SetHiddenInGame(false) 复活（PR-6：重新 Add 即恢复，幂等无窗口）；
     * 线程约定：仅限游戏线程（非游戏线程断言，不再入口派发——调用端自行保证游戏线程）。
     * @param Mesh          静态网格
     * @param BucketIndex   桶索引
     * @param NumCustomDataFloats   每实例 CustomData 数量
     * @param Params        组件创建参数
     */
    UInstancedStaticMeshComponent* GetOrCreateBucket(UStaticMesh* Mesh, int32 BucketIndex, int32 NumCustomDataFloats, const FISMComponentParams& Params);
    /**
     * 只获取指定索引的桶组件（不创建；越界/不存在返回 nullptr）。首桶传 BucketIndex=0。
     * 线程约束：仅限游戏线程调用（不做派发）——无锁读与游戏线程写操作并发即数据竞争，
     * 且返回组件的增量用法亦要求游戏线程；跨线程需求请在游戏线程取指针后自行缓存。
     * @param Mesh          静态网格
     * @param BucketIndex   桶索引
     */
    UInstancedStaticMeshComponent* GetBucketComponent(UStaticMesh* Mesh, int32 BucketIndex) const;

    // ---- 球面分桶配置 ----
    /**
     * 配置球面分桶：注入配置并推导块大小/桶数（§3.3 推荐算法，全局共享，S2/S3 同一划分）。
     * 算法：总格 ≤ S → 单块（1 桶）；否则按纵横比 sqrt(S*Lat/Lon) 取块边长，
     * 面积校验（块面积 ≤ S，密铺时每桶 ≤ 2048 实例的容量保险）后取 ceil 得桶数——桶数随网格规模自适应。
     * 必须在批量添加之前调用（未配置 = 全部单桶，功能正确但无剔除收益）；
     * 网格配置变更需重新调用（块大小/桶数按新配置重推导）。
     * @note 推导结果存于只读状态（LatBlockSize/LonBlockSize/LatBlockNum/LonBlockNum），
     *       之后所有 Mesh 的分桶判定共享同一划分（S2/S3 一致性）。
     * @param Config 球面分桶配置
     */
    void ConfigureSphereBuckets(const FISMSphereBucketConfig& Config);
    /**
     * 分桶阈值兼容开关（默认 4096）：新组创建时继承为组阈值；阈值 ≤ 0 = 强制单桶
     * @param Threshold 阈值
     */
    void SetBucketingThreshold(int32 Threshold) { BucketingThreshold = FMath::Max(0, Threshold); }

    // ---- 实例操作（单个 + 批量共存，仿引擎 ById 范式：单个 = 批量传单元素的薄封装） ----
    // 调用端策略：高频路径（Mass 处理器）单帧累积后调批量；低频路径（单值呼吸等）直接调单个。
    // 核心路径分流：分桶判定 → 按桶分组 → ById 增量 API（详见各接口注释）。

    // ---- 添加 ----
    /**
     * 单个添加实例（薄封装 BatchAddInstances 单元素），返回稳定全局句柄。
     * @return 首元素句柄；添加失败时返回默认（无效）句柄。
     * @param Mesh          静态网格
     * @param Transform     变换
     * @param GridCoord     格子坐标
     * @param NumCustomDataFloats   每实例 CustomData 数量
     * @param ISMParams     ISM 组件参数
     * @param bWorldSpace   世界空间标志
     * @param CustomData    自定义数据
     */
    FGlobalInstanceId AddInstance(
        UStaticMesh* Mesh,
        const FTransform& Transform,
        const FIntPoint& GridCoord,
        const int32 NumCustomDataFloats,
        const FISMComponentParams& ISMParams,
        bool bWorldSpace = false,             // 与引擎 AddInstancesById 同语义：false=局部坐标，true=世界坐标
        const TArray<float>& CustomData = TArray<float>());
    /**
     * 分桶判定链：!bDisableBuckets && 组阈值 > 0 && 已配置球面分桶 && GridCoords 非空，
     * 任一失败 → 单桶路径（全部进 Bucket[0]）。
     * 分桶路径按 GetBucketKey(GridCoords[i]) 分组，一次遍历完成，每桶一次 AddInstancesById；
     * CustomData 长度必须 == Transforms.Num() × NumCustomDataFloats，否则静默跳过。
     * @return 与 Transforms 一一对应（保序）的全局句柄数组
     * @param Mesh          静态网格
     * @param Transforms    变换数组
     * @param GridCoords    格子坐标数组
     * @param NumCustomDataFloats   每实例 CustomData 数量
     * @param ISMParams     ISM 组件参数
     * @param bWorldSpace   世界空间标志
     * @param CustomData    自定义数据
     */
    TArray<FGlobalInstanceId> BatchAddInstances(
        UStaticMesh* Mesh,
        const TArray<FTransform>& Transforms,
        const TArray<FIntPoint>& GridCoords,  
        const int32 NumCustomDataFloats,
        const FISMComponentParams& ISMParams,
        bool bWorldSpace = false,             
        const TArray<float>& CustomData = TArray<float>());

    // ---- 移除 ----
    /**
     * 单个移除实例（薄封装 BatchRemoveInstances 单元素）；计数归零的桶事件驱动停用。
     * @param Mesh          静态网格
     * @param InstanceId    实例句柄
     */
    void RemoveInstance(UStaticMesh* Mesh, FGlobalInstanceId InstanceId);
    /**
     * 批量移除实例：按句柄的 BucketIndex 分组，每桶一次 RemoveInstancesById（避免逐桶遍历）；
     * 计数归零的桶事件驱动停用（SetHiddenInGame，PR-6），组件常驻复用。
     * @param Mesh          静态网格
     * @param InstanceIds   实例句柄数组
     */
    void BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds);

    // ---- 更新 Transform ----
    /**
     * 单个更新 Transform：句柄定位桶 → 引擎原生增量接口 UpdateInstanceTransformById。
     * @param Mesh          静态网格
     * @param InstanceId    实例句柄
     * @param NewTransform  新变换
     * @param bWorldSpace   世界空间标志
     * @param bTeleport     瞬移标志
     * @param bWorldSpace true 表示按世界空间解释，false 表示按实例局部空间。
     * @param bTeleport   true 表示不做插值平滑（瞬移）。
     * @note ById 版本无 bMarkRenderStateDirty——ById 增量路径默认不触发全量重建。
     */
    void UpdateInstanceTransform(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, const FTransform& NewTransform, bool bWorldSpace = false, bool bTeleport = false);
    /**
     * 批量更新 Transform。
     * @param Mesh          静态网格
     * @param InstanceIds   实例句柄数组
     * @param NewTransforms 新变换数组
     * @param bWorldSpace   世界空间标志
     * @param bTeleport     瞬移标志
     * @param bWorldSpace true 表示按世界空间解释，false 表示按实例局部空间。
     * @param bTeleport   true 表示不做插值平滑（瞬移）。
     * @note 调用层无需分组（分组仅能省取桶指针数组索引，收益近零反而引入 TMap 分配开销）。
     * 引擎无 ById 批量接口（BatchUpdateInstancesTransforms 按连续实例下标 StartInstanceIndex，不适合 ById 增量句柄语义），
     * 故直接逐实例 ById 更新；批量合并由引擎帧末 FlushChanges 自动聚合（脏实例一次提交）。
     */
    void BatchUpdateInstancesTransforms(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<FTransform>& NewTransforms, bool bWorldSpace = false, bool bTeleport = false);

    // ---- 更新 CustomData ----
    /**
     * 批量更新 CustomData：按桶分组 SetCustomDataById；长度强校验（Num × FloatsPerInstance），
     * 不匹配静默跳过（排查材质无反应问题时优先核对调用方 FloatsPerInstance 一致性）。
     * @param Mesh          静态网格
     * @param InstanceIds   实例句柄数组
     * @param CustomData    自定义数据
     */
    void BatchUpdateInstanceCustomData(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds, const TArray<float>& CustomData) const;
    /** 单实例单值更新（病害/水分等低频变化）：引擎原生增量接口 SetCustomDataValueById
     * @param Mesh          静态网格
     * @param InstanceId    实例句柄
     * @param CustomDataIndex 自定义数据索引
     * @param Value         新值
     */
    void UpdateInstanceCustomData(UStaticMesh* Mesh, FGlobalInstanceId InstanceId, int32 CustomDataIndex, float Value);

    // ---- 调试 ----
    /**
     * 调试统计：输出球面配置/块大小/桶数（V-4 判读基准），逐 Mesh 输出桶数与 Active/Disabled 状态，
     * 再逐桶输出实例数与包围盒尺寸（NFR-5 诊断）。
     * @note Active 以自维护计数 > 0 判断（PR-6 权威状态，不依赖引擎查询 API）。
     */
    UFUNCTION(BlueprintCallable)
    void DebugPrintBucketStats();

    // ---- 调试：桶包围盒可视化（AABB 线框 + 桶索引配色 + 内缩 1cm 防共面重合） ----
    /**
     * AABB 线框调试封装入口（画面列表 + 每帧线框绘制一体）：
     * - 开启：游戏画面左上角绘制 Mesh 列表（名称/实例数/桶数，实时刷新）；
     *   MeshPath 为空时自动选中并绘制第一个 Mesh，非空则直接绘制指定 Mesh；
     *   画面中 ↑/↓ 切换绘制的 Mesh（立即生效），鼠标左键点击列表行亦可切换，Esc 关闭；
     * - 关闭：停止画面列表与每帧线框绘制，并清除全部持久调试线。
     * 线框配色：活动桶按索引取 HSV 渐变（相邻桶可区分），停用桶灰色（PR-6 状态一目了然）；
     * 内缩 1cm：相邻桶共面边界线不重合；按 Mesh 过滤绘制，规避跨 Mesh 66 层完全重叠。
     */
    UFUNCTION(BlueprintCallable)
    void SetDebugAABB(bool bEnable, const FString& MeshPath = TEXT(""));

    /** AABB 线框调试总开关（SetDebugAABB 的状态位，蓝图/控制台可直接读写） */
    UPROPERTY(BlueprintReadWrite, Category = "Debug")
    bool bDebugAABB = false;

    /** 一次性绘制指定 Mesh 的全部桶包围盒（持久线，先清后绘；重复调用自动替换）。兼容旧调用，推荐 SetDebugAABB */
    void DrawBucketBounds(UStaticMesh* Mesh, float LineThickness = 4.0f);
    /** 按软引用路径绘制（控制台/内部用，避免重新加载资产）；路径无效时静默返回。
        bPersistent=true=一次性持久线（先清后绘）；false=非持久线（每帧调用自动覆盖，无残留） */
    void DrawBucketBoundsByPath(const FString& MeshPath, float LineThickness = 4.0f, bool bPersistent = true);
    /** 清除全部持久调试线（含桶包围盒绘制） */
    void ClearBucketBoundsDebug();

private:
    /**
     * 创建并注册一个桶组件（普通 ISM，无自研组件）。
     * - 新对象归属 AttachParent 的 Owner（生命周期随 Owner，RF_Transient 不入磁盘）；
     * - 渲染/碰撞参数全部透传自 Params（见 FISMComponentParams 注释）；
     * - 增量能力由 5.4+ ById API 原生保证，无需开关属性（引擎无 bUseIncrementalISM）；
     * - 只能在游戏线程调用。
     * @return 注册成功的桶组件；Mesh 或 AttachParent 无效时返回 nullptr。
     * @param Mesh          静态网格
     * @param NumCustomDataFloats   自定义数据数量
     * @param Params        ISM 组件参数
     */
    static UInstancedStaticMeshComponent* CreateISMComponent(UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);

    // 内部球面映射：格子坐标 → 桶索引（O(1) 纯整数：块行列号 → 桶线性索引，越界 clamp 保确定性）
    /**
     * 内部球面映射：格子坐标 → 桶线性索引（O(1) 纯整数运算，热路径）。
     * 公式：(LatIdx / LatBlockSize) * LonBlockNum + (LonIdx / LonBlockSize)。
     * - 越界 clamp 到 [0, Cells-1]：保证确定性，实例不丢失（包围盒略扩展）；
     * - 桶列边界对齐 0° 经线（LonIdx 从 0 起），天然满足 C-7 回绕规避；
     * - 未配置球面分桶恒返回 0（单桶）。
     * @return 桶索引
     * @param GridCoords 格子坐标
     */
    int32 GetBucketKey(const FIntPoint& GridCoords) const;

    // Mesh → 桶组（懒创建，全生命周期常驻）
    UPROPERTY()
    TMap<FISMMeshKey, FISMBucketGroup> MeshToBucketGroups; 

    // ---- AABB 线框调试状态（bDebugAABB 封装） ----
    struct FISMDebugEntry // 画面列表条目（每次构建时实时取实例数/桶数）
    {
        FString PathName;
        FString DisplayName;
        int32 Instances = 0;
        int32 Buckets = 0;
    };
    /** 构建排序后的 Mesh 调试列表（DisplayName 升序；画面绘制/键盘导航/鼠标命中共用同一序列） */
    void BuildDebugMeshEntries(TArray<FISMDebugEntry>& OutEntries) const;
    /** 切换当前绘制目标（更新路径，线框下一帧自动生效） */
    void SetDebugAABBTarget(const FString& MeshPath);
    /** 每帧输入处理：↑/↓ 循环切换 · 左键点击列表行 · Esc 关闭 */
    void HandleDebugAABBInput();
    /** UDebugDrawService 画布回调：bDebugAABB 开启时在游戏画面绘制 Mesh 列表 */
    void OnDebugCanvasDraw(UCanvas* Canvas, APlayerController* PC);

    FString DebugAABBMeshPath;       // 当前绘制目标（空 = 仅显示列表不绘制）
    FDelegateHandle DebugDrawHandle; // UDebugDrawService 注册句柄（Deinitialize 注销）

    int32 BucketingThreshold = 4096; // 全局分桶阈值（新组创建时继承；≤ 0 = 强制单桶）

    // ---- 球面分桶配置状态（ConfigureSphereBuckets 注入后只读，天然线程安全） ----
    FISMSphereBucketConfig SphereConfig;        // 原始配置（含 LatCells/LonCells，GetBucketKey clamp 用）
    bool bSphereBucketsConfigured = false;      // 是否已注入有效球面配置（两轴格子数 > 0）
    int32 LatBlockSize = 0;                     // 纬度向块边长（格数，面积校验后 ≤ sqrt(每桶上限)）
    int32 LonBlockSize = 0;                     // 经度向块边长
    int32 LatBlockNum = 0;                      // 纬度向桶数 = ceil(LatCells / LatBlockSize)
    int32 LonBlockNum = 0;                      // 经度向桶数 = ceil(LonCells / LonBlockSize)
};
