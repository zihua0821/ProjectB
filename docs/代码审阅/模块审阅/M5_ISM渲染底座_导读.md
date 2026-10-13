# M-5 导读：ISM 渲染底座（双维度分桶）

> **模块审阅 · 第 5/8 模块**　产出：2026-10-04（AI 静态导读）
> 上游：全部渲染调用方（M-2 渲染 / M-3 注册 / M-6 格子作物 / M-8 圆环）
> 本模块 = 自建渲染底座：句柄 + 桶组 + 双维度分桶 + 调试三件
> 行号基准：M2 双维度调整后的最新版本（.h 481 行 / .cpp 823 行；经 2026-10-06 审计验证锚点一致）
> 【10-07 核验】本模块文件未被近期改动触及（收口批/优化小批/S3）——全部锚点有效；
> 注释池关联：O1（UpdateBounds 注释材料——修订阶段处理）；M-2 优化小批（GT 外框+并行收集）不涉本模块。

---

## 一、文件清单与入口

| 文件 | 内容 | 阅读入口 |
|---|---|---|
| [Public/Subsystem/ISMManager.h](file:///F:/UEProjects/ProjectB/Source/ProjectB/Public/Subsystem/ISMManager.h)（481 行） | 5 结构体 + 类声明（公开接口 6 区 + 私有 3 区） | 句柄 → 参数 → 桶组 → 类头（职责+线程模型）→ 接口 |
| [Private/Subsystem/ISMManager.cpp](file:///F:/UEProjects/ProjectB/Source/ProjectB/Private/Subsystem/ISMManager.cpp)（823 行） | 组件管理 → 分桶系统 → 实例操作（增/删/改）→ 调试 | L44 起按函数序 |

**cpp 函数地图**（最新行号）：
```
L44  Initialize / L52 Deinitialize       子系统生命周期
L69  CreateISMComponent（私有）          桶组件创建（参数透传 + RF_Transient）
L94  GetBucketComponent（只读查询）      BucketIndex + GroupKey 定位
L105 ResolveEffectiveParams（私有）      ★两级表参数解析（种子继承 + 漂移告警）
L146 ConfigureSphereBuckets              球面配置注入（块大小/桶数推导）
L199 GetBucketKey（私有）                格子坐标 → 桶索引（O(1) 纯整数，热路径）
L215 AddInstance / L228 BatchAddInstances    ★添加（双维度分流）
L330 BatchRemoveInstances / L370 RemoveInstance   移除（按桶分组 → ById）
L375 BatchUpdateInstanceCustomData / L424 UpdateInstanceCustomData
L440 UpdateInstanceTransform / L455 BatchUpdateInstancesTransforms   句柄定位 → ById 增量
L478 GetOrCreateBucket                   ★懒创建 + 复活（SetHiddenInGame(false)）
L521 RegisterMeshParams / L545 GetMeshParams   网格参数注册表（单一权威）
L560 DebugPrintBucketStats / L597~ DrawBucketBounds 系列 / L655 ClearBucketBoundsDebug
L665 Tick（调试输入） / L681 SetDebugAABB
```

---

## 二、四层结构（核心认知）

```
① 句柄 FGlobalInstanceId（.h L62-86）——"实例的全局坐标"：
     GroupKey（逻辑组） + BucketIndex（空间桶） + LocalId（引擎稳定 ID，非数组下标）
     双构造（L76-80：旧 2 参版保持兼容 + 新 3 参版）；相等/Hash 三字段全参与（L83/L86）
     铁律：Remove+Add 后句柄更新；不跨操作复用

② 桶组 FISMBucketGroup（.h L149-176）——"一个 (Mesh, GroupKey) 的组件集合"：
     Buckets（懒创建·可空） / BucketInstanceCounts（★权威活动状态·PR-6） / TotalInstanceCount
     NumCustomDataFloats / bDisableBuckets / CachedParams / bParamsRegistered / bParamMismatchWarned
     BucketingThreshold（≤0=单桶）

③ 两级表（MeshToBucketGroups，.h L449）：
     主键 = FISMMeshKey(Mesh, GroupKey)——双维度分桶的实现载体
     默认组 (Mesh, 0) 条目兼任"Mesh 级参数注册载体"（种子）

④ 分桶双维度（正交）：
     空间分桶（维度一）：组内按球面网格分桶；开关=bDisableBuckets（强制关）/ 依据=GridCoords（空=不启用）
     逻辑分桶（维度二）：GroupKey 划分独立组件组（0=默认组；卫星=轨道序号+1）
     ——"键即开关"：无独立 bool
```

---

## 三、关键机制明细

### 3.1 添加路径（BatchAddInstances L228——双维度分流）
```
入参：GridCoords（空间依据，可空）+ GroupKey（逻辑依据，无默认值——编译器兜底）
流程：① 定位/创建桶组（FISMMeshKey(Mesh, GroupKey)）
      ② ResolveEffectiveParams（L105）：组未注册 → 默认组种子继承（含告警状态）；已注册 → 组内缓存为准
      ③ 空间分桶分流：GridCoords 空 → 单桶(0)；非空 → 逐实例 GetBucketKey(GridCoords[i]) 分组
      ④ 每桶一次引擎 AddInstancesById（返回物理 ID → 组装 FGlobalInstanceId）
      ⑤ 返回保序句柄数组（与 Transforms 一一对应）
```

### 3.2 空桶生命周期（PR-6）
```
Remove 归零 → SetHiddenInGame(true)（停用，组件常驻）
重新 Add → GetOrCreateBucket 无条件 SetHiddenInGame(false)（复活，幂等无窗口）
计数权威 = BucketInstanceCounts（自维护，不依赖引擎查询）
（提示：Bounds 的"重建分支吞补偿"坑——写后帧内旧值、帧末补偿有条件，见 P4 报告⑦）
```

### 3.3 分桶映射（GetBucketKey L199）
```
格子坐标 → 块行列号 → 桶线性索引： (LatIdx / LatBlockSize) * LonBlockNum + (LonIdx / LonBlockSize)
越界 clamp（确定性，实例不丢）；桶列边界对齐 0° 经线（C-7 回绕规避）；未配置 → 恒 0（单桶）
```

### 3.4 线程模型（.h L191-194——全 GT 铁律）
```
所有组件级操作仅限游戏线程（创建组件/增删实例/更新）；非 GT 时断言（不隐式派发）
原因：ISM 写接口本身游戏线程专属（无锁）；调用端自行保证（M-2 渲染处理器因此是 GT）
```

### 3.5 调试三件
```
DebugPrintBucketStats（L560，蓝图可调）：球面配置 + 逐 Mesh 桶数与 Active/Disabled + 逐桶实例数/包围盒
SetDebugAABB（L681）+ 画面列表（BuildDebugMeshEntries/OnDebugCanvasDraw L461/L467）：
   左上角 Mesh 列表（名称/实例数/桶数/组数——一轨一桶后按 Mesh 聚合），↑/↓/点击/Esc 交互
DrawBucketBoundsByPath（L603）：HSV 渐变配色（活动）/灰色（停用）+ 内缩 1cm + 按 Mesh 过滤
```

---

## 四、数据流

```
调用方（渲染/格子/作物）──BatchAddInstances(GroupKey+GridCoords)──▶ 桶组组件
更新（每帧高频）──UpdateInstanceTransform/ByBatch(句柄)──▶ 句柄自解析 → ById 增量
移除（低频）──BatchRemoveInstances──▶ 按桶分组 ById 移除 → 归零停用
读取（无）——本模块不提供实例读取（调用方持句柄；调试经 DebugPrint）
```

---

## 五、断点地图（M-5 轮建议 5 点）

> **走查模式（10-06 定，用户自主 + AI 协助）**：直接按上表行号在 Rider 设点、自主走读；高频函数用条件断点（勿裸断每帧入口）；
> 发现/疑问同步 AI（口头结论优先）；行号若已漂移先让 AI 重校。

| # | 断点（文件:行） | 触及时机 | 观察到什么 | 重点变量 | 线程 |
|---|---|---|---|---|---|
| 1 | ISMManager.cpp:228（BatchAddInstances） | 轨道重建/作物播种 | **双维度分流**（GridCoords 空 + GroupKey 值） | GroupKey / GridCoords.Num() / 分流结果 | GT |
| 2 | ISMManager.cpp:105（ResolveEffectiveParams） | 新桶组首次创建 | 两级表种子继承（默认组） | 种子来源 / bParamMismatchWarned | GT |
| 3 | ISMManager.cpp:199（GetBucketKey） | 空间分桶时（格子域） | 坐标→桶索引映射（clamp 分支） | GridCoords / 返回桶号 | GT |
| 4 | ISMManager.cpp:440（UpdateInstanceTransform） | 渲染每帧（条件断点） | 句柄三字段自解析 | GroupKey/BucketIndex/LocalId | GT |
| 5 | ISMManager.cpp:478（GetOrCreateBucket） | 首桶创建/复活 | 懒创建与 SetHiddenInGame(false) | 新组件指针 / 复活标志 | GT |

---

## 六、自查清单（对照代码走读勾选）

```
□ 1. 句柄三字段默画 + 双构造的兼容关系（L76-80——谁还在用 2 参版）
□ 2. 桶组 9 字段各用途（重点：为什么 BucketInstanceCounts 是权威而不查引擎）
□ 3. 双维度分流代码段（L228 内：GridCoords 判定 + GroupKey 定位）与"正交矩阵"四组合对应
□ 4. 两级表种子继承全路径（L105 + L478 联动——默认组 (Mesh,0) 载体的作用）
□ 5. 空桶"停用-复活"链路（Remove 归零 → Add 复活；PR-6 幂等）
□ 6. 全 GT 线程模型的依据（对照 M-2 渲染处理器为何设 GT——同源约定）
□ 7. ById 增量 API 使用面（对比引擎非 ById 版；批量接口为何无 ById 版仍逐实例调用——注释 L357-358）
□ 8. GetBucketKey 公式 + clamp 行为（越界不丢实例的代价-收益）
```

---

## 七、走查记录

> 【10-09 定】断点走查未进行——本模块以“仅看变动”模式审阅（成熟底座：技术文档/Review 清单已覆盖）；
> 变动对照结论见下方附节。

---

## 附：本次变动审阅（10-08·仅看改动——git diff 依据：b35f3ef）

> 定位（用户 10-08 裁定）：ISM 管理器为成熟底座（既有技术文档/Review 清单已覆盖全量）——
> 本次（M2 双维度分桶·已提交 b35f3ef）为【修补性扩展】：**审阅只逐处对照下列改动，不必全量走读**。
> 变动规模：.h +77 行 / .cpp +285 −141 行（下文按文件与主题分组；代码行号=当前磁盘）。

### A. 结构体与句柄（.h L20-86）

| # | 改动 | 位置 | 审查点 |
|---|---|---|---|
| A1 | `FISMMeshKey` +`GroupKey` 字段；双构造加 `InGroupKey = 0` 默认参；`operator==` 加 GroupKey 比较；`GetTypeHash` 改 HashCombine 两字段 | L36/43-44/48/51 | Key 作 TMap 主键——**相等与 Hash 必须同步含 GroupKey**（否则不同组碰撞）——已含 ✓ |
| A2 | `FGlobalInstanceId` +`GroupKey` 字段（首个成员）；+**三参构造**（旧两参版保留兼容）；`operator==` 三字段；`GetTypeHash` 三层 HashCombine | L67/76-88 | 句柄相等/Hash 三字段全参与 ✓；旧两参构造的默认 GroupKey=0（存量代码不破坏） |

### B. 接口签名（.h）

| # | 改动 | 位置 | 审查点 |
|---|---|---|---|
| B1 | `GetOrCreateBucket` / `GetBucketComponent` 尾参 `const int32 GroupKey = 0`（默认值——旧调用零改） | L239/246 | 默认值语义：0=默认组（与旧行为等价） |
| B2 | `AddInstance` / `BatchAddInstances` 的 GroupKey 位于 GridCoords 之后、**无默认值** | L272-287/308-317 | 有意设计：漏改调用 = 编译报错（不静默错位）——调用点已 6 处补参 |
| B3 | `ResolveEffectiveParams` 声明迁入私有成员（原 .cpp 匿名函数） | L427-433 | 成为成员原因：需访问 MeshToBucketGroups（两级表种子查询） |
| B4 | `FISMDebugEntry` +`Groups` 字段（一轨一桶后按 Mesh 聚合） | L452-460 | 调试列表行 `groups=%d` 数据源 |

### C. 参数解析——两级表种子（.cpp L105-140·核心新逻辑）

```
ResolveEffectiveParams（组未注册时四分支）：
① 先查默认组 (Mesh,0) 注册条目 → 已注册则【以其为种子】定格组参数，
   并继承 bParamMismatchWarned（L116——避免同源漂移跨组重复告警）
② 无种子 + 传入非空 → 传入值首次定格（兼容旧调用）
③ 无种子 + 传入空 → 返回默认（调用方判空）
④ 组已注册 → 组缓存为准；传入不一致 → 每网格一次性告警（暴露漂移）
审查点：种子继承在 GetOrCreateBucket/BatchAdd 【之前】执行（组首次触碰时插队）——路径无遗漏？
```

### D. 批量操作分发（.cpp·句柄自解析）

| # | 改动 | 位置 | 审查点 |
|---|---|---|---|
| D1 | AddInstance 透传 GroupKey | L215-226 | 入口一致性 |
| D2 | BatchAddInstances：`Key(Mesh, GroupKey)`；单桶路径 `Emplace(GroupKey, 0, Id)`；空间分桶路径 `FGlobalInstanceId(GroupKey, BucketIdx, Id)`；GetOrCreateBucket 两处传 GroupKey | L241/265/270/298/304 | 两条路径的句柄组装均含 GroupKey（保序数组） |
| D3 | BatchRemoveInstances：分组从“按桶”→**【按组→桶】双层**（TMap<int32, TMap<int32,…>>）；每外层组独立计数对齐/归零停用 | L336-365 | 句柄自解析 GroupKey；**跨组句柄数组亦正确**（全轨汇总销毁场景）；组不存在跳过 |
| D4 | BatchUpdateInstanceCustomData：同款双层分组；FloatsPerInstance 取首个有效句柄所在组 | L375-420 | 同组约束依赖“同 Mesh 各组 NumCustomDataFloats 一致”（注释声明） |
| D5 | UpdateInstanceCustomData / UpdateInstanceTransform：`Key(Mesh, InstanceId.GroupKey)` | L429/445 | 单实例路径句柄自解析 |
| D6 | BatchUpdateInstancesTransforms：去头部单组查询→逐实例按 `Gid.GroupKey` 查组 | L464-467 | 跨组数组正确；每实例一次 Find（热路径——规模/性能权衡点） |

### E. 桶管理（.cpp L478）

```
GetOrCreateBucket：Key(Mesh, GroupKey) + 尾参透传——组内桶创建路径统一
```

### F. 调试链路（.cpp）

| # | 改动 | 位置 | 审查点 |
|---|---|---|---|
| F1 | RegisterMeshParams / GetMeshParams 注释补“默认组 (Mesh,0)=Mesh 级注册载体”（**实现不变**——Key(Mesh) 默认 0） | L524/548 | 注释与两级表机制对齐 |
| F2 | DebugPrintBucketStats：日志行加 `(group=%d)` | L576 | 一轨一桶后逐组可读 |
| F3 | DrawBucketBoundsByPath：单组 Find → **同 MeshPath 收集全部组**遍历绘制 | L604-655 | 多组场景线框覆盖完整 |
| F4 | BuildDebugMeshEntries：**按 Mesh 聚合**（实例/桶求和 + Groups 计数）；OnDebugCanvasDraw 行加 `groups=%d` | L712-731/806 | 列表语义：一行=一 Mesh（组数单列） |

### G. 项目侧调用点（6 处补参·非本文件改动）

```
CellManager（地块）/ CellVisualSync（轮廓）/ CropVisualSync（作物）/ OrbitCoverageVisual（圆环）/
FarmlandActor（验证实验台×2）——GridCoords 后补 /*GroupKey=*/0（M2 执行记录已录）
唯一非 0 调用：SatelliteManager::RegisterRenderInstances（一轨一桶：GroupKey=轨道序号+1）
```

### 变动审阅自查（勾选）

```
□ 1. 两句柄结构的 == / Hash 与字段完整性对应（A1/A2）——为何必须三字段全参与
□ 2. 两级表种子继承的触发时机与四分支覆盖（C）——告警状态为何继承（L116）
□ 3. 单桶/空间分桶两路径的句柄组装一致性（D2）
□ 4. 移除/更新双层分组的句柄自解析正确性（D3/D4/D6）——跨组数组场景
□ 5. Add 接口无默认值 vs Bucket 接口有默认值的设计差异（B1/B2——编译器兜底 vs 兼容）
□ 6. 调试链路的聚合口径（F3/F4——一行=一 Mesh + groups 列）
```
