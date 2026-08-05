# ISMManager 技术文档

**版本**: v1.0  
**更新日期**: 2026-08-04  
**分支**: MassCrop  
**服务对象**: Cell（单元格）、Plot（地块）、Crop（农作物）

---

## 一、系统概述

### 1.1 定位与职责

**`UISMManager`** 是一个 **WorldSubsystem**（`Subsystem/ISMManager.h`），随 `UWorld` 自动创建和销毁，全局唯一。它是项目中所有 Instanced Static Mesh（ISM）实例的**统一管理中心**，职责包括：

- **组件管理**：按静态网格体（`UStaticMesh*`）自动创建或复用 ISM 组件
- **分桶管理**：实例数量超过阈值时自动将实例分散到多个 ISM 组件（桶）
- **批量操作**：批量添加/移除实例、批量设置 CustomData
- **对象池**：为增量 ISM 组件提供实例槽位的申请/释放/预分配
- **增量刷新**：统一收集本帧被修改的增量组件并批量 Flush

### 1.2 服务的系统

| 系统 | Mesh | ISM 类型 | 使用接口 |
|------|------|---------|---------|
| Cell（单元格轮廓） | `OutlineMesh` | 普通 ISM | `BatchAddInstances` / `BatchRemoveInstances` / `BatchSetInstanceCustomData` |
| Plot（地块） | `LandCellMesh` | 普通 ISM | `BatchAddInstances`（全量创建）+ `UpdateInstanceCustomData` |
| Crop（农作物） | 作物各阶段 Mesh | 增量 ISM | `AcquireSlot` / `ReleaseSlot` / `PreAllocateSlots` + `FlushAllIncrementalComponents` |

### 1.3 核心设计理念

- **以 Mesh 为键**：所有组件按 `FSoftObjectPath`（软引用路径）索引，资产重定向后键值仍稳定
- **实例句柄稳定**：`FGlobalInstanceId` 对外屏蔽物理索引变化，外部系统无需关心实例在数组中的位置
- **分桶透明化**：分桶由管理器自动决策，外部只需持有 `FGlobalInstanceId`
- **增量与普通并存**：同一套管理器同时服务普通 ISM（全量重建）与增量 ISM（脏队列推送），通过接口自动分流

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
| `bUseIncrementalISM` | false | 创建增量 ISM 组件（`UIncrementalInstancedStaticMeshComponent`） |

### 2.4 FISMBucketGroup（桶组）

```cpp
USTRUCT(BlueprintType)
struct FISMBucketGroup
{
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> Buckets;  // 一个 Mesh 对应的所有桶
    int32 TotalInstanceCount = 0;        // 该 Mesh 的实例总数
    int32 NumCustomDataFloats = 0;       // 每实例 CustomData 数量
    bool bDisableBuckets = false;        // 强制单桶
    FISMComponentParams CachedParams;    // 缓存创建参数（扩容时复用）
    int32 BucketingThreshold = 0;        // 该 Mesh 的分桶阈值（0 使用全局逻辑）
};
```

---

## 三、组件管理

### 3.1 创建组件（CreateISMComponent）

```cpp
static UInstancedStaticMeshComponent* CreateISMComponent(
    UStaticMesh* Mesh, int32 NumCustomDataFloats, const FISMComponentParams& Params);
```

- `bUseIncrementalISM=true` → 创建 `UIncrementalInstancedStaticMeshComponent`，否则普通 `UInstancedStaticMeshComponent`
- 应用全部参数：StaticMesh / NumCustomDataFloats / 碰撞 / 导航 / Nanite / 阴影 / 绘制距离
- `SetupAttachment(AttachParent)` + `RegisterComponent()` + `MarkRenderStateDirty()`

### 3.2 获取或创建（GetOrCreateISMComponent / GetOrCreateBucket）

- `GetOrCreateISMComponent`：按 Mesh 键查桶组，`Buckets[0]` 存在则直接返回；否则创建（用于单桶场景）
- `GetOrCreateBucket`：按 `(Mesh, BucketIndex)` 获取/创建指定桶（用于分桶场景）

### 3.3 线程安全

两个获取接口都做了线程保护：

```
游戏线程调用 → 直接创建
非游戏线程调用 → FFunctionGraphTask 派发到游戏线程执行 + WaitUntilTaskCompletes 等待
```

---

## 四、分桶机制

### 4.1 分桶参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `INSTANCES_PER_BUCKET` | 2048 | 单个桶（ISM 组件）的实例容量上限 |
| `BucketingThreshold`（成员默认） | 4096 | 全局分桶阈值，`SetBucketingThreshold()` 可调整 |
| `FISMBucketGroup.BucketingThreshold` | 0 | 单 Mesh 覆盖阈值（0 使用全局逻辑） |
| `bDisableBuckets` | false | 强制单桶 |

### 4.2 触发逻辑（BatchAddInstances，ISMManager.cpp L148-202）

```
已实例数 + 本次新增数 > BucketingThreshold（4096）？
    ├─ 否 → 场景1（单桶）：全部实例进入 Buckets[0]
    └─ 是 → 场景2（分桶）：按 INSTANCES_PER_BUCKET（2048）切块
             BucketIdx = 全局序号 / 2048，LocalOffset = 全局序号 % 2048
```

**注意**：即使之前是单桶，一旦累计超过阈值也会切到分桶模式（`bShouldStartBucketing` 判定）。

### 4.3 实例定位

- 批量添加时，每个实例的 `FGlobalInstanceId.BucketIndex` 由全局序号推导
- 后续所有操作（移除、CustomData 更新）都通过 `BucketIndex` 找到正确的桶
- 分桶对调用方**完全透明**——外部只持有 `FGlobalInstanceId`

---

## 五、批量接口

### 5.1 BatchAddInstances

```cpp
TArray<FGlobalInstanceId> BatchAddInstances(
    UStaticMesh* Mesh, const TArray<FTransform>& Transforms,
    const int32 NumCustomDataFloats, const FISMComponentParams& ISMParams,
    const TArray<float>& CustomData = TArray<float>());
```

- 内部自动决策单桶/分桶
- **CustomData 严格校验**：`CustomData.Num() == 实例数 × NumCustomDataFloats` 才写入，否则**静默跳过**
- 返回与输入 Transform 一一对应的 `FGlobalInstanceId` 数组

### 5.2 BatchRemoveInstances

```cpp
void BatchRemoveInstances(UStaticMesh* Mesh, const TArray<FGlobalInstanceId>& InstanceIds);
```

- 按 `BucketIndex` 分组，再按组调用 `RemoveInstancesById`（避免逐桶遍历）
- 跳过无效 ID

### 5.3 BatchSetInstanceCustomData

```cpp
void BatchSetInstanceCustomData(UStaticMesh* Mesh,
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
增量组件？ → IncComp->UpdateInstanceCustomDataIncremental（脏队列推送，需 Flush）
普通组件？ → Bucket->SetCustomDataValueById（父类全量路径）
```

> 这是三大系统统一使用的接口——**组件类型不同会自动走不同路径**，无需调用方区分。

---

## 六、对象池接口（仅支持增量 ISM 组件）

> 头文件明确注释：`// ---- 对象池接口（仅支持 bUseIncrementalISM = true 的组件） ----`

### 6.1 AcquireSlot / ReleaseSlot

```cpp
FGlobalInstanceId AcquireSlot(UStaticMesh* Mesh, const FTransform& WorldTransform,
    TArrayView<const float> CustomData = TArrayView<const float>());
void ReleaseSlot(UStaticMesh* Mesh, const FGlobalInstanceId& InstanceId);
```

**AcquireSlot 流程**：
1. 从 `FreeSlotIndices`（MPSC 队列）取空闲槽位
2. 队列为空 → `ExpandCapacity` 扩容（默认每次 512 个）后再取
3. `GetInstanceIndexForId` 将 ID 转物理索引
4. `UpdateInstanceTransformIncremental`（世界变换 → 局部变换写入）
5. 有 CustomData → `UpdateInstanceCustomDataIncremental`
6. 标记该组件到 `DirtyIncrementalComponents`（待 Flush）

**ReleaseSlot 流程**：
1. `SetInstanceVisibility(InstanceIndex, false)` 隐藏（零缩放 + 移到远处/原点）
2. 将 `FGlobalInstanceId` 放回 `FreeSlotIndices` 队列供复用
3. 标记 Dirty（隐藏也要推送渲染线程）

### 6.2 ExpandCapacity / PreAllocateSlots

```cpp
void ExpandCapacity(UStaticMesh* Mesh, int32 AdditionalCount);  // 扩容（默认 512/批）
void PreAllocateSlots(UStaticMesh* Mesh, int32 Count);          // 预分配（= ExpandCapacity）
```

**占位实例策略**（`ISMManager.cpp` L370-376）：

```
网格是 Nanite → 占位实例初始变换 = FTransform(ParkedLocation)（极远处 1e7,1e7,-1e7）
网格非 Nanite → 占位实例初始变换 = FTransform::Identity（原点）
```

- `ParkedLocation(1.0e7, 1.0e7, -1.0e7)` 定义于 `ProjectB.h`
- 占位实例创建后全部入空闲队列，由 `AcquireSlot` 逐步"点亮"
- 预分配的意义：运行期零分配，且物理索引保持稳定

### 6.3 FlushAllIncrementalComponents

```cpp
void FlushAllIncrementalComponents();
```

- 遍历 `DirtyIncrementalComponents`（本帧被触碰过的增量组件），逐个 `FlushIncrementalUpdates()`
- 将脏队列中的变更批量推送到渲染线程（GPU Scene）
- **增量组件的所有修改必须最终通过本接口提交才会显示**

### 6.4 隐藏策略（SetInstanceVisibility，增量组件）

```
显示（true） → CustomData[0] = 1.0（不透明度还原；不碰 Transform）
隐藏（false）→ 非 Nanite：移到原点 + 零缩放
             Nanite：移到 ParkedLocation（1e7,1e7,-1e7）+ 零缩放
```

---

## 七、三大系统接入方式

| 系统 | 接入点 | 关键调用 |
|------|--------|---------|
| Cell 轮廓 | `PrecreateISMComponents` 预创建普通组件；`CellVisualSyncProcessor` 批量增删改 | `GetOrCreateISMComponent` + `BatchAdd/Remove/SetCustomData` |
| Plot 地块 | `CreateMassCell` 全量创建；`PlotVisualSyncProcessor` 更新 CustomData | `BatchAddInstances`（全量）+ `UpdateInstanceCustomData` |
| Crop 作物 | `PrecreateISMComponents` 预创建增量组件 + `PreAllocateSlots`；`CropVisualSyncProcessor` 增删改 | `AcquireSlot` / `ReleaseSlot` / `FlushAllIncrementalComponents` |

**生命周期**：`UCellManagerComponent::Initialize` → `PrecreateISMComponents`（预创建组件）→ `CreateMassCell`（全量添加实例）→ 各 Processor 运行时增删改。

---

## 八、关键接口清单

| 接口 | 类别 | 说明 |
|------|------|------|
| `GetOrCreateISMComponent` | 组件 | 获取或创建某 Mesh 的组件（单桶首桶） |
| `GetISMComponent` | 组件 | 只获取不创建 |
| `GetOrCreateBucket` | 组件 | 获取或创建指定索引的桶 |
| `BatchAddInstances` | 批量 | 批量添加实例（自动分桶 + CustomData 校验） |
| `BatchRemoveInstances` | 批量 | 按桶分组批量移除 |
| `BatchSetInstanceCustomData` | 批量 | 按桶分组批量设置 CustomData |
| `UpdateInstanceCustomData` | 单实例 | 单值更新（增量/普通自动分流） |
| `AcquireSlot` / `ReleaseSlot` | 对象池 | 槽位申请/释放（仅增量组件） |
| `PreAllocateSlots` | 对象池 | 预分配槽位 |
| `SetBatchExpandSize` | 配置 | 扩容批次（默认 512） |
| `SetBucketingThreshold` | 配置 | 分桶阈值（默认 4096） |
| `FlushAllIncrementalComponents` | 刷新 | 提交所有脏增量组件到渲染线程 |
| `DebugPrintBucketStats` | 调试 | 打印各 Mesh 的桶数与实例数 |

---

## 九、已知问题与注意事项

1. **增量组件必须走增量接口**：`UIncrementalInstancedStaticMeshComponent` 重写了 `SendRenderInstanceData_Concurrent`（空操作），父类接口（`AddInstancesById` / `SetCustomDataById` / `RemoveInstancesById`）写入的数据**不会同步到渲染线程**。增量组件只能使用 `AcquireSlot` / `ReleaseSlot` / `UpdateInstanceCustomData`（增量分支）+ `FlushAllIncrementalComponents`。
2. **CustomData 严格校验**：`BatchAddInstances` / `BatchSetInstanceCustomData` 对 CustomData 长度有强校验，不匹配则**静默跳过**，无日志——排查"材质无反应"问题时优先检查 `NumCustomDataFloats` 一致性。
3. **对象池接口仅支持增量组件**：对普通 ISM 调用 `AcquireSlot` 会因 `Cast<UIncrementalInstancedStaticMeshComponent>` 失败而返回无效 ID。
4. **分桶触发取决于实例数**：`BucketingThreshold=4096`，是否分桶由实际实例数决定（地块/轮廓由 `DA_PlanetConfig` 蓝图配置的 `NumLat`/`NumLon` 决定，代码默认值仅为预设）。
5. **`ParkedLocation` 定义在 `ProjectB.h`**：隐藏占位实例的"停靠点"，坐标为 `(1e7, 1e7, -1e7)`，确保移出视锥范围。
6. **非游戏线程调用创建接口**：会自动派发任务到游戏线程并同步等待（`WaitUntilTaskCompletes`），调用方需接受可能的阻塞。
7. **`Deinitialize` 清空所有状态**：`MeshToBucketGroups` / `DirtyIncrementalComponents` / `FreeSlotIndices` 全部释放。
8. **轮廓组件曾因"增量组件 + 非增量接口"组合导致不显示**：已回退为普通 ISM（方案 A），见《单元格系统技术文档》。

---

## 十、相关文件索引

| 文件 | 职责 |
|------|------|
| `Source/ProjectB/Public/Subsystem/ISMManager.h` | 管理器接口与数据结构定义 |
| `Source/ProjectB/Private/Subsystem/ISMManager.cpp` | 分桶、批量、对象池实现 |
| `Source/ProjectB/Public/Component/IncrementalInstancedStaticMeshComponent.h` | 增量 ISM 组件（脏队列 + 双 SceneProxy） |
| `Source/ProjectB/Private/Component/IncrementalInstancedStaticMeshComponent.cpp` | 增量更新实现 |
| `Source/ProjectB/ProjectB.h` | `ParkedLocation`、`ECC_Cell` 定义 |
| `Source/ProjectB/Public/Subsystem/ISMPoolManager.h` | 旧版池管理器（未使用，参考） |
