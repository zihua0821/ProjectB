# ISMManager 技术文档

**版本**: v3.0
**更新日期**: 2026-08-23
**分支**: MassCrop  
**服务对象**: Cell（单元格）、Plot（地块）、Crop（农作物）

> **2026-08-23 v3.0 修订（线程约定定案）**：确立**硬性线程约定**——`UISMManager` 全部组件级操作（创建组件、增删实例、更新 Transform / CustomData）**仅限游戏线程**执行。视觉处理器（`Cell/Plot/Crop`）统一 `bRequiresGameThreadExecution = true`；对外接口在**非游戏线程时断言**；`GetOrCreateBucket` 历史"入口即派发"机制按约定移除（不再隐式切线程，调用端自行保证游戏线程）。详见 §3.3。
> **2026-08-22 v2.2 修订**：组件管理接口收敛——删除 `GetOrCreateISMComponent` / `GetISMComponent` / `GetFirstBucketComponent`（含只读首桶），创建统一走 `GetOrCreateBucket(Mesh, 0, ...)`、只读统一走 `GetBucketComponent(Mesh, 0)`；`GetOrCreateBucket` 线程派发修正为**入口即派发**（先切游戏线程再操作共享数据）；`CreateISMComponent` 移除冗余 `MarkRenderStateDirty()`（渲染属性均在 Register 前设置）。
> **v2.0 变更说明**（2026-08-07）：分桶机制由"按实例添加序号切分"重构为**位置判据空间分桶**——管理器**内置球面分桶**（`FISMSphereBucketConfig` 纯数据注入 + 内部球面映射），**分桶输入 = 格子坐标**（业务寻址语言），`INSTANCES_PER_BUCKET` 参与空间划分（块大小推导），空桶事件驱动停用。详见《ISMManager空间分桶重构方案.md》与需求文档 v2.6。
> **2026-08-20 v2.1 修订**：新增只读接口 `GetBucketComponent`（按桶索引获取组件，不创建）——AFarmlandActor 压力测试样例（ISMManager 分桶验证）接入时用于增量变换更新；§2.3 删除 `bUseIncrementalISM` 字段行（代码已清理，字段不存在）。
> **2026-08-19 审计修订（ue-codegraph 源码复核）**：`bUseIncrementalISM` 非引擎属性（5.5.4 全引擎源码 0 匹配，5.4+ ById API 即增量路径）——全文归因修正；`BucketingThreshold` 语义修正（成员默认 4096、组阈值创建时继承全局值、≤ 0 = 强制单桶）；§9 重复条目删除（编号 1-11）。

---

## 一、系统概述

### 1.1 定位与职责

**`UISMManager`** 是一个 **WorldSubsystem**（`Subsystem/ISMManager.h`），随 `UWorld` 自动创建和销毁，全局唯一。它是项目中所有 Instanced Static Mesh（ISM）实例的**统一管理中心**，职责包括：

- **组件管理**：按静态网格体（`UStaticMesh*`）自动创建或复用 ISM 组件
- **空间分桶**：通过**内置球面映射**（球面网格参数以 `FISMSphereBucketConfig` 纯数据注入，分桶输入 = 格子坐标，规则内聚于管理器内部几何层）将实例按**实际格子位置**映射到桶（ISM 组件），保障空间局部性使包围盒紧凑、剔除生效
- **批量操作**：批量添加/移除实例、批量设置 CustomData
- **增量渲染**：引擎 5.4+ ID-based API 原生增量路径（`AddInstancesById` / `RemoveInstancesById` / `SetCustomDataValueById`，无开关属性），无需自研组件
- **懒创建**：组件/桶按需创建，空桶不预先分配
- **空桶事件驱动停用**：移除实例后计数归零即停用组件（无每帧轮询），组件常驻复用（组件级池化）

### 1.2 服务的系统

| 系统 | Mesh | ISM 类型 | 使用接口 |
|------|------|---------|---------|
| Cell（单元格轮廓） | `OutlineMesh` | 普通 ISM | `BatchAddInstances` / `BatchRemoveInstances` / `BatchUpdateInstanceCustomData`（单桶，`bDisableBuckets`） |
| Plot（地块） | `LandCellMesh` | 普通 ISM | `BatchAddInstances`（全量创建）+ `UpdateInstanceCustomData` |
| Crop（农作物） | 作物各阶段 Mesh | 普通 ISM（引擎原生增量） | `BatchAddInstances` / `BatchRemoveInstances`（ById API 原生增量） |

### 1.3 核心设计理念

- **以 Mesh 为键**：所有组件按 `FSoftObjectPath`（软引用路径）索引，资产重定向后键值仍稳定
- **实例句柄稳定**：`FGlobalInstanceId` 对外屏蔽物理索引变化，外部系统无需关心实例在数组中的位置
- **分桶透明化**：分桶由管理器自动决策（调用方传 `Transforms` + `GridCoords`），外部只需持有 `FGlobalInstanceId`
- **位置判据分桶**：分桶按实例**实际位置**（空间划分，非容量满桶分裂）；密铺/离散统一同一套空间分桶逻辑
- **球面参数数据注入**：格子坐标 → 桶索引的映射由管理器**内置球面映射**承载（私有实现，不对外暴露抽象/不做注入）；网格几何（格子数）经 `FISMSphereBucketConfig` 以**纯数据**流入管理器，未配置 = 全部单桶
- **唯一参数参与空间划分**：`INSTANCES_PER_BUCKET=2048` 决定块大小（每块覆盖 ≤ 2048 个格子），同时是容量保险
- **引擎原生增量**：所有组件均为 `UInstancedStaticMeshComponent`，5.4+ ById API 即增量路径（`AddInstancesById` 等），不再需要自研增量组件

---

## 二、核心数据结构

### 2.1 FISMMeshKey（Mesh 键）

```cpp
USTRUCT(BlueprintType)
struct FISMMeshKey
{
    FSoftObjectPath MeshPath;   // 静态网格体的软引用路径，作为唯一标识
    // 支持从 FSoftObjectPath / UStaticMesh* 构造
    // operator== 与 GetTypeHash 基于 MeshPath
};
```

**设计意图**：使用软引用路径而非指针，确保资产重载、资源重定向后键值稳定，存档加载时不会失配。

### 2.2 FGlobalInstanceId（全局实例句柄）

```cpp
USTRUCT(BlueprintType)
struct FGlobalInstanceId
{
    int32 BucketIndex = INDEX_NONE;   // 所在桶（ISM 组件）的索引
    FPrimitiveInstanceId LocalId;     // 桶内的稳定物理实例 ID
    bool IsValid() const;
};
```

**要点**：
- 分桶后实例由 `(BucketIndex, LocalId)` 唯一定位
- `LocalId` 是 `FPrimitiveInstanceId`（UE5 实例化渲染的稳定 ID），**不是数组下标**——物理索引（InstanceIndex）变化时 ID 保持不变

### 2.3 FISMComponentParams（组件创建参数）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `AttachParent` | nullptr | 挂载父组件 |
| `CollisionEnabled` | NoCollision | 碰撞启用状态 |
| `CollisionObjectType` | ECC_WorldDynamic | 碰撞对象类型 |
| `CollisionResponseToAllChannels` | ECR_Ignore | 默认碰撞响应 |
| `bCanEverAffectNavigation` | false | 是否影响导航 |
| `bDisallowNanite` | false | 禁用 Nanite（CustomData 控制透明度的材质需要） |
| `bCastShadow` | true | 是否投射阴影 |
| `MinDrawDistance` / `MaxDrawDistance` | 0 | 绘制距离（0 不设置） |
| `bDisableBuckets` | false | **强制不分桶**（单桶） |

### 2.4 FISMBucketGroup（桶组）

```cpp
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Buckets;  // 一个 Mesh 对应的所有桶
    TArray<int32> BucketInstanceCounts;   // 每桶活动实例数（与 Buckets 对齐，事件驱动停用依据）
    int32 TotalInstanceCount = 0;        // 该 Mesh 的实例总数
    int32 NumCustomDataFloats = 0;       // 每实例 CustomData 数量
    bool bDisableBuckets = false;        // 强制单桶
    FISMComponentParams CachedParams;    // 缓存创建参数（扩容时复用）
    int32 BucketingThreshold = 0;        // 该 Mesh 的分桶阈值（创建组件时继承全局值，≤ 0 = 强制单桶；兼容开关）
    // 注：球面配置不存于桶组——由管理器全局持有（ConfigureSphereBuckets 注入，S2/S3 共享）
};
```

### 2.5 球面分桶配置与内部映射（管理器内置）

```cpp
// 球面分桶配置（纯数据）：由持有业务配置的调用方（UCellManagerComponent::Initialize）
// 解析 PlanetConfig 后一次性注入；未配置 = 全部单桶（功能正确，无剔除收益）
USTRUCT(BlueprintType)
struct FISMSphereBucketConfig
{
    int32 LatCells;        // 纬度格子数（NumLat - 1）
    int32 LonCells;        // 经度格子数（NumLon）
    int32 InstancesPerBucket = 2048;  // 唯一参数（FR-7/C-5）：块大小推导 + 容量保险
};

/** 配置球面分桶（全局共享，S2/S3 同一划分）；未配置 = 全部单桶 */
void ConfigureSphereBuckets(const FISMSphereBucketConfig& Config);
```

**内部球面映射**（ISMManager.cpp 私有实现，不对外暴露）：

```
GridCoords(格子坐标) → clamp 越界 → 格子索引 (LatIdx, LonIdx)
  → 桶 key（(LatIdx/LatBlockSize) * LonBlockNum + (LonIdx/LonBlockSize)）
```

**要点**：
- 分桶输入 = **格子坐标**（`FIntPoint`，业务原生寻址语言：`CellManagerComponent` 全部 API 用 `Coords`；`FCellStaticFragment::Coords` 原生持有）——调用方零转换，无浮点边缘误差，**严格同格同桶**；
- 桶列边界对齐 0° 经线（经度块从 `LonIdx=0` 起按固定块大小切分）——任何桶的经纬区间不跨 0°，包围盒不会跨圈撑爆（C-7 回绕规避）；
- 配置注入后字段只读，无状态，天然线程安全（NFR-2）；
- 分桶与 `bWorldSpace` 解耦（分桶输入 = 纯整数格子坐标，与坐标空间无关；`bWorldSpace` 仅透传引擎）；
- 球面几何以**数据**流入管理器（依赖方向正常），分桶规则内聚于管理器内部几何层——不对外暴露抽象接口、不做外部注入（无 `FISMBucketMapper` / `SetBucketMapper`）；分桶不再依赖实例位置 → 球心/半径/经纬范围从配置中消失；
- 桶包围盒由引擎自动合并实例变换（`CalculateBounds`），管理器无需计算任何球面几何；
- 平面分桶：未来非球面场景需要时，将内部球面映射抽为私有策略类（接口不变），本轮不预支（YAGNI）。

---

## 三、组件管理

### 3.1 创建组件（CreateISMComponent）

```cpp
static UInstancedStaticMeshComponent* CreateISMComponent(
    UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);
```

- 始终创建普通 `UInstancedStaticMeshComponent`；增量路径由 5.4+ ById API 原生保证（**无 `bUseIncrementalISM` 引擎属性**，不应透传赋值，审计）
- 应用全部参数：StaticMesh / NumCustomDataFloats / 碰撞 / 导航 / Nanite / 阴影 / 绘制距离
- `SetupAttachment(AttachParent)` + `RegisterComponent()`；全部渲染属性（Mesh/阴影/Nanite/剔除距离）均在 Register 前设置，注册时按当前属性构建渲染代理，无需二次 `MarkRenderStateDirty()`

### 3.2 获取或创建（GetOrCreateBucket，唯一创建入口）

- `GetOrCreateBucket(Mesh, BucketIndex, ...)`：按 `(Mesh, BucketIndex)` 获取/创建指定桶（懒创建 + 扩容对齐计数数组）；**首桶传 `BucketIndex=0`**（单桶模式即唯一桶）；桶处于停用态（空桶）时**先复活再返回**（`SetHiddenInGame(false)`）
- `GetBucketComponent(Mesh, BucketIndex)`：只读查询指定桶（不创建，越界/不存在返回 nullptr；首桶传 0）；**仅限游戏线程调用**（不做派发）：无锁裸读与游戏线程写操作（`FindOrAdd` / `SetNum` / 赋值）并发即数据竞争，且返回组件指针的增量用法（`UpdateInstanceTransform` 等）亦要求游戏线程。跨线程需求的正确姿势：在游戏线程取一次指针缓存使用（AFarmlandActor 压力测试即此模式）
- 接口收敛说明：曾有独立的首桶接口（`GetOrCreateISMComponent` / `GetISMComponent` / `GetFirstBucketComponent`），v2.2 起全部删除——首桶无独立语义，就是 `Bucket[0]`

### 3.3 线程约定（核心规范）

**`UISMManager` 全部组件级操作——创建组件、增删实例、更新 Transform / CustomData——必须在游戏线程执行。** 这是本管理器的**硬性线程约定**，所有对外接口均以此为前提。依据如下：

1. **ISM 组件创建**（`NewObject` + `RegisterComponent`）属 UObject 创建 / 组件注册，游戏线程专属；
2. **ISM 组件内部写接口**（`AddInstancesById` / `RemoveInstancesById` / `UpdateInstanceTransform` / `SetCustomDataById` / `SetCustomDataValueById`）直接修改 `UInstancedStaticMeshComponent` 成员（`PerInstanceSMData` / `PerInstanceSMCustomData` / `PerInstancePrevTransform`），**无锁、无线程派发**（`InstancedStaticMesh.cpp` 全文件无 `IsInGameThread` / `ScopeLock` / `FFunctionGraphTask` / `ENamedThreads`），设计上仅在游戏线程调用；
3. 本管理器接口直接透传上述引擎接口，因此同样**仅限游戏线程**。

**调用约束**：

- 所有对外接口必须由**游戏线程**调用，**禁止子线程直接调用**（不依赖管理器内部派发）；
- 跨线程需求的正确姿势：worker 线程**只收集、不碰 ISM**（如 `ParallelForEachEntityChunk` 并行收集），回到游戏线程后统一提交写接口；
- 视觉处理器（`Cell/Plot/CropVisualSyncProcessor`）统一 `bRequiresGameThreadExecution = true`，保证其 `Execute` 运行在游戏线程，从而安全调用本管理器；
- 为防误用，本管理器对外接口在**非游戏线程时断言**（`check(IsInGameThread())`）；
- **`GetOrCreateBucket` 历史"入口即派发"机制已按约定移除**——不再隐式切线程，调用端自行保证游戏线程（跨线程如有需要，程序化保证由视觉处理器统一承担）。

---

## 四、分桶机制（v2.0：位置判据空间分桶）

### 4.1 分桶参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `INSTANCES_PER_BUCKET` | 2048 | **唯一分桶参数**：参与空间划分（决定块大小）+ 容量保险 |
| `BucketingThreshold`（成员默认） | 4096 | 兼容开关（`SetBucketingThreshold` 可调；阈值 ≤ 0 = 强制单桶，无"0 = 2048 逻辑"语义） |
| `FISMBucketGroup.BucketingThreshold` | 0 | 单 Mesh 覆盖阈值（创建组件时继承全局值；≤ 0 = 强制单桶） |
| `bDisableBuckets` | false | 强制单桶 |

### 4.2 分桶判据（位置，非容量）

```
分桶判据 = 实例实际位置（格子坐标），永不"桶满才分裂"
- 空间分桶：同一空间区域的实例必然落入同一桶（空间局部性 → 包围盒紧凑 → 剔除生效）
- 密铺/离散统一使用同一套空间分桶逻辑（实际每桶数量 = 空间分布自然结果）
- 桶数 = 星球网格划分结果，随网格规模自适应（无需重新标定）
```

### 4.3 球面经纬块映射（管理器内部，FR-6）

映射公式（O(1)）：

```
BucketIdx = (LatIdx / LatBlockSize) * LonBlockNum + (LonIdx / LonBlockSize)
LatBlockNum = ceil(LatCells / LatBlockSize)
LonBlockNum = ceil(LonCells / LonBlockSize)
```

其中 `LatCells = NumLat - 1`、`LonCells = NumLon`（格点数量 → 格子数量，与 `CreateMassCell` 同源）。

> 分桶输入为**格子坐标**（`FIntPoint`）：调用方传 `GridCoords`（与 `Transforms` 一一对应），管理器内部纯整数映射（clamp → 经纬块）→ 桶索引。调用方不感知桶（不传 key）；**严格同格同桶**（业务寻址语言同源，无浮点边缘误差）。

**块大小推导算法**（每块覆盖 ≤ 2048 个格子，近正方形）：

```
1. 若 LatCells × LonCells ≤ 2048 → 单块（桶数 = 1）
2. 否则按纵横比分配：
   fLat = sqrt(2048 × LatCells / LonCells)
   fLon = sqrt(2048 × LonCells / LatCells)
   LatBlockSize = max(1, round(fLat))
   LonBlockSize = max(1, round(fLon))
3. 面积校验：LatBlockSize × LonBlockSize > 2048 → 长边减 1 直至 ≤ 2048
4. 钳制到网格规模内；桶数 = LatBlockNum × LonBlockNum
```

示例：280×250（70k 格）→ 块 47×43（2021 格）→ **36 桶**（≈需求示例 ~35 桶）；12×36（当前默认资产）→ 432 ≤ 2048 → **单桶**（当前规模自适应不分桶）。

### 4.4 空桶事件驱动停用（PR-6）

**意义**：空组件（0 实例）本身不产生 draw 提交；停用的收益在：① 渲染收集/提交路径跳过（C 实验实测每空组件相关性判断 0.1~0.3μs，空桶达数百~上千时累计可观）；② 停用状态 = 明确的空桶语义标记（`DebugPrintBucketStats` 输出，V-4 验收依据）。

```
BatchRemoveInstances：
  每组 RemoveInstancesById 后，BucketInstanceCounts[桶] -= 移除数
  归零 → 停用：SetHiddenInGame(true)（组件常驻，组件级池化）
GetOrCreateBucket（重新 Add）：
  停用桶 → SetHiddenInGame(false) 复活 → 计数重置
```

- **事件驱动**：停用判定只发生在 Remove 调用内，**无每帧轮询**
- **空桶常驻**：不销毁、不重建，避免运行期组件创建/销毁抖动
- **计数维护**：`BucketInstanceCounts` 增删同步维护（不依赖 `GetInstanceCount()`）

### 4.5 实例定位

- 批量添加时，每个实例的 `FGlobalInstanceId.BucketIndex` 由管理器**内部球面映射**按格子坐标推导（私有 `GetBucketKey(GridCoords)`）
- 后续所有操作（移除、CustomData 更新）都通过 `BucketIndex` 找到正确的桶
- 分桶对调用方**完全透明**——外部只需传 `Transforms` + `GridCoords` + `bWorldSpace`、持有 `FGlobalInstanceId`

---

## 五、批量接口

### 5.1 BatchAddInstances（v2.0：空间分桶）

```cpp
TArray<FGlobalInstanceId> BatchAddInstances(
    UStaticMesh* Mesh,
    const TArray<FTransform>& Transforms,
    const TArray<FIntPoint>& GridCoords,   // 与 Transforms 一一对应；空 = 全部单桶
    const int32 NumCustomDataFloats,
    const FISMComponentParams& ISMParams,
    bool bWorldSpace = false,   // 与引擎 AddInstancesById 同语义：false=局部坐标，true=世界坐标
    const TArray<float>& CustomData = TArray<float>());
```

**流程**：

```
1. bDisableBuckets 或未配置球面分桶（SphereConfig 无效）或 GridCoords 为空 → 单桶（Buckets[0]）
2. 否则：BucketIdx = GetBucketKey(GridCoords[i]) → 按桶分组   // 纯整数，与 bWorldSpace 无关
   → 每组 GetOrCreateBucket（复活停用桶）→ AddInstancesById(切片, bWorldSpace) → SetCustomDataById
   → 收集 FGlobalInstanceId(BucketIdx, LocalId)
3. 返回与输入 Transform 一一对应的句柄数组（保序）
```

- **分桶与坐标空间解耦**：分桶输入 = `GridCoords`（纯整数格子坐标，业务寻址语言），不依赖实例位置/`bWorldSpace`；`bWorldSpace` 与引擎 `AddInstancesById(Transforms, bWorldSpace, ...)` 参数完全一致，直接**透传**，默认 false = 局部坐标。S1/S2/S3 现行调用方传的均为世界坐标（星球中心在原点），须显式传 `true`（FR-13）。
- **CustomData 严格校验**：`CustomData.Num() == 实例数 × NumCustomDataFloats` 才写入，否则**静默跳过**
- 不再按全局序号切块（v1.0 行为），`INSTANCES_PER_BUCKET` 交给内部球面映射推导块大小

### 5.2 BatchRemoveInstances

```cpp
void BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds);
```

- 按 `BucketIndex` 分组，再按组调用 `RemoveInstancesById`（避免逐桶遍历）
- 跳过无效 ID
- 每组移除后更新计数，归零 → 事件驱动停用（§4.4）

### 5.3 BatchUpdateInstanceCustomData

```cpp
void BatchUpdateInstanceCustomData(UStaticMesh* Mesh,
    const TArray<FGlobalInstanceId>& InstanceIds, const TArray<float>& CustomData) const;
```

- 同样按 `BucketIndex` 分组批量设置
- 校验 `CustomData.Num() == 实例数 × FloatsPerInstance`

### 5.4 UpdateInstanceCustomData（单实例，自动分流）

```cpp
void UpdateInstanceCustomData(UStaticMesh* Mesh,
    FGlobalInstanceId InstanceId, int32 CustomDataIndex, float Value);
```

```
Bucket->SetCustomDataValueById(InstanceId.LocalId, CustomDataIndex, Value);  // 引擎原生增量路径
```

> 三大系统统一使用的接口——引擎原生接口本身即增量路径，无需额外分流。

---

## 六、对象池接口（已移除）

> **2026-08-07 起已移除**：实例级透明池化（`AcquireSlot` / `ReleaseSlot` / `PreAllocateSlots` / `FlushAllIncrementalComponents` / `SetBatchExpandSize`）与自研增量组件一起被清理。
> 架构改为 **空间分桶 + 引擎原生增量 API**：实例增删走 `BatchAddInstances` / `BatchRemoveInstances`（引擎内建 free list 复用槽位），空桶按需懒创建，不再需要外部池。

---

## 七、三大系统接入方式

| 系统 | 接入点 | 关键调用 |
|------|--------|---------|
| Cell 轮廓 | `PrecreateISMComponents` 预创建普通组件；`CellVisualSyncProcessor` 批量增删改 | `GetOrCreateBucket(Mesh, 0, ...)` + `BatchAdd/Remove/SetCustomData`（单桶，`bDisableBuckets`） |
| Plot 地块 | `CreateMassCell` 全量创建；`PlotVisualSyncProcessor` 更新 CustomData | `ConfigureSphereBuckets` + `BatchAddInstances`（全量，填 `GridCoords`）+ `UpdateInstanceCustomData` |
| Crop 作物 | `PrecreateISMComponents` 预创建组件；`CropVisualSyncProcessor` 增删改 | `BatchAddInstances` / `BatchRemoveInstances`（持有 `FGlobalInstanceId`） |

**生命周期**：`UCellManagerComponent::Initialize` → `ConfigureSphereBuckets`（解析 PlanetConfig 填充球面配置，一次性注入）→ `PrecreateISMComponents`（预创建组件）→ `CreateMassCell`（全量添加实例）→ 各 Processor 运行时增删改。

---

## 八、关键接口清单

| 接口 | 类别 | 说明 |
|------|------|------|
| `GetOrCreateBucket` | 组件 | 获取或创建指定索引的桶（唯一创建入口；首桶传 0；停用桶自动复活；入口即派发线程安全） |
| `GetBucketComponent` | 组件 | 只获取指定索引的桶组件（不创建；首桶传 0；压力测试等外部直接操作组件的场景用） |
| `ConfigureSphereBuckets` | 配置 | 注入球面分桶配置（纯数据；未配置 = 全部单桶） |
| `BatchAddInstances` | 批量 | 批量添加实例（`GridCoords` 格子坐标分桶 + `bWorldSpace` 坐标空间参数 + CustomData 校验） |
| `BatchRemoveInstances` | 批量 | 按桶分组批量移除（空桶事件驱动停用） |
| `BatchUpdateInstanceCustomData` | 批量 | 按桶分组批量更新 CustomData |
| `UpdateInstanceCustomData` | 单实例 | 单值更新（引擎原生增量路径） |
| `SetBucketingThreshold` | 配置 | 分桶阈值兼容开关（默认 4096） |
| `DebugPrintBucketStats` | 调试 | 打印各 Mesh 的桶数、实例数、块大小、包围盒与停用状态 |

---

## 九、已知问题与注意事项

1. **CustomData 严格校验**：`BatchAddInstances` / `BatchUpdateInstanceCustomData` 对 CustomData 长度有强校验，不匹配则**静默跳过**，无日志——排查"材质无反应"问题时优先检查 `NumCustomDataFloats` 一致性。
2. **球面配置注入时机**：必须先 `ConfigureSphereBuckets` 再批量添加（未配置时全部单桶，功能正确但无剔除收益）——`DebugPrintBucketStats` 显示单桶分布时应优先检查配置注入。
3. **`GridCoords` 与 `Transforms` 一一对应**：长度必须一致（或为空 = 单桶），且格子坐标与实例实际位置同源（业务寻址一致）；错位会导致实例落入错误桶（不丢失，但空间局部性受损）。
4. **引擎原生增量路径**：5.4+ ById API 即 O(1) 增量更新（无开关属性），无需手动 Flush。
5. **球面配置一致性**：球面配置（`LatCells`/`LonCells`）由调用方解析 PlanetConfig 填充（与 `CreateMassCell` 同源），网格配置变更需重新 `ConfigureSphereBuckets`，避免划分错位。
6. **空桶停用是事件驱动**：停用发生在 Remove 归零瞬间；桶组件常驻复用，禁止外部每帧轮询其状态。
7. **分桶规模自适应**：块大小按 2048/桶推导，桶数 ≈ 总格子数/2048（示例 70k → ~36 桶）；当前默认资产（12×36=432 格）自动单桶。
8. **线程约定（v3.0 起）**：**所有管理器接口仅限游戏线程调用**（ISM 组件创建 + 写接口均游戏线程专属）。`GetOrCreateBucket` 历史"入口即派发"已移除；只读接口（`GetBucketComponent`）同样仅限游戏线程（无锁读与写并发竞争 + 组件增量写法限游戏线程），跨线程调用方应在游戏线程缓存组件指针。处理器需跨线程时遵循"worker 收集 + 游戏线程提交"范式（见 §3.3）。
9. **`Deinitialize` 清空所有状态**：`MeshToBucketGroups` 全部释放。
10. **轮廓组件曾因"增量组件 + 非增量接口"组合导致不显示**：已回退为普通 ISM（方案 A），见《单元格系统技术文档》。
11. **坐标空间参数 `bWorldSpace`**：默认 false = 局部坐标（与引擎 `AddInstancesById` 一致）；传入世界坐标必须显式传 `true`——组件挂载非单位时错传会渲染错位（分桶归属不受影响，分桶输入 = 格子坐标）。排查实例位置异常时先确认调用方声明的空间与实际数据一致（现状三处调用方均为世界坐标，显式传 `true`）。
12. **非 Nanite 组件 Bounds 无需手动更新（踩坑记录）**：曾因“单桶 + 全部 Bounds=(0,0,0)”的调试现象误判引擎 ById 增量 API 不自动重算组件 Bounds，遂在 `BatchAddInstances` 空桶→非空处与 `BatchRemoveInstances` 归零处显式调用 `UpdateBounds()` 并把“必须手动维护”计入文档。经 UE 5.4+ 源码查证该判断**有误**：引擎所有实例数据变更（`Add` / `Remove` / `UpdateTransform` / `CustomData`）最终都汇聚到 `FPrimitiveInstanceDataManager` 的 `MarkComponentRenderInstancesDirty()` → `MarkRenderInstancesDirty()`（ActorComponent.cpp，需 `IsRegistered()` 且 `bRenderStateCreated`）→ 标记帧末更新 → `SendRenderInstanceData_Concurrent()` 内 `FlushChanges()` 成功后**自动 `UpdateBounds()`**（InstancedStaticMesh.cpp）。因此运行期手动 `UpdateBounds()` 是**冗余**且非必要的，本项目已**全部移除**（仅保留 `SetHiddenInGame(true)` 空桶停用）。注意：当初 “Bounds=(0,0,0)” 的真因是 `GetOrCreateBucket` 未继承全局 `BucketingThreshold`（见 §11 第 1 轮），并非引擎不自动更新——移除手动更新不会影响该修复结论。

---

## 十、相关文件索引

| 文件 | 职责 |
|------|------|
| `Source/ProjectB/Public/Subsystem/ISMManager.h` | 管理器接口、数据结构与球面分桶配置声明 |
| `Source/ProjectB/Private/Subsystem/ISMManager.cpp` | 内部球面映射、分桶、批量、增量实现 |
| `docs/渲染体系/ISMManager空间分桶重构方案.md` | 重构方案（球面化设计、块大小算法、实施步骤） |
| `docs/渲染体系/ISM分桶与增量渲染_需求文档.md` | 需求文档 v2.6（FR-2/FR-6/FR-7、C-5/C-7、PR-6 等） |
| `Source/ProjectB/ProjectB.h` | `ECC_Cell` 定义 |

> 注：`IncrementalInstancedStaticMeshComponent.h/.cpp`（自研增量组件）与 `ISMPoolManager.h/.cpp`（旧版池管理器）已删除；《增量ISM组件重构方案.md》已弃用。

---

## 十一、性能验证结论（2026-08-20）

### 11.1 验证方法

- **测试台**：`AFarmlandActor`（`Farm/FarmlandActor.h/.cpp`，作物压力场景样例），DebugGame Editor 运行（引擎 Development → 性能数据可信）
- **主场景**：70,000 实例平面网格（Cols=265）映射球面网格（Lat=Lon=Cols）→ 36 桶；每帧 106 次阶段切换（Remove+Add）、100 次增量变换更新、100 次 CustomData 更新；运行 45 秒取稳态；随机种子 12345 可复现
- **三轮 PIE 迭代**（每轮日志回贴核对后修复）：

| 轮次 | 现象 | 根因 | 修复 |
|------|------|------|------|
| 1 | 单桶（`Buckets: 1`）+ 全部 Bounds=(0,0,0) | `GetOrCreateBucket` 未继承全局 `BucketingThreshold`（当时误判为“ById 增量 API 不自动更新 Bounds”，见 §9-12） | 新组创建即继承配置（当时为压制现象误加“空桶→非空时显式 `UpdateBounds()`”，该手动维护已按 §9-12 移除） |
| 2 | Bounds 修复（~13200 全图）但仍单桶 | **判定时序鸡生蛋**：分桶判定先于继承执行（判定在 `BatchAddInstances` 内、继承在 `GetOrCreateBucket` 内——分桶路径才调用，而判定已失败走了单桶路径） | `BatchAddInstances` FindOrAdd 后立即继承（预创建组不覆盖） |
| 3 | 分桶成功：每 Mesh 36 桶全 Active、Bounds 局部化 ~2200（= 45 格 × 100cm）、实例分布精确 | — | — |

### 11.2 实测数据（稳态均值）

| 场景 | fps | tick avg | switches 段（106/帧） | 说明 |
|------|-----|----------|----------------------|------|
| 单桶 + 零 Bounds | 33.6 ~ 34.4 | 0.83ms | 0.58ms | 剔除失效 |
| 单桶 + Bounds 修复 | 36.2 ~ 43.0 | 0.96 ~ 1.21ms | 0.68 ~ 0.97ms | 全图 Bounds，剔除仍无效 |
| 36 桶默认视角 | 40.0 ~ 48.0（均值 44.6） | 0.91 ~ 1.02ms | 0.61 ~ 0.70ms | 分桶生效，默认视野覆盖大半地图 |
| 36 桶 + 拉高相机/朝向天空 | **60+** | — | — | 视锥剔除生效，提交量骤降 |
| 剔除失效 + 超大实例数 | **10 ~ 20** | — | — | GPU 提交量上限压力 |

### 11.3 结论

1. **GPU 提交量是唯一显著瓶颈**：frame time ≈ 22.7ms（44fps）而游戏线程 tick 仅 ~0.95ms（≈1000fps 容量）——CPU 侧远未饱和
2. **分桶 + 视锥剔除收益显著**：剔除生效 60+ vs 剔除失效+超大实例 10~20fps；Bounds 按桶局部化（45 格 × 100cm）是剔除生效的前提
3. **CPU 增量操作与剔除正交**：切换（Remove+Add）与变换/CustomData 更新是游戏线程增量 API 调用，不随剔除状态变化；成本线性且低（106→583 切换线性外推 ~3.6ms，仍在 16.6ms 帧预算内），各档 fps 下限 ≥ 40
4. **需求三档达标**：平均档 60fps（剔除生效）✅；640 峰值切换（tick 预算内）✅；远期超限档（分桶自适应，50 万实例 → 256 桶）✅

### 11.4 判读基准

- 桶数核对：`DebugPrintBucketStats` 输出桶数 = `LatBlockNum × LonBlockNum` 推导值（未配置 = 1）（V-4 验证点）
- 剔除收益归因：同视角 A/B 对照（`bConfigureSphereBuckets` 开/关 + 相同相机位置）——单桶 Bounds 覆盖全图，无论相机在哪都全量提交
- 分桶正确性：每桶计数求和 = TotalInstances（随机种子 12345 复现）

### 11.5 已知缺口

- **PR-6 空桶事件驱动运行期观测**：`DebugPrintBucketStats` 仅构建后打印一次，运行期 Active/Disabled 变化不可见；需管理器增加周期统计日志后补专项验证
- 细分收益曲线（`InstancesPerBucket` = 1024/512 → 81/156 桶）未跑，结论已表明剔除收益主导，此项仅作数据留存

---

## 十二、引擎 API 使用明细（UE 5.5.4）

> 本章仅收录本管理器实际调用的引擎 ISM 增量化接口。要点：**引擎 ById 体系（UE 5.4+）是原生增量路径，无开关属性**；**ById 批量接口并不齐备**——Add/Remove/CustomData 有 ById 批量版本，而 **Transform 只有单实例版本**（替代语义见 §12.4 ①）。

### 12.1 组件创建与配置（CreateISMComponent）

| 引擎 API | 用途 | 备注 |
|----------|------|------|
| `NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient)` | 创建普通 ISM 组件 | `RF_Transient` 由引擎托管；始终用普通 ISM，增量路径由 ById 原生保证 |
| `SetStaticMesh(Mesh)` | 绑定网格体 | — |
| `SetNumCustomDataFloats(N)` | 固化每实例 CustomData 数 | 调用后**重分配整个 CustomData 缓冲区并全部清零**，需在添加前调用一次 |
| `SetCollisionEnabled/SetCollisionObjectType/SetCollisionResponseToAllChannels` | 碰撞配置 | 默认无碰撞 |
| `SetCanEverAffectNavigation(b)` | 是否影响寻路 | 默认 `false` |
| `bDisallowNanite = b` | 禁用 Nanite | 无 Nanite 支持的网格体置 `true`，走传统渲染路径 |
| `SetCastShadow(b)` | 阴影 | 默认 `true` |
| `SetCullDistances(Min, Max)` | 组件级距离剔除 | 仅当 Min/Max > 0 时调用 |
| `SetupAttachment(AttachParent)` + `RegisterComponent()` | 挂载并注册 | 全部渲染/碰撞属性在 Register 前设置，注册时按当前属性构建代理 |

### 12.2 实例增删（BatchAddInstances / BatchRemoveInstances）

| 引擎 API | 签名要点 | 语义 |
|----------|---------|------|
| `AddInstancesById(TArrayView<const FTransform>, bWorldSpace, bUpdateNavigation=true)` | `TArrayView<const FTransform>` + `bool` | 批量添加，**返回 `TArray<FPrimitiveInstanceId>`（稳定 ID，非数组下标）** |
| `SetCustomDataById(TArrayView<const FPrimitiveInstanceId>, TArrayView<const float>)` | Id 数组 + CustomData 扁平数组 | 批量写整段 CustomData（每实例 `FloatsPerInstance` 个分量） |
| `RemoveInstancesById(TArrayView<const FPrimitiveInstanceId>, bUpdateNavigation=true)` | Id 数组 | 批量移除，引擎内建 free list 复用槽位 |

**ById API 前置约束**（引擎头文件原文注释）：`Only be used if no other manipulations are performed that cause invalidation of IDs`——**不能用于 HISM**，GIS 经编辑器修改后 ID 追踪可能丢失。本项目统一用普通 ISM + 运行期 ById，满足约束。

### 12.3 单实例更新（Transform / CustomData）

| 引擎 API | 签名 | 语义 |
|----------|------|------|
| `UpdateInstanceTransformById(FPrimitiveInstanceId, FTransform, bWorldSpace=false, bTeleport=false)` | 单 Id | **唯一**的 ById 变换更新接口；内部 `IdToIndex` 转下标后调 `UpdateInstanceTransform(..., bMarkRenderStateDirty=false, ...)`，**不触发全量重建** |
| `SetCustomDataValueById(FPrimitiveInstanceId, CustomDataIndex, Value)` | 单 Id | 单实例改单个 float 分量；**低频呼吸**（病害/水分）用 |

### 12.4 关键引擎机制（易踩坑）

**① Transform 没有 ById 批量接口**：引擎 `BatchUpdateInstancesTransforms(int32 StartInstanceIndex, ...)` 按**连续实例下标**工作，不认 `FPrimitiveInstanceId` 句柄；`UpdateInstances(TArray<int32> UpdateInstanceIds, ...)` 虽可按索引数组批量，但 `UE_DEPRECATED(5.4)`（正是被 ById 体系替代）。因此批量变换只能**逐实例 `UpdateInstanceTransformById`**；批量合并在引擎帧末 `FlushChanges` 内自动聚合（见 §9-12），调用层分组无收益。

**② 帧末自动 `UpdateBounds`**：所有 ById 数据变更（Add/Remove/UpdateTransform/CustomData）最终都汇聚到 `FPrimitiveInstanceDataManager::MarkComponentRenderInstancesDirty()` → `MarkRenderInstancesDirty()`（需 `IsRegistered()` 且 `bRenderStateCreated`）→ 帧末 `SendRenderInstanceData_Concurrent()` 内 `FlushChanges()` 成功后**自动 `UpdateBounds()`**。运行期**无需手动更新 Bounds**（见 §9-12）。

**③ `FPrimitiveInstanceId::IsValid()` 是弱判定**：只判 `Id != INDEX_NONE`，**不校验 Id→Index 映射是否存在**。调用端需先确认句柄来源有效（本管理器以 `FGlobalInstanceId` 句柄包装，增删改前先 `Gid.IsValid()` + `Group->Buckets.IsValidIndex()` 双重过滤）。

**④ 资源位置**：上述接口实现在 `Engine\Source\Runtime\Engine\Private\InstancedStaticMesh.cpp` 与 `Public\InstancedStaticMesh\ISMInstanceDataManager.h`（UE 5.4+ 路径，源码从 `<EngineRoot>\Source` 直读）。
