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

### 阶段 2：纯实体卫星骨架
- **改动**：
  1. 定义卫星 fragment：`FMassOrbitFragment`（轨道名、当前角度、目标角度、速度）
  2. `MassSpawner` 生成卫星实体（或复用格子实体批量创建模式）
  3. 挂 `UMassStateTreeTrait`，配最简 StateTree（最小决策：空闲/待命）
  4. **Build.cs 追加依赖**：MassSpawner / MassCommon / MassSignals / MassAIBehavior / StateTreeModule（详见《Mass AI模块调研.md》第四章）
- **验证**：实体生成、fragment 注入、最简 StateTree 运行

### 阶段 3：ISM 渲染接入
- **改动**：
  1. `UISMManager` 新增卫星实例类型（复用球面分桶配置）
  2. 卫星实体 → ISM 实例绑定（沿用 `FPlotRenderFragment.InstanceId` 同款模式，新增 `FSatelliteRenderFragment`）
  3. 位置来源：`FMassOrbitFragment` 角度 → 世界位置（复用轨道数学层）
- **验证**：卫星在轨道上可见渲染、剔除正确

### 阶段 4：轨道移动 Processor
- **改动**：
  1. `OrbitMovementComponent` 的 1D 角度推进逻辑迁入 `FMassOrbitMovementProcessor`
  2. 轨道定义数据源：`OrbitManagerComponent` 覆盖计算几何化（遍历 `GetAllCoords` + 投影判定），作为 agent 可及边界
  3. 数学层 `PlanetBlueprintFunctionLibrary` 保留，改格子坐标来源（fragment/几何）
- **验证**：卫星沿轨道移动，覆盖集合正确（DebugDraw 对照）

### 阶段 5：智能对象认领/占用打通（v1.3 后置）
- **改动**：
  1. 认领时校验 Mass fragment（状态以实体为准）
  2. `FMassSmartObjectHandler` 打通认领/占用闭环（FindCandidatesAsync→Claim→Use）
  3. 成熟通知走 `UMassSignalSubsystem`（SignalEntity/DelaySignalEntity）
- **验证**：两个 agent 不抢同一格子（冲突仲裁生效）

### 阶段 6：完整 StateTree 决策
- **改动**：行为树节点（选格子/排队/作业）语义 → StateTree 任务（MassFindSmartObjectTask/MassClaimSmartObjectTask/MassUseSmartObjectTask 或自定义任务）；选点 → 普通 EQS（规模小）或 MassEQS（上规模）
- **验证**：卫星行为与旧行为树语义等价

### 阶段 7：玩家共享
- **改动**：玩家交互走同一智能对象协议（GameplayInteractions + 智能对象），玩家与 AI 共享认领/占用
- **验证**：玩家与 AI 对同一格子的交互互斥正确

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
- 阶段 1（底座）✅ 已完成；阶段 2（实体骨架）依赖阶段 1，但与阶段 3（ISM 渲染）/ 阶段 4（轨道 Processor）可部分并行
- 阶段 5（认领/占用）依赖阶段 2 与阶段 1，但可早于完整 StateTree 决策

**风险**：
1. **行为树语义迁移量大**：所有 BT 节点语义需重写为 StateTree 任务（一次性成本，无法复用旧资产）
2. **纯实体无 Actor 的交互路径**：玩家点选/交互需智能对象或射线命中替代，需在阶段 6 前验证
3. 轨道覆盖从物理 Overlap 改几何：边界判定语义可能微变，需对照验证
4. 6 万格子遍历计算覆盖：需控制频率（仅在轨道变更时计算，不进 Tick）
5. `UISMManager` 新增卫星实例类型的分桶与剔除需验证（复用格子配置但 mesh/规模不同）

---

## 七、版本记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.5 | 2026-09-03 | **阶段 1 实施完成**：坐标统一、FarmSmartObjectTypes 类型层、DA_FarmCell 资产、SmartObjectManagerComponent 分批注册、意图委托（方案 C）状态驱动启停；B 路径选点定稿 |
| v1.4 | 2026-09-01 | **共享底座合并为阶段1**：格子坐标来源 + 智能对象底座（都只依赖格子层）；实体骨架顺延为阶段2 |
| v1.3 | 2026-09-01 | **智能对象底座提前**：拆分为底座层（阶段2，只依赖格子层）与认领/占用层（阶段5）；阶段编号顺延（1~7） |
| v1.2 | 2026-09-01 | **一步到位决策**：卫星不要 Actor，纯实体 + ISM 渲染；删除阶段 -1；新增规模分档 |
| v1.1 | 2026-09-01 | 依据模块调研修订：阶段1/3/4 依赖与 API 细化、ZoneGraph 不适用结论、MassEQS 路径 |
| v1.0 | 2026-08-31 | 定稿：目标架构、依赖顺序、6 阶段执行计划（-1~5）、并行与风险 |
