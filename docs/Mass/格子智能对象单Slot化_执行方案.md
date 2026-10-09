# 格子智能对象单 Slot 化（N10）执行方案（草案 v0.1）

> 依据：M-4 静态审阅（2026-10-08 用户裁定）——"卫星在服务某格时玩家仍能操作另一域不重要，双 Slot 合并做"
> 状态：**草案——待用户审改**；敲定后实施（窗口与 N8/N9 同批 = 模块审阅收口后、注释修订前）
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
  · IsSlotClaimed(Coords) / IsClaimValid(Coords, Handle) / IsSlotEnabled(Coords)（去 SlotIndex）
  · 删除：GetSlotHandle（泛型重复）/ GetObjectHandle（零调用）/ GetSlotIndexForEditMode（见 D3）
  · 保留单句柄入口：GetCellSlotHandle（合并后"两个明确"自然收敛为一个）
  · 账本：单 TMap；启停：单 SetSlotEnabled；处理：委托改造见 3.3
3.3 委托与启用语义：
  · CellManager 四广播点 → 两口：PlowLand (enable) / RemoveLand (disable)
    播种·收获·移除作物不再控制启用（见 D2）
  · 启用语义 = 耕地状态（见 D1——行为变化：空耕地进入活跃集合）
3.4 任务侧（回退 M-1 收口批的两域句柄——结构更简单）：
  · FMassSatelliteTaskFragment：PlotClaimHandle/CropClaimHandle → 单 CellClaimHandle
  · 认领：ClaimWorkTarget 单次认领（去"队列涉及域循环"）；ResolveDomainHandle/域常量删除
  · 所有权检查：单句柄 IsClaimValid（IsOwnershipValid 简化）
  · BeginQueueItem：直接传单句柄
3.5 PC：ExecuteEditModeWithClaim 单认领；GetSlotIndexForEditMode 处置见 D3
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

## 五、分步实施计划（敲定后执行；每步可编译）

```
Step 1 管理器接口单域化：删泛型句柄接口/GetObjectHandle；去 SlotIndex 参数；BeginPlay 清理
Step 2 语义改造：单账本 / 单启用 / 委托两口处理 / GetSlotIndexForEditMode 处置（D3）
Step 3 任务链 + PC 单句柄化（编译期兜底：两域句柄字段删除 → 全部引用报错逐个收敛）
Step 4 资产重建（用户）：DA_FarmCell 单 Slot 挂 7 行为 → 编译 + 运行验证（种植→水肥→收获→重播全链）
```

## 六、影响面清单

```
代码（~6 文件）：
  SmartObjectManagerComponent.h/.cpp（主体：接口/账本/启用/委托/BeginPlay）
  CellManagerComponent.cpp（广播两点化 + 前置防护核对）
  SatelliteStateTreeTasks.cpp（单句柄 + 认领简化 + 域常量删 + BeginQueueItem）
  SatelliteMassFragments.h（Task fragment 两柄→单柄）
  BasePlayerController.cpp（单认领 + 映射函数处置）
  （SatelliteManagerComponent 的销毁归还链：两域释放→单释放）
资产（用户操作）：DA_FarmCell 单 Slot 重建
文档：M-4 导读（重构后重写再审）/ 主清单 N10 卡回填 / 交接文档
```

## 七、验收

```
· 编译通过（用户验证）
· 种植→水肥→除草虫→收获→重播全链无回归；玩家操作（含抢占拒绝）正常；卫星作业链正常
· 全库零残留：GetSlotHandle / GetObjectHandle / Plot·Crop 双域句柄字段 / GetSlotIndexForEditMode（若 D3③）
· 活跃集合行为按 D1 裁定（空耕地是否接水肥作业——重点观察项）
```

---

## 附：M-4 审阅挂起说明（用户裁定 10-08）

```
格子侧（本方案）与轨道侧（N8）均将结构性重构——当前代码形态不是终态，
M-4 审阅【挂起】：不再审当前代码；待 N8/N10 落定后【重写 M-4 导读】再审一轮。
（M-4 已收获的分析不浪费：本方案 §二 事实核查 + 差异清单即 M-4 审阅产出。）
```
