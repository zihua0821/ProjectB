# Mass AI 模块调研

> 本文档为 ProjectB 接入 Mass AI 体系前的模块级调研：MassGameplay 底层插件与 MassAI 上层插件的完整模块地图、关键 API 实测结论、ZoneGraph 适配性分析、以及 Build.cs 依赖增量清单。
> 结论状态：**定稿**（2026-09-01，基于 ue-codegraph 对引擎插件 71.5 万符号的完整索引检索）。
> 配套文档：同目录《Mass AI重构计划.md》《Mass AI迁移与智能对象接入调研.md》《Mass视觉与自建ISM管理器_技术选型.md》。

---

## 一、调研背景

- 项目现状：已接入 `MassEntity`（6 万格子实体数据层）+ `SmartObjectsModule`（智能对象基础），**Mass AI 体系（MassGameplay 底层 + MassAI 上层）尚未接入**。
- 目标：为「卫星 AI 迁移至 Mass」确定模块依赖清单、验证关键 API 可用性、排除不适配的模块。
- 手段：ue-codegraph 检索（引擎插件已注册为 codebase `unreal-engine-plugins`，36920 文件 / 714947 符号）。

---

## 二、插件与模块地图

### 2.1 插件 `Engine/Plugins/Runtime/MassGameplay`（底层，16 模块）

| 模块 | 职责 | 项目相关性 |
|---|---|---|
| **MassCommon** | 基础 fragment（变换/朝向/标签），Mass 体系基石 | ✅ 必需 |
| **MassActors** | `UMassAgentComponent`（Actor↔Entity 桥）、`UMassAgentSubsystem`、`FMassEntityConfig` | ❌ **不需要**（v1.2 纯实体决策，无 Actor） |
| **MassMovement** | `FMassVelocityFragment`/`FMassForceFragment`、移动处理器 | ⚠️ 待评估（轨道为角度制） |
| **MassSignals** | `UMassSignalSubsystem`（SignalEntity/DelaySignal）、`MassSignalProcessorBase` | ✅✅ 事件驱动核心 |
| **MassSmartObjects** | `FMassSmartObjectHandler`（FindCandidatesAsync→Claim→Use 闭环）、`FMassSmartObjectUserFragment` | ✅✅ 阶段3 核心 |
| **MassSpawner** | `UMassSpawnerSubsystem`（SpawnEntities/DoSpawning）、实体模板 | ✅ 卫星生成 |
| **MassEQS** | EQS 的 Mass 版（Generators/Items/Processors） | ✅✅ 选点迁移路径 |
| **MassLOD** | 实体 LOD 分级管理 | ⚠️ 视规模 |
| **MassRepresentation** | 实体↔Actor/ISM 表示同步 | ⚠️ 项目已自建 ISM，待评估 |
| **MassSimulation** | 模拟阶段组织（固定/变步长） | ⚠️ |
| MassReplication / MassGameplayDebug / MassGameplayEditor / MassGameplayExternalTraits / MassGameplayTestSuite | 复制/调试/工具 | ❌ 暂不需要 |

### 2.2 插件 `Engine/Plugins/AI/MassAI`（AI 层，7 模块）

| 模块 | 职责 | 项目相关性 |
|---|---|---|
| **MassAIBehavior** | **StateTree 集成**：`UMassStateTreeTrait`/`UMassStateTreeSchema`/`UMassStateTreeProcessor`/`MassStateTreeExecutionContext`；**智能对象任务**：`MassFindSmartObjectTask`/`MassClaimSmartObjectTask`/`MassUseSmartObjectTask`；**ZoneGraph 任务**：`MassZoneGraphPathFollowTask`/`MassZoneGraphStandTask`/`MassZoneGraphFindSmartObjectTarget` | ✅✅✅ 阶段4 核心 |
| **MassNavigation** | 移动处理器、避障（Avoidance/Steering/SmoothOrientation） | ⚠️ 轨道角度制下待评估 |
| **MassZoneGraphNavigation** | ZoneGraph 车道跟随处理器、`FMassZoneGraphCachedLaneFragment` | ❌ 与轨道语义不匹配 |
| MassAIDebug / MassAIReplication / MassAITestSuite / MassNavigationEditor | 调试/复制/测试 | ❌ |

### 2.3 依赖插件（非 Mass 系）

| 插件 | 定位 | 项目相关性 |
|---|---|---|
| **StateTree**（Runtime/StateTree） | 决策树框架本体 | ✅ 必需（阶段4） |
| **SmartObjects**（Runtime/SmartObjects） | 智能对象运行时 | ✅ 已接入 |
| **GameplayInteractions**（Runtime/GameplayInteractions） | 玩家侧交互 | ⚠️ 阶段5 |
| **ZoneGraph**（Runtime/ZoneGraph） | 车道图导航 | ❌ 不适用（见第五章） |

---

## 三、关键 API 实测结论（MCP 检索）

### 3.1 `UMassAgentComponent`（MassActors）
- `EntityConfig`（`FMassEntityConfig`）+ `AgentHandle`（`FMassEntityHandle`）
- 支持 Entity 创建/销毁/暂停（Enable/Disable/KillEntity）、Puppet 复制、Pooling
- **v1.2 决策变更：卫星不要 Actor，此模块不引入**（MassAgentComponent 是 Actor↔Entity 桥，纯实体方案不需要）

### 3.2 `UMassStateTreeSchema`（MassAIBehavior）
- `IsStructAllowed`/`IsExternalItemAllowed`：校验哪些 fragment/外部数据可进 StateTree
- **StateTree 挂载到 Mass 实体的 schema 机制确认**（阶段4 依据）

### 3.3 `FMassSmartObjectHandler`（MassSmartObjects）
- 完整闭环：`FindCandidatesAsync`（FVector 或 `FZoneGraphCompactLaneLocation` 锚点）→ `ClaimCandidate`/`ClaimSmartObject`（可带 `ESmartObjectClaimPriority`）→ `StartUsingSmartObject` → `StopUsingSmartObject`
- 依赖 `UMassSignalSubsystem` 做异步回调
- **与既有调研（Mass↔智能对象桥）完全一致**（阶段3 依据）

### 3.4 `UMassSignalSubsystem`（MassSignals）
- `SignalEntity`/`SignalEntities`/`DelaySignalEntity`/`SignalEntityDeferred`
- 命名信号 + 延迟信号 + 处理器订阅（`SubscribeToSignal`）
- **"成熟队列通知卫星收获"的天然机制**（阶段3 依据）

---

## 四、Build.cs 依赖增量清单

```cpp
// 阶段 1+ 需追加（PublicDependencyModuleNames）
"MassCommon", "MassMovement", "MassSignals",
"MassSmartObjects", "MassSpawner", "MassAIBehavior",
"StateTreeModule", "MassEQS"
// MassActors：v1.2 纯实体决策后不引入（无 Actor）
// ZoneGraph / MassZoneGraphNavigation / MassNavigation：不引入（见第五章）
// MassRepresentation / MassLOD / MassSimulation：视阶段需要再评估
```

> 注意：接入前需在 `.uproject` 启用插件：MassAI、StateTree、GameplayInteractions（ZoneGraph 不启用）。

---

## 五、ZoneGraph 适配性分析（结论：不适用）

### 5.1 ZoneGraph 是什么
ZoneGraph 是 UE 的**空间导航图系统**：用样条形状（Spline Shapes）定义路径，离线烘焙为紧凑数据存储（`FZoneGraphStorage`），专为 Mass 大规模实体导航设计。**不是 NavMesh，是车道/路径级图**。

### 5.2 核心数据结构（源码实据）
`FZoneGraphStorage`（`ZoneGraphTypes.h:682`）：
- `Zones` / `Lanes`：区域与车道
- `LanePoints` / `LaneTangentVectors` / `LaneUpVectors`：车道点序列（路径本体）
- `LanePointProgressions`：沿车道累积距离（"沿路走了多远"）
- `LaneLinks`：车道拓扑连接（可 A* 寻路，`FZoneGraphAStarWrapper`）
- `ZoneBVTree`：空间加速结构（点→最近车道查询）

查询 API（`UE::ZoneGraph::Query`）：`GetLaneLength`/`GetLaneWidth`/`GetLaneTags`/`AdvanceLaneLocation`。

### 5.3 生态定位
```
ZoneGraph（车道图定义+查询）
   ↑ MassZoneGraphNavigation（FMassZoneGraphCachedLaneFragment 缓存车道数据）
   ↑ MassAIBehavior（PathFollowTask / StandTask / FindSmartObjectTarget）
   ↑ MassCrowd（人群系统）
```

### 5.4 与项目轨道的对比

| 维度 | ZoneGraph | 项目轨道系统 |
|---|---|---|
| 路径定义 | 样条形状 → 烘焙 Lane | `FOrbitData`（Transform + Radius） |
| 移动方式 | 沿 Lane 距离推进 | 沿轨道角度推进（1D 角度制） |
| 图拓扑 | LaneLinks 多车道互联 | 环形单轨（无分支） |
| 寻路 | Lane 图 A* | 环形角度就近 |

**结论**：轨道是单环形、无分支、1D 角度寻路——ZoneGraph 的多车道图/拓扑连接/空间 BVTree 能力用不上（语义不匹配）。**不引入 ZoneGraph 及 MassZoneGraphNavigation/MassNavigation**；轨道移动继续自定义 `FMassOrbitMovementProcessor`，其中"沿轨道的累积角度"可借鉴 `LanePointProgressions` 的设计思路。

### 5.5 与 NavMesh 的区分
| | NavMesh | ZoneGraph |
|---|---|---|
| 数据结构 | 三角形网格（体素化） | 车道点序列（曲线） |
| 适合 | 自由地形寻路 | 结构化路径（道路/轨道/通道） |
| Mass 配合 | ❌ 无原生集成 | ✅ 原生 |

---

## 六、对重构计划的修正（4 条）

1. **阶段 1（实体骨架）依赖确定**：MassGameplay 插件（MassCommon/MassSignals/MassSpawner）+ MassAI 插件（MassAIBehavior），**不含 MassActors**（v1.2 纯实体决策）
2. **阶段 4（决策迁 StateTree）选点路径升级**：存在 **MassEQS**——EQS 选点可留在 EQS 语义内迁移（MassEQS），不必硬塞 StateTree 任务；上规模后启用，`UMassEnvQueryGenerator` 需按 Mass 接口重写
3. **ZoneGraph 确定不引入**：轨道为环形角度制，`MassZoneGraph*Task` 仅作参考
4. **渲染统一走 UISMManager**：卫星实体 + ISM 分桶渲染，复用格子管线（阶段 2）

---

## 七、版本记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-09-01 | 定稿：模块地图、关键 API 实测、ZoneGraph 不适用结论、Build.cs 依赖清单 |
