# 增量 ISM 组件重构方案（定案版）

> ## ⚠️ 已弃用（2026-08-07）
> 本方案所基于的架构已变更：**不再自研/重构增量 ISM 组件**，改为采用 **空间分桶 + 引擎原生增量 API**（UE 5.4+ `bUseIncrementalISM` / `AddInstancesById` / `RemoveInstancesById`），分桶逻辑由外部管理器 `UISMManager` 承载，实例级透明池化一并取消（原生 free list + 组件级池化已足够）。本文档仅作历史存档，不再适用。

**版本**: v1.0  
**更新日期**: 2026-08-04  
**分支**: MassCrop  
**决策状态**: 22 个决策点全部定案（已被新架构取代）  
**目标**: 透明封装 + 内置池化，普通 ISM 与增量 ISM 无缝互换

---

## 一、决策定案总表

| # | 决策点 | 定案 |
|---|--------|------|
| 1 | Remove 语义 | C 参数化（`bUsePooling=true`→隐藏回收；false→真删） |
| 2 | 池化入口 | A 完全透明（Add/Remove 内部自动池化） |
| 3 | 变更来源 | A 引擎变更集（`PrimitiveInstanceDataManager`） |
| 4 | 池归属 | A 组件内部 |
| 5 | 旧接口 | A 彻底删除（AcquireSlot/ReleaseSlot/PreAllocateSlots） |
| 6 | Compact | 显式 `Compact()` + 自动（`bAutoCompact` + 阈值） |
| 7 | Flush | C 混合：`bAutoFlush` 参数，UISMManager 改 Tickable 帧末统一 Flush |
| 8 | 分桶 | 分桶在 UISMManager，池化在组件（职责分离） |
| 9 | bUsePooling | 按场景（作物 true / 地块、轮廓 false） |
| 10 | 互换契约 | 父类一致语义：`GetInstanceCount()` 不含隐藏、Remove 后 ID 无效 |
| 11 | 渲染入口 | SendRenderInstanceData_Concurrent 拦截 + 双 SceneProxy |
| 12 | 隐藏方式 | CustomData[0]=0 优先，移动+缩放 0 兜底 |
| 13 | ID 语义 | 业务用 Mass 实体句柄 |
| 14 | 预分配 | OnRegister 自动（`PreAllocateCount`） |
| 15 | 包围盒 | 显隐更新精确 bounds（防 ParkedLocation 撑爆） |
| 16 | 物理 | `bKeepBodyWhenHidden` 参数（默认删 Body） |
| 17 | Nanite | 共享辅助函数 + 差异参数化 |
| 18 | 线程 | 空闲池 MPSC，跨线程由 UISMManager 代理 |
| 19 | Compact 重排 | B 全量重建（业务 Mass 句柄不受影响） |
| 20 | 接口契约 | 业务句柄（Mass 实体） |
| 21 | 迁移 | B 一次性重构 |
| 22 | 测试 | 暂缓 |

---

## 二、目标架构

```
调用方（Crop 等）
    │ AddInstance / RemoveInstance / SetCustomData（父类接口，无第二套 API）
    ▼
UIncrementalInstancedStaticMeshComponent（对外 = 父类接口 + 参数，完全透明）
    ├─ 池化层：空闲队列(MPSC) + 预分配 + 扩容 + Compact
    ├─ 变更消费：读 PrimitiveInstanceDataManager 变更集 → FDirtyInstance
    └─ 渲染层：双 SceneProxy + WriteView 写入 → GPU Scene
    │
    └─(bAutoFlush=true)→ UISMManager(UTickableWorldSubsystem)
                           帧末统一 Flush + 分桶 + 跨线程代理
```

**职责分离**：
- **UISMManager（外部管理器）**：分桶决策（实例数 > 4096 → 按 2048/桶切分）、Flush 调度、跨线程代理
- **组件（UIncrementalInstancedStaticMeshComponent）**：本桶内的池化复用、增量更新、渲染同步

---

## 三、组件层重构（核心）

### 3.1 对外接口与参数

```cpp
// ===== 对外：父类接口全部可用（不再有第二套 API）=====
// AddInstance / AddInstances / RemoveInstance / RemoveInstancesById
// SetCustomData* / UpdateInstanceTransform*（走父类，内部自动转增量）

// ===== 新增参数 =====
UPROPERTY(EditAnywhere, Category = "Instancing")
bool bUsePooling = true;          // 池化开关（决策1C）
UPROPERTY(EditAnywhere, Category = "Instancing")
int32 PreAllocateCount = 0;       // OnRegister 自动预分配（决策14A）
UPROPERTY(EditAnywhere, Category = "Instancing")
int32 BatchExpandSize = 512;      // 池空扩容批次
UPROPERTY(EditAnywhere, Category = "Instancing")
bool bAutoCompact = false;        // 自动收缩开关（决策6）
UPROPERTY(EditAnywhere, Category = "Instancing")
float CompactAutoThreshold = 0.3f;// 空闲率阈值
UPROPERTY(EditAnywhere, Category = "Instancing")
bool bAutoFlush = false;          // 帧末自动 Flush（决策7C）
UPROPERTY(EditAnywhere, Category = "Instancing")
bool bKeepBodyWhenHidden = false; // 隐藏时保留物理体（决策16C）

// ===== 新增方法 =====
void Compact();                   // 显式收缩（决策6）
int32 GetActiveInstanceCount() const;  // 有效实例数（决策10）

// ===== 重写（决策10）=====
virtual int32 GetInstanceCount() const override;   // = 有效实例数（不含隐藏）
virtual bool IsValidId(FPrimitiveInstanceId Id) const override; // Remove 后失效
```

### 3.2 池化机制

```
AddInstance(s) ──► 池化模式（bUsePooling=true）
    ├─ 空闲队列非空 → 取槽位（还原 Transform + CustomData[0]=1.0）→ 占用标记
    └─ 空闲队列空   → 扩容 BatchExpandSize → 取一个
    → 记录占用者（决策10：ID 有效性）

RemoveInstance(s) ──► 池化模式
    ├─ 隐藏：材质有 Opacity 通道 → CustomData[0]=0（轻量，决策12B）
    │         否则 → 移动（Nanite→ParkedLocation / 普通→原点）+ 缩放0（决策12A）
    ├─ 物理：bKeepBodyWhenHidden=false → 缩放0 删 Body（决策16）
    ├─ 槽位回空闲队列 + 清除占用标记（决策10：ID 失效）
    └─ 自动 Compact 检查：空闲率 > CompactAutoThreshold && bAutoCompact → Compact()

GetInstanceCount() = 总槽位 - 空闲数（决策10）
IsValidId(Id) = 占用标记有效（决策10）
```

### 3.3 变更消费（决策3、11）

```cpp
void SendRenderInstanceData_Concurrent() override   // 不再置空
{
    // 1. 读取 PrimitiveInstanceDataManager 已记录的变更集（父类接口已自动写入）
    //    - Add / Remove / TransformChanged / CustomDataChanged
    // 2. 翻译为 FDirtyInstance（InstanceIndex + Transform/CustomData + 标志位）
    // 3. 入脏队列（MPSC）
    // 4. 池化语义注入：
    //    - Add 但池化 → 槽位复用（不改变数组长度）
    //    - Remove 但池化 → 隐藏回收（不触发 RemoveAt/RemoveAtSwap）
}

// 渲染线程
// 双 SceneProxy（保留）→ 静态辅助函数 ProcessDirtyInstances_RenderThread 消费脏队列
// → WriteView → MarkGPUStateDirty
```

> **关键实现点**：池化模式下需拦截父类的 `AddInstanceInternal`/`RemoveInstanceInternal` 的数组操作（改为槽位复用/隐藏回收），同时保留 `PrimitiveInstanceDataManager` 的变更记录用于渲染同步。这是方案的核心难点（见风险表）。

### 3.4 包围盒与 Compact（决策15、19）

```
显隐变化 → bNeedsBoundsUpdate = true → Flush 时 UpdateBounds + UpdatePrimitiveInstances
    （隐藏实例移到 ParkedLocation 后必须立即排除出 bounds，防止撑爆视锥剔除）

Compact()
    1. 收集空闲槽位（按索引排序）
    2. 逐个 RemoveAt 空闲槽位（数组收缩，触发重排）
    3. 内部维护"槽位 → 物理索引"映射重建（Relocated 事件内部消化）
    4. 业务层无感知：Mass 实体句柄不依赖实例索引（决策19B）
```

---

## 四、UISMManager 层调整

```cpp
// 1. 类型变化：UWorldSubsystem → UTickableWorldSubsystem（决策7C 需要 Tick）
UCLASS()
class PROJECTB_API UISMManager : public UTickableWorldSubsystem

// 2. Tick 中：帧末自动 Flush 所有 bAutoFlush=true 的组件
virtual void Tick(float DeltaTime) override
{
    if (bHasDirtyAutoFlushComponents) FlushAllIncrementalComponents();
}

// 3. 保留（职责不变，决策8）
//    - 分桶：FISMBucketGroup / GetOrCreateBucket / BucketingThreshold
//    - FlushAllIncrementalComponents（显式路径，B 风格）
//    - DirtyIncrementalComponents 登记

// 4. 移除（决策5A 彻底删除）
//    - AcquireSlot / ReleaseSlot / PreAllocateSlots / ExpandCapacity
//    - FreeSlotIndices（池下沉到组件）
//    - SetBatchExpandSize（组件参数化）

// 5. 新增：跨线程代理（决策18）
//    - 非游戏线程的组件操作 → 派发到游戏线程（复用 GetOrCreateBucket 模式）
//    - 或组件空闲池本身 MPSC
```

---

## 五、调用方（Crop 系统）迁移（决策21B 一次性）

```cpp
// ===== CropVisualSyncProcessor.cpp =====
// 之前：
CropRender.InstanceId = ISMManager->AcquireSlot(DesiredMesh, CropTransform, {1,0,0});
ISMManager->ReleaseSlot(CropRender.CurrentMesh, CropRender.InstanceId);
ISMManager->FlushAllIncrementalComponents();

// 之后：直接父类接口（透明池化）
// （组件 bUsePooling=true 时内部自动池化）
const FPrimitiveInstanceId NewId = CropISMComp->AddInstanceById(CropTransform, /*bWorldSpace*/true);
CropISMComp->SetCustomDataById(NewId, {1.0f, 0.0f, 0.0f});
CropISMComp->RemoveInstancesById({OldId});

// ===== CellManagerComponent::PrecreateISMComponents =====
// 之前：ISMManager->PreAllocateSlots(Mesh, Count);
// 之后：组件参数 PreAllocateCount = Count（OnRegister 自动预分配）

// ===== 移除所有 AcquireSlot/ReleaseSlot 相关代码 =====
```

---

## 六、实施步骤（4 阶段）

| 阶段 | 内容 | 验证点 |
|------|------|--------|
| **P1 组件层** | 池化（空闲队列/预分配/扩容/Compact）+ 语义重写（GetInstanceCount/IsValidId）+ 隐藏策略 | 单组件池化功能正确 |
| **P2 变更消费** | 读 `PrimitiveInstanceDataManager` 变更集翻译脏队列；`SendRenderInstanceData_Concurrent` 拦截；池化拦截 Add/Remove 数组操作 | 父类接口全部生效（修复"写了不显示"） |
| **P3 管理器调整** | 改 Tickable；移除池接口；跨线程代理 | Crop 迁移后可跑通 |
| **P4 调用方迁移** | Crop 全改父类接口；PreAllocateCount 参数化；清理 ISMPoolManager 等废弃文件 | 整体回归 |

---

## 七、风险与注意点

| 风险 | 等级 | 缓解 |
|------|------|------|
| `PrimitiveInstanceDataManager` 变更集 API 暴露程度（决策3A 前提） | 🔴 高 | P2 前先读 `ISMInstanceDataManager.h` 验证 ChangeSet 可读性；若不可读 → 回退决策 3B（重写接口） |
| 池化拦截 Add/Remove 数组操作（与父类 `AddInstanceInternal`/`RemoveInstanceInternal` 冲突） | 🔴 高 | 重写 `AddInstanceInternal`/`RemoveInstanceInternal`（protected virtual），池化分支不走父类数组操作但保留 `PrimitiveInstanceDataManager` 记录 |
| GetInstanceCount() 重写 vs 引擎内部 `PerInstanceSMData.Num()` | 🟡 中 | 引擎内部直接读数组（不走 GetInstanceCount），外部语义一致即可 |
| ParkedLocation 撑爆包围盒（决策15） | 🟡 中 | 显隐时强制 UpdateBounds（现有 bNeedsBoundsUpdate 机制） |
| Compact 重排的索引映射（决策19B） | 🟡 中 | 组件内部维护"槽位→物理索引"映射，Relocated 内部消化 |
| 跨线程调用（决策18） | 🟡 中 | UISMManager 派发游戏线程（复用现有模式）或池队列 MPSC |
| UTickableWorldSubsystem 改动 | 🟢 低 | 项目已有 ISMPoolManager Tickable 先例 |

---

## 八、决策回滚预案

- 若 **决策 3A（引擎变更集）** 验证失败 → 回退 3B：重写父类接口记录自己的脏数据（工作量+，但方案不变）
- 若 **决策 10（父类语义）** 实现中发现引擎兼容问题 → 回退：`GetInstanceCount()` 保留池化语义（含隐藏），但 `GetActiveInstanceCount()` 提供有效数（次优，需再确认）
- 若 **决策 7C（自动 Flush）** 引入时序问题 → 回退 7B（显式 Flush，Crop 处理器末尾调用）

---

## 九、相关文件索引

| 文件 | 职责 |
|------|------|
| `Source/ProjectB/Public/Component/IncrementalInstancedStaticMeshComponent.h` | 组件重构（池化/参数/语义重写） |
| `Source/ProjectB/Private/Component/IncrementalInstancedStaticMeshComponent.cpp` | 组件实现 |
| `Source/ProjectB/Public/Subsystem/ISMManager.h` | 管理器（Tickable/分桶/跨线程代理） |
| `Source/ProjectB/Private/Subsystem/ISMManager.cpp` | 管理器实现 |
| `Source/ProjectB/Private/Mass/CropVisualSyncProcessor.cpp` | Crop 迁移（父类接口） |
| `Source/ProjectB/Private/Farm/CellManagerComponent.cpp` | PreAllocateCount 参数化 |
| `Source/ProjectB/ProjectB.h` | ParkedLocation（保留） |
| `docs/增量ISM组件技术文档.md` | 现有组件技术文档（重构后需同步更新） |
| `docs/ISMManager技术文档.md` | 现有管理器技术文档（重构后需同步更新） |
