# 格子智能对象单 Slot 化（N10）执行方案（草案 v0.1）

> 依据：M-4 静态审阅（2026-10-08 用户裁定）——"卫星在服务某格时玩家仍能操作另一域不重要，双 Slot 合并做"
> 状态：**✅ 已实施·编译验证通过（10-11）**（一次性单批交付·9 文件 + 认领判定内联微调）；实施记录见文末
> 关联：N8（轨道槽位自建——轨道侧替换）/ N9（卫星配置体系重整）/ N2（活跃农田集合 Observer 化——见 D4）/ M-4 审阅（挂起——本方案落定后重审）

---

## 一、目标

```
格子智能对象：【双 Slot（Plot=0 / Crop=1）】→【单 Slot】——一实例一作业席位
覆盖范围（M-4 审阅四条意见一并落地）：
  ① 双 Slot 合并（主体）
  ② 接口"同步更正"——泛型 SlotIndex 退场（合并后天然单域）
  ③ 句柄获取接口收窄（删 GetSlotHandle 泛型 + GetObjectHandle 零调用）
  ④ BeginPlay 空实现清理
```

## 二、现状事实（本轮核查取证）

```
【双 Slot 现状】DA_FarmCell Slot[0]=Plot / Slot[1]=Crop；管理器双账本 ClaimLedger[2]；
  双启用（SetPlot/CropSlotEnabled）；委托带 SlotIndex（四广播点）；任务侧两域句柄（M-1 收口批）
【广播四点实际语义（核实修正）】PlowLand (0,true) / RemoveLand (0,false) / PlantCrop (1,true)
  / RemoveCrop (1,false)——收获【不】广播（Crop 收获后保持启用；(1,false) 实为 RemoveCrop）
【时长机制（核实修正）】ReadActionDuration 按【具体行为类】查询（GetBehaviorDefinition<具体类>）
  ——不依赖双 Slot；单 Slot 挂 7 类同样工作（合并无障碍）
【域并发边界（核实）】选点层已是格级互斥（任一域被认领→整格跳过）；卫星认领=队列涉及域全认；
  域分离唯一实质：卫星【单域队列】时玩家可操作另一域——用户裁定【不重要】→ 合并成立
【差异清单裁决】启用位细分（弱——fragment 才是权威，可选冗余保险）/ 行为载体（不成立）/
  卫星认领对齐（不成立——认领按涉及域发起，单域时只认一个）——均不构成保留理由
【句柄接口调用面（三查）】GetObjectHandle 零调用 / GetSlotHandle 仅内部包装 /
  GetPlot·CropSlotHandle 仅内部（SetPlot·CropSlotEnabled 用）——无外部消费者
【附带发现（编号待定·审阅记录）】活跃农田集合口径与注释不符：卫星侧只监听 Slot 1，
  "已耕地广播"被忽略——集合实际 = "播种过的格子"（注释写"已耕地+已播种"）
```

## 三、目标形态

```
3.1 资产：DA_FarmCell 单 Slot 挂全部 7 行为类（Irrigation/Fertilize/Weeding/Pest/Plow/Harvest/Plant）
    ——编辑模式→时长的映射代码【零改动】（按具体类查询，不涉 Slot）
3.2 管理器接口（单域化）：
  · TryClaimSlot(Coords, Priority) / ReleaseClaim(Handle)（不变）
  · IsSlotClaimed(Coords) / IsClaimValid(Coords, Handle)（去 SlotIndex）
  · 删除：GetSlotHandle（泛型重复）/ GetObjectHandle（零调用）/ GetSlotIndexForEditMode（见 D3）
  · 保留单句柄入口：GetCellSlotHandle（合并后“两个明确”自然收敛为一个）
  · 账本：单 TMap
  · 【10-09 深化裁定：Slot 恒启用——启停机制整体删除】
    删：SetPlotSlotEnabled / SetCropSlotEnabled / IsSlotEnabled / HandleCellSlotStateChanged（订阅+处理）
    + 注册时“默认禁用”两行（RegisterCell L146-148）——Slot 保持注册启用态（恒可用）
    依据：卫星任务全程按 fragment 状态判断（CollectCellGaps 直读），不依赖 Slot 启停
3.3 委托与启用语义（【10-09 深化裁定：整个机制删除】）：
  · CellManager：OnCellSlotStateChanged 委托 + 四广播点（PlowLand/RemoveLand/PlantCrop/RemoveCrop）全删
  · SmartObjectManager / SatelliteManager 两侧订阅与处理函数全删
  · 活跃农田集合 ActiveFarmlandCoords 删除（其维护源即 Slot 1 广播——无启停即无维护）
  · 卫星任务侧判断（D1 裁定补充）：CollectCellGaps 加前置门卫——无作物（CropState==nullptr）不产生缺口
    → 卫星只服务【播种状态】的格子（选点/认领/队列三处经同源函数自动生效）
    —— 语义收敛：原“引擎启用门卫 + 缺口”双门卫 → 单门卫（任务侧状态判断）
3.4 任务侧（回退 M-1 收口批的两域句柄——结构更简单）：
  · FMassSatelliteTaskFragment：PlotClaimHandle/CropClaimHandle → 单 CellClaimHandle
  · 认领：ClaimWorkTarget 单次认领（去"队列涉及域循环"）；ResolveDomainHandle/域常量删除
  · 所有权检查：单句柄 IsClaimValid（IsOwnershipValid 简化）
  · BeginQueueItem：直接传单句柄
3.5 PC：ExecuteEditModeWithClaim 单认领 + 恒走认领协议（删 IsSlotEnabled 守卫分支——见 3.7）；
    GetSlotIndexForEditMode 处置见 D3

3.6 选点改造（集合删除连带）：
  · FindBestWorkTarget：遍历 ActiveFarmlands → 改为遍历格子全集（CellManager::GetAllCoords——注册链同款接口）；
    覆盖带硬过滤保留（多数格前滤淘汰）
  · 消费面核实：GetActiveFarmlandCoords 仅选点一处（Tasks L314）——影响封闭
  · 性能备注：N 星 × 432 格 / 0.2s（选点节流）——过滤为纯数学（便宜）；实测不达标时
    由 N2（重述为“选点扫描性能优化·条件触发”）引入缓存

3.7 PC 认领守卫重写：
  · ExecuteEditModeWithClaim：删“IsSlotEnabled→直执行”分支——恒走认领协议
    （TryClaim 失败 = 被占用 → 拒绝；抢占拒绝语义保留）
```

## 四、设计决策点（待用户裁决）

| # | 决策点 | 选项 | AI 推荐 |
|---|---|---|---|
| D1 | 启用语义与活跃集合 | ① 启用=耕地状态（耕地 enable/RemoveLand disable）——集合=已耕地格子（行为变化：空耕地入集合，将接受水肥作业）② 保持现状等价（集合="播种过"——需旁路机制维护，与"单启用"不自洽） | **①**（单状态位贯穿；空耕地接水肥作业需您确认是否符合游戏设计——若不可，则选②并接受额外维护逻辑） |
| D2 | 播种/收获/移作物的广播去留 | ① 全部移除（启用只看耕地）② 保留转发（恒单域——无信息量） | **①**（接口最小化） |
| D3 | GetSlotIndexForEditMode 去向 | ① 保留改语义（"操作是否需要认领"判定——None/Count=-1，其余=0）② 改名重构为 bool 判定（ShouldClaimForEditMode）③ 删除（PC 内联判定） | **②**（语义直白；命名实施时定） |
| D4 | 与 N2（集合 Observer 化）的边界 | ① N10 落地过渡语义（D1①），N2 后续重写集合维护 ② 与 N2 合并设计 | **①**（解耦推进；N2 重写时不受 N10 约束） |
| D5 | 与 N8 的实施顺序 | ① N10 先（格子侧，独立于轨道侧）→ N8 ② N8 先 ③ 同批交叉 | **①**（均为 SmartObjectManager 文件改动——N10 先落定格子侧接口形态，N8 再动轨道侧，避免同文件交叉编写） |
| D6 | 实施窗口 | 与 N8/N9 同批（模块审阅收口后、注释修订前） | 确认（理由同 N8/N9：穿透性重构不撕导读；赶在注释修订前） |

**【10-09 用户裁定：D1 = 启用语义耕地状态 + 卫星任务侧判断播种状态（CollectCellGaps 前置门卫——无作物不产生缺口）；D2~D6 全部按推荐】（D3 ShouldClaim 改名 / D4 与 N2 解耦 / D5 N10 先于 N8 / D6 同批）**

**【10-09 深化裁定：Slot 恒启用——启停机制整体删除（比 D1/D2 原形态更彻底）：无启停/无委托/无活跃集合；卫星全部按 fragment 状态判断（任务侧门卫）；选点改全格扫描；PC 恒走认领】（见 3.2/3.3/3.6/3.7）**

## 五、分步实施计划（敲定后执行；每步可编译）

```
Step 1 管理器接口单域化：删泛型句柄接口/GetObjectHandle；去 SlotIndex 参数；BeginPlay 清理
Step 2 语义改造：单账本 / 单启用 / 委托两口处理 / GetSlotIndexForEditMode 处置（D3）
Step 3 任务链 + PC 单句柄化（编译期兜底：两域句柄字段删除 → 全部引用报错逐个收敛）
       + CollectCellGaps 前置门卫（D1 补充：无作物 → 无缺口——卫星只服务播种状态）
Step 4 资产重建（用户）：DA_FarmCell 单 Slot 挂 7 行为 → 编译 + 运行验证（种植→水肥→收获→重播全链）
```

## 六、影响面清单

```
代码（~7 文件）：
  SmartObjectManagerComponent.h/.cpp（主体：接口/账本/启停三件删/委托删/BeginPlay）
  CellManagerComponent.h/.cpp（委托声明 + 四广播点全删）
  SatelliteManagerComponent.h/.cpp（活跃集合删 + 订阅/处理删 + 销毁归还链两域→单）
  SatelliteStateTreeTasks.cpp（单句柄 + 认领简化 + 域常量删 + CollectCellGaps 门卫 + 选点全扫）
  SatelliteMassFragments.h（Task fragment 两柄→单柄）
  BasePlayerController.cpp（单认领 + 恒认领守卫 + 映射函数处置）
资产（用户操作）：DA_FarmCell 单 Slot 重建
文档：M-4 导读（重构后重写再审）/ 主清单 N10 卡回填 / 交接文档
```

## 七、验收

```
· 编译通过（用户验证）
· 种植→水肥→除草虫→收获→重播全链无回归；玩家操作（含抢占拒绝）正常；卫星作业链正常
· 全库零残留：GetSlotHandle / GetObjectHandle / Plot·Crop 双域句柄字段 / GetSlotIndexForEditMode（若 D3③）
· 卫星只服务播种状态（空耕地零候选——重点观察项）
· 启停链全删：全库零残留（SetPlot·CropSlotEnabled / IsSlotEnabled / OnCellSlotStateChanged / ActiveFarmlandCoords）
· 选点扫描性能（全格遍历）——运行观察项（不达标 → N2 缓存）
```

---

## 附：M-4 审阅挂起说明（用户裁定 10-08）

```
格子侧（本方案）与轨道侧（N8）均将结构性重构——当前代码形态不是终态，
M-4 审阅【挂起】：不再审当前代码；待 N8/N10 落定后【重写 M-4 导读】再审一轮。
（M-4 已收获的分析不浪费：本方案 §二 事实核查 + 差异清单即 M-4 审阅产出。）
```

---

## 附：实施记录（10-11）

```
【交付形态】一次性单批（Step 1~3 合流交付）——节奏修正说明：接口删参与调用点物理绑定
  （删 SlotIndex 参数 → 调用点当场报错），"每步可编译"在形态切换类改造中不成立
  （除非先加新保旧做临时兼容——双形态共存无验证价值）；Step 4 资产重建仍为独立用户步骤
【改动面】9 文件（净删 ~280 行）：
  SmartObjectManagerComponent.h/.cpp —— 接口单域化（GetCellSlotHandle 单入口/去 SlotIndex 参）
    + 单账本 TMap + 启停链删 + BeginPlay 清理 + GetSlotIndexForEditMode→ShouldClaimForEditMode
  CellManagerComponent.h/.cpp —— 委托声明 + 四广播点全删
  SatelliteManagerComponent.h/.cpp —— 活跃集合删 + 订阅/处理删 + 销毁归还链单句柄
  SatelliteMassFragments.h —— 两域句柄 → CellClaimHandle（单字段）
  SatelliteStateTreeTasks.cpp —— 域常量/ResolveDomainHandle 删 + 单句柄全链 + CollectCellGaps 门卫
    + 认领单次化 + 选点全扫（GetAllCoords）
  BasePlayerController.cpp —— 恒走认领协议 + 认领判定内联（终态：EditMode == None/Count——含 10-11 微调）
【命名裁定（D3②）】ShouldClaimForEditMode（bool 判定）/ GetCellSlotHandle（单句柄入口）
【10-11 微调·用户裁定“冗余”】ShouldClaimForEditMode 删除——单 Slot 化后职责缩水至“枚举有效性”
  （等价 EditMode != None/Count），且管理器不应依赖编辑枚举；判定内联 PC
  （EditMode == None/Count 直执行——拦截 None 防“空点击误抢占卫星”→任务误取消）；
  管理器与 EEditMode 彻底解耦（.h/.cpp 删 Crops/CropData.h include）——零残留核验通过
【零残留核验（交付前）】被删符号全库 0 匹配：GetSlotHandle / GetObjectHandle / Plot·CropClaimHandle /
  GetSlotIndexForEditMode / SetPlot·CropSlotEnabled / IsSlotEnabled / OnCellSlotStateChanged /
  ActiveFarmlandCoords / CaptureSlotHandles / HandleCellSlotStateChanged
【reindex】涉及文件逐文件即时刷新 + 连带引用者补刷（共 17 文件）
【待办（用户）】编译 → DA_FarmCell 单 Slot 重建（7 行为类挂 Slot 0）→ 浅测：
  种植全链（种植→水肥→除草虫→收获→重播）/ 空耕地零候选观察 / 玩家抢占拒绝
```

---

## 附：选点性能治本（10-11·运行期回归修复）

```
【现象（用户实测）】N10 后运行：卫星数增加时明细掉帧、帧数不稳
  （1 星无感 / 编辑改 10 星明细；槽位球与轨迹绘制均未开）
【根因（代码级三层）】
  ① N10 全格扫描：GetAllCoords 返回【全部格子】——代码默认 (NumLat13−1)×NumLon36=432，
    但实际蓝图资产配置 = 54000 格（用户澄清）——单次扫描 ~5-20ms（非此前估算的 432 量级）
  ② 选点节流 0.2s，但【全星同帧同相】：同批创建→同帧初始化节拍→每个到点时刻落在同一帧
  ③ → 每 0.2s 一帧内 N 星串行全扫（10 星 ≈ 数十 ms 尖峰）→ 周期性尖峰（帧不稳）
【修复·治本 B（覆盖格清单缓存）】
  · Manager 新增：FCellScanEntry 扫描表（坐标+格心位置；Initialize 一次性 O(总格数)）
    + OrbitCoverageIndices（每轨覆盖带索引：Initialize/轨道重建时重算、删轨清除）
  · FindBestWorkTarget 改【遍历覆盖清单】（百格量级）——删除全格前滤段与每扫描 GetAllCoords 拷贝
  · 前置门卫提前（无作物在 Timer 读/缺口收集之前直接跳过）
  · 单次扫描 ~5-20ms → ~0.2ms（~25-100×）
【修复·附加 A（节拍错峰）】SelectTarget/Drift 首次节流初始化按实体序号错相（20 槽）
  ——全星尖峰摊平为每帧单星（一行级）
【验证（用户）】同场景复跑（1→10 星复现步骤）——【✅ 实测通过（10-12：20-30 星帧数无明显下降）】
  启动日志新增“[SatelliteManager] Cell scan table built: N cells”（N=实际格数）
【体检验收（10-12）】全库同模式扫描（全量遍历/热路径重算/规模失真）额外发现并修复：
  · CellVisualSyncProcessor：每帧 GT 串行全扫全格（54000）——改为【并行两段式】
    （worker 并行收集+局部缓冲锁合并+GT 批提交——样板 CropVisualSyncProcessor）；
    同步补强线程纪律：GT 预提取 FCellOutlineCalcParams 快照（两计算函数不再在 worker 访问 UObject）
  · 其余核实无隐患（Crop/Plot 查询过滤+并行 ✓；旧组件死代码 ✓）；
    CropGrowth 满播种串行—观察项；SmartObject 分批注册—启动观察项；
    ComputeCoveredCells 全量×轨（编辑热路径）——【暂缓·已记录（10-12 用户裁定：影响不大不做修复；
    已挂 N11 卡补充项——将来实施时评估顺带优化：可复用 Manager 覆盖缓存）】
```
