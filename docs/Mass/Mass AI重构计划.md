# Mass AI 重构计划

> 本文档为 ProjectB「卫星 AI 迁移至 Mass」的正式重构计划：目标架构、依赖顺序、分阶段执行计划、规模分档决策、并行机会与风险。
> 计划状态：**定稿**（2026-08-31，2026-09-01 v1.2 修订：**彻底一步到位，卫星不要 Actor**）。
> 配套调研见同目录《智能对象架构与落地.md》《Mass AI模块调研.md》《Mass AI迁移与智能对象接入调研.md》《Mass视觉与自建ISM管理器_技术选型.md》。

---

## 一、现状基线（磁盘直读，2026-08-31）

| 模块 | 现状 | 状态 |
|---|---|---|
| 格子/作物层 | `UCellManagerComponent` + 6 万 Mass 实体 + `UISMManager` 球面分桶 | ✅ 新版可用 |
| 格子坐标来源 | `FCellStaticFragment.Transform` 已写入世界变换 | ✅ 正确来源 |
| 轨道定义层 | `UOrbitManagerComponent`：依赖已废弃 `UFarmlandComponent` 签名；`BeginPlay` 用旧 `bIsInstancesReady/OnInstancesReady` 绑定；`PlanetActor` 中 `SetCellManager` 调用被注释 → **运行时 CellManager 恒 null，InitOrbit 永不执行** | ⚠️ 断链 |
| 轨道运动层 | `UOrbitMovementComponent`（1D 角度制） | ✅ 逻辑可保留（迁 Processor） |
| 轨道数学层 | `PlanetBlueprintFunctionLibrary`：`CalculateOrbitAngleFromCell(FTransform, FOrbitData)` 几何重载、`ConvertLocationToCellCoord` | ✅ 纯几何可用 |
| 卫星 AI | Actor 行为树 + EQS + `TQueue`，全在 Actor 上 | ⚠️ 不再运行（放弃旧链路验证） |
| 智能对象 | 调研定稿（职责边界 = 调度契约层） | 📄 已调研 |

**关键决策（v1.2）**：旧行为树链路**放弃运行验证**，卫星直接以纯实体为目标形态，不做 Actor 壳过渡。

---

## 二、目标架构（终态：纯实体，无 Actor）

```
                ┌─────────────────────────────────────────┐
                │  卫星 = Mass 实体（纯数据，无 Actor）      │
                │   FMassOrbitFragment（轨道运行时状态）     │
                │   StateTree（决策，UMassStateTreeTrait）  │
                │   MassSignals（事件：成熟通知等）          │
                └──────┬──────────────┬───────────────────┘
                       │              │
         FMassOrbitMovementProcessor  │
         （1D 角度移动，自定义）        │
                       │              │
                ┌──────▼──────────────▼───────────────────┐
                │  渲染：UISMManager 分桶 ISM（复用格子管线）│
                └─────────────────────────────────────────┘
```

- **决策层** → Mass StateTree；**移动层** → `FMassOrbitMovementProcessor`（自定义 1D 角度制）
- **渲染层** → `UISMManager` 球面分桶（与 6 万格子同一管线，新增卫星实例类型）
- **交互协议层** → 智能对象（动态可交互子集，非全量 6 万）
- **格子数据源** → Mass 实体 fragment（`FCellStaticFragment`）/ 纯几何

**为什么不要 Actor（v1.2 决策依据）**：
1. 消灭双轨期维护成本（Actor 壳 → 纯实体的过渡阶段）
2. 轨道移动是 1D 角度数据计算，Actor 组件是包袱
3. 卫星与格子架构同构（实体 + ISM），星球大规模对象统一
4. 玩家交互可由智能对象/射线命中替代，不需要 Actor 兜底

---

## 三、依赖顺序

```
阶段 1：共享底座（格子坐标来源统一 + 智能对象底座注册）✅ 已完成 2026-09-03
   ↓
阶段 2：纯实体卫星骨架（MassSpawner 生成 + fragment + 最简 StateTree 决策）
   ↓
阶段 3：ISM 渲染接入（UISMManager 分桶，复用格子管线）
   ↓
阶段 4：轨道移动 Processor（OrbitMovementComponent 逻辑迁入）
   ↓
阶段 5：智能对象认领/占用打通（依赖实体骨架 + 底座）
   ↓
阶段 6：完整 StateTree 决策（行为树语义替换）
   ↓
阶段 7：玩家共享
```

**为什么共享底座最先（v1.4 修正）**：智能对象分三层，依赖不同——底座层（子集注册/声明/查询）只依赖格子层（已就绪），与格子坐标来源统一一样零依赖卫星；认领层（`FMassSmartObjectHandler`）依赖 agent 实体；决策层（StateTree 任务）依赖完整决策。底座应最先做：既可用纯格子数据验证"格子↔智能对象"链路、尽早暴露接口问题，也为阶段 2 实体骨架提供已就绪的查询基础。

---

## 四、分阶段执行计划

### 阶段 1：共享底座（✅ 已实施完成 2026-09-03）

> 实施细节与架构定稿见《智能对象架构与落地.md》《智能对象资产配置参考（DA_FarmCell）》。

**实际落地内容**：
1. **格子坐标统一** ✅：`CellManagerComponent::GetCellWorldTransform`（读 `FCellStaticFragment.Transform`）；`PlanetActor::GetCellWorldTransform_Implementation` 断链修复（弃 Farmland）
2. **智能对象类型层** ✅：`Mass/FarmSmartObjectTypes.h`——`FCoordinatePayload`（继承 `FSmartObjectDefinitionData`）+ 操作行为类（`UFarmActionBehaviorBase` 基类统一 `Duration` + 7 派生类：Harvest/Irrigation/Fertilize/Plow/Plant/Weeding/Pest）；FarmPrototype 标记废弃（阶段 5 移除）
3. **资产定义** ✅：`DA_FarmCell`（手动建）——双 Slot（Plot=0/Crop=1），各挂行为定义；**不配 ActivityTags**（B 路径不走引擎查询）
4. **注册组件** ✅：`SmartObjectManagerComponent`（挂 PlanetActor）——全量分批注册（5000/帧）+ 微查询捕获 Slot 句柄 + `Coord→Handle` 映射（O(1)）+ `SetPlotSlotEnabled/SetCropSlotEnabled` 封装
5. **状态驱动启停** ✅：**意图委托（方案 C）**——CellManager 声明 `OnCellSlotStateChanged`，Plow/RemoveLand/Plant/RemoveCrop 广播 `(Coord, SlotIndex, bEnabled)`，SmartObjectManager 订阅执行（规避 Defer 中间态）

**实施中发现的关键点**：
- `FindSmartObjects` 默认过滤 disabled Slot（`bShouldIncludeDisabledSlots=false`）——资产 Slot 默认需 enabled 或用查询标志（见《智能对象架构与落地.md》4.2）
- **B 路径选点**（不走 FindSmartObjects）：卫星决策直接查状态枚举 → SmartObjectManager 按坐标取 SlotHandle → Claim
- 运行时停 Tick 用 `SetComponentTickEnabled(false)`（`bCanEverTick` 运行时修改无效）
- **验证**：控制台（~）调试通过；注册完成日志确认全量注册成功

### 阶段 2：纯实体卫星骨架（✅ 已实施完成 2026-09-16）

**实际落地内容**：
1. **轨道数据化**：新增 `FOrbitDefinition`（OrbitName/Transform/Radius）+ `PlanetConfig.OrbitDefinitions` 数组；`OrbitManager::InitOrbit` 优先读配置（空则兜底 Default）
2. **Fragment 层**：`Mass/SatelliteMassFragments.h`——`FMassOrbitFragment`（轨道名/当前角度/目标角度/角速度/方向/移动状态）+ `FSatelliteTag`；`ESatelliteMoveState` 自旧组件迁入
3. **生成组件**：`SatelliteManagerComponent`（挂 PlanetActor）——**手写批量创建**（决策 A：CreateArchetype + BatchCreateEntities，非 MassSpawner）；角度沿轨道均分；参数（数量/角速度/StateTree 资产/网格）蓝图可配
4. **StateTree 挂载**：`FMassStateTreeInstanceFragment` + `FMassStateTreeSharedFragment`（`AddConstSharedFragmentToEntity`）；**实例分配/激活/Tick/清理全部由引擎三个 Processor 自动完成**
5. **构建配置**：`.uproject` 启用 MassAI 插件；Build.cs 加 `MassAIBehavior` / `StateTreeModule`

**验证**：实体数量 = 格子数 + 卫星数（54003）；`mass.PrintEntityFragments` 确认 fragment 值；`MassStateTreeActivatedTag` 确认 StateTree 激活

### 阶段 3：ISM 渲染接入（✅ 已实施完成 2026-09-16）

**实际落地内容**：
1. **渲染 fragment**：`FSatelliteRenderFragment { FGlobalInstanceId }`（注册时回写）
2. **共享数据**：`FMassSatelliteSharedFragment`（ConstShared）——轨道几何 + Mesh + Scale（同值去重）
3. **渲染同步 Processor**：`USatelliteRenderSyncProcessor`（游戏线程）——`CurrentAngle` → `ComputeOrbitWorldPosition` → `BatchUpdateInstancesTransforms`
4. **单桶路径**：空 `GridCoords` + `bDisableBuckets`——卫星桶与格子球面分桶完全隔离（卫星数量少、位置动态，分桶无收益）
5. **执行排序**：`SatelliteOrbitMovement`（移动）→ `SatelliteRenderSync`（渲染）——同帧用最新角度

**验证**：卫星在轨道上可见（3 颗均分 360°）；运行时移动跟随正常

### 阶段 4：轨道移动 Processor（✅ 已实施完成 2026-09-16）

**实际落地内容**：
1. `USatelliteOrbitMovementProcessor`——精确迁移 `UOrbitMovementComponent::UpdateOrbitMovement` 全部语义（Idle 推进 / MovingToTarget 最短弧 / Paused 不动 / 角度归一化）
2. 到达事件以 `MoveState = Paused` 状态表达（委托交付改为状态表达；信号留后续）
3. **覆盖集合几何化：已关闭**——被"**球面几何反推**"替代（见阶段 5）：卫星选点直接用「位置方向 → lat/lon → 格子索引」（复用 `ConvertLocationToCellCoord`，与 `CreateMassCell` 互逆），**不再依赖 CoveredCoordsMap**

**验证**：卫星沿轨道可见移动；`mass.PrintEntityFragments` 确认 CurrentAngle 持续推进

### 阶段 5：认领/占用打通（✅ 已实施完成 2026-09-17）

**实际落地内容（B 路径版，不走 FindCandidatesAsync）**：
1. **认领/释放封装**：`SmartObjectManagerComponent::TryClaimSlot / ReleaseClaim`（坐标 → SlotHandle → `MarkSlotAsClaimed`/`MarkSlotAsFree`）
2. **任务 fragment**：`FMassSatelliteTaskFragment { TargetCoord, ClaimHandle }`（跨任务传递）
3. **选点**：球面反推最近格子 → 直读格子实体 `FCropStateFragment`（成熟判定）→ **选点后立即认领**（移动前占位防抢）
4. **执行**：复用玩家同一入口 `CellManager->HandleLeftMouseClicked(HarvestCrop, Coord)`；成功/失败均释放（统一重选出口）
5. **预置工具**：`CellManagerComponent::PresetMatureCropPlots`（赤道行均分预置"已耕地+已播种+直接成熟"地块，供演示；正常播种需等生长时间）

**验证**：`claimed → harvested` 循环日志连续出现（认领→移动→收获→释放→继续巡航闭环成立）；手动操作对 Slot 启停的同步阶段 1 已就绪

### 阶段 6：完整 StateTree 决策（✅ 已实施完成 2026-09-18）

**实际落地内容**：
1. **三个自定义 Task**（`SatelliteStateTreeTasks.h/.cpp`，继承 `FMassStateTreeTaskBase`）：`Select Target`（选点+认领）/ `Move To Target`（等待到达）/ `Harvest`（收获+释放，EnterState 瞬时完成）
2. **状态编排**（资产 ST_Satellite）：`Cruise → MoveToTarget → Harvest → Cruise`，转换均为 `On State Succeeded`；Task 通过 Mass Context 直读/写 fragment（无 External Data 配置）
3. **手写状态机退役**：删除 `SatelliteTaskProcessor`；`ESatelliteTaskState` 与 fragment `TaskState` 字段清理（状态由 StateTree 表达）
4. **关键发现与对策——Mass StateTree 是信号驱动的**：`UMassStateTreeProcessor` 只在收到订阅信号时 Tick（无信号不跑）；等待/轮询类任务必须外部驱动 → 新增 `USatelliteStateTreeTickProcessor`（每帧向卫星发 `NewStateTreeTaskRequired` 信号，Build.cs 加 `MassSignals`）
5. **失败处理**：Task 业务失败全内部消化（认领失败→Running 跳过）；结构性失败（数据/引用缺失）→ 日志 Warning + 默认转根到 Cruise 自愈

**验证**：行为与阶段 5 等价（claimed/harvested 日志）+ StateTree 调试器可实时观察状态流转

### 阶段 7：玩家共享（待做，下一阶段）
- **改动**：玩家交互走同一智能对象协议（GameplayInteractions + 智能对象），玩家与 AI 共享认领/占用
- **验证**：玩家与 AI 对同一格子的交互互斥正确

---

## 四·附：遗留待办清单（阶段 6 后）

| 项 | 说明 | 优先级 |
|---|---|---|
| **阶段 7 玩家共享** | 玩家认领走同一协议（High 优先级抢占已由引擎保证）；玩家直调与认领的一致性 | 下一阶段 |
| **旧系统退役** | `FarmPrototype`（已标废弃）+ 旧卫星 Actor/行为树/EQS/OrbitMovementComponent 的清理（待确认关卡是否还有旧卫星在用） | 中 |
| 心跳降频优化 | `SatelliteStateTreeTickProcessor` 当前每帧发信号；规模化后降频（如 0.2s）+ 成熟事件（MassSignals）加速 | 低（规模化时） |
| MassEQS 选点 | 当前选点为 O(1) 球面反推 + 单格状态直读；若将来需求变为"多条件/多目标扫描"再上 MassEQS | 低（按需求） |

---

## 五、规模分档决策（v1.2 新增）

| 规模 | 卫星形态 | 渲染 | 选点 |
|---|---|---|---|
| 现在~数十 | 纯实体 | ISMManager | 普通 EQS |
| 数百+ | 纯实体 | ISMManager | **MassEQS** |

**结论**：形态与渲染**不分档**（始终纯实体 + ISM）；仅选点方案按规模升级（普通 EQS → MassEQS）。MassEQS 的核心价值是"批量异步请求队列"（`UMassEQSSubsystem` 按查询类合批），上规模后启用；`UMassEnvQueryGenerator` 需按 Mass 接口重写（旧 `EQSGenerator_CoveredCells` 不能直接用）。

---

## 六、并行机会与风险

**可并行**：
- 阶段 1~6 ✅ 已完成（底座 → 骨架 → 渲染 → 移动 → 认领 → StateTree 决策）；阶段 7（玩家共享）待做

**风险（阶段 6 后更新）**：
1. ~~行为树语义迁移量大~~ → **已完成**（阶段 6：三个自定义 Task + 状态编排；旧 BT 可退役）
2. ~~纯实体无 Actor 的交互路径~~ → **已验证**（B 路径：球面反推 + 状态直读 + 坐标直连认领，不依赖 Actor/引擎查询）
3. ~~轨道覆盖从物理 Overlap 改几何~~ → **方案替换关闭**：Mass 版选点改用球面几何反推（O(1)，与网格生成互逆），不再需要覆盖集几何化
4. 旧系统退役范围：FarmPrototype 与旧 BT/EQS/卫星 Actor 的清理需确认关卡引用关系后执行
5. 玩家共享（阶段 7）：玩家直调与认领协议的统一（玩家 High 抢占已由引擎保证，仅 Claimed 状态可抢）

---

## 七、版本记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.6 | 2026-09-18 | **阶段 2~6 全部实施完成**：轨道数据化、卫星实体骨架、ISM 单桶渲染、移动 Processor、认领闭环（B路径）、StateTree 决策（含信号驱动心跳关键发现）；新增遗留待办清单 |
| v1.5 | 2026-09-03 | **阶段 1 实施完成**：坐标统一、FarmSmartObjectTypes 类型层、DA_FarmCell 资产、SmartObjectManagerComponent 分批注册、意图委托（方案 C）状态驱动启停；B 路径选点定稿 |
| v1.4 | 2026-09-01 | **共享底座合并为阶段1**：格子坐标来源 + 智能对象底座（都只依赖格子层）；实体骨架顺延为阶段2 |
| v1.3 | 2026-09-01 | **智能对象底座提前**：拆分为底座层（阶段2，只依赖格子层）与认领/占用层（阶段5）；阶段编号顺延（1~7） |
| v1.2 | 2026-09-01 | **一步到位决策**：卫星不要 Actor，纯实体 + ISM 渲染；删除阶段 -1；新增规模分档 |
| v1.1 | 2026-09-01 | 依据模块调研修订：阶段1/3/4 依赖与 API 细化、ZoneGraph 不适用结论、MassEQS 路径 |
| v1.0 | 2026-08-31 | 定稿：目标架构、依赖顺序、6 阶段执行计划（-1~5）、并行与风险 |
