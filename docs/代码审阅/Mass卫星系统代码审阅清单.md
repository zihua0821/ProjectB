# Mass 卫星系统 · 代码审阅清单（AI 辅助代码专项）

> **用途**：对 2026-09-17 ~ 10-01 半个月内 AI 辅助生成的代码做【时间线审阅】——按提交演进
> 掌握"每个时间做了什么 → 当时为什么这么做 → 后来怎么变的 → 合不合理"，
> 达到"代码虽 AI 生成、认知上如同自己逐步写出"的掌握度；
> 审阅完成后统一修订注释（修订阶段只做注释的事）。
> 本文件是唯一台账——每阶段报告一经产出即落此文档，含证据与裁决记录。
> **分支**：`MassCrop`　**行号基线**：`461c1c0`（2026-10-01，HEAD）——全文行号引用以此提交为准
> **当前状态（10-03）**：核对阶段收官（P1~P8 全部无异议·56 条 E 全表态）；下一步 = 执行第十章 M1~M4 → 注释统一修订
> **关联文档**：`交接文档_卫星转场链路.md`（系统现状）/ `卫星转场链路_Bug复盘.md`（易错点史）/
> `轨道全局规划_运行期问题复盘.md`（运行期问题史）

---

## 一、审阅目的

```
① 掌握（核心）：代码为 AI 辅助生成、时间跨度半个月（卫星系统从零到完整）。
   按时间顺序逐行详细审阅——掌握每个时间点"做了什么 / 当时为什么这么做 / 后来怎么演化"，
   达到"如同自己逐步写出"的认知掌握度
② 评估：对每个阶段的设计决策做合理性评估（含与后续 bug / 返工 / 复盘文档的关联）
③ 收口：审阅完成后统一修订注释——注释失准问题一律后置到修订阶段，审阅期间只记录
```

## 二、审阅范围

### 2.1 时间跨度与提交

```
起点：09f4587（09-17 卫星纯实体跑起来）　→　终点：461c1c0（10-01 轨道编辑交互完成）
共 15 次提交、10 个自然日、80 个文件变更（+6680 / -2128）

完整提交清单（15 个；分组结构见第四章推进表）：
09-17（4）09f4587 纯实体跑起来 / 4c832c8 渲染与轨道移动 / 1d07bab 巡收获 / 9f7cf17 刷mesh
09-18（2）d26ab5e Mass AI重构+简单StateTree / 66da5ec 认领协议+清理旧系统
09-19（1）6ced859 农业闭环自动化
09-21（1）665d2bf ISM优化
09-22（1）4805a47 轨道编辑系统
09-23（1）f9ce05d 移除颜色预设+界面优化
09-25（1）b4f3548 重构卫星轨道运动与作业系统
09-26（1）6b9e0bb 槽位回轨+角速度同步转场
09-29（1）2998211 全局规划+离轨作业
10-01（2）9763842 交接文档+测试资料 / 461c1c0 编辑交互专项
```

### 2.2 审阅对象（按类别）

**A 类：核心新增（约 6500 行，审阅主体）**

| 模块 | 文件 |
|---|---|
| 卫星状态机 | `SatelliteStateTreeTasks.cpp/h` |
| 卫星管理 | `SatelliteManagerComponent.cpp/h` |
| 数据定义 | `SatelliteMassFragments.h` / `OrbitPlanningTypes.h` |
| 槽位与认领 | `SmartObjectManagerComponent.cpp/h`（+374/+108 修改） |
| 处理器 | `SatelliteOrbitMovement/OrbitPlan/RenderSync/StateTreeTick` 四组 Processor |
| 轨道编辑 | `OrbitEditorWidget.cpp/h` + `BasePlayerController.cpp/h`（编辑接线）+ `SimulationControlSubsystem` |
| 可视化 | `OrbitCoverageVisualComponent.cpp/h` |
| 几何支撑 | `PlanetBlueprintFunctionLibrary.cpp/h`（+143/+88 修改） |

**B 类：支撑性修改（约 700 行，需过一遍）**

```
CellManagerComponent（+273 修改）/ ISMManager（+116）/ OrbitManagerComponent（137 改）/
CropGrowthProcessor / 三个视觉同步处理器 / FarmSmartObjectTypes 等
```

### 2.3 排除项

```
已删除的旧 AI 行为树（BTTask_* / BTDecorator_* / EQSTest_* / EQSGenerator_* /
SatelliteAIController / AutomationSatellite / OrbitMovementComponent / FarmPrototype）
—— 它们是被删除的旧系统，不属于本次"生成"产物；存档于 docs/参考/旧AI行为树
```

---

## 三、审阅方式与判级

### 3.1 协作方式（2026-10-02 修正）

```
① 时间线审阅 —— 以提交演进为轴逐阶段细读（diff 级）+ 交叉验证（调用方检索 / 生命周期溯源 /
   配置字段溯源 / 与文档承诺对照），每阶段产出审阅报告（模板见第四章）；用户对照核对
② 核对期间可以改代码 —— 审阅发现的逻辑问题可直接修复
③ 修订阶段只做注释 —— "修订"专指最后统一的注释修订；注释失准问题（B 组）
   一律后置到修订阶段处理，审阅期间只记录、不动注释
④ B/C 组裁决推迟（2026-10-02 用户决定）——前导发现的 B 组（注释失准）与 C 组（逻辑/边界）
   不在前导阶段单独裁决，推迟至时间线审阅（P1~P8 报告）核对后一并决定
⑤ 代码修改攒批执行（2026-10-02 再修订）——审阅与讨论中裁决的代码修改统一攒入【第十章待执行清单】，
   在【注释修订阶段之前】集中实施；A 组死代码清理为先行的例外（已执行）
```

### 3.2 问题分类

| 分类 | 含义 | 典型证据 |
|---|---|---|
| **A 死代码** | 全库零调用/零读写的函数或字段 | 调用方检索为零 + 非 UFUNCTION/UPROPERTY 绑定 |
| **B 注释失准** | 注释与现行实现/命名不符、引用错误、术语残留 | 对照实现逐句核验 |
| **C 逻辑/边界** | 疑点或低风险边界行为（待用户判断） | 行级推演 + 数据流追踪 |
| **D 已核对无误** | 已核验一致、无需处理（供用户抽查） | 语义/调用方/注释三者一致 |

### 3.3 裁决规则

```
每条问题由用户裁决（四选一，记录于各表"裁决"列）：
· 成立   —— 确认问题，进入对应处理（死代码删/注释修订阶段改）
· 不成立 —— 判误报（AI 在"裁决记录"节补记原因）
· 待定   —— 需进一步验证（须写明下一步验证动作）
· 已解释 —— 表象可疑但实为有意设计（注释修订时补声明）
```

---

## 四、时间线推进表（8 个主题阶段）

> 组织原则（2026-10-02 定）：按提交时间顺序归并为主题阶段；阶段内以提交为小步依次审。
> 目标：掌握"每个时间做了什么 → 当时为什么这么做 → 后来怎么变 → 合不合理"。

| 阶段 | 日期 | 提交 | 主题 | 状态 |
|---|---|---|---|---|
| **P1** | 09-17 | 4 | 起步日：纯实体跑起来 → 渲染与轨道移动 → 自动巡收获 → 刷 mesh | **✅ 已核对（10-03）** |
| **P2** | 09-18 | 2 | 重构日：Mass AI 重构 + 简单 StateTree 决策；玩家共享认领协议 + 清理旧系统 | **✅ 已核对（10-03）** |
| **P3** | 09-19 | 1 | 农业闭环自动化系统 | **✅ 已核对（10-03）** |
| **P4** | 09-21 | 1 | ISM 管理器参数与实例分桶优化【支撑域·已确认纳入】 | **✅ 已核对（10-03）** |
| **P5** | 09-22~23 | 2 | 轨道编辑系统（诞生 + 界面优化/移除颜色预设） | **✅ 已核对（10-03）** |
| **P6** | 09-25~26 | 2 | 卫星轨道运动与作业系统重构 + 槽位回轨/角速度同步转场（贝塞尔→极坐标关键演化段） | **✅ 已核对（10-03）** |
| **P7** | 09-29 | 1 | 轨道全局规划 + 卫星离轨作业（K=5 / 群体决策 / 漂移） | **✅ 已核对（10-03）** |
| **P8** | 10-01 | 2 | 轨道编辑交互专项 + 收尾（耐力回归/交接文档） | **✅ 已核对（10-03）** |

> 说明：提交数合计 15；P4（ISM 优化）为支撑域——已确认纳入审阅范围（2026-10-02）。
> 前导发现（第五章）产生于"终态视角自查"，将在对应阶段被引用（如贝塞尔死代码 → P6 揭示来历）。

### 每阶段产出格式（模板 · 报告写入第六章）

```
① 本节提交与变更概览（提交 + 文件清单，按当时视角）
② 当时做了什么——功能归纳（不含后见之明）
③ 关键实现与决策点（代码级细读；引当时 diff 要点）
④ 终态对照——这些代码在最终版本里的形态与演化弧线
⑤ 合理性评估与讨论（AI 初审 → 用户裁决；含与后续 bug / 返工 / 复盘文档的关联）
⑥ 发现问题记录（代码问题→当场修；注释问题→仅记录进第七章）
```

---

## 五、前导发现（几何与数据基础 · 终态视角自查）

> 定位：本清单产生于"按最终代码自查"（在时间线审阅确立前完成），不废弃——
> A 组死代码的"来历"将在时间线对应阶段（如 P6/P7）揭示引用；B 组注释失准条目直接进入第七章。

### 5.0 自查动作记录

```
1. 细读 PlanetBlueprintFunctionLibrary.h/.cpp 全文（轨道几何 + 离轨作业路径函数区）
2. 细读 SatelliteMassFragments.h / OrbitPlanningTypes.h 全文（逐字段对照消费点）
3. 细读 FOrbitDefinition（OrbitManagerComponent.h）
4. 交叉验证：
   · 全库调用方检索（6 个疑似死代码函数 + 2 个 getter，均零调用）
   · fragment 字段读写链追踪（PhaseControlPoint / PhaseStartLocation / ClaimHandle）
   · 空间约定溯源（CellManager L280 "星球中心在原点" + L220-228 格子生成 + 选点 L307 投影）
   · 配置字段溯源（CellOffsetZ / HoverHeight 的定义与使用）
5. 与交接文档/方案文档的承诺对照（贝塞尔"不采用"决策 → 相关函数应为死代码）
```

### 5.1 先行基准事实（核对时的参考基准）

```
· 空间约定：星球中心 = 世界原点（CellManager.cpp L280 注释"CellTransforms 为世界坐标（星球中心在原点）"，
  bWorldSpace=true 佐证）——全套几何在此前提下自洽
· 覆盖模型：现行实现 = 平面带（点到轨道平面距离 ≤ 服务距离，与轨道半径无关）
  权威实现：PlanetBPFL.cpp L95-98 IsPointInOrbitCoverage
· 转场轨迹：极坐标同步（方案 2）——贝塞尔已于第一轮废除（见交接文档 3.1）
```

### 5.2 A 组：疑似死代码（证据：全库检索零调用/零读写）

| # | 位置 | 内容 | 证据与推断 | 裁决 |
|---|---|---|---|---|
| A1 | `PlanetBPFL.cpp` L153-159 | `EvaluateCubicBezier` | 唯一调用方是同文件 `ValidateTransferPath`（其自身亦无人调）；贝塞尔转场已废除 | **成立·清理** |
| A2 | `PlanetBPFL.cpp` L161-186 | `BuildOrbitTransferControlPoints` | 零调用方；贝塞尔遗物 | **成立·清理** |
| A3 | `PlanetBPFL.cpp` L188-202 | `ValidateTransferPath` | 零调用方；贝塞尔遗物 | **成立·清理** |
| A4 | `PlanetBPFL.cpp` L130-151 | `IsAtNearestPhase` | 零调用方；旧"最近点判据"——现行 Depart 判据已改为"提前量点相位差 ≤1.5°" | **成立·清理** |
| A5 | `PlanetBPFL.cpp` L118-128 | `ComputeOrbitTangent` | 零调用方；原为 A2 的切向参数服务 | **成立·清理** |
| A6 | `PlanetBPFL.cpp` L10-16 | `CalculateOrbitAngleFromCell(FIntPoint, UObject*)` 重载 | 零调用方（FTransform 版仍在用——选点 Tasks.cpp L344） | **成立·清理** |
| A7 | `SatelliteMassFragments.h` L195-201 | `PhaseControlPoint1/2` 字段 | fragment 字段零读写；贝塞尔遗物（注释即写"贝塞尔控制点"） | **成立·清理** |
| A8 | `SatelliteMassFragments.h` L188-189 | `PhaseStartLocation` 字段 | 3 处写入（三任务 EnterState）、零消费；唯一"读取"是 Approach EnterState 自写自读（Tasks.cpp L941）；Tick 全走参数化 θ/r/v | **成立·清理** |
| A9 | `SatelliteManagerComponent.h` L129-130 / L244-246 | `TransferControlFactor` 配置项 + `GetTransferControlFactor()` | Getter 零调用；配置项面板显示"贝塞尔控制点比例 k"——误导（无任何代码读取该参数） | **成立·清理** |

> 裁决（2026-10-02）：A1~A9 全部【清理】——含头文件声明与相关注释、三处赋值行。
> 执行记录（2026-10-02 已完成）：BPFL.h -40 行 / BPFL.cpp -95 行（含 PlanetInterface.h include 与 FIntPoint 重载）
> / SatelliteMassFragments.h -12 行（PhaseStartLocation + PhaseControlPoint1/2）/ SatelliteStateTreeTasks.cpp -4 行
> （3 处赋值；Approach 的 OverrideLocation 初值改用局部变量 DisplayLocation）/ SatelliteManagerComponent.h -7 行。
> 执行后行号以新基线为准（前导发现条目中的行号仍为 461c1c0 口径）。

### 5.3 B 组：注释失准（✅ 已统一修订——10-13，处置见 §七 池）

| # | 位置 | 问题 | 现状 vs 注释 | 裁决 |
|---|---|---|---|---|
| B1 | `OrbitManagerComponent.h` L3-4 | 文件引用错误 | 注释称覆盖判定在"SatelliteMassFragments.h 的 IsPointInOrbitCoverage"；实际在 `PlanetBlueprintFunctionLibrary.h` | ✅ 已处置（10-13·Phase 3） |
| B2 | `OrbitManagerComponent.h` L27-29 | 模型描述过时 | 注释称"覆盖区 = 管（中线=轨道圆、半高=服务距离）∩ 球面"；现行实现为平面带（BPFL L68-74 注释已写明"与轨道半径无关"）——同项目两处注释互相矛盾 | ✅ 已处置（10-13·Phase 3） |
| B3 | `SatelliteMassFragments.h` L164-166 | 生命周期描述过时 | ClaimHandle 注释称"选点任务写入，执行任务释放"；实际释放点在回轨起飞瞬间（Return::EnterState，唯一释放点）；字段本体已降级为"兼容字段/首项句柄"（Tasks.cpp L484） | ✅ 已处置（10-13·Phase 3） |
| B4 | `SatelliteMassFragments.h` L80-82 | 措辞含糊 | OverrideLocation 注释"世界/星球本地坐标"——两词并列无法区分；实际=世界坐标（星球中心=原点约定下二者等价，建议统一表述） | ✅ 已处置（10-13·Phase 3） |
| B5 | `SatelliteMassFragments.h` L188 | 旧术语残留 | PhaseStartLocation 注释"（Fly 开始瞬间的位置）"——"Fly"是旧任务名；现行任务名为 Approach/Return/Drift | ✅ 已处置（10-13·Phase 3） |
| B6 | `SatelliteManagerComponent.h` L244-246 | 误导性配置注释 | "贝塞尔控制点比例 k（d = k × 起终点距离）"描述已废除的算法（与 A9 联动处理） | ✅ 已处置（10-13·Phase 3） |
| B7 | `PlanetBPFL.h` L56 | 历史引用 | "与旧 UOrbitMovementComponent::CalculateOrbitPosition 同源"——旧组件已从代码库删除；建议改为明确的历史注记措辞 | ✅ 已处置（10-13·Phase 3） |
| B8 | `PlanetBPFL.cpp` L47-57 | 注释编号断档 | "// 1." 之后跳到 "// 3."，缺 "// 2."——顺手修 | ✅ 已处置（10-13·Phase 3） |
| B9 | `PlanetBPFL.h` L1 | 模板残留 | "Fill out your copyright notice..."——UE 模板注释；且全文件无职责头（其余核心文件均有"// 文件名 // 职责"格式） | ✅ 已处置（10-13·Phase 3） |

### 5.4 C 组：逻辑/边界观察（✅ 已处置——10-13）

| # | 位置 | 观察 | 评估 | 裁决 |
|---|---|---|---|---|
| C1 | `PlanetBPFL.cpp` L13 | 失败时 `return 0.0f`——0° 是合法角度，调用者无法区分"失败"与"恰好在 0°" | 当前无调用方；若按 A6 删除则自然消解 | ✅ 已处置（10-13·Phase 3） |
| C2 | `PlanetBPFL.cpp` L29 | `Projected.Normalize()` 对"目标在轨道轴向（投影≈零）"退化情形无显式处理，atan2(0,0)=0 静默返回 0° | 实际格子不可能恰在轨道轴上，影响可忽略；如需严谨可加零向量早退 | ✅ 已处置（10-13·Phase 3） |
| C3 | `PlanetBPFL.cpp` L23 | `GetRotation().GetUpVector()` 与全套其余代码的 `GetAxisZ()` 风格不一 | UE 中二者等价（FQuat::GetUpVector()=GetAxisZ()），纯风格统一候选 | ✅ 已处置（10-13·Phase 3） |
| C4 | `ComputeOrbitHoverPoint`（L102-107）+ 选点 L307 + CellManager L228 | 几何计算一致地把格子投影到理想球面 R（忽略 CellOffsetZ 抬升）：选点 L307 重新乘回 PlanetRadius、悬停点函数取方向乘 R+H——CellOffsetZ 仅影响渲染不影响作业几何 | 系统自洽、无 bug；但此"约定"未在任何注释声明。**需确认**：① 资产 CellOffsetZ 实际值；② 设计意图是否"几何以理想球面为准"。确认后注释补充声明 | ✅ 已处置（10-13·Phase 3） |

### 5.5 D 组：已核对无误项（无需处理，供抽查）

```
· ComputeOrbitWorldPosition / DecomposeToOrbitPlane / IsPointInOrbitCoverage：
  语义、调用方、注释三者一致（覆盖判定"选点/可视化共用"属实：Tasks.cpp L308 + CoverageVisual.cpp L396）
· OrbitPlanningTypes.h 全部结构：字段注释与两处消费点（CollectOrbitSnapshots 写 / BuildOrbitPlan 读）
  逐项吻合，含 WaitAngleLimitDeg"同口径充当位移上限"（L993 确实复用）——干净
· 跨源角度比较链：DecomposeToOrbitPlane 的 [-180,180] 输出在三个消费点均做了
  NormalizeAngleDegrees360 归一——第 13 条易错点修复完备
· FSatelliteRenderFragment / FMassSatelliteSharedFragment：字段全部有使用，注释准确
```

---

## 六、时间线阶段报告索引（P1~P8·全部已产出）

> **位置**：`docs/代码审阅/时间线审阅/`（含本目录 README 索引·已收官归档）；八份报告均已核对（P1~P8 全无异议）

| 阶段 | 报告文件 | 一句话 |
|---|---|---|
| P1 | `P1_0917_起步日.md` | 纯实体跑起来→渲染/移动→巡收获；"选点即认领/帧序链"第一版 |
| P2 | `P2_0918_重构日.md` | 任务链搬进 StateTree（三任务+心跳）；玩家共享认领；旧系统退役 |
| P3 | `P3_0919_农业闭环.md` | 评分选点+活跃农田集合+全操作执行+作业时长；覆盖模型 v1 |
| P4 | `P4_0921_ISM治理.md` | ISM 参数注册表+空桶 Bounds 修复+轮廓分桶 |
| P5 | `P5_0922-23_轨道编辑系统.md` | 编辑系统诞生；覆盖判定换代；摧毁链缺归还埋雷 |
| P6 | `P6_0925-26_作业系统重构.md` | 相位统一+六状态+队列；贝塞尔→极坐标；槽位系统；8 缺陷复盘 |
| P7 | `P7_0929_全局规划.md` | K=5+群体决策+漂移+均匀化；运行期 13 问题全消；帧序收编 |
| P8 | `P8_1001_编辑专项收尾.md` | 选定暂停+重建链三修复+按轨重建；耐力回归通过；**终态** |

> 跨阶段主线（导览）：结构债线 / 覆盖模型线 / 转场模型线 / 摧毁链线 / 构建防线——详见本目录 README。

---

## 七、注释修订待办池（随审阅汇总；全部阶段审完后统一实施）

> 规则：各阶段 B 组（注释失准）裁决为"成立"的条目 + A 组裁决为"删除"条目涉及的注释 + C 组裁决为"已解释/需补声明"的条目，统一汇总至此；待用户确认后一次性实施（修订阶段只做注释）。

| 来源 | 条目 | 目标文件 | 修订动作 | 状态 |
|---|---|---|---|---|
| 前导发现 | B1~B9 | 见 5.3 | 逐条改 | **✅ 已处置（10-13·Phase 3）**：B1/B2/B3/B5/B6 随 N 系列重构自然消解（文件删除/注释重写）；B4/B7/B8/B9 实修 |
| 前导发现 | A1~A9（已裁决·清理） | 见 5.2 | 删除（含头文件声明与相关注释） | **已执行（10-02）** |
| P4 报告 | O1 深化（10-03）：UpdateBounds 调用点注释材料——引擎机制（写 API 不内联/帧末补偿四条件/重建分支吞补偿）+ 双重失效（误剔除/过度提交）+ Nanite 例外 | ISMManager.cpp | 修订阶段按材料补注释 | **✅ 已实施（10-13·Phase 3）**：两处（单桶/分桶路径）扩写——双重失效/Nanite 例外/引擎机制齐备 |
| P5 报告 | B10（候选）：OrbitCoverageVisualComponent.h 头注释引用文件错（写"SatelliteMassFragments.h"，应为 PlanetBlueprintFunctionLibrary.h） | OrbitCoverageVisualComponent.h | 逐条改 | **✅ 已修正（10-13·Phase 3）** |
| 模块审阅 M-2 | S3 发现（10-06）：注释“引擎智能对象实例位置不开放运行时修改”**不准确**——UE5.5 有公开 API `USmartObjectSubsystem::UpdateSmartObjectTransform`（SmartObjectSubsystem.h L487-493；槽位位置=实例 Transform×槽偏移，GetSlotLocation 可读）。现状自算缓存的真实理由 = 消费模式（高频/批量/跨线程）+ 线程约束（worker 读）+ 成本（每帧空间分区更新），而非“引擎不支持” | SmartObjectManagerComponent.cpp | 修订为准确表述 | **✅ 已消解（随 N10 重构）**：原注释随单 Slot 重写消失；“消费模式+线程约束+成本”选型理由已体现于 N8 槽池注释 |

> **Phase 3 实施记录（10-13）**：范围 = 9 月中~9 月底变更涉及的全部文件（15 提交 git 清单筛后 40 文件），
> 按模块五批"挨个修订注释"（规范：7 种格式 + @param 不省略 + 禁模糊代号直述设计 + 文件头统一）。本池全部条目闭环：
> B/C 组——B1/B2/B3/B5/B6/C1 自然消解，B4/B7/B8/B9/B10/C2/C3/C4 实修（含 C3 GetUpVector→GetAxisZ 等价替换、C4 理想球面约定声明）；
> O1/D3/S3——O1 扩写、D3 死注释块删除、S3 随 N10 消解；另清 9 处批次号（N8~N11）+ 6 处"方案 A/方案 B/乙案"代号 + 1 处不可见字符（Manager.cpp）；
> 收尾终扫：全库 N8~N11 零残留；观察项：10 处老文件 UTF-8 BOM（范围外·编译正常，未来可统一）。
> 【✅ 编译+运行验证通过（10-13）】含收尾裁定（UOrbitSlotBehavior 删除 / PlotOffsetZ 改名）+ 分组抖动缺头修复（ProjectBGameInstance.cpp）。明细见交接文档 §11.8。

---

## 八、裁决记录（用户核对）

> 用户逐条裁决后，AI 将结果回填各表"裁决"列，并在本节留总账。

| 阶段 | 裁决时间 | 结果摘要（成立 N / 不成立 N / 待定 N / 已解释 N） | 备注 |
|---|---|---|---|
| 前导·A 组 | 10-02 | 成立 9 / 0 / 0 / 0 | 死代码 A1~A9 已清理（五文件行数见 5.2 执行记录） |
| 前导·B 组 | 10-02 | 推迟 | B1~B9 待核对——**推迟至时间线审阅核对后一并决定（用户决定）** |
| 前导·C 组 | 10-02 | 推迟 | C1~C4 待讨论——同上推迟 |
| 结构 | 10-02 | — | P4 确认纳入审阅范围 |
| 时间线审阅 | 10-02 | — | **P1~P8 八份报告一次性产出**（docs/代码审阅/时间线审阅/）；⑤ 评估区已全部核对 |
| P1 核对 | 10-02 | **E1~E7 全部无异议**（7/0/0/0） | 报告 ⑤ 区已标注；另补充单桶决策论据（时刻运行因素） |
| P2 核对 | 10-02 | **E1~E7 全部无异议**（7/0/0/0） | 报告 ⑤ 区已标注；另发起“信号驱动范式”讨论（答复要点见下） |
| T1/T2 线程规范 | 10-02 | 裁决：**方案 C**（专职 GT 槽推进；Plan 保持 worker） | 已入第十章 M1（攒批执行） |
| 卫星单桶 | 10-02 | 裁决：**改“一轨一桶”**（多轨实例分摊） | 已入第十章 M2（攒批执行） |
| P3 核对 | 10-02 | 除 E6/E7 外无异议；E6 修正（双入口不优雅）+ E7 下调（重播待强化） | 追加议题 N1~N3（第十一节）+ 缺陷候选 D1~D4 |
| P4 核对 | 10-03 | **E1~E5 全部无异议**（5/0/0/0） | O1 认同；延伸调查“增量 API 与 Bounds”完成——原因/边界源码级明确（P4 报告 ⑦） |
| P5 核对 | 10-03 | **E1~E7 全部无异议**（7/0/0/0）；B10/观察项认同 | 唯一整改项：ApplyOrbitChanges“调试入口”定位不成立 → 入 M3（改名或删除） |
| P6 核对 | 10-03 | **E1~E8 全部无异议**（8/0/0/0）；观察项认同 | 补充：贝塞尔叙事修正（工具≠问题）；衍生 N5（槽位查询接口收窄）；虚拟相位讲解已提供 |
| P7 核对 | 10-03 | **E1~E8 全部无异议**（8/0/0/0）；观察项认同 | 防撞修正议题（实测仍有碰撞→盲区复核成立）→ 入 N6；群体决策流程图需求登记（交接文档待办） |
| P8 核对 | 10-03 | **E1~E7 全部无异议**（7/0/0/0）；观察项认同 | **全八阶段核对收官**（P1~P8 与 HEAD 合龙）——审阅核对阶段完成；下一步=攒批代码修改执行 |
| D1~D4 归属 | 10-03 | **归属建议全部认可**（D1→N2 / D2→N1 / D3→注释修订 / D4→未来项） | 锁定；随对应议题/阶段执行 |
| 收尾综述 | 10-13 | **Phase 3 注释统一修订完成+验证通过**（40 文件五批——B/C 组/O1/D3/S3 全闭环） | 收尾裁定 2 项（UOrbitSlotBehavior 删除 / PlotOffsetZ 改名）+ 分组抖动缺头修复；明细见 §七 池 |

### 讨论记录：信号驱动范式（P2 追问，10-02；10-02 二次修订——ue-codegraph 重检）

```
问题：Mass StateTree 信号驱动需外部心跳——是否任务设计问题（不该在任务内 Tick / 轮询任务本身是问题）？正确范式？
答复要点：
· 非职责错位：状态机（决策逻辑）与驱动源（何时评估）本就是两层——任务内 Tick 是“持续任务”（Running）的固有部分
· 引擎实现细节：UMassStateTreeProcessor 订阅 14 个信号（MassStateTreeProcessors.cpp L245-266）；
  且对“非 Running 的树”自我补信号（L327-345：Tick(0) 立即重试 → 仍非 Running → 补发 NewStateTreeTaskRequired）——
  “状态转移内的连续评估”引擎已处理；外部心跳的精确作用 = 给“持续 Running 的树”续命
· 【重检修正】UMassStateTreeProcessor 在游戏线程执行（构造函数 L228 显式 bRequiresGameThreadExecution=true +
  UMassStateTreeSubsystem trait GameThreadOnly=true 自动提升双保险）——项目六个 StateTree 任务代码全部在 GT 执行
· 理想形态 = 事件驱动 + 兜底心跳；当前“每帧全量信号”= 正确性优先的取舍；
  规模上升时演进：分层心跳（按状态过滤实体集）/ 关键路径事件化
```

### 重检证据补充（ue-codegraph，10-02）

```
引擎侧：
· 调度链：UMassProcessor::DispatchProcessorTasks（MassProcessor.h:53）← TriggerParallelTasks（MassExecutor.cpp:217）
· 任务线程：FMassProcessorTask → AnyHiPriThreadHiPriTask（TaskGraphInterfaces.h:100 定义）；GT 版 → GameThread
· 并行开关：MASS_DO_PARALLEL = !UE_SERVER（MassProcessingTypes.h L9-11）；phase 分支 mass.FullyParallel
· 自动提升：查询/需求线程 Traits（MassRequirements.h L102-127）——StateTreeSubsystem=true / Signal・SmartObject=false
· Spawner：AMassSpawner 全方法表 + UMassSpawnerSubsystem API（SpawnEntities/DoSpawning/DestroyEntities）+
  UMassEntityConfigAsset / FMassEntityTemplate（索引确认）
项目侧：
· UpdateOrbitSlots 真实调用方仅 Movement::Execute（索引曾对 TickComponent 报幽灵调用——陈旧快照，磁盘裁决排除“双推”假警）
· TickOrbitPlanning 唯一调用方 PlanProcessor::Execute（组件 Tick 已退出运行时驱动——一致）
· 线程规范观察点修正：仅 Movement/Plan 两处理器为非 GT 且调用项目自定义 UObject 方法（T1/T2 已裁决：方案 C，见第十章）——任务链本身无此问题
· 工具边界：search_code 覆盖符号/成员级文本（局部变量不在检索域，改磁盘/符号级裁决）；
  文本索引存在变更记录源幽灵行（需 reindex_file 刷新磁盘源）
```

### 讨论记录：贝塞尔与设计的关系（P6 追问，10-03）

```
用户结论：首版贝塞尔转场的根因不是贝塞尔曲线本身，而是“曲线的计算没有理清”——
该问题在极坐标阶段得到清晰分析（切向同步/两分量/δ0/运动目标结构）；
在设计清晰之后，贝塞尔曲线预测可以作为（未来）路径优化的一种候选方式。
落档：已同步交接文档 §3.1（替换原“不采用贝塞尔（追运动目标必然外溢缺陷）”的绝对化措辞）；
P6 报告 3.4 加“后见修正”；记忆已录（important_decision）。
现状语义：当前转场=极坐标（定稿）；贝塞尔不作现行方案，但不再定性为“工具错配”——
而是“当时计算未理清”；未来路径优化（平滑/避让）可评估贝塞尔预测。
```

### 讨论记录：虚拟相位的定位（P6 追问二次，10-03）

```
问题：虚拟相位与群体决策几乎无关——是否多余？
结论：不多余——它不是“回位目标/决策参考”，而是【单星的轨道时间轴状态】；三条不可替代职责：
① 漂移中断的连续锚（承重·正常流程）：中断帧写回“当前滑行相位”（Tasks L1241）+ 释放目标槽 + 清覆盖
   → 无槽+无覆盖窗口期由渲染③（VirtualPhase 相位点）接管，且 Movement 继续按 ω 推进——
   “中断”即“无缝回到轨道巡航”（视觉上沿轨自然前进），等离轨时机后起飞（Approach 起点兜底同源）
② 无槽星全链兜底（防御·鲁棒性）：显示（渲染③）/ 离轨判据（DepartWait）/ 起飞起点（Approach）——
   “无槽”作为被允许且自愈的中间态，全靠虚拟相位保持“像一颗正常在轨卫星”
③ 归位对齐目标（到达写回）：回轨/漂移到达时 VirtualPhase := 槽相位——“影子追实体”，
   保证任何时点回退到③兜底都不跳变
反事实：删除后需引入替代锚（冻结旧槽点/保留覆盖到起飞）并重约定清覆盖时序——成本高于收益；
若未来约定“永不允许无槽态”（异常即重建），可再评估精简。
语义修正：注释“回轨依据”易误读为“回位”——其真实语义=“这颗星现在在轨道时间轴上的哪个位置”；
方向是“影子（相位）追槽”，不是“星追原位”。注释修订阶段可把此定位写准。
【定义精化（追问三次，10-03）：名义相位（nominal phase）——三个相位概念辨析】
· 名义相位 = VirtualPhase：只受时间（+ω·dt）与三个对齐动作（初始化·到达归位←槽相位·漂移中断←显示）驱动；
  “实时的是时间，名义的是位置”——匀速轨道上相位≡ω·t，它把“时间”编进相位，离轨后替代物理位置承担时间轴表达
· 槽相位（SlotPhase）：群体决策分配的空间席位（目标）
· 显示方位（DisplayTheta）：从显示位置反算的方位角——“实际”之果，随飞行/悬停到处跑，不可作锚
关系：名义永远向现实收敛——到达时“名义←目标槽”，中断时“名义←显示”——保证它始终是当前现实的合理抽象。
命名注：virtual 本义=“名义上的”（非“虚假”）；中文“虚拟”易误导——它不是假位置，而是名义位置。
注释修订建议（供实施）：
  /** 名义轨道相位（度；按 ω 推进的时间轴状态——与物理位置/槽位分配无关）
   *  读：无槽兜底三件（渲染/离轨判据/起飞起点）；写：初始化均分/到达归位（←槽相位）/漂移中断（←滑行相位） */
【时间语义澄清（追问四次，10-03）】
· “是时间吗”——是：轨道相位时间（相位=时间×ω，按轨道周期归一化的时间；非绝对时钟，每圈回绕）
· “全程还是仅在轨”——两个维度分开答：
  推进上：【卫星实例存续期全程】——离轨/作业/飞行照常走；三例外：全局选定暂停（SIMULATION_PAUSE）、
  ω≈0 静止轨道（不参与作业体系）、销毁重建=新代际重新均分（不跨代际继承）
  语义上：【轨道语境】的时间——不表示作业进度/飞行进度（那是 Task.PhaseElapsed/Alpha 的职责）
· 设计选择“角度而非秒”的理由：①轨道几何的天然坐标（直接代入算位置）②与槽相位同为角度，
  对齐/补偿（δ0）免换算③归一化 [0,360) 无累积漂移④与 ω 解耦（改 ω 不破坏已表达相位）
【轨道级变量为何不可行（追问五次，10-03）】用户提议：轨道时间放轨道级（同轨共享）？
事实修正：同轨卫星 VirtualPhase【不相同】——①初始均分（星 i = 360i/N，本就不同）；②作业历史
  个性化重写（回轨归位 P←新槽相位 / 漂移中断 P←滑行相位）——差值随时演化，非共享常量。
“轨道时间”已存在（另一系统）：槽网格基准相位 FOrbitSlotEntry.CurrentPhaseDeg（轨道级、GT 推进）——
  那是【槽资源】的时钟；VirtualPhase 是【每星】的名义账本——两轴有对齐协议（同速+归位对齐+初始均分）
为何不能轨道级化（四理由）：
① 存储无节省：个性化偏移仍需每星一份；“基准+偏移”组合=双字段，违背单一事实源道德
② 【决定性】线程模型：Movement 为 worker 处理器——fragment 并行安全；组件/轨道数据=UObject=worker 禁区
   （T1/T2 原则“能不在 GT 就不在 GT”）；且 ω 已是“拷入 fragment 的运动参数”的同款先例
③ 数据域归属：Mass 实体状态放 fragment（实体自含）；槽（智能对象）在组件、卫星（实体）在 fragment——各归其域
④ 对齐体系需要“每星可写的绝对值”（归位/中断直接重写某星相位）；共享变量多星写=竞争温床
历史背书：复盘 Bug4“双推进源”——修复=同帧同序推进+对齐协议（而非合并变量）；合并会重新引入共享可变状态
```

### 讨论记录：M2 扩展方式的重新设计（10-04 · 双维度分桶模型）

```
背景（流程复盘）：M2 首版实施超前于“执行级设计”——用户及时叫停（“不确定符不符合设计，先讨论计划”）。
教训入规：M 类大项实施前必须经过【执行级设计】环节（路线 → 细则 → 敲定 → 实施）。

一、需求本质（用户定调）：原有 ISM 管理器只有【空间分桶】——不分桶即“全部一个桶”，缺少【逻辑分桶】；
   应把两个维度都在对外接口中显式区分（各带开关与依据），内部实现取改动面小的方案。

二、路线评估（AI 草案 → 用户选定）：五条候选路线——
   · 路线1 键+标签轻量扩展（(Mesh,键) 组键；选中——改动集中、旧路径零破坏）
   · 路线2 句柄持组件引用（组件生命周期/池化冲突，不取）
   · 路线3/4 组件单元/渲染通道模型（语义最净但内核重塑，改动大，不取）
   · 路线5 卫星自管渲染（重复造轮子、打散 ISM 底座定位，不取）
   维度澄清：空间分桶 = 一个组件的多个桶；逻辑分组 = 多个组件的组织方式——不同维度、正交可组合。

三、敲定细则（10-04 用户确认“全部按推荐来”）：
   ① 逻辑维度“依据即开关”：GroupKey=0 → 默认组（与旧行为等价）；≥1 → 逻辑组；无独立 bool
   ② 轨道键编码：轨道序号 + 1（0 保留默认组）
   ③ 参数形态 b（相邻重排）：GroupKey 位于 GridCoords 之后、无默认值——
      漏改调用 = 编译报错（不静默错位，谓“编译器兜底”）
   ④ 命名：逻辑分桶 / GroupKey（替换 GroupTag）
   ⑤ 调试列表按 Mesh 聚合 + groups 列（非按组拆行）
   ⑥ 两级表：默认组 (Mesh,0) 条目兼任 Mesh 级注册载体（种子继承含告警状态）
   正交矩阵：逻辑键{0/≥1} × 空间{关/开} 四种组合均有确定语义（见 .h 注释）
四、落地与差异：内部机制 ~80% 复用；差异 = 接口整形 + 命名统一 + 6 处调用点补参（停用旧体系里的
   引擎原生 AddInstance 核实为无关、零改）；已实施代码按敲定方案调整完成（9 文件）。
```

---

## 九、单阶段闭环操作流程（每阶段依据 · 2026-10-02 定）

> 本流程为每个阶段（P1~P8）的执行依据；用户已确认采用。

### 9.1 闭环六步

| 步骤 | 谁 | 动作 | 产出 |
|---|---|---|---|
| **1 提交盘点** | AI | `git show --stat {hash}` 逐提交列变更清单（文件/行数） | 报告①"本节提交与变更概览" |
| **2 当时视角细读** | AI | `git show {hash}` 读每个提交完整 diff → 归纳"做了什么、为什么"（配合当时日期的文档/复盘佐证；不含后见之明） | 报告②"当时做了什么" |
| **3 关键实现与决策点** | AI | 从 diff 提取关键代码段逐段讲透：解决什么问题 / 用什么方案 / 取舍在哪 | 报告③ |
| **4 终态对照** | AI | 这批代码在最终版本里：保留至今（给当前行号）/ 被谁改写（指向后续阶段）/ 已删除 | 报告④（演化弧线） |
| **5 核对与评估** | 用户 | 看报告 → 对照代码抽查（AI 附"自查位置"清单）→ 对"合理性评估"逐条表态、对"问题记录"逐条裁决 | ⑤ 讨论结论 |
| **6 处理与收尾** | AI | 代码问题当场修（裁决成立者）；注释问题记入第七章；报告落档第六章 → 更新推进表 → 进入下一阶段 | 台账更新 |

### 9.2 工具细节（怎么看"当时的代码"）

```
AI 代劳（每阶段执行）：
  git show {hash} --stat              # 该提交改了哪些文件
  git show {hash}                     # 该提交的完整 diff（细读来源）
  git show {hash}:{file}              # 该提交时某文件的全文（对照用）
  git diff {hashA}..{hashB} -- {file} # 两提交之间某文件的演化

用户自查（Rider 内，无需命令行）：
  · Log 面板 → 选中提交 → 右侧文件树与 diff
  · 任意文件右键 → Git → Show History → 该文件逐个提交的变化
```

### 9.3 报告详略原则

```
关键决策点全讲透；样板/机械代码归纳成一句话——保证"逐行级覆盖"但不逐行贴代码。
用户点名哪段讲浅了即展开。
```

### 9.4 问题分流（已定）

```
· 代码问题（逻辑 bug / 死代码 / 风险）→ 用户裁决成立 → 当场修（不等收尾）
· 注释问题 → 仅记录进第七章（修订阶段统一处理，审阅期间不动注释）
```

### 9.5 会话推进节奏

```
· 每次会话：读交接文档 + 本清单 → 确认当前阶段（P1~P8）与状态 → 继续闭环
· 每阶段一个回合：AI 出报告 → 用户核对/裁决 → AI 处理并落档 → 进入下一阶段
· 全部阶段完成后：汇总第七章 → 注释统一修订 → 状态收尾
```

---

## 十、待执行代码修改清单（10-02 定 · 执行窗口：注释修订阶段之前；M1~M4 + S3 均已执行完毕 ✅）

> 规则（2026-10-02 用户修订）：审阅与讨论中裁决的**代码修改**不再当场实施，统一攒入本节；
> 全部审阅核对完成后、注释修订阶段之前，集中执行。【先行例外：A 组死代码清理已于 10-02 执行】

### M1 T1/T2 线程规范修复（裁决：方案 C——专职 GT 槽推进处理器）【✅ 已完成（10-04·编译+运行验证通过）】

```
背景（10-02 讨论定案）：
· T1：Movement（worker）chunk lambda 内 "bSlotsUpdated 仅首个 chunk" 全局副作用在 chunk 并行下有竞态窗口
· T2：Movement/Plan（worker）调用项目自定义 UObject 方法（引擎 Traits 体系不覆盖项目组件）；
  引擎侧已核实：SmartObjectSubsystem/SignalSubsystem trait=false（引擎允许 worker）、
  状态树处理器在 GT（项目六个任务代码合规）
修复方案（遵循 Mass“能不在 GT 就不在 GT”——只把“必须 GT 的最小部分”移出）：
① 新增专职 GT 处理器（建议名 USatelliteSlotSyncProcessor）：
   · bRequiresGameThreadExecution = true；唯一职责 = 调用 SmartObjectManager->UpdateOrbitSlots(dt)（含 DrawDebug）
   · 帧序：Plan 之后（ExecuteAfter 规划处理器名）→ 本处理器 → Movement（ExecuteBefore("SatelliteOrbitMovement")）
     ——保持“先槽后星”与现有相对时序（Plan 读推进前槽相位）
   · 暂停宏：SIMULATION_PAUSE_RETURN（编辑暂停冻结槽推进——与现状一致）
② Movement：删除 lambda 内槽推进段（含 bSlotsUpdated / UpdateOrbitSlots）——T1 竞态随之消除；
   保留纯 fragment 相位推进（worker 并行安全）
③ Plan：保持 worker（不拆）——访问性质核实：只读稳定数据（fragment/RuntimeOrbits）+ 引擎允许的 Subsystem 调用 +
   两个仅自用的节流时间戳（单写者单读者）；注释中如实声明“节流时间戳为 worker 私有数据”
   【备选：若后续要求全部 UObject 访问 GT 化 → 节流状态迁至处理器实例成员（执行时再议）】
④ 【并入·帧序组名全量显式化（N4-B，10-02 用户确认并入 M1）】：卫星全链设组
   （SatellitePlan / SatelliteSlot / SatelliteMovement / SatelliteRender / SatelliteHeartbeat）+ 组间依赖声明——
   替代现状“仅 Movement 设组、其余靠类名/无声明”；新处理器（SlotSync）按组名接入
验收：帧序链 Plan→Slot→Movement→Render；编辑暂停冻结；位移/相位无漂移（R1 短跑复验可选）
【执行记录（10-04）】① 新建 USatelliteSlotSyncProcessor.h/.cpp（GT + 唯一职责 UpdateOrbitSlots + 暂停宏）；
② Movement 删除槽推进段与 ConstShared 需求（纯相位推进、include 清理）；③ Plan 保持 worker + 线程说明注释；
④ 全链组名（Plan/Slot/Movement/Render/Heartbeat）+ 后继声明前驱单向链；⑤ 暂停白名单注释 4→5 同步
（SimulationControlSubsystem.h + SmartObjectManager 两处驱动者指涉注释）。
验证：组名链 9 处声明就位 / 旧组名字符串零残留 / 9 文件已 reindex。待用户编译验证。
```

### M2 卫星渲染改“一轨一桶”（裁决：10-02 用户要求）【✅ 已完成（10-04·编译+运行验证通过；首版超前实施→叫停→双维度设计敲定→按敲定方案实施）】

```
【注】下为 10-02 原始设计要点（历史记录）；最终敲定版（双维度分桶 / GroupKey / 参数相邻重排）见上方同名讨论记录。
背景：全局单桶（现行）在满配（每轨 50 星）×多轨下实例过多；每轨独立桶可分摊实例并恢复逐桶 Bounds/剔除能力。
关键设计（与“球面分桶”的本质区别）：
· 分桶键 = 逻辑固定属性（轨道），不是位置——卫星永不跨组迁移 → 无迁移成本、无句柄代际风险
  （与 P1 报告的“时刻运行”结论不冲突：那条针对球面位置分桶）
· 组织：ISMManager 分组键从 FISMMeshKey(Mesh) 扩展为 (Mesh, GroupTag)——每 (Mesh, 轨道) 一个 ISM 组件
  （组内单桶 Buckets[0]）
· 句柄扩展：FGlobalInstanceId 增 GroupTag 字段（默认 0）——Batch*/Update*/Remove* 接口签名不变
  （组从句柄自解析）；仅 BatchAddInstances/AddInstance 增 GroupTag 参数（卫星注册传轨道标记）；
  旧调用方（地块/作物/轮廓）默认 0 零改动；句柄相等/Hash 需同步带 GroupTag
· Mesh 参数注册（RegisterMeshParams）维持 Mesh 级；组内 CachedParams 从 Mesh 级初始化（两级表结构，执行时细化）
· 卫星侧：轨道→GroupTag 映射（稳定轨道序）；RegisterRenderInstances 按轨传 tag；销毁/移除由句柄接管；渲染同步调用点不变
· bDisableBuckets 语义注明：卫星该值含义 =“禁用球面位置分桶，组内单桶”（与一轨一桶兼容）
验收：每轨一个 ISM 组件；多轨实例分摊；单轨行为与现状一致（视觉/剔除无回归）
【执行记录（10-04·两阶段：首版实施 → 设计敲定后调整）】
阶段一（首版实施）：内部机制（键/句柄/表/分发/种子/调试区）+ 卫星侧逐轨注册。
阶段二（设计敲定“双维度分桶”后调整——最终形态）：
① 命名：GroupTag → GroupKey（逻辑组键）；概念定名“逻辑分桶”（与空间分桶成对）
② 接口重排（双维度依据相邻）：BatchAddInstances/AddInstance 的 GroupKey 移至 GridCoords 之后、
   无默认值（漏改调用 = 编译报错，不会静默错位——编译器兜底）
③ 调用点补参（6 处活跃）：CellManager（地块）/ CellVisualSync（轮廓）/ CropVisualSync（作物）/
   OrbitCoverageVisual（圆环）/ FarmlandActor（验证实验台×2）——GridCoords 后补 /*GroupKey=*/0
④ 注释双维度文档化（.h 结构体/类头/接口注释分块）
兼容：GetOrCreateBucket/GetBucketComponent 保持尾参默认值（外部旧调用零改）。
验证：命名零残留（三通道核验）；9 文件已 reindex。待用户编译验证。
【验证结果（10-04·用户日志）】编辑模式中修改 Orbit_02 → 仅重建该轨（Rendered 10 → ISM group 2）；
新增 Orbit_03 → 独立注册（group 3）；total 20→30 对账正确；零告警。
一轨一桶✓（group=轨道序号+1）/ 按轨重建✓（其他轨零触碰）/ 编译✓——M2 完成。
验收：每轨一个 ISM 组件（调试统计显示 group 键）；多轨实例分摊；单轨行为无回归。
```

### M3 ApplyOrbitChanges 处置（✅ 裁决：删除——10-03 用户确认）【✅ 已完成（10-04）】

```
事实链（10-03 磁盘+资产级核查）：C++ 全项目零调用（仅 .h L154 声明 + .cpp L514 定义）；
蓝图资产零引用（Content 全量二进制筛查）；Exec 通道无关联（6 个 Exec 均不涉全量重建）。
“保留为调试入口”定位不成立——无任何通道实际使用它；UFUNCTION(BlueprintCallable) 仅暴露可能性。
推荐：删除（含头文件声明与“供调试”注释；全量能力不失——等价于逐轨循环 ApplyOrbitChangesForOrbit，
编辑器逐轨提交已覆盖；未来如需真调试入口建议另加 Exec 命令而非 UFUNCTION 常驻暴露）。
若保留：改名如 RebuildAllSatellites() + 注释明确“全量重置”语义、去掉空话。
【裁决确认（10-03）】用户：“删除吧”——锁定删除方案：移除 .h 声明（含 UFUNCTION 与“供调试”注释）
+ .cpp 定义；全量能力不失（= 逐轨循环 ForOrbit；编辑器逐轨提交已覆盖）。与 M1/M2 同批执行。
【执行记录（10-04）】① 删除：.h 声明（含 UFUNCTION + “供调试”注释）+ .cpp 定义（含“Applied orbit changes”日志）；
② 连带修正 6 处注释提及为按轨语义：.h×4（结构体头/分区注释/UpdateOrbitData/运行时数据字段）
+ .cpp×1（CreateAllSatellites 槽注册注）+ OrbitEditorWidget.h×1（接口列举过时名 → CommitOrbitChanges）；
③ 词边界全库核验：`ApplyOrbitChanges\b` 零残留；3 文件已 reindex。全量重建能力不失（=逐轨循环）。
```

### M4 槽位查询接口收窄（裁决：删除按索引版 WorldPosition——10-03 用户确认）【✅ 已完成（10-04·方案甲：内联）】

```
执行内容：
· 移除 SmartObjectManagerComponent.h 的 GetOrbitSlotWorldPosition（按索引）公开声明；
· .cpp 实现体改造：ByClaim 版本内联索引解析（或转 private 辅助函数——执行时择一，对外行为不变）；
· 不动：GetOrbitSlotPhase（按索引——规划快照承重）、GetOrbitSlotCount、TryClaimOrbitSlot、ByClaim 三件
· 复核依据：外部零调用（Rider 精确检索）且无明确未来用途（规划只查 Count+槽0基准；调试绘制读内部数据）
验收：槽位功能零回归（检索确认无残留调用）；代码编译由用户自验
【执行记录（10-04·方案甲内联）】① .h 移除公开声明（含注释）；② .cpp 删除定义体；
ByClaim 实现内联索引解析（原两步→直查表，行为等价：ResolveSlotIndex=-1 时 IsValidIndex 拦住）；
③ 不动面确认：Phase（按索引版——规划快照承重）/Count/认领/ByClaim 三件均未动；
④ 核验：词边界 `GetOrbitSlotWorldPosition\b` 全库清零（findstr 磁盘直读裁决——Rider 搜索有
VFS 刷新延迟，刚编辑文件需用 findstr/Grep 兜底）；ByClaim 版 7 处调用面零影响；2 文件已 reindex。
```

### S3 弱指针化（M-3 审阅发现 · 独立小项）【10-07 用户裁定：独立（不并入 N9）】

```
背景（M-3 静态审阅 S3）：父对象/跨对象指针的强弱指针规范化。全项目扫描分类：
真目标（非拥有观察引用——对象可能先于持有者销毁）5 处 → 改 TWeakObjectPtr：
  ① BasePlayerController::PreHoveredPlanetActor（悬停高亮缓存）
  ② SurfacePawn::HoveredPlanet（球体导航缓存）
  ③ CellManagerComponent::BasePlayerController（玩家操作入口缓存；附隐患：L1034 无判空直接解引用）
  ④ CropManagerComponent::BasePlayerController（旧组件）
  ⑤ FarmlandComponent::BasePlayerController（旧组件）
宿主引用 2 处 → 可统一弱化（对齐先例 CellManager::OwnerActor 已是弱）：
  SatelliteManagerComponent::PlanetActor / OrbitCoverageVisualComponent::PlanetActor
不宜动（强引用语义正确）：配置资产引用（PlanetConfig/FarmlandConfig/DataTable/SmartObjectDefinition/
  StateTree/Mesh 等）/ 自建子组件（CreateDefaultSubobject 同级）/ 子系统引用（SmartObjectSubsystem）
状态：【✅ 已完成（10-07·编译验证通过）】14 文件改动——
· 声明：7 处 TObjectPtr→TWeakObjectPtr（组件宿主×2：Satellite/OrbitVisual::PlanetActor；
  非拥有观察×5：PC::PreHoveredPlanetActor / Pawn::HoveredPlanet / Cell·CropManager·Farmland::BasePlayerController）
· 使用点适配：布尔判断全部改 IsValid()（弱指针无 operator bool——引擎显式 delete；实测验证）；
  SurfacePawn::GetHoveredPlanet getter 改 .Get()；operator-> 使用点零改动
· 加固：CellManager L1034 无判空解引用 → 前置 IsValid 守卫（无效返默认色）；FarmlandComponent OutlineColor
  同款守卫；两处 PlantCrop 读取点改条件表达式（PC 无效传空串——安全退化）
· 核验：`TObjectPtr<APlanetActor>|TObjectPtr<ABasePlayerController>` 全库零残留；布尔残余仅 2 处注释掉的旧代码（预期）；
  14 文件已 reindex；编译由用户验证
```

---

## 十一、后续改进议题（N 系列）登记（10-02 起 · 已裁决/待审议）

> 【系列定名（10-09）】本登记集正式命名：**“后续改进议题（N 系列）”**——审阅/讨论衍生的改进·重构·专题·流程总集。
> 【10-11 状态】重构批次四份方案全部定稿（N8 全裁·D6=方案 A / N9 ✅ / N10 ✅ / N11 ✅）——【排期锁定·方案 A】：
> ① 重构批次：N10 → N8 → N9 → N11（每项：实施→编译→浅测门禁→修补；计划 AI ~9~12 轮 + 测试 ~3.5 窗）
> ② 批次总验证（门禁）：三链全回归 + R1/R2 耐力复跑　③ 注释统一修订　④ N3 EQS 回归
> ⑤ 后续独立项：N6+N7 → N1（按发起）；N2 条件触发。详细节拍见交接文档 §11.7。

### N1 操作体系统一化：Action 资产化 + 重播重设计 + AI/玩家路径统一【用户发起·待评估】

```
现状诊断（10-02 核查，证据：CellManagerComponent.cpp L292-381/L837-949/L1018-1024 等）：
· 双入口 90% 重复：HandleLeftMouseClicked 与 ExecuteFarmAction 两份 switch，唯一差异 = 播种来源
  （玩家 SelectedCropName / AI 空串）；空串→重播的语义链为隐式约定
· 重播无选择权：PlantCrop 重播分支【强制沿用】原 CropID（注释“忽略传入名”）——玩家无法换种；
  重播成本/策略/自动重播均无配置概念（版本停留/停留时长/连作规则）
· 操作体系与实现脱节：CropDried / RemoveCropDried 空实现；EEditMode 枚举齐全但无“支持矩阵”
· 知识分散四处：时长（智能对象行为定义）/权重（FSatelliteScoringWeights）/作物数据（CropDataTable）/
  重播延迟（FarmlandConfig）——同一操作的知识无统一实体
评估结论（成立性）：方向【成立】——正逢 EQS 回归前窗口，建议：
· L1（数据层·并入本期或 EQS 前置）：UFarmActionDefinition 资产（ActionId/目标Slot/时长/播种来源策略枚举/
  评分权重/支持标志）；统一单入口 ExecuteAction(ActionId, Coords, Context)；EEditMode 保留为输入语言
  （映射表转 ActionDefinition，不废枚举）；重播策略显式化（KeepExisting/Selected/Fixed/Random）
· L2（可选·与 EQS 同期）：缺口判定/评分归口 Action 资产（评分遍历 Actions 而非硬编码六分支）
· 不建议：执行逻辑类化派发（保持现有 CellManager 命令式实现，先收数据不收逻辑）
待用户审议后转入方案阶段（需求方案+执行方案并行）
【用户认可（10-02）】承载边界（数据策略 ✅ / 执行逻辑不类化 ❌）已获认可；草案待立项时再出——当前仅记录
```

### N2 活跃农田集合替代实现【🎉 已立项·后续推进（10-02 用户确认）】

```
本质：ActiveFarmlandCoords 是“从格子实体派生的索引缓存”（权威 = 格子 fragment；缓存 = TSet）——
     问题不在 TSet 本身，而在【维护协议】：意图委托驱动 + 双源 + Defer 落盘前的意图广播缝隙（自愈但脏）
候选对比：
· 现状（意图委托+TSet）：简单；弱点是同步协议脆弱（任何绕过 CellManager 公开函数的路径会脱同步）
· 最小修：初始化/存档重建扫描 + 缝隙对齐——成本最低，未治本
· 【推荐】Mass Observer 化：以“作物存在”的 fragment 变化（Observer 在 Defer 落盘后触发）维护
  活跃作物【实体列表】，选点直接遍历实体（不再坐标反查）——事件源更真实、消除跨组件耦合、Mass 原生；
  前提：修复 FCropTag 授予/移除链（见 D1），以 Tag 为可观察标记
· 查询直用：StateTree 任务无法直接用处理器查询（RegisterQuery 需处理器宿主）——需选点搬家，不取
配套：D1 修复（FCropTag 落地）是本议题的天然契机；与 N1 同期或紧随
【Observer 机制解释已提供（10-02，引擎源码实证）】——用户问询“Mass Observer 化是什么”，答复要点：
· UMassObserverProcessor = 对“实体组成（archetype composition）变化”的声明式订阅处理器——
  字段：ObservedType（Fragment/Tag 类型）+ Operation（Add/Remove）（MassObserverProcessor.h）
· 事件源 = 组成真实变化（AddFragment/AddTag/RemoveFragment/RemoveTag，含 Deferred 命令 flush 时——
  MassEntityManager.cpp HasObserversForBitSet → OnCompositionChanged）——不是业务意图广播
· 触发 = 变化当下同步；执行 = 同（类型×操作）的观察者构成 FMassRuntimePipeline 按序跑（HandleFragmentsImpl）
· 与普通 Processor 的区别：不走 processing phase/不每帧循环——“变化驱动”而非“帧驱动”
· 注意：观察者在命令冲刷上下文同步执行——处理纯数据容器最稳（碰 UObject 状态时按 M1 线程纪律处理）
状态：待审——用户理解后决定是否立项
【立项确认（10-02）】用户：“Mass Observer 化非常好，确认后续推进”——本项目已立项，后续按批次推进
（与 D1 修复绑定；排期可与 N1/EQS 一并梳理）
【口径补充（10-07，M-3 S4）】用户：“意图委托这些现在就看看，之后尽量迁移 Mass Observer”——
维持已立项；方向明确为“尽量迁移 Mass Observer 形态”（观察者驱动实体列表，替代意图委托+双源 TSet）。
【口径重述（10-09·受 N10 深化裁定影响）】原“活跃农田集合”已被 N10 删除（选点改全格直扫）——本议题
  重定义为【选点扫描性能优化·条件触发】：仅当全格扫描实测不达标时，引入 Observer 实体列表缓存；
  D1（FCropTag）随之降级为可选配套。
```

### N3 EQS 回归排期【已定】

```
用户决定（10-02）：EQS 回归预期在本期修订（审阅核对 → 攒批代码修改 → 注释修订）完成后启动
衔接：N1 评分权重挂 Action 资产，正好为 EQS 评分器提供统一数据源——建议 N1 先于/同期 EQS
```

### 附：新发现缺陷候选（D1~D4）【✅ 归属已锁定（10-03 用户认可）】

```
归属（✅ 10-03 用户认可·锁定）：D1→随 N2 Observer 化修复（已绑定）；D2→留待 N1 立项时处理；
D3→注释修订阶段清理；D4→未来项（存档落地时处理，N2 关联）。
```

```
D1 FCropTag 死守卫：全项目仅“定义 + RemoveLand 检查”两处，【从未授予】——CellManagerComponent.cpp:698 的
   “有作物不许移除耕地”守卫生效不了（Tags 永远为空）；同时是 N2 的观察点基础
D2 空实现操作：CropDried / RemoveCropDried（L1018-1024）为空函数——枚举齐全、实现空心
D3 死注释块：PlantCrop 内 L923-944 大段被注释的“随机初始生长时间”代码（注释修订阶段一并清理）
D4 存档空壳：GetSaveData/ApplySaveData（L383-394）未实现——未来存档落地时需处理“活跃集合重建”（N2 关联）
观察项（非本次范围）：CropManagerComponent/CropBase 旧作物体系已停用（PlanetActor.cpp L37-39 创建被注释），
  源码树仍存——总体清理候选
```

### 附：D5 顺时针方向语义缺陷【2026-10-04 发现·已修复（M1 验证期）】

```
用户报告（新增顺时针轨道后）：离轨路径方向反 / 漂移异常——彻查确认 4 处“沿运动方向量 vs 相位增量”
换算缺陷（逆时针时数学恒等、从未暴露；自 P6 极坐标换代起遗留）：
· Approach 切向推进（Tasks）：FindDelta(start→hover)×dir —— 多乘 dir → 顺时针飞行方向反转【已去乘】
· Return δ0 补偿（Tasks）：FindDelta(H→S)×dir —— 多乘 dir → 顺时针落点偏差 2×δ0【已去乘】
· Drift 总相位增量（Tasks）：ω·T·dir+Δs —— 位移项缺 dir → 顺时针前/后移符号反【改 (ω·T+Δs)×dir】
· 规划弧段（Manager）：同款公式 —— 防撞弧区间符号反【改 (ω·T+Δs)×dir】
验证：4 处修复在 dir=+1（逆时针）时数学恒等（零回归）；顺时针全链经数值推演闭环。
规约沉淀：见交接文档易错点 #20（“沿运动方向量→相位增量一律乘 dir；直接相位差不得乘 dir”）。
```

### N4 Mass 化增强候选清单【10-02 盘点·参考】（用户要求“项目架构更贴合 Mass”）

```
已用 ✅：Fragments/Tags/SharedFragment（含 ConstShared 按值去重）/ Archetype 手写创建 / EntityQuery（处理器层）/
        依赖图（部分）/ Deferred Commands / StateTree+SmartObject 集成 / MassSignals（发送侧：心跳）/ 批量 API
将用 🆕：Observers（N2 已立项）
候选 🔶：
· B 帧序组名体系全量显式化：现状仅 Movement 设了 InGroup（其余靠类名引用或无声明）——
  【已并入 M1（10-02 用户确认）】：卫星全链设组（Plan/Slot/Movement/Render/Heartbeat）+ 组间依赖声明
· C 信号事件化扩展：落座发单 SignalEntity 唤醒待返星（点对点）；分层心跳（按状态过滤实体集）——与心跳演进绑定
· D MassEntityView：任务/处理器循环取 fragment 的写法升级（缓存 archetype 偏移，可选打磨）
· E 调试工具链：mass.debug / ProcessorScheduling 可视化 / MassDebugger（低成本·演示价值）
· F Tag 化设计习惯：需观察/需筛选的离散状态用 Tag 表达（N2 的 FCropTag 落地为示范）——
  边界：Observer/查询只响应“组成增删”，不响应“fragment 字段值改变”（设计时需分辨）
有意不用 ⛔（理由已录·防误判）：MassSpawner+Trait（同质小规模/与认领链深耦合）、
  MassRepresentation（自建 ISMManager）、MassLOD（当前规模）、ZoneGraph（自建环形轨道）
```

### N5 槽位查询接口收窄（按索引版 WorldPosition 零调用）【✅ 已裁决：删除（10-03）→ 入 M4】

```
事实（Rider 精确检索）：按索引版——GetOrbitSlotWorldPosition 外部零调用（仅 ByClaim 实现内部自调）；
GetOrbitSlotPhase 外部 1 处（规划快照读“槽0基准相位”，承重）；GetOrbitSlotCount 规划用（承重）。
ByClaim 三件 12 处调用（任务链/渲染/抄账 ResolveSlotIndex）。
复核结论（回应用户“是否违反唯一性”）：不违反——唯一性管【状态字段】（P6 已禁双字段：
fragment 只留句柄）；查询接口只是同一权威数据的两种【寻址方式】，分工由时序决定：
认领前（无句柄）→ 索引入口（规划快照/认领动作）；认领后 → ByClaim（契约已写死于注释 L111）。
但按索引版 WorldPosition 已退化为内部工具（公开零调用）——可收窄：
· 方案甲（AI 推荐）：降为 private（或删除，ByClaim 实现内联索引解析）——公开面=规划两件+认领+ByClaim
· 方案乙：保留公开+注释显性化“索引入口=认领前专用”
【裁决确认（10-03）】用户：“没用的接口，如果没用明确以后会用到，那就删除吧”——锁定【删除】：
公开面移除 GetOrbitSlotWorldPosition（按索引）；ByClaim 实现内联索引解析或转私有辅助；
GetOrbitSlotPhase/Count（规划承重）与认领/ByClaim 面不动。入 M4（攒批）。
```

### N6 防撞修正（实测仍有碰撞——盲区复核成立）【用户发起·待启动】

```
用户反馈（10-03）：测试中发现仍会发生碰撞；怀疑“计算防撞时没有计算轨道上完整的所有卫星”。
复核结论：成立。现防撞检查域（BuildOrbitPlan L1005-1030）仅比对【漂移弧段】——
  a) 在途漂移星（State==Drift 的弧段）b) 本轮已发单弧段（函数内局部 IssuedArcs）
盲区清单：
· 【主因】不检查其它在轨星（巡航/在槽星）：前移漂移以 >ω 追越路径上的星——穿越瞬间角度重合=碰撞
  （前移优先规避了“停住被穿”，但“追尾穿越”无防护；终点间距合规≠路径安全）
· “槽间星”（漂移中断后无槽巡航）不占任何槽——间距计算与防撞均不可见
· 回轨飞行星的落点时间窗（次要）
修正方向候选（待启动时展开草案）：
· A 时间-空间重叠检查（建模各星随时间轨迹，检查最小角距——语义完整、成本高）
· B 全域弧量化（过保守——几乎处处重叠→漂移永动失效，不取）
· C 【倾向】位移限距（漂移仅调整相邻间隙、禁止跨越式重排——把“穿越”从设计上排除，
  与“消除过近”哲学一致；K=5 滚动节拍局部收敛）或 A 的简化时间窗版
启动方式：用户发起（已确认“需要继续修正”）——届时出需求/执行草案。
```

### N7 选点与规划算法专题审阅【用户发起·待启动（2026-10-06）】

```
背景（M-1 模块审阅中发现）：FindBestWorkTarget（选点评分：覆盖过滤→缺口收集→单向轴行程→
提前量→距离扣分）与 BuildOrbitPlan（规划：乙案落座→均匀化→漂移位移/防撞）——两个算法的
“导读/断点/静态审阅”均不够直观，用户要求单独开专题审阅。
已知疑点（随专题一并裁决）：
· 选点只用 Gaps[0]（最高缺口）作格子评分，而作业队列执行全量缺口——评分与收益不一致
  （“缺口多而单项低”的格子被系统性低估）——修正方向：综合评分（sum/加权），需行为变更评估。
专题形式建议：流程图 + 数值例（真实可达数据） + 几何图示；与 N6（群体决策流程图需求）**合并制作**
（同一主题域的可视化理解工具）。
```

### N8 轨道槽位去智能对象化（自建轻量槽池）【用户发起·待启动（2026-10-06）】

```
背景（M-2 模块审阅 S4）：从需求回看——轨道槽位的需求仅【随轨道移动的槽位 + 占有/释放】，
而智能对象实际使用面 = 4 API（Create/Destroy/MarkClaimed/MarkFree）+ 1 句柄类型；
空间查询/行为定义/启停/荷载/条件系统 0 使用；验证仅句柄 .IsValid() 字段检查。
当初否决自建的理由（离轨方案 §12.6“两套认领语义并存，无收益”）在三个环境变化下不再充分：
① K=5 扩容后规模 ×5（250 UObject 实例/轨）；② 位置语义认知（S3：引擎位置“撒谎”/移动成本）；
③ 功能面认知（空间查询与乙案落座不同构，永不使用——“零新写”省下的以“100% 负担承担 5% 使用面”偿付）。
评估结论：自建（轻量槽池 + 带代际结构化句柄）在架构上更契合需求；现状已是“半自建”
（位置/相位/推进全在项目侧）——重构=收回最后的状态机。
要点备忘（待启动时展开）：
· 形态：每轨相位缓存 + 状态位数组 + 代际计数；句柄=OrbitRef+Index+Generation（重建++——ISM 前车之鉴）
· 接口形状不变（TryClaim/Release/ByClaim 三件/Count/Phase/UpdateOrbitSlots）——调用方近零改动（封装红利）
· 句柄类型穿透面：Fragment.SlotClaimHandle / 任务链 / 规划器 / 渲染（机械替换，编译期全暴露）
· 边界：格子智能对象保留（零改动——其用法正当：空间注册/两域荷载/行为定义/启停）
· 收益：删除引擎依赖（4 API+250 实例/轨+微查询绕路+资产要求）/注册链 μs 级/重建链=清空+代际++/
  位置语义统一（S3 议题永久消失）/代码净减；成本：穿透替换+回归验证（测试手段齐全）
· 【重建链效率场景（10-07，M-3 S5）】用户：轨道重建时智能对象释放/重建是多层循环，效率不好——
  自建时一并解决。现状操作量（单轨）：Destroy（逐星×3 指针+≤3 ReleaseClaim+渲染收集循环）
  + RegisterSingleOrbitSlots（N×K 槽逐个销毁+N×K 逐个创建——每槽引擎 API）
  + Create（逐星 TryClaim）≈ O(N×K) UObject 操作+多次线性遍历；自建后 = 数组重置+代际++（O(1)）
· 叙事：从“统一智能对象”升级为“按需求匹配分层选型”（更强的架构能力叙事）
· 帧序定位（两问补充 10-06）：自建后槽推进【仍在 GT + 同一帧序节点】——GT 约束源于 DrawDebug
  （要求 GT）+ 组件数据纪律 + worker 读者同序——与智能对象无关；SlotSync 处理器与帧序链零改动
  （M-1 拆分红利：底层替换隔离在处理器层之下）；槽池数据从 SmartObjectManager 移出（归属方案时定）
· 【Mass 化评估（10-06，M-2 导读 S6）：不取】——引擎对照 MassCrowd（Lane 共享资源表在子系统，
  实体持句柄，trait 允许 worker）——共享资源保持中心化是引擎范式；项目现状与其完全同构。
  N8 组件自建与 Mass 化是【替代终态非先后步骤】——决策时二选一；若需彻底解线程约束，
  范式路径是“子系统化+并发安全”（Crowd 先例）而非 Mass 化（但收益仍小，不值得为此改）
启动方式：用户发起；【排期裁定（10-06）：实施放最后 = 模块审阅全部收口后、注释统一修订之前】
（理由：穿透性重构会撕碎后续模块导读/断点地图；与 N2 等结构级重构统一批次；必须在注释修订前完成）
——方案草案可任意时点先行（纯文档）；详细讨论见 M-2 导读 S4。
【执行方案已审定（10-11·D1~D6 全部）：docs/卫星系统/轨道槽位去智能对象化_执行方案.md
  （独立 UObject 类与 Manager 同文件 / FName 句柄 / 全池代际++ / 删 Priority / 槽位球 CVar；
  D6 = 方案 A：N10 → N8 → N9 → N11 锁定）；穿透面已核实：~25 调用点 / 7 文件】
【✅ 已实施（10-11·一次性单批·7 文件）】——待编译验证；
  交付形态：Step 1+2+3 合流（同 N10 纪律）；关键细化：每轨单代际（简化等价）/句柄自带 OrbitName
  查询去首参/CVar ProjectB.OrbitSlotDebugDraw（项目首个）/销毁归还格子与轨道两路分离；
  零残留：OrbitSlotDefinition 与 SmartObjectManager 轨道接口全库 0 匹配；
  细节见执行方案文末“附：实施记录”（含用户验证清单）。
```

### N9 卫星配置体系重整（配置资产化 + FOrbitDefinition 统一）【用户发起·确认做（10-07）】

```
背景（M-3 静态审阅 S1+S2；10-07 用户裁定：S1 做 / S2 统一 FOrbitDefinition / 蓝图确认无使用）：

一、S1 配置资产化【做】——现状配置散三处：
· 组件 UPROPERTY×13：卫星数量/角速度/方向/默认轨道名/Mesh/缩放/径向速度/槽倍数/规划 4 项/评分权重
· 任务侧匿名常量×7（S6 已盘：容差 1.5°/3°、时长 2s/6s、优先级、默认时长）
· 规划侧常量×4（FallbackOrbitRadius / DefaultServiceDistance / PlanSlotLeadToleranceDeg / PlanMoveMinDeg）
→ 汇总入独立【卫星配置数据资产】（与 PlanetConfig/FarmlandConfig 同模式）；命名/字段分组方案时定

二、S2 统一 FOrbitDefinition【保留该名，删 FRuntimeOrbitData】：
· 合并后 FOrbitDefinition = 几何四字段（OrbitName/Transform/Radius/ServiceDistance）
  + 运行时五字段（SatelliteCount/AngularVelocity/bClockwise/VisualColor/CoverageColor）
· 全库引用替换：FRuntimeOrbitData → FOrbitDefinition（Manager/SmartObjectManager/PC/Widget/
  OrbitCoverageVisual 等）；定义位置从 OrbitManagerComponent.h 迁出（归属方案时定）
· 退役 OrbitManagerComponent（C++ 零调用 + 蓝图经用户确认无使用）——从 PlanetActor 摘除创建
· 删 PlanetConfig::OrbitDefinitions 资产字段——BuildRuntimeOrbits 直接默认轨（用户：“放一个默认的就好”）
· CalculateOrbitAngleFromCell 签名简化（函数体实际只用 Transform——改双 Transform 直传；
  任务侧临时构造 Tasks L337-339 随之删除）

排期：与 N8 同批（结构级批次：模块审阅收口后、注释修订前）；执行方案按【执行级设计】流程先出草案。
【方案草案已出（10-07）：docs/卫星系统/卫星配置体系重整_执行方案.md（草案 v0.1——含 7 个设计决策点 D1~D7 待审）】
关联：S6（原定“随 N7”——范围已并入本议题）；S8 评分疑点仍留 N7 裁决（评分字段形态影响配置资产设计——
  方案时评估与 N7 的先后依赖）。
【范围并入（10-09 用户裁定）】M-8 收口批⑧“编辑语义合并”（删 SetOrbitData；CommitOrbitChanges 带数据
  一体化；Slate 回调适配）——与 FRuntimeOrbitData→FOrbitDefinition 改名同文件（PC/Widget）同批实施。
【D 决策点裁定（10-09）】D1~D7 全部按推荐——GlobalConfigSubsystem 挂接 / Public/Mass/OrbitTypes.h
  （唯一 FOrbitDefinition）/ 经组件 getter 读配置 / 删组件旧字段 / HoverHeight 迁入卫星配置 /
  删 IPlanetInterface::GetOrbitData；方案即可实施（窗口：N 系列重构批次）。
【✅ 编译+运行验证通过（10-12·一次性全批·16 改 + 2 新 + 2 删）】；
  实施记录见执行方案文末（关键细化：StateTreeAsset 补入资产 / 任务 getter×8 兜底 / enum class 直用）；
  零残留：FRuntimeOrbitData/OrbitManagerComponent/OrbitDefinitions 全库 0 匹配；reindex 21 文件。
【⑧编辑语义合并——已裁定取消（10-12）】目标已由 N11 统一节拍器达成（数据推送与实体重建同拍编排）；
  两接口语义分工明确化（10-12 命名改造）：数据层 UpdateOrbitData / 实体层 RebuildOrbitEntities（广义实体——
  槽位/卫星/渲染实例/选点缓存，非 Mass Entity 单指）；合并会丢失"只改数据不重建实体"能力（颜色场景实证）。
```

### N10 格子智能对象单 Slot 化（双 Slot 合并）【用户发起·确认做（10-08）】

```
背景（M-4 静态审阅四条意见 → 用户裁定）：① 双 Slot（Plot/Crop）合并做（“玩家域并发不重要”）；
② 接口同步更正（泛型 SlotIndex 退场）；③ 句柄接口收窄（GetObjectHandle 零调用/GetSlotHandle 仅内部包装）；
④ BeginPlay 空实现清理。
事实核查（M-4 审阅产出）：收获不广播（(1,false) 实为 RemoveCrop）/ 时长按具体行为类查询（不依赖双 Slot）/ 
选点已格级互斥（域并发唯一实质=卫星单域队列时玩家可操作另一域——裁定不重要）。
附带发现（待处置）：活跃农田集合口径与注释不符（只监听 Slot 1——“已耕地广播”被忽略；
集合实际=“播种过格子”，注释写“已耕地+已播种”）——N10 D1 处置。
方案草案已出（10-08）：docs/卫星系统/格子智能对象单Slot化_执行方案.md（草案 v0.1——含 D1~D6 待审）。
【裁定（10-09）】D1 = 启用语义耕地状态 + 卫星任务侧判断播种状态（CollectCellGaps 前置门卫——
无作物不产生缺口；现状核实：水肥/杂草缺口不依赖作物——空耕地会被选中的真隐患）；
D2~D6 全部按推荐（D5：N10 先于 N8）；方案即可实施。
【深化裁定（10-09）】Slot 恒启用——启停机制整体删除（Set/Is SlotEnabled + 委托 + 四广播 + 活跃集合
ActiveFarmlandCoords）；选点改全格扫描（GetAllCoords）；PC 恒走认领；卫星全部按 fragment 状态判断。
排期：与 N8/N9/N11 同批；【D5 已确认（10-09）：N10 先于 N8（同文件顺序解耦）】。
【✅ 已实施·编译验证通过（10-11）——一次性单批交付·9 文件净删 ~280 行 + 认领判定内联微调】；
  交付形态说明：Step 1~3 物理绑定（接口删参 → 调用点报错）——"每步可编译"不成立，单批交付；
  命名裁定（D3②）：ShouldClaimForEditMode / GetCellSlotHandle；零残留核验全库 0 匹配；
  细节见执行方案《格子智能对象单Slot化_执行方案.md》文末"附：实施记录"。
关联：M-4 审阅【挂起——待 N8/N10 落定后重写导读再审】。
```

### N11 轨道覆盖可视化重构（M-8 收口批残余·10-09 转入）【用户发起·确认做（10-09）】

```
背景（M-8 审阅 S3/S4/S7/S8 裁定；10-09 用户裁定“重构类项转 N 系列”）：
· ②删三缓存字段（PlanetActor/CellManager/SatelliteManager）→ 使用点按需经 GetOwner
  （全局规范“高配缓存/低频现取”首个落地）
· ④ComputeCoveredCells → BPFL 纯函数 + 混合色（多轨重叠 = 全部命中色求平均；去“列表序靠前 break”）
· ⑤RebuildCellHighlight 维持原逻辑（执行④时可选顺便结构整理——无独立改动）
· ⑥事件驱动按需重建（无节流）：HandleRuntimeOrbitChanged(OrbitName) 直接单轨处理（存在→RebuildOrbitVisual；
  不存在→移除条目+实例）；整体移除 Tick 机制（三件套/TickComponent/bPendingRebuild/TimeSinceRebuild/RebuildInterval）
  + 约定注释“调用端负责频率”（S6 随之消除）
排期：与 N8/N9/N10 同批（模块审阅已收官；注释修订前）。
执行方案已审定（10-10）：docs/卫星系统/轨道覆盖可视化重构_执行方案.md（四项均裁 + 设计细节实施时定；
  批内顺序：N9 先行——FOrbitDefinition 更名波及 BPFL 签名）。
【补充项（10-12 性能体检查出·用户裁定暂缓）】ComputeCoveredCells 全量遍历（54000 格×逐轨判定——实际资产规模）
  ——编辑拖动每次重算 ~10ms（0.1s 节流）；不影响游玩、编辑可感；
  本卡实施时【评估顺带优化】：可复用 Manager 覆盖缓存（OrbitCoverageIndices/CellScanTable——与选点治本同源），
  遍历清单合并 ~0.05ms；可与④ BPFL+混合色一并实施。
【✅ 编译+运行验证通过（10-12·全批·3+1 文件）】；
  实施细化（§九统一节拍器）：面板 RequestThrottledRebuild→RequestThrottledUpdate（到点 ①Push ②Commit）；
  SetX×10/ApplyPickedColor 删内部 Push（推送职责上移）；颜色路径挂节拍器+关窗兜底；
  补充项裁定：维持全量版迁 BPFL+混合色（节流配套后事件 ≤5Hz≈现状一半——缓存优化不做）；
  零残留：bPendingRebuild/RebuildInterval/TimeSinceRebuild 等全库 0 匹配；reindex 6 文件；实施记录见执行方案文末。
关联：M8 导读附一（S3~S8 讨论）+ 收口批清单；延后项（槽位球归拢 N8 后 / 通道分离更后 / 单轨显示增强）不在本卡。
```
