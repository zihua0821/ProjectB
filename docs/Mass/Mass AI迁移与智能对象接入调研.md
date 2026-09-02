# Mass AI 迁移与智能对象接入调研

> 本文档记录 ProjectB 在「Mass 大规模农场 + AI 自动化播种收获」背景下，关于智能对象（Smart Objects）接入、卫星 AI 向 Mass AI 迁移、以及轨道系统重构的调研结论：现状、职责边界、决策、接入方案、依赖顺序与关键证据。
> 结论状态：**调研定稿**（2026-08-31）。本文档为后续迁移的蓝图蓝图，不含代码提交。

---

## 一、背景与目标

- 现状：Mass 负责格子/作物数据层（`UCellManagerComponent` 建 6 万级 Mass 实体 + `UISMManager` 球面分桶渲染）；AI 为「Actor 行为树 + EQS + 轨道角速度移动」的自动化卫星。
- 目标：让 AI 与 Mass 通过**智能对象**桥接，最终把卫星 AI 从 Actor 行为树迁移到 **Mass StateTree**，并使用态对象作为多 agent 冲突仲裁与交互协议层。
- 量级：格子（作业单元）约 6 万。已确认**数据量级可行**，但不能做成 6 万 Actor——只能走纯数据智能对象。

---

## 二、智能对象职责边界（通用语义）

> 核心定位：Smart Object 是「交互机会 + 占用契约」的**调度契约层**，不是数据层、逻辑执行层，更不是决策层。它回答「这里有什么交互点 → 现在可不可用、是否被占用 → 被占用后触发哪个行为」。

### 2.1 职责（它负责什么）

| 职责 | 引擎落点 |
|---|---|
| 声明交互点 | `USmartObjectDefinition` + `FSmartObjectSlotDefinition`（slot 位置/朝向、`ActivityTags`、`UserTags`）|
| 提供空间可查询性 | `USmartObjectSubsystem` + `USmartObjectSpacePartition`（HashGrid / Octree）；「给范围返回候选」，不排序 |
| 管理 slot 生命与占用 | `FSmartObjectRuntimeSlot::State` + `MarkSlotAsClaimed/Occupied/Free`（冲突仲裁核心）|
| 承载并按需激活行为 | slot 上的 `BehaviorDefinition` + `Activate/Deactivate`（仅挂载点，不实现逻辑）|
| 绑定拥有者 | `FSmartObjectRuntime::OwnerData` + `USmartObjectComponent`（存引用/索引，非数据本体）|
| 生命周期可增删 | `CreateSmartObject / RemoveSmartObject` / `PersistentCollection` |

### 2.2 边界（它不该负责什么）

1. 不持有业务核心数据/状态（作物生长、血量、库存等）——属于 Owner（Actor/Entity/System），用 `OwnerData` 存引用。
2. 不执行交互结果（砍一刀→扣血→掉落）——属于 `BehaviorDefinition` + 交互者。
3. 不负责决策（选谁、选哪个、值不值得）——属于 EQS / StateTree / 行为树。
4. 不负责移动/导航/到达——只提供 slot transform 作为目的地。
5. 不与业务对象强绑定生命周期——只在「可交互」时注册。
6. 不是数据容器/存档。

> 核心真源：把「何时可交互（业务状态）」封装成 slot 可用状态；把「交互后做什么」委托给 BehaviorDefinition；把「选谁来、怎么选」完全留给 AI 决策层。

---

## 三、是否采用智能对象：决策分析

### 3.1 决策

**确定采用，且以「多 agent 并行 / Mass StateTree」为目标，四项诉求全要**。四项诉求 = 冲突/占用仲裁、玩家+AI 共享交互、复杂多步交互流程、语义化可交互声明。

### 3.2 价值 vs 现状对照

| 智能对象价值 | 现状 | 结论 |
|---|---|---|
| 冲突/占用仲裁 | ❌ 无（每星私有 `TQueue<EEditMode>`，多星会重复做同一格子）| 最硬的增量价值 |
| 玩家+AI 共享交互 | ⚠️ 半有（都走 `HandleLeftMouseClicked`，但无「占用中」概念）| 需要 |
| 复杂多步交互流程 | ❌ 硬编码 BTTask 序列 | 需要 |
| 语义化可交互声明 | ❌ 按 `EPlotState/ECropState` 硬判断 | 需要 |

### 3.3 关键取舍（重要）

- **不要全量注册 6 万**智能对象（一次性注册 + 生命周期 + 球面 HashGrid 失真 + 状态同步风暴）。
- **采用「动态可交互子集」**：只在格子进入「需交互」状态（成熟可收 / 待耕 / 待种 / 缺水缺肥）时动态 `CreateSmartObject`；收获/耕完即 `RemoveSmartObject`。真源永远是 Mass fragment，智能对象只是「当前可交互格子」的视图。
- 数量 = 当前活跃任务数（远小于 6 万），避开 HashGrid 球面失真与状态同步。

---

## 四、智能对象接入方案

### 4.1 分层架构（映射到项目）

| 层 | 职责（= 已认可边界） | 落点 |
|---|---|---|
| 定义层 | 声明交互点（slot + ActivityTags + 行为定义）| 编辑器 asset（`USmartObjectDefinition`）|
| 创建/注册层 | 何时可交互 → 动态增删 | `UCellManagerComponent`（状态跃迁时调用）|
| 占用/仲裁层 | slot 认领/释放、多 agent 抢闸 | 行为树 BTTask（执行前后 claim/release）；未来 StateTree |
| 行为执行层 | 交互后做什么 | 自定义 `BehaviorDefinition::Activate` → 还原坐标 → `Planet->HandleLeftMouseClicked` |
| 决策层 | 选谁、哪个格子 | 沿用 EQS / 行为树（不归智能对象管）|

### 4.2 关键做法

- **数据载荷**：格子坐标放进**实例级** `OwnerData`（复用 `FCoordinatePayload`，需从 `FarmPrototype.h` 挪到公共头）。不同坐标不能放共享 `DefinitionData`。
- **动态注册**：`CellManagerComponent` 在成熟/待耕/缺水等边缘状态调用 `CreateSmartObject(Definition, CellTransform, FConstStructView::Make(FCoordinatePayload(Coords)))`；维护 `TMap<FIntPoint, FSmartObjectHandle>`。
- **认领与执行**：`GetSlotTransform(ClaimHandle)` / OwnerData 还原坐标 → 定位格子实体（`GetCellEntity`）→ 复用 `HandleLeftMouseClicked(EditMode, Coords)` 写 Mass fragment。
- **接入深度**（两档）：
  - **A 最小接入（先验证仲裁）**：保住现有 EQS + `BTTask_HandleFarmAction`，执行前后加 `MarkSlotAsClaimed → MarkSlotAsOccupied → 执行 → MarkSlotAsFree`。
  - **B 完整接入（智能对象驱动定位）**：`FindSmartObjects(QueryBox)` 按 ActivityTag 找候选 → Claim → Use，行为定义 Activate 执行；EQS 只辅助评分。

---

## 五、AutomationSatellite 现状复盘

### 5.1 系统组成

- `AAutomationSatellite`（APawn）：`OrbitMovementComponent`、`SatelliteMeshComponent`、`FOrbitData OrbitData`、角速度/方向、**每星私有** `TQueue<EEditMode>`。
- `ASatelliteAIController`（AAIController）：挂 `SatelliteBehaviorTree`，BeginPlay `RunBehaviorTree`。
- `UOrbitMovementComponent`：角度制状态机 `Idle / MovingToTarget / Paused`；cos/sin×Radius → `OrbitData.Transform.TransformPosition` 转球面；`CalculateOrbitPosition(angle)`。
- `UOrbitManagerComponent`（挂星球）：`TMap<FName, FOrbitData> Orbits`；`CoveredCoordsMap`（格子→角度）；`InitOrbit` 建默认 `Default` 轨道（Radius 30000）。

### 5.2 行为树节点

- **Service**：`BTService_UpdateSatelliteData`（每 0.1s 写 PlanetActor / OrbitData / CurrentCellCoord）。
- **EQS**：`EQSGenerator_CoveredCells`（遍历 `CoveredCoordsMap` 生成候选点）、`EQSTest_PlotState`（Filter）、`EQSTest_PlotScore`（加权评分）。
- **Task**：`BuildTaskSatelliteQueue`（按格子状态生成操作序列）、`GetNextTask`（弹队列）、`ConvertLocationToCoord`、`MoveToCoord`（目标格→目标角度→沿轨道转）、`HandleFarmAction`（调 `HandleLeftMouseClicked`）、`Idle`。
- **Decorator**：`IsCellInOrbitCoverage` / `IsAtTargetCellCoord` / `IsSatelliteMoveState` / `IsTaskSatelliteQueueEmpty`。

### 5.3 运转流程（据代码推理）

```
[Service] 写 PlanetActor/OrbitData/CurrentCellCoord
  → [EQS] 在 CoveredCoordsMap 内生成候选，PlotState 过滤 + PlotScore 打分
  → [ConvertLocationToCoord] EQS 最佳点 → TargetCoord
  → [IsCellInOrbitCoverage] 确认在覆盖内
  → [BuildTaskSatelliteQueue] 生成操作序列 → [GetNextTask] 弹一个动作
  → [MoveToCoord] 计算目标格轨道角度 → 沿轨道转 → [IsAtTargetCellCoord] 确认到达
  → [HandleFarmAction] Planet->HandleLeftMouseClicked(EditMode, TargetCoord)
  → [Idle] 恢复
```

### 5.4 机制要点

- **运动是 1D 角度制**：卫星只在轨道平面内「转角度」，通过 `CalculateOrbitAngleFromCell` 把目标格换算成角度。只服务轨道扫过的格子。
- **覆盖判定用物理 overlap**：`UpdateOrbitCoveredCoords` 用 `OverlapMultiByObjectType(ECC_Cell)` 盒子（半径=轨道半径）求重叠。
- **队列每星私有，无全局认领**：EQS 打分「谁先算谁选」，多星可能重复做同一格子——缺冲突仲裁的根源。
- **数据经黑板 + `FInstancedStruct`**：多处 `FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(key)).Get<FIntPoint>()`。

---

## 六、UFarmlandComponent 废弃说明

### 6.1 结论

`UFarmlandComponent` **已废弃**，被 `UCellManagerComponent` 取代；`UCropManagerComponent` 一并废弃、职责并入该组件。

### 6.2 证据

- `PlanetActor.cpp` L31-32：`CreateDefaultSubobject<UFarmlandComponent>` 与 `<UCropManagerComponent>` **被注释掉** → 运行时恒为 `nullptr`。
- `PostInitializeComponents`（L58-76）：其初始化（`SetFarmlandConfig/SetPlanetConfig/Initialize`）全部注释。
- 全局引用仅剩旧链路（`CropManagerComponent`/`CropBase`/`FarmlandComponent` 自身/`PlanetActor`），无任何运行时创建点。

### 6.3 新旧职责对照（谁是取代者）

| 旧 `UFarmlandComponent` | 新 `UCellManagerComponent` |
|---|---|
| 三个 `UInstancedStaticMeshComponent`（土地/杂草/轮廓）| `UISMManager` 球面分桶 ISM + `PrecreateISMComponents` |
| `PlotInfoIndex`（`TMap<FIntPoint,FFarmLandPlotInfo>`）| Mass 实体 fragment（`FCellStaticFragment`/`FPlotStateFragment`/`FPlotRenderFragment`/`FCropRenderFragment`）|
| 地块操作（Plow/Wet/Fertilize/Weed/Pest…）| `CellManagerComponent::HandleLeftMouseClicked` 等 |
| 作物生长由 `UCropManagerComponent` 处理 | 并入：`ProcessMatureQueue`/`OnAutoHarvest`/`PlantCrop`/`HarvestCrop`/`OnRePlantCrop` |
| 委托事件（`OnPlotStateChanged` 等）| Mass fragment + 成熟队列（`MatureCropQueue`）|

> 命名提醒：旧 = `UFarmlandComponent`（地块/plot 层），新 = `UCellManagerComponent`（格子/cell 层）。二者代号都称「CellManager」，实为同一职责的旧/新两版。

### 6.4 连带失效

因 `FarmlandComponent`/`CropManagerComponent` 为 null，以下接口恒返回空/默认值：
- `GetPlotInfo` / `GetPlotCropInfo` / `GetCellWorldTransform_Implementation`（`PlanetActor.cpp` L102-146）。

---

## 七、轨道系统：环形路径寻路的本质与重构

### 7.1 本质：轨道 = 环形路径寻路

卫星不需要 A*/NavMesh/地表寻路，其唯一「寻路」就是**沿既定环形轨道飞到某个角度**。因此轨道系统从骨子里属于 **AI 的移动/路径层**，而非行星数据组件。

| 原组件内容 | 本质 | Mass AI 下归属 |
|---|---|---|
| `OrbitData`（半径/变换/覆盖）| 移动环境定义 | 静态/共享资产或环境数据（多星共享轨道定义）|
| `CurrentAngle` / 目标角度 | agent 运行时移动状态 | 卫星 agent 的 fragment（`FMassOrbitFragment`）|
| `UpdateOrbitMovement`（角度→位置）| 移动能力实现 | agent 移动处理器（`FMassOrbitMovementProcessor`），或被 StateTree 编排 |
| `CalculateOrbitAngleFromCell` | 寻路求解 | AI 决策辅助（StateTree/自定义 task 调用）|
| `CoveredCoordsMap` | 可及空间边界 | agent 可交互范围（配合智能对象）|

### 7.2 断链根因（比「SetCellManager 未调」更根本）

`OrbitMovementComponent`（沿轨道转）不依赖格子，是好的；坏的是**轨道↔格子换算**层：
- `CalculateOrbitAngleFromCell(Coord, OrbitData, PlanetActor)` 内部走 `GetCellWorldTransform(PlanetActor, Coord)`；
- `PlanetActor::GetCellWorldTransform_Implementation` 因 `FarmlandComponent` 为空，落入默认实现 `IPlanetInterface::GetCellWorldTransform_Implementation` → **返回 `FTransform()`（Location=(0,0,0)）**；
- 于是 `ToCell = (0,0,0) - OrbitCenter` → 算出角度恒为同一错误值。

**受影响的不止轨道**：`EQSGenerator_CoveredCells` 也用 `GetCellWorldTransform`，同样拿不到格子位置 → 候选失效（旧代码跑不起来的深层原因）。

### 7.3 正确来源：格子实体 fragment

在 Mass 架构下，格子世界坐标本来就存在格子实体 fragment（`FCellStaticFragment.TFm`，`CreateMassCell` 中 `CellTransforms` 写入）。重构核心动作 = **把「格子世界坐标」来源从已坏的 `GetCellWorldTransform` 换成格子实体 fragment 或几何函数**：

```
旧（坏）: CalculateOrbitAngleFromCell(Coord, OrbitData, PlanetActor)
            └─ 依赖 GetCellWorldTransform(旧接口) → FTransform() → 全错
新（对）: 直接用格子实体 FCellStaticFragment.TFm 拿世界位置
            → CalculateOrbitAngleFromCell(CellWorldTransform, OrbitData)  ← 纯几何，正确
```

覆盖集合 `CoveredCoordsMap` 同理：用球面几何（`LatLonToWorldCoordinates` / `ConvertLocationToCellCoord`，均与组件无关）直接算，不再用物理 overlap + 旧 Farmland。

### 7.4 重构方向：向 AI 系统聚合

- **运动层（`OrbitMovementComponent`）保留**：轨道飞行是玩法核心，最多作为卫星 agent 的移动驱动。
- **数学层（`PlanetBlueprintFunctionLibrary`）保留但修正格子坐标来源**。
- **覆盖层（`OrbitManagerComponent`）重构**：几何化 + 去 Farmland 依赖；或随后在 Mass 里由「卫星 agent 轨道状态 fragment」直接提供「当前扫过的格子」。
- **AI 选点层（EQS + 行为树）迁到 Mass StateTree**。

### 7.5 与 MassNavigation / ZoneGraph 的关系

卫星是**三维环形轨道**，而 `ZoneGraph + MassNavigation` 是为地表格网/折线路径设计，语义不匹配。故卫星移动应作为 Mass agent 的**专用移动处理器**（`FMassOrbitMovementProcessor`），不硬套 `MassNavigation`。这也与智能对象闭环：轨道覆盖 = agent 可交互空间范围，智能对象 = 范围内可交互点。

---

## 八、迁移依赖顺序

> 轨道作为「agent 移动能力」**逻辑上依赖 Mass agent 骨架先出现**；但不必等整个卫星 AI 完整迁完才动轨道——`MassAgentComponent` 提供「决策」与「移动」解耦。

```
[0] 智能对象底座（格子→可交互点）        ← 不依赖 AI 形态，先做
[1] Mass agent 骨架：MassAgentComponent 绑定卫星
       │   卫星仍用 OrbitMovementComponent 沿轨道飞，先不动移动
[2] AI 决策迁 StateTree（何时扫、去哪个格子、认领交互）
[3] 轨道/移动并入 Mass：FMassOrbitMovementProcessor + FMassOrbitFragment
       │   ←「轨道 = agent 移动能力」在此合并
[4] 智能对象接入 + 玩家共享（收尾）
```

**关键**：`UMassAgentComponent` 可把 Actor（`AAutomationSatellite`）绑定成 Mass agent，同时保留它自己的组件/移动 → 移动层与决策层**分开迁**：先迁「大脑」（决策→StateTree），「手脚」（轨道移动）最后再并入 Mass processor。

---

## 九、结论与建议

1. **智能对象**：确定采用，做「动态可交互子集」而非全量 6 万；状态真源 = Mass fragment。
2. **UFarmlandComponent**：已废弃，被 `UCellManagerComponent` 取代；`OrbitManagerComponent`/`GetCellWorldTransform` 等因依赖它而断链。
3. **轨道系统**：本质是环形路径寻路（AI 移动模型），重构方向为并入 AI 移动层；先修「格子世界坐标来源」，运动层保留。
4. **迁移起点**：不是轨道，也不是一次性迁整个 AI，而是「智能对象底座 → `MassAgentComponent` 绑定卫星 + StateTree 决策 → 轨道移动最后并入 Mass」。

---

## 十、相关文件索引

- `Source/ProjectB/Public/Planet/AutomationSatellite.h`、`AutomationSatellite.cpp`
- `Source/ProjectB/Public/Planet/AI/SatelliteAIController.h`
- `Source/ProjectB/Public/Planet/OrbitMovementComponent.h`、`OrbitManagerComponent.h`
- `Source/ProjectB/Public/Planet/PlanetActor.h`、`PlanetActor.cpp`
- `Source/ProjectB/Public/Planet/PlanetBlueprintFunctionLibrary.h`
- `Source/ProjectB/Public/Farm/FarmlandComponent.h`、`CellManagerComponent.h`
- `Source/ProjectB/Private/Planet/AI/` 下 Task/Service/Decorator/EQS 全部节点
- `Source/ProjectB/Public/Mass/FarmPrototype.h`（旧智能对象原型，`FCoordinatePayload`/`UFarmHarvestBehavior`）
- `ProjectB.Build.cs`（依赖清单）
- 引擎：`Engine/Plugins/Runtime/MassGameplay/Source/MassSmartObjects`、`Engine/Plugins/Runtime/SmartObjects`、`Engine/Plugins/Runtime/MassAI`、`Engine/Plugins/Runtime/MassActors`（`MassAgentComponent`）

---

## 版本记录

- **v1.0（2026-08-31）**：首次定稿——智能对象职责边界与决策、接入方案（动态子集）、AutomationSatellite 复盘、UFarmlandComponent 废弃说明、轨道系统「环形路径寻路」本质与重构方向、Mass AI 迁移依赖顺序。
