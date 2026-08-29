# ISMManager 空间分桶重构方案

- 版本：v1.1（2026-08-19）
- 状态：方案定案，待实施
- 分支：MassCrop
- 关联文档：《ISM分桶与增量渲染_需求文档.md》（v2.6 定稿）、《ISMManager技术文档.md》（v2.0，同步更新）、《增量ISM组件重构方案.md》（已弃用）

---

## 一、背景与目标

### 1.1 背景

需求文档 v2.5 定稿了分桶模型：**分桶判据 = 实例实际位置**（空间分桶，永不"桶满才分裂"），唯一参数 = 每桶最大实例数量（`INSTANCES_PER_BUCKET=2048`）**参与空间划分**（块大小 = 覆盖 ≤ 2048 个格子的连续格子块），球面使用经纬块映射，密铺/离散统一。

审计发现 `UISMManager` 现行实现（v1.0）与需求严重不符：

```cpp
// ISMManager.cpp L163（现状）
int32 BucketIdx = CurrentGlobalIdx / INSTANCES_PER_BUCKET;   // 按添加序号切分 = 容量满桶分裂
```

这是**按实例添加序号切分**——第 0~2047 个添加的实例进桶 0，与实例空间位置无关。后果：
- 空间局部性失效：同一区域实例被拆散到不同桶，组件包围盒 ≈ 整体 AABB，视锥/距离剔除失效（需求 §1.1-2 的痛点复现）；
- 分桶收益完全消失（实验 B/C 的 +78% fps 收益无法获得）。

### 1.2 目标

1. 分桶逻辑重构为**位置判据空间分桶**：管理器**内置球面分桶**（球面参考系以纯数据配置注入），球面经纬块先行；不再预留对外可插拔映射层（YAGNI，未来平面场景按需内部抽离）；
2. 唯一参数 `INSTANCES_PER_BUCKET=2048` 参与空间划分（决定块大小），同时保留容量保险语义；
3. 空桶**事件驱动停用**（Remove 归零即停用，无每帧轮询）；
4. **数据注入而非策略注入**：球面网格参数（格子数）由调用方解析 PlanetConfig 后以 `FISMSphereBucketConfig` 一次性注入；分桶输入 = **格子坐标**（业务原生寻址语言，与 `PlantCrop(Coords)` 等 API 同源），分桶规则内聚于管理器内部几何层，调用方不感知桶；
5. **坐标空间显式声明**：`BatchAddInstances` 增加 `bWorldSpace` 参数（引擎 `AddInstancesById` 同语义，默认 false = 局部坐标），显式区分世界/局部输入，根治混用隐患（痛点 1.1-5）；S1/S2/S3 现行调用方显式传 `true`（世界坐标）；**分桶与坐标空间解耦**——分桶输入 = 格子坐标（与位置无关），`bWorldSpace` 纯透传引擎。

### 1.3 范围

| 纳入 | 排除 |
|------|------|
| 管理器内置球面分桶（S2 地块 / S3 作物，球面参数数据注入） | 平面分桶（未来按需从管理器内部抽离策略类，本轮不预支） |
| `BatchAddInstances` 分桶语义改造 + 坐标空间参数（`bWorldSpace`） | M-1~M-3（CropManagerComponent 渲染已注释，另行决策） |
| 空桶事件驱动停用 | 可见性感知优化（OQ-4，P2） |
| `DebugPrintBucketStats` 诊断扩展 | S2 恢复 PlotVisualSyncProcessor（B-6，本轮不恢复） |

---

## 二、现状差距分析

| 需求项（v2.5） | 现状（ISMManager v1.0） | 差距 | 本方案 |
|----------------|------------------------|------|--------|
| FR-2/FR-6 位置判据分桶 | `BucketIdx = 全局序号 / 2048`（容量分裂） | ❌ 核心架构不符 | 管理器内置球面映射（配置数据注入） |
| FR-6 经纬块映射公式 | 无映射层概念 | ❌ 缺失 | 管理器私有球面映射（§3.2） |
| FR-6 桶列对齐 0° 经线 | 无 | ❌ 缺失 | 经度块从 LonIdx=0 起切分，天然对齐 |
| FR-7 唯一参数参与空间划分 | 2048 仅作容量切块 | ❌ 未参与划分 | 块大小推导算法（§3.3） |
| FR-5 懒创建 | `GetOrCreateBucket` 已有 | ✅ | 保留 |
| D-4 组件级池化 | 空桶常驻已有 | ✅ | 保留 |
| PR-6 空桶事件驱动停用 | Remove 后无停用逻辑 | ❌ 缺失 | 移除计数归零即停用（§3.5） |
| C-5 兼容开关 | `BucketingThreshold` 成员默认 4096（`SetBucketingThreshold` 可调），组阈值创建时继承全局值 | ✅ | 保留为兼容开关（阈值 ≤ 0 = 强制单桶，无"0 = 2048 逻辑"语义） |
| C-7 经度回绕 | 无 | ❌ 缺失 | 管理器内部划分规则保证（§3.2） |
| NFR-5 诊断 | `DebugPrintBucketStats` 输出桶数/实例数 | ⚠️ 部分 | 扩展包围盒/块大小/停用状态输出 |

---

## 三、架构设计

### 3.1 总体架构

```
                ┌─────────────────────────────────────┐
                │           UISMManager                │
                │  (WorldSubsystem，外部管理器 D-3)      │
                │                                     │
  ┌───────────┐ │  内部球面映射（私有实现）：            │
  │ 业务侧     │ │  ┌─────────────────────────────┐   │
  │ (数据源)   │ │  │ GridCoords(格子坐标)         │   │
  │ 解析       │──▶│  纯整数 → 经纬块 → BucketIdx │──▶ BucketIdx
  │ PlanetConfig│ │  └─────────────────────────────┘   │
  └───────────┘ │                                     │
  (FISMSphereBucketConfig 一次性注入)                   │
                │  FISMBucketGroup:                    │
                │    Buckets[BucketIdx] ──▶ ISM 组件   │──▶ AddInstancesById
                │    BucketInstanceCounts[激活/停用]     │     SetCustomDataById
                └─────────────────────────────────────┘
```

- **球面分桶配置（纯数据）**：`FISMSphereBucketConfig`（`LatCells`/`LonCells`/`InstancesPerBucket`）由调用方解析 PlanetConfig 后一次性注入（`ConfigureSphereBuckets`）——业务知识以**数据**流入管理器，依赖方向正常；未配置 = 全部单桶（功能正确，无剔除收益）；
- **内部球面映射（私有实现）**：格子坐标 → 桶索引的映射逻辑（经纬块划分、0° 经线对齐、块大小推导）完全封装在管理器内部，不对外暴露抽象接口、不做外部注入；S2/S3 共享同一划分，调用方不感知桶；
- **输入**：调用方传 `Transforms`（引擎渲染位置）+ `GridCoords`（分桶依据，业务寻址语言）；分桶对调用方**完全透明**（FR-1/FR-2），与 `bWorldSpace` 解耦；
- **输出**：`FGlobalInstanceId(BucketIdx, LocalId)`，语义与现状一致（D-6）。
- **桶包围盒**：由引擎自动合并实例变换（`CalculateBounds`），管理器无需计算任何球面几何。

### 3.2 球面分桶配置与内部映射（管理器内置）

**配置（纯数据注入，调用方一次填充）**：

```cpp
// 球面分桶配置：由持有业务配置的调用方（UCellManagerComponent::Initialize）
// 解析 PlanetConfig 后一次性注入；未配置 = 全部单桶（功能正确，无剔除收益）
USTRUCT(BlueprintType)
struct FISMSphereBucketConfig
{
    int32 LatCells = 0;       // 纬度格子数（NumLat - 1，与 CreateMassCell 同源）
    int32 LonCells = 0;       // 经度格子数（NumLon）
    int32 InstancesPerBucket = 2048;  // 唯一参数（FR-7/C-5）：块大小推导 + 容量保险
};

/** 配置球面分桶（全局共享，S2/S3 同一划分）；未配置 = 全部单桶 */
void ConfigureSphereBuckets(const FISMSphereBucketConfig& Config);
```

> **配置精简收益**：分桶输入 = 格子坐标（不依赖实例位置）→ 球心/半径/经纬范围从配置中消失，`FISMSphereBucketConfig` 仅剩网格几何（格子数）+ 唯一参数。

**内部球面映射（ISMManager.cpp 私有实现，不对外暴露）**：

```cpp
// 格子坐标 → 桶索引（O(1)，纯整数）；越界 clamp 保证确定性，实例不丢失
int32 UISMManager::GetBucketKey(const FIntPoint& GridCoords) const
{
    const int32 LatIdx = FMath::Clamp(GridCoords.X, 0, LatCells - 1);
    const int32 LonIdx = FMath::Clamp(GridCoords.Y, 0, LonCells - 1);

    // 经纬块公式：桶列边界对齐 0° 经线（LonIdx 从 0 起，天然满足 C-7）
    return (LatIdx / LatBlockSize) * LonBlockNum + (LonIdx / LonBlockSize);
}
```

**设计要点**：

1. **O(1) 纯整数映射**：一次除法 + 一次乘法，无浮点运算（无经纬反算，比位置映射更廉价）；
2. **回绕规避（C-7）**：经度方向从 `LonIdx=0`（0° 经线）开始按固定 `LonBlockSize` 切块，块边界永远落在整数格子列上，任何桶的经纬区间都不会跨越 0° 经线，包围盒不会跨圈撑爆；
3. **边界安全**：`GridCoords` 越界 clamp 到边界格子（实例不丢失，包围盒略扩展）；极点安全由业务网格结构保证（纬度范围 ±60° 无极点奇点）；
4. **线程安全**：配置注入后所有字段只读，无状态，任何线程可安全调用（NFR-2）；
5. **权威格子坐标，严格同格同桶**：分桶输入 = 业务原生寻址语言（`CellManagerComponent` 全部 API 用 `FIntPoint Coords`；`FCellStaticFragment` 原生持有），无浮点边缘误差——同格实例必然同桶（原"位置反算 + 确定性/连续性"论证升级为定义保证）；
6. **与 `bWorldSpace` 解耦**：分桶输入与坐标空间无关（纯整数），`bWorldSpace` 仅透传引擎；局部/世界混用不再影响分桶归属；
7. **数据注入而非策略注入**：网格几何以数据流入管理器（依赖方向正常），分桶规则内聚于管理器内部几何层——不对外暴露抽象接口、不做外部注入，调用方无需算 key。

**为什么"传格子坐标"是正解（本方案定案理由）**：

- 业务域寻址语言 = 格子坐标（`PlantCrop(Coords)` / `HarvestCrop(Coords)` / `UpdateOutline(Coords)` / `GetAllCoords()` 等全部 API；`FCellStaticFragment::Coords` 原生持有）——接口语言与业务语言对齐，调用方零转换（S2 生成循环变量、S3/S1 的 `Cell.Coords` 直接填）；
- 经纬是格子 → 位置的推导中间量（`CreateMassCell` L218），调用方反而没有现成经纬——传经纬需先换算，且仍带浮点边缘；
- 三维 `Transform` 仍需传给引擎（渲染位置，`AddInstancesById` 必需），但不参与分桶；
- 分桶职责唯一化：管理器 = 球面网格分桶（格子坐标 → 桶），调用方 = 业务数据提供者，职责边界清晰。

### 3.3 块大小推导算法（推荐定案：面积 ≤ 2048 且近正方形）

**输入**：`LatCells`、`LonCells`（格子数量）、`S = INSTANCES_PER_BUCKET`（每桶最大实例数量，唯一参数）

**算法**（按网格纵横比分配，块面积 ≤ S 且接近正方形）：

```
1. 若 LatCells × LonCells ≤ S（总格子 ≤ 2048）→ 单块：
       LatBlockSize = LatCells, LonBlockSize = LonCells → 桶数 = 1
2. 否则，按纵横比分配边长：
       fLat = sqrt(S × LatCells / LonCells)
       fLon = sqrt(S × LonCells / LatCells)
       LatBlockSize = max(1, round(fLat))
       LonBlockSize = max(1, round(fLon))
3. 面积校验（确保每块覆盖 ≤ S 格，密铺时每桶 ≤ 2048 实例）：
       while (LatBlockSize × LonBlockSize > S):
           长边减 1（LatBlockSize ≥ LonBlockSize ? LatBlockSize-- : LonBlockSize--）
4. 钳制：LatBlockSize = min(LatBlockSize, LatCells)，
         LonBlockSize = min(LonBlockSize, LonCells)
5. 桶数：LatBlockNum = ceil(LatCells / LatBlockSize)
         LonBlockNum = ceil(LonCells / LonBlockSize)
         总桶数 = LatBlockNum × LonBlockNum
```

**示例验证**：

| 网格（格） | fLat/fLon | 块大小 | 面积 | 桶数 | 备注 |
|-----------|-----------|--------|------|------|------|
| 280 × 250（70k） | 47.9 / 42.8 | 47 × 43 | 2021 ≤ 2048 | 6 × 6 = **36** | ≈ 需求示例 ~35 桶 ✓ |
| 350 × 200（70k） | 59.9 / 34.2 | 60 × 34 | 2040 ≤ 2048 | 6 × 6 = **36** | ✓ |
| 12 × 36（当前默认资产） | — | 12 × 36（单块） | 432 ≤ 2048 | **1** | 当前规模无需分桶，自适应 |
| 560 × 500（远期档：70k 网格分辨率每轴翻倍，总面积 ×4） | 47.9 / 42.8 | 47 × 43 | 2021 ≤ 2048 | 12 × 12 = **144** | ≈ 需求远期 ~140 桶 ✓ |

> 算法特性：桶数 ≈ 总格子数 / 2048（向上取整），**随星球网格规模自适应，无需重新标定**（FR-7/B-5）；块面积恒 ≤ 2048 保证密铺时每桶 ≤ 2048 实例（容量保险，防极端聚集）；块形状接近正方形保证剔除粒度均匀（与实验 B/C 的 BPA≈6 同区间）。

### 3.4 BatchAddInstances 重构流程

**新签名**（分桶输入 = 格子坐标，与引擎 `AddInstancesById` 的 `bWorldSpace` 参数解耦；默认 false = 局部坐标）：

```cpp
TArray<FGlobalInstanceId> BatchAddInstances(
    UStaticMesh* Mesh,
    const TArray<FTransform>& Transforms,
    const TArray<FIntPoint>& GridCoords,  // 新增：与 Transforms 一一对应的格子坐标（业务寻址语言）；空 = 全部单桶
    const int32 NumCustomDataFloats,
    const FISMComponentParams& ISMParams,
    bool bWorldSpace = false,             // 与引擎 AddInstancesById 同语义：false=局部坐标，true=世界坐标
    const TArray<float>& CustomData = TArray<float>());
```

> `bWorldSpace` 与引擎 `UInstancedStaticMeshComponent::AddInstancesById(Transforms, bWorldSpace, bUpdateNavigation)` 参数语义一致（ISMManager.cpp L142/L172 现状硬编码 `true`），**仅透传引擎**（FR-13）；分桶输入 = `GridCoords`（纯整数，与坐标空间无关）。

**流程**：

```
1. 前置校验：Mesh 有效、Transforms 非空、GridCoords 为空或长度 == Transforms 长度
2. 分桶判定：
   ├─ bDisableBuckets 为 true，或未配置球面分桶（SphereConfig 无效），或 GridCoords 为空
   │     → 单桶路径（Buckets[0]）：AddInstancesById(Transforms, bWorldSpace) + SetCustomDataById
   └─ 否则 → 空间分桶路径：
       a. BucketIdx = GetBucketKey(GridCoords[i])   // 管理器内部球面映射（§3.2，纯整数，与 bWorldSpace 无关）
          → 按桶分组（TMap<int32, TArray<int32>>：桶 → 实例下标集合，一次遍历）
       b. 每组：GetOrCreateBucket(复活停用桶) → AddInstancesById(切片 Transform, bWorldSpace)
          → SetCustomDataById(切片 CustomData) → 收集 FGlobalInstanceId(BucketIdx, LocalId)
3. Group.TotalInstanceCount += AddCount
4. 返回与输入一一对应的句柄数组（保序：按输入下标填充）
```

**与现状差异**：删去 `CurrentGlobalIdx / INSTANCES_PER_BUCKET` 容量切分逻辑与场景 1/2 切换判定（`bShouldStartBucketing`）；`INSTANCES_PER_BUCKET` 不再直接切块，而是交给内部球面映射推导块大小（FR-7）；分桶输入 = **格子坐标**（无位置反算、无"世界位置换算"步骤，与 `bWorldSpace` 解耦）；坐标空间由 `bWorldSpace` 显式声明（现状硬编码 `true`，消除混用隐患，痛点 1.1-5）；分桶由管理器内置球面映射承载（配置数据注入，无外部映射层/无 key 参数）。

### 3.5 空桶事件驱动停用（PR-6）

**意义（为何需要"停用"）**：空组件（0 实例）本身不产生 draw 提交，"停用"的收益不在提交量，而在：

1. **渲染收集路径跳过**：隐藏标记使空组件不进入渲染收集/提交分支（C 实验实测每空组件相关性判断 0.1~0.3μs，空桶数量达数百~上千时累计可观）；
2. **包围盒残留防护**：Remove 归零后强制 `UpdateBounds()`，防止引擎增量路径残留旧实例包围盒（残留会误导遮挡剔除与 Debug 统计）；
3. **状态语义**：停用 = 明确的空桶标记，`DebugPrintBucketStats` 输出活跃/停用分布（V-4 验收依据：事件驱动停用生效、无轮询）。

**机制**：

```
BatchRemoveInstances：
  1. 按 BucketIndex 聚合 LocalId（现状已有）
  2. 每组 RemoveInstancesById 后：BucketInstanceCounts[组] -= 移除数
  3. 若该桶计数归零 → 停用：
        Bucket->SetHiddenInGame(true)      // 轻量停用，组件常驻（组件级池化 D-4）
        Bucket->UpdateBounds()             // 强制更新包围盒，防膨胀（记忆规范）
  4. 统计输出（NFR-5）

GetOrCreateBucket（重新 Add 时）：
  - 组件存在但处于停用态 → SetHiddenInGame(false) 复活，计数重置
```

**要点**：
- **事件驱动，无每帧轮询**（PR-6 硬性要求）：停用判定只发生在 Remove 调用内，O(1)；
- **空桶常驻**（D-4）：组件不销毁、不重建，避免运行期创建/销毁抖动；
- **计数来源**：`FISMBucketGroup.BucketInstanceCounts`（TArray<int32>，与 Buckets 对齐），增删同步维护，不依赖 `GetInstanceCount()`（C-6：非 virtual 不可 override，且增量路径下计数遍历成本高）。

### 3.6 数据结构变更

```cpp
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
    // ... 现有字段保持不变（Buckets / TotalInstanceCount / NumCustomDataFloats /
    //      bDisableBuckets / CachedParams / BucketingThreshold）...

    TArray<int32> BucketInstanceCounts;   // 新增：每桶活动实例数（与 Buckets 对齐，事件驱动停用依据）
};
```

> 球面配置**不存于桶组**：由管理器全局持有（`ConfigureSphereBuckets` 注入，S2/S3 共享同一球面划分）。

**球面配置来源与生命周期（数据注入）**：

- 管理器**不解析业务配置**（PlanetConfig = 业务对象）；由持有业务配置的调用方读取并填充数据后注入：

```cpp
// UCellManagerComponent::Initialize（配置点，一次性）
FISMSphereBucketConfig Cfg;
Cfg.LatCells = NumLat - 1;                       // 与 CreateMassCell 同源
Cfg.LonCells = NumLon;
Cfg.InstancesPerBucket = INSTANCES_PER_BUCKET;   // 唯一参数（FR-7/C-5）
ISMManager->ConfigureSphereBuckets(Cfg);
```

- 管理器内部据此推导块大小（§3.3 算法），S2/S3 自动生效（调用方不感知桶）；
- 网格配置变化（如切换星球）→ 调用方重新 `ConfigureSphereBuckets` 即可。

---

## 四、接口变更清单

| 接口 | 变更 | 影响调用方 |
|------|------|-----------|
| `BatchAddInstances` | 增加 `GridCoords`（与 Transforms 一一对应，空 = 单桶）+ `bWorldSpace`（引擎同语义，默认 false=局部）；分桶由内部球面映射承载（按格子坐标，纯整数） | S2/S3 填格子坐标（同源数据）；S1 不传（单桶）；三处显式传 `true` |
| `ConfigureSphereBuckets` | **新增**：注入球面分桶配置（纯数据；未配置 = 全部单桶） | `UCellManagerComponent::Initialize` 调用一次 |
| `DebugPrintBucketStats` | 扩展输出：每桶包围盒尺寸、块大小、停用桶数（NFR-5） | 无 |
| `GetOrCreateISMComponent` / `GetOrCreateBucket` | 内部复活停用桶 | 无（语义不变） |
| `BatchRemoveInstances` | 内部增加停用判定 + UpdateBounds | 无（签名不变） |
| 其余接口（`BatchUpdateInstanceCustomData` / `UpdateInstanceCustomData` / `SetBucketingThreshold` 等） | 不变 | 无 |

### 4.1 调用方适配明细

**CellManagerComponent::Initialize（配置点）**：解析 PlanetConfig（与 `CreateMassCell` 同源计算）填充 `FISMSphereBucketConfig`（`LatCells = NumLat-1`、`LonCells = NumLon`）调用 `ISMManager->ConfigureSphereBuckets(Cfg)`（一次）。

**S2 地块（CellManagerComponent.cpp L260）**：`CreateMassCell` 循环变量 `LatIdx/LonIdx` 直接填 `GridCoords`；调用增加 `bWorldSpace=true`（`CellTransforms` 为世界坐标：L206-210 由 `Radius × 经纬球面公式` 构造，星球中心在原点）。

**S3 作物（CropVisualSyncProcessor.cpp L95）**：`Cell.Coords` 填 `GridCoords`；调用增加 `bWorldSpace=true`（`CropTransform = Cell.Transform + CropOffset`，继承自地块世界坐标）。

**S1 轮廓（CellVisualSyncProcessor.cpp L140）**：`bDisableBuckets` 保持单桶，**不传 `GridCoords`**（空 = 全部单桶）；调用增加 `bWorldSpace=true`（同源于 `Cell.Transform`）。

---

### 4.2 接口形态定案：单个 + 批量 共存（v1.2 新增）

**背景**：引擎增量原生 API 本身就是“单个 + 批量”并存范式（`AddInstancesById` + `AddInstanceById`、`RemoveInstancesById` + `RemoveInstanceById`、`BatchSetCustomDataById` + `SetCustomDataById`，其中单个版本即 `MakeArrayView(&Id, 1)` 薄封装）。管理器据此采用**八接口共存**，调用端按场景选择：低频走单个、高频单帧合批后走批量。

#### 4.2.1 八接口清单

| 操作 | 单个接口 | 批量接口 | 现状 |
|------|---------|---------|------|
| 添加 | `AddInstance` | `BatchAddInstances` | **已落地**（单接口为批量单元素薄封装） |
| 移除 | `RemoveInstance` | `BatchRemoveInstances` | **已落地**（单接口为批量单元素薄封装） |
| 更新 Transform | `UpdateInstanceTransform` | `BatchUpdateInstancesTransforms` | **已落地**（均收敛到管理器：单接口/Batch 内部按桶分组循环 `UpdateInstanceTransformById`（引擎无 ById 批量接口）；不再经 `GetBucketComponent` 直调） |
| 更新 CustomData | `UpdateInstanceCustomData` | `BatchUpdateInstanceCustomData` | **均有**（前者单实例单值，后者按桶分组批量更新） |

#### 4.2.2 调用端策略（单帧合批）

- **高频路径**（Mass 同步处理器）：先累积到局部 `PendingXXX` 数组，帧末一次性调批量接口——局部数组天然线程隔离，并行处理器无需加锁。
- **低频路径**（如 Plot 单值更新）：直接调单个接口。
- **句柄即时性约束**：批量接口当场返回句柄、可同帧回填（Mass fragment / 压测 `Inst.Id`），是硬约束。

#### 4.2.3 边界说明

- **管理器不做内部延迟合批**（排除“调用单接口 → 管理器 Tick 统一合并”形态）：会引入提交延迟 + 句柄回填中间态，与本系统“句柄须即时可用”冲突；合批主动权在调用端（单帧合批），管理器内部不聚合。
- 批量接口承载“分桶分组只算一遍 + 每桶一次引擎调用”的归并价值；单个接口为低频/语义清晰提供便利。
- **Transform 更新的引擎限制**：引擎只有单实例 `UpdateInstanceTransformById`（`bWorldSpace/bTeleport`），**无 ById 批量接口**（`BatchUpdateInstancesTransforms` 按实例数组下标，不具备 ById 增量句柄语义）。故管理器批量 Transform 接口内部“按桶分组一次 + 组内循环 ById 更新”，归并价值在**消除逐桶遍历**（分桶分组只算一遍），引擎调用次数仍为 N（每个实例一次 `UpdateInstanceTransformById`）。

---

## 五、实施步骤

| 步骤 | 内容 | 产出/验证 |
|------|------|----------|
| 0 | **引擎增量重编**（前置）：`Runtime/Engine` 模块编译，使补丁 002 生效（源码已改、二进制仍含 bug，见风险 R7） | 引擎启动后 `UpdateInstanceTransformById` 可直接使用（C-2 修复验证） |
| 1 | ISMManager.h：新增 `FISMSphereBucketConfig` + `ConfigureSphereBuckets` 声明、`FISMBucketGroup` 扩展、新接口签名（含 `GridCoords`） | 编译通过 |
| 2 | ISMManager.cpp：内部球面映射（私有 `GetBucketKey`，格子坐标输入）+ 块大小推导 + `BatchAddInstances` 重构 + 停用逻辑 + `DebugPrintBucketStats` 扩展 | 编译通过 |
| 3 | 调用方适配：CellManagerComponent::Initialize 注入球面配置；S2/S3 填 `GridCoords`、S1 不传；三处调用补 `bWorldSpace=true` | 编译通过 |
| 4 | 单元/手工验证：`DebugPrintBucketStats` 输出桶数符合划分（V-4） | 桶数 = 配置推导值 |
| 5 | 性能验证：V-1 分桶对照复跑（Development 构建，C-3）、稳态/极端档回归 | V-1/V-2/V-3 |
| 6 | 功能回归：种植/收获/阶段切换/病害无视觉异常（V-5） | 回归通过 |
| 7 | **八接口落地 + 调用端合批**：新增单接口薄封装（`AddInstance`/`RemoveInstance`/`UpdateInstanceTransform`）+ 批量 `BatchUpdateInstancesTransforms`；高频调用端（`CropVisualSyncProcessor`/`FarmlandActor` 压测）改为单帧合批（按 Mesh 归组累积 → 批量）；`FarmlandActor` 增量变换从 `GetBucketComponent` 直调收敛到管理器批量接口 | 编译通过；高频路径批量调用（见 §4.2.2） |

---

## 六、验证方案（对应需求 §10）

| 验收项 | 验证方式 |
|--------|----------|
| V-4 组件规模 | `DebugPrintBucketStats`：桶数 = `LatBlockNum × LonBlockNum` 推导值（未配置 = 1）；空桶停用状态正确（移除全部实例后显示停用） |
| V-1 分桶对照 | B/C 组（BPA=5/8）≥ 60fps 复跑；A 组（BPA=1）维持 <40fps |
| V-2/V-3 吞吐 | 平均档 106 / 极端档 583 切换/帧稳态与峰值回归（Development 构建） |
| V-5 迁移回归 | S2/S3 渲染正确性抽查 |
| V-6 远期档 | 50 万实例 @30fps（D 档，28~30fps 区间可接受；网格分辨率每轴翻倍档） |
| V-7 验证环境 | 全部性能验证在 **Development / DebugGame** 构建下进行（C-3） |
| 桶分布正确性 | 日志抽查：同一格子坐标的实例落同一桶（定义保证）；相邻格子落相邻桶；跨 0° 经线的两个边界格子落在不同桶列（回绕规避验证） |

---

## 七、风险与对策

| 编号 | 风险 | 影响 | 对策 |
|------|------|------|------|
| R1 | 未配置球面分桶（`SphereConfig` 无效）或未传 `GridCoords` | 全部走单桶（功能正确，性能退化） | `ConfigureSphereBuckets` 调用点明确；`DebugPrintBucketStats` 暴露单桶状态可发现 |
| R2 | 桶数随网格激增（远期 50 万） | 组件数 ~140 × 81 ≈ 11300 | 块大小自适应（桶数 ≈ 总格/2048），V-6 实测确认 |
| R3 | 球面配置（`LatCells`/`LonCells`）与网格生成不一致（如 PlanetConfig 变更） | 桶划分与格子坐标错位 | 配置单一来源（PlanetConfig 解析，与 `CreateMassCell` 同源）；变更时重新 `ConfigureSphereBuckets` |
| R4 | 跨线程调用（NFR-2） | 球面配置/桶组并发访问 | 配置注入后只读；`GetOrCreateBucket` 沿用既有游戏线程派发机制 |
| R5 | 停用状态与增量路径冲突 | Remove 后立即 Add 同桶 | 复活判定在 `GetOrCreateBucket` 内原子完成（先复活后 Add），无窗口 |
| R6 | `bWorldSpace` 漏传/错传（默认 false = 局部坐标） | 组件挂载非单位时实例空间错位（痛点 1.1-5）；分桶归属不受影响（分桶输入 = 格子坐标） | 调用方显式传 `true`；空间语义抽查（组件挂载非单位场景验证） |
| R7 | 引擎补丁 002 未编译生效（C-2 参数错位 bug 仍存于运行中二进制） | 增量变换更新仍会误触发全量重建 | 实施/验证前完成引擎增量编译（`Runtime/Engine` 模块，见步骤 0）；补丁已登记引擎补丁清单 002（本地提交 9c096042607b）；编译后 `UpdateInstanceTransformById` 可直接使用，无需规避 |

---

## 八、范围外（后续决策）

1. **M-1~M-3**（CropManagerComponent 三处 TODO）：其渲染代码已注释，是否清理或迁移另行决策；
2. **平面分桶**：未来非球面场景需要时，将内部球面映射抽为私有策略类（接口不变）；本轮不预支抽象（YAGNI）；
3. **OQ-4 可见性感知优化** / **OQ-5 远期 50 万补齐**：P2；
4. **块大小调优**：按实测剔除收益微调块大小系数（OQ-1 结论：2048 起步，实测后调优）。

---

## 九、版本记录

- **v1.3（2026-08-22）**：八接口实际落地——新增单实例薄封装 `AddInstance`/`RemoveInstance`/`UpdateInstanceTransform` 与批量 `BatchUpdateInstancesTransforms`（批量 Transform 因引擎无 ById 批量接口，内部按桶分组一次 + 组内循环 `UpdateInstanceTransformById`，归并价值在消除逐桶遍历）；高频调用端合批改造：`CropVisualSyncProcessor` 逐个 Remove/Add → 按 Mesh 归组累积后批量（句柄按实体回填）、`FarmlandActor` 压测 ①阶段切换/②增量变换均改为按 Mesh 归组合批（后者收敛到管理器批量接口，不再经 `GetBucketComponent` 直调）；同步更新 §4.2.1/§4.2.3、实施步骤表新增步骤 7。

- **v1.2（2026-08-22）**：接口形态定案——管理器采用“单个 + 批量”八接口共存（添加/移除/更新 Transform/更新 CustomData 各含单个 + 批量），调用端按场景选择（低频单接口、高频单帧合批后批量）；明确**管理器不做内部延迟合批**（句柄即时性约束），合批主动权在调用端。新增 §4.2 接口形态定案。

- **v1.1（2026-08-19）**：引擎补丁 002 落地（本地提交 9c096042607b，官方修复 ce24c23badd6 / JIRA UE-231099）——实施步骤新增步骤 0（引擎增量重编前置）；风险表新增"补丁未编译生效"项（原 R8 → 现 R7），R 编号顺延修正（原 R5~R8 → R4~R7）；验证表补齐 V-6/V-7 行；关联需求文档升级 v2.6（C-2 由"必须规避"降级为"已修复记录"，保留未打补丁环境规避后备）。
- **v1.0（2026-08-07）**：方案定稿——管理器内置球面分桶（`FISMSphereBucketConfig` 数据注入 + 内部经纬块映射，格子坐标输入，桶列对齐 0° 经线），块大小推导算法（面积 ≤ 2048 近正方形），`BatchAddInstances` 增加 `GridCoords` + `bWorldSpace`，空桶事件驱动停用，接口变更清单与调用方适配明细，实施步骤与验证方案，风险与对策。
