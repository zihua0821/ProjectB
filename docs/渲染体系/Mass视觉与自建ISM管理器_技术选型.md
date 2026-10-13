# Mass 视觉渲染 vs 自建 ISM 管理器 —— 技术选型

> 本文档记录「作物/单元格/地块的可视化驱动」在 **Mass 自带的视觉渲染模块** 与 **自建 UISMManager（球面分桶 + 增量 ISM）** 之间的技术选型：现状、候选方案、关键证据与最终决策。
> 结论状态：**已定稿**（2026-08-23）。后续是否优化分桶另行评估，本卷只沉淀选型结论。

---

## 一、背景与核心问题

### 1.1 现状

项目当前的可视化驱动是 **自建 ISMManager + Mass 实体数据** 的组合：

- **Mass** 承担逻辑实体生命周期与每帧运算：作物生长、地块/细胞状态等，产出 `FTransformFragment` 等 fragment；
- **自建 UISMManager** 持有真正的渲染资产：按球面经纬划分把实例分到多个 `UInstancedStaticMeshComponent`（桶），提供 `BatchAddInstances` / `BatchRemoveInstances` / `UpdateInstanceCustomData` 等批量增量接口；
- 二者通过三个视觉同步处理器对接（`CropVisualSyncProcessor` / `CellVisualSyncProcessor` / `PlotVisualSyncProcessor`），把 Mass fragment 翻译成对 ISMManager 的桶写入。

> 也就是说：**Mass 管"实体怎么变"，ISMManager 管"实例画在哪"。**

### 1.2 核心问题

Mass 带有一个官方的 ISM 渲染模块（`MassRepresentation` 模块，`UMassRepresentationSubsystem` / `UMassVisualizationComponent` / `MassUpdateISMProcessor`）。于是产生一个自然的疑问：

> **能不能直接使用 Mass 的渲染模块来画这些实例，同时保留球面分块？** 如果能，自建 ISMManager 里"真正独有"的部分其实只有球面分块这一层。

---

## 二、两条候选路线

### 2.1 路线 A：直接使用 Mass 渲染模块 + 球面分块

- 用 `UMassRepresentationSubsystem` 注册视觉描述（`FStaticMeshInstanceVisualizationDesc`），由 Mass 的 `MassUpdateISMProcessor` 自动把实体 transform 写到 ISM 组件上；
- 期望通过某种方式让"球面分桶"仍然生效。

### 2.2 路线 B：Mass 实体数据 + 自建 ISMManager（现状，已选）

- Mass 只负责实体数据与 fragment；
- ISMManager 负责球面分桶 + 增量写入；
- 三个视觉同步处理器作为桥梁，把 fragment 翻译成桶写入。

---

## 三、关键源码证据（为何路线 A 不能直接承载球面分块）

以下结论均来自引擎源码（UE 5.5.4），非猜测。

### 3.1 Mass 渲染的分发轴是「视觉描述 × LOD」，不是空间

- 实体上只携带**一个**视觉描述句柄：`FMassRepresentationFragment.StaticMeshDescHandle`（[MassRepresentationFragments.h L66](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Public/MassRepresentationFragments.h#L66)）；
- `MassUpdateISMProcessor` 用 `ISMInfo[Representation.StaticMeshDescHandle.ToIndex()]` 定位**唯一**一个 `FMassInstancedStaticMeshInfo`（[MassUpdateISMProcessor.cpp L56](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Private/MassUpdateISMProcessor.cpp#L56)）；
- 一个 `FMassInstancedStaticMeshInfo` = 一个视觉描述，内部按 **LOD 显著性** 分成多个 `FMassLODSignificanceRange`，每个 range 绑定**一组** ISM 组件（`StaticMeshRefs`，[MassRepresentationTypes.h L580/L742](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Public/MassRepresentationTypes.h#L580)）。

> 结论：Mass 的路由输入只有 `(StaticMeshDescHandle, LODSignificance)`，二者都是"逻辑/距离"维度的量，**没有"按空间桶选 ISM"的入口**。球面分块在 Mass 渲染模型里无处安放。

### 3.2 `AddBatchedTransform` 是"全写"，不是"只选一个"

`FMassLODSignificanceRange::AddBatchedTransform`（[MassVisualizationComponent.cpp L646](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Private/MassVisualizationComponent.cpp#L646)）会把实体的 transform **写进该 LOD range 绑定的全部 ISM**，循环里没有任何"只挑一个"的分支。

> 即：Mass 是一份实例 transform **复制写**到多个绑定 ISM（为 LOD 多级/多网格切换设计），而球面分块要求"一个实例**只进一个桶的一项 ISM**"。语义相反。

### 3.3 Mass 明确不支持 HISM

`EndVisualChanges` 中有断言：

```cpp
ensureMsgf(!Cast<UHierarchicalInstancedStaticMeshComponent>(ISMComponent),
    TEXT("The UMassVisualizationComponent does not support driving a HISM ..."));
```
（[MassVisualizationComponent.cpp L518](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Private/MassVisualizationComponent.cpp#L518)）

> 若分桶依赖 HISM，路线 A 被直接堵死；项目用的是普通 `UInstancedStaticMeshComponent`，这点不冲突，但需注意。

### 3.4 External-ID 跟踪路径不支持 custom data

在 `bRequiresExternalInstanceIDTracking` 路径下有断言：

```cpp
ensureMsgf(CustomFloatData.Num() == 0, TEXT("Custom floats not supported with this set up just yet."));
```
（[MassVisualizationComponent.cpp L453](file:///F:/Epic%20Games/Source/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/Private/MassVisualizationComponent.cpp#L453)）

> 项目的 Plot 等需要写 custom data（如 opacity）。路线 A 要支持 custom data 必须走默认（非 external-ID）路径，且该路径依赖 LOD range 机制——进一步绑定到 3.1/3.2 的空间缺失。

### 3.5 即便强行接，代价高

若硬要把"桶"伪装成"视觉描述"（每桶一个 desc + 自定义处理器每帧改 `StaticMeshDescHandle`），会带来：

- desc 数量 = 视觉类型 × 球面桶数，且每个 desc 的 LOD significance range 都要建好；
- 每帧改 `StaticMeshDescHandle` + 依赖"缺失即移除"（`EndVisualChanges` 的 collect-unwanted-and-remove），迁移瞬间可能有一帧双写；
- 需自行管理 `PrevTransform` / `PrevLODSignificance`。

> 等于又自己写了一遍"路由逻辑"，省下的只是 Mass 自带的批量写入 + 实例生命周期那半段。

---

## 四、决策与理由

### 4.1 决策

**保留现状（路线 B）：Mass 管实体数据 + 自建 ISMManager 做球面分桶与增量写入。**

### 4.2 理由

1. **球面分块是核心诉求**，而 Mass 渲染模块的分发轴（视觉描述 × LOD）与空间分块正交，无法直接承载；
2. 自建 ISMManager 这套**本质上已经是"Mass 驱动 ISM"的正确形态**——只是 ISM 后端换成自己的分桶实现；
3. 与"直接用 Mass 渲染模块"的差别，仅在于少了 Mass 自带的"批量写入 / 实例生命周期"那层，而那层恰恰**不具备空间分块能力**；
4. 强行接路线 A 会引入 desc 数量膨胀、每帧改 handle、一帧双写等复杂度，收益不成比例。

---

## 五、补充：Nanite / LOD / HISM 的三角关系

选型中一并澄清了「Nanite 与 LOD、HISM 是否冲突」的问题，结论对分桶设计有指导意义。

### 5.1 Nanite 取代了"手工网格 LOD + 距离切换"

- 传统"建 `LOD0/LOD1/LOD2` 多套网格 + 按距离切换"被 Nanite 内部的 cluster 层级细节（按屏幕覆盖率自动选精度）取代；
- 选 Nanite 后，**手工多档网格 LOD 与距离切换不再必要**。

### 5.2 Nanite 没取代"实例的空间组织与剔除"

- HISM 有**两个职责**：① 距离 LOD 批次切换（被 Nanite 取代）；② 层级包围盒剔除 / 空间组织（**不被 Nanite 取代**）；
- 项目的球面分桶正是承担了 HISM 的职责②，但用**组件级视锥剔除**（粒度 = 一个桶），而非 HISM 的 **cluster 级**（粒度 = 桶内若干实例）。这是精度上的降级，是当前自建方案与 HISM 的主要差距。

### 5.3 分桶与 Nanite 是分工，不是二选一

| 层 | 职责 | 由谁承担 |
|---|---|---|
| GPU 绘制 | 怎么画得省（逐实例剔除） | Nanite（GPU 端，按 LocalBounds） |
| 渲染线程 | 画多少、哪些根本不提交 | 分桶 → 组件级视锥剔除 |

> 两者互补：分桶控制渲染线程负载，Nanite 优化 GPU 绘制效率。

---

## 六、性能验证依据（2026-08-20）

压力测试（使用实际 Nanite 网格体）确认：

- **Nanite 逐实例剔除** 在 GPU 端生效（A 实验 GPU 23ms → 14ms），减少 GPU 绘制负载；
- 但**渲染线程成本与提交实例总数成正比**（A 实验 draw 27ms），Nanite **不能**解决；
- **分桶**通过组件级视锥剔除减少提交实例数（7万 → 2万），是**降渲染线程负载的关键**；
- 距离剔除（MaxDrawDistance）在大包围盒下失效，且 Nanite 路径可能绕过 CPU 端判断，**不可依赖**。

> 结论：**分桶仍是必需**（控渲染线程），Nanite 是**补充**（优化 GPU）。二者不是互相替代的关系。

---

## 七、当前结论与后续方向

### 7.1 确认的现状

- 采用 **Mass 实体数据 + 自建 ISMManager**（路线 B）；
- 分桶 = 球面经纬块映射，`GetBucketKey` 用纯整数 O(1) 定位桶；
- 每个桶是一个普通 `UInstancedStaticMeshComponent`，组件级视锥剔除 + 空桶事件驱动停用；
- 分桶的块大小推导目标是**容量**（`InstancesPerBucket`，默认 2048），而非**空间剔除精度**。

### 7.2 明确不进一步优化的决定（2026-08-23）

- 当前**不需要进一步优化分桶**；
- 因此以下方向**暂不实施**，仅作为已知的潜在改进点备查：
  1. 把块大小推导从"容量约束"升级为"容量 ∧ 角跨度"双约束（点亮 `MaxBucketAngularSpanDeg`，控制桶的实际几何范围以提升组件级剔除命中率）；
  2. 按 Nanite / 非 Nanite 桶分别定制分桶策略（`bDisallowNanite` 区分）；
  3. 若要桶内细粒度剔除，只能切更小桶或上 HISM（后者与 Nanite 互斥，需权衡）。

### 7.3 保留的待办

- 距离剔除（`MaxDrawDistance`）已被判为不可依赖，无需再考虑；
- 球面分块的精度与"容量 vs 剔除"错位，留待后续性能回归再评估。

---

## 八、相关文件索引

- [ISMManager 技术文档](ISMManager技术文档.md)
- [增量 ISM 组件技术文档](历史归档/增量ISM组件技术文档.md)
- [球面AABB剔除精度问题与解决方案](球面AABB剔除精度问题与解决方案.md)
- `Source/ProjectB/Private/Subsystem/ISMManager.cpp` / `Public/Subsystem/ISMManager.h`
- `Source/ProjectB/Private/Mass/CropVisualSyncProcessor.cpp` / `CellVisualSyncProcessor.cpp` / `PlotVisualSyncProcessor.cpp`
- 引擎：`Engine/Plugins/Runtime/MassGameplay/Source/MassRepresentation/`
