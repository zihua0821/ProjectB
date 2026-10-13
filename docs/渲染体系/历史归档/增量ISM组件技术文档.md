# 增量 ISM 组件技术文档

> ## ⚠️ 已弃用（2026-08-07）
> 本组件 `UIncrementalInstancedStaticMeshComponent` 及其源码（`Source/ProjectB/Public/Component/IncrementalInstancedStaticMeshComponent.h` / `Private/Component/IncrementalInstancedStaticMeshComponent.cpp`）**已从项目中删除**。
> 架构定案改为 **空间分桶 + 引擎原生增量 API**（`AddInstancesById / RemoveInstancesById / SetCustomDataValueById`），由 `UISMManager` 承载。本文档仅作历史存档，不再适用。

**组件**: `UIncrementalInstancedStaticMeshComponent`（已删除）  
**版本**: v1.0  
**更新日期**: 2026-08-04  
**分支**: MassCrop  
**上层管理**: `UISMManager`（对象池 + Flush 调度）

---

## 一、系统概述

### 1.1 解决的问题

常规 `UInstancedStaticMeshComponent` 在每次修改实例数据时需调用 `MarkRenderStateDirty()`，触发 **GPU 场景全量重建**——所有实例的 Transform / CustomData 全部重新上传。当组件内实例数量大（上千）且更新频繁时，全量重建成为性能瓶颈。

**增量 ISM 组件**通过自定义 SceneProxy 和渲染线程脏队列实现**增量路径**：

- 游戏线程修改只记录"脏实例"（实例索引 + 新数据）到 MPSC 队列
- 渲染线程仅推送**实际发生变化的实例**到 GPU Scene
- 避免全量重建，更新开销与"变化实例数"成正比，与"总实例数"无关

### 1.2 与 UISMManager 的关系

```
UISMManager（WorldSubsystem）
    ├─ 创建增量组件（bUseIncrementalISM=true）
    ├─ AcquireSlot / ReleaseSlot（对象池）
    ├─ UpdateInstanceCustomData（自动分流到增量接口）
    └─ FlushAllIncrementalComponents（统一 Flush）
                            │
                            ▼
        UIncrementalInstancedStaticMeshComponent（本组件）
```

增量组件**自身不含**分桶、对象池逻辑——这些由 `UISMManager` 管理；它只负责"单个组件内的增量数据推送"。

### 1.3 核心机制（三个关键点）

1. **脏队列（Dirty Queue）**：游戏线程与渲染线程通过 **MPSC 队列**传递脏实例数据
2. **双 SceneProxy**：非 Nanite / Nanite 各有独立的增量 SceneProxy
3. **切断全量同步路径**：重写 `SendRenderInstanceData_Concurrent()` 为空——父类接口（`AddInstancesById` 等）的修改**不会同步到渲染线程**

> ⚠️ 第 3 点是本组件最重要的约束：**所有视觉修改必须走增量接口（`Update*Incremental` / `SetInstanceVisibility`）+ `FlushIncrementalUpdates()`**，否则数据只存在于 CPU 侧、GPU 永远看不到（单元格轮廓曾踩过此坑）。

---

## 二、架构设计

### 2.1 类层次

```
UInstancedStaticMeshComponent（父类）
    └─ UIncrementalInstancedStaticMeshComponent（本组件）
        ├─ CreateStaticMeshSceneProxy()  → 按 Nanite 创建对应增量代理
        ├─ SendRenderInstanceData_Concurrent() → 空实现（切断全量路径）
        └─ 增量接口：Update*Incremental / SetInstanceVisibility / FlushIncrementalUpdates

FPrimitiveSceneProxy（渲染线程代理基类）
    ├─ FIncrementalSceneProxy（非 Nanite，继承 FInstancedStaticMeshSceneProxy）
    └─ FIncrementalNaniteSceneProxy（Nanite，继承 Nanite::FSceneProxy）
```

### 2.2 数据流总览

```
游戏线程                               渲染线程
───────                               ────────
修改实例数据
  │ UpdateInstanceTransformIncremental / CustomDataIncremental
  │   ├─ 写 PerInstanceSMData / PerInstanceSMCustomData（CPU 权威数据）
  │   └─ ENQUEUE_RENDER_COMMAND
  ▼                                        ▼
FDirtyInstanceQueue（MPSC 队列）──────► AddDirtyInstance / AddDirtyInstanceCustomData
  │                                        │
  ▼ FlushIncrementalUpdates()              ▼
ENQUEUE_RENDER_COMMAND ──────────────► TriggerIncrementalUpdate
                                          │ 队列非空
                                          ▼
                              ProcessDirtyInstances_RenderThread
                                ├─ 消费脏队列
                                ├─ 写入 InstanceSceneDataBuffers（GPU 数据）
                                └─ MarkGPUStateDirty → GPU Scene 更新
```

### 2.3 双 SceneProxy 说明

| 代理类型 | 继承 | 适用 | 数据源 |
|---------|------|------|--------|
| `FIncrementalSceneProxy` | `FInstancedStaticMeshSceneProxy` | 非 Nanite 网格 | `CustomInstanceDataProxy`（`FInstanceDataSceneProxy`） |
| `FIncrementalNaniteSceneProxy` | `Nanite::FSceneProxy` | Nanite 网格 | 自身持有的 `InstanceDataSceneProxy` |

两者共享同一套脏队列逻辑（`FDirtyInstanceQueue`），由**静态辅助函数** `ProcessDirtyInstances_RenderThread` 统一消费，避免代码重复（遵循"SceneProxy 重复逻辑提取为静态辅助函数"实践规范）。

---

## 三、核心数据结构

### 3.1 FDirtyInstance（脏实例）

```cpp
struct FDirtyInstance
{
    int32 InstanceIndex = INDEX_NONE;  // 组件本地实例索引（非 ID）
    FTransform NewTransform;           // 新变换
    TArray<float> NewCustomData;       // 新 CustomData
    bool bUpdateTransform = false;     // 独立标志位：本次是否更新 Transform
    bool bUpdateCustomData = false;    // 独立标志位：本次是否更新 CustomData
};
```

> **独立标志位设计**：Transform 与 CustomData 分离控制，确保只更新 CustomData 时不会意外覆盖 Transform（遵循"ISM 增量更新分离 Transform 与 CustomData"实践规范）。

### 3.2 FDirtyInstanceQueue（MPSC 脏队列）

```cpp
struct FDirtyInstanceQueue
{
    TQueue<FDirtyInstance, EQueueMode::Mpsc> DirtyInstances;  // 多生产者单消费者队列
    void AddDirtyInstance(int32 InstanceIndex, const FTransform& NewTransform);
    void AddDirtyInstanceCustomData(int32 InstanceIndex, TArrayView<const float> NewCustomData);
    bool IsEmpty() const;
};
```

**MPSC（多生产者单消费者）**：多个游戏线程任务可并发入队，渲染线程单点消费。

---

## 四、增量更新接口

### 4.1 UpdateInstanceTransformIncremental（世界空间输入）

```cpp
void UpdateInstanceTransformIncremental(int32 InstanceIndex, const FTransform& NewTransform);
```

1. 校验 `InstanceIndex` 有效性
2. **世界空间 → 组件局部空间转换**：`NewTransform.GetRelativeTransform(GetComponentTransform())`，写入 `PerInstanceSMData[InstanceIndex].Transform`
   - ⚠️ 必须转换：`PerInstanceSMData` 要求局部空间，直接写世界变换会导致渲染/物理错位
3. 物理同步：`bPhysicsStateCreated` 时调用 `UpdateIncrementalInstanceBodyTransform`
4. SceneProxy 存在 → `ENQUEUE_RENDER_COMMAND` 推送脏实例（按 Nanite 分流到对应代理）
5. 设置 `bNeedsBoundsUpdate = true`（延迟到 Flush 时统一更新包围盒，避免每帧 UpdateBounds）

### 4.2 UpdateInstanceCustomDataIncremental（严格数量校验）

```cpp
void UpdateInstanceCustomDataIncremental(int32 InstanceIndex, TArrayView<const float> NewCustomData);
```

1. **严格校验**：`NewCustomData.Num() != NumCustomDataFloats` → 输出 Warning 并**直接返回**（不写入）
2. `Memcpy` 到 `PerInstanceSMCustomData`（CPU 权威数据）
3. SceneProxy 存在 → 推送脏实例（`bUpdateCustomData=true`）

> ⚠️ 数量不一致会**静默失败**（仅日志警告）：调用方必须保证传入的 float 数量与组件 `NumCustomDataFloats` 完全一致。

### 4.3 UpdateInstanceCustomDataValueIncremental（单值更新）

```cpp
void UpdateInstanceCustomDataValueIncremental(int32 InstanceIndex, int32 DataIndex, float Value);
```

- 读取当前完整 CustomData → 修改指定索引 → 调用 4.2 全量接口
- 适合低频单值修改（如 `SetInstanceVisibility` 设置不透明度）

### 4.4 UpdateInstanceIncremental（Transform + CustomData 组合）

```cpp
void UpdateInstanceIncremental(int32 InstanceIndex, const FTransform& NewTransform,
                               TArrayView<const float> NewCustomData);
```

> ⚠️ **注意与 4.1 不同**：本接口的 `NewTransform` 是**组件局部空间**输入（直接写入，不做转换）。调用前需自行完成世界→局部转换。

- Transform 总是更新；CustomData 仅在 `NewCustomData.Num() == NumCustomDataFloats` 时更新

### 4.5 SetInstanceVisibility（显隐控制）

```cpp
void SetInstanceVisibility(int32 InstanceIndex, bool bNewVisible);
```

| 操作 | 行为 |
|------|------|
| **显示（true）** | `NumCustomDataFloats > 0` → `UpdateInstanceCustomDataValueIncremental(index, 0, 1.0)`（只改不透明度，**不碰 Transform**，避免渲染线程竞态） |
| **隐藏（false）** | 非 Nanite → 移到**原点** `(0,0,0)` + 零缩放；Nanite → 移到 `ParkedLocation` `(1e7,1e7,-1e7)` + 零缩放 |

> 遵循"实例隐藏处理逻辑规范"：CustomData[0] 控制不透明度；Nanite 移到极远处、非 Nanite 移到原点；隐藏时缩放统一为 0。

---

## 五、渲染线程同步

### 5.1 脏队列推送

所有增量接口在游戏线程通过 `ENQUEUE_RENDER_COMMAND` 将脏实例推送到对应 SceneProxy 的 `FDirtyInstanceQueue`（Nanite / 非 Nanite 各自入队）。

### 5.2 ProcessDirtyInstances_RenderThread（静态辅助函数）

位于匿名命名空间 `IncrementalInstancedStaticMeshComponent` 中，被两个 SceneProxy 共用：

1. 获取 `FInstanceSceneDataBuffers`（GPU 数据的 CPU 镜像），空则报错返回
2. `BeginWriteAccess` 获取写视图
3. 消费脏队列：
   - `bUpdateTransform` → 世界变换 × `PrimitiveWorldToLocal` → 写入 `InstanceToPrimitiveRelative[InstanceIndex]`
   - `bUpdateCustomData` → 按 `NumFloats` 裁剪数量 → 写入 `InstanceCustomData[InstanceIndex * NumFloats + i]`
4. `EndWriteAccess`
5. `SceneInfo->MarkGPUStateDirty()` → 驱动 GPUScene 增量更新

### 5.3 FlushIncrementalUpdates（提交入口）

```cpp
void FlushIncrementalUpdates();
```

1. `ENQUEUE_RENDER_COMMAND` → `TriggerIncrementalUpdate` → 队列非空才调用 `UpdateInstances_RenderThread`（消费脏队列）
2. 非 Nanite 且 `bNeedsBoundsUpdate` → `UpdateBounds()` + `UpdatePrimitiveInstances()`（刷新包围盒，防止视锥剔除错误）

> 遵循"ISM 增量移动后需更新包围盒"实践规范：实例移动后必须更新组件包围盒，否则移出原范围会被视锥剔除。此处通过 `bNeedsBoundsUpdate` 延迟到 Flush 统一处理，避免每帧开销。

### 5.4 SendRenderInstanceData_Concurrent 空实现（关键约束）

```cpp
void UIncrementalInstancedStaticMeshComponent::SendRenderInstanceData_Concurrent()
{
    // 主动切断全量同步路径。
    // 实例数据的初始同步由 CreateSceneProxy 内部的 FlushChanges 完成，
    // 后续更新通过增量通道推送。
}
```

**后果**：
- 父类接口（`AddInstancesById` / `SetCustomDataById` / `RemoveInstancesById`）修改 CPU 数据后，渲染线程**不会同步**
- 只有增量通道（`Update*Incremental` + `FlushIncrementalUpdates`）能推送渲染
- 因此调用方**必须**使用增量接口——这也是 `UISMManager` 将对象池接口限定为增量组件的原因

---

## 六、物理体同步

### 6.1 UpdateIncrementalInstanceBodyTransform

```cpp
void UpdateIncrementalInstanceBodyTransform(int32 InstanceIndex,
    const FTransform& WorldSpaceInstanceTransform, bool bTeleport);
```

| 情况 | 行为 |
|------|------|
| 缩放近乎零（隐藏） | 删除该实例的 BodyInstance（`TermBody` + delete） |
| 已有 BodyInstance | `SetBodyTransform` + `UpdateBodyScale` |
| 无 BodyInstance | 新建并 `InitInstanceBody` |

> 物理体随显隐自动创建/销毁，避免隐藏实例仍占用物理开销。

---

## 七、与 UISMManager 的集成

| UISMManager 接口 | 内部调用 | 说明 |
|-----------------|---------|------|
| `AcquireSlot` | `UpdateInstanceTransformIncremental` + `UpdateInstanceCustomDataIncremental` | 从空闲队列取槽位并"点亮" |
| `ReleaseSlot` | `SetInstanceVisibility(index, false)` | 隐藏 + 回收槽位 |
| `UpdateInstanceCustomData` | `UpdateInstanceCustomDataIncremental`（增量分支） | 单值更新自动分流 |
| `FlushAllIncrementalComponents` | `FlushIncrementalUpdates` | 批量提交所有脏组件 |
| `ExpandCapacity` | 批量 `AddInstancesById`（父类，仅占位） | 创建隐藏占位实例入空闲池 |

**使用要求（增量组件）**：

1. 添加/移除实例 → `AcquireSlot` / `ReleaseSlot`（不要用 `BatchAddInstances`）
2. 更新 CustomData → `UpdateInstanceCustomData`（或直接调组件增量接口）
3. 每帧/每次批量修改后 → `FlushAllIncrementalComponents()`

---

## 八、关键接口清单

| 接口 | 空间输入 | 说明 |
|------|---------|------|
| `UpdateInstanceTransformIncremental` | 世界空间 | 更新变换（内部转局部） |
| `UpdateInstanceCustomDataIncremental` | - | 更新 CustomData（严格数量校验） |
| `UpdateInstanceCustomDataValueIncremental` | - | 更新单值 CustomData |
| `UpdateInstanceIncremental` | **局部空间** | 变换 + CustomData 组合更新 |
| `SetInstanceVisibility` | - | 显隐控制（CustomData[0] / 移到远处） |
| `FlushIncrementalUpdates` | - | 提交脏队列 + 更新包围盒 |

---

## 九、已知问题与注意事项

1. **父类接口写入不显示**：`SendRenderInstanceData_Concurrent` 为空，`AddInstancesById` / `SetCustomDataById` / `RemoveInstancesById` 对增量组件无效——所有修改必须走增量接口 + Flush。
2. **CustomData 数量严格匹配**：`UpdateInstanceCustomDataIncremental` 数量不一致会拒绝写入（仅警告日志），调用方必须保证 `NewCustomData.Num() == NumCustomDataFloats`。
3. **世界/局部空间混淆风险**：`UpdateInstanceTransformIncremental` 接收**世界空间**（内部转换），而 `UpdateInstanceIncremental` 接收**局部空间**（直接写入）——两者输入约定不同，极易混淆。
4. **必须 Flush**：脏队列只在 `FlushIncrementalUpdates` 时消费；不调用则修改不生效。`UISMManager::FlushAllIncrementalComponents` 会统一处理，但必须被调用。
5. **包围盒**：变换更新仅设置 `bNeedsBoundsUpdate`，实际 `UpdateBounds` 在 Flush 时执行（非 Nanite）；若组件被视锥剔除需检查是否 Flush 过。
6. **Nanite 与非 Nanite 行为差异**：隐藏位置不同（Nanite → ParkedLocation / 非 Nanite → 原点）；包围盒更新仅非 Nanite 执行。
7. **初始同步依赖 SceneProxy 创建**：实例初始数据同步发生在 `CreateStaticMeshSceneProxy` 内部（`FlushChanges`）——即组件注册时的数据快照；之后才走增量通道。

---

## 十、相关文件索引

| 文件 | 职责 |
|------|------|
| `Source/ProjectB/Public/Component/IncrementalInstancedStaticMeshComponent.h` | 组件接口与数据结构定义 |
| `Source/ProjectB/Private/Component/IncrementalInstancedStaticMeshComponent.cpp` | 增量更新、脏队列、物理同步实现 |
| `Source/ProjectB/Public/Subsystem/ISMManager.h` | 上层管理器（对象池 + Flush 调度） |
| `Source/ProjectB/Private/Subsystem/ISMManager.cpp` | AcquireSlot / ReleaseSlot / Flush 实现 |
| `Source/ProjectB/ProjectB.h` | `ParkedLocation` 定义（Nanite 隐藏停靠点） |
| `docs/渲染体系/ISMManager技术文档.md` | 上层管理器技术文档 |
