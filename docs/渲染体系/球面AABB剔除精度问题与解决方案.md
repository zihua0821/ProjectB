# 球面分桶 AABB 剔除精度问题与解决方案

**版本**: v1.1
**更新日期**: 2026-08-21
**分支**: MassCrop
**关联**: 《ISMManager技术文档.md》（分桶机制 v2.0+）、《ISMManager空间分桶重构方案.md》

---

## 一、问题描述

引擎渲染剔除（视锥剔除、距离剔除、遮挡剔除、LOD 选择）**只支持 AABB + Sphere**（`FConvexVolume::IntersectBox`、`FPrimitiveBounds`、`FOcclusionFeedback::AddPrimitive(BoundsOrigin, BoundsBoxExtent)` 入参均为 AABB）。而球面分桶的桶是**球面弯曲的四边形**，用 AABB 包裹必然产生膨胀——桶比实际大，剔除判定变保守：**相机看不到的桶提前进入视锥，多画、多投影阴影**。

## 二、膨胀的三来源（量化公式）

| 来源 | 成因 | 公式 | 能否消除 |
|---|---|---|---|
| ① 切向跨度 | 桶横跨地表距离，任何盒体都要包住 | ≈ R·α | ❌ 真实分布 |
| ② 径向弧高 | 球面弯曲使桶中部高出弦线 | R·(1-cos(α/2)) ≈ R·α²/8 | ✅ 只有减小角跨度 α（α 减半 → 弧高降 1/4） |
| ③ AABB 方向错位 | 非赤道桶的径向/切向混合投影到世界轴 | 盒体放大 1.3~2 倍（实测 1.87 倍） | ✅ OBB 法线对齐可消除 |

**核心结论**：膨胀大头是 ③（方向错位），其次 ②（弧高，随 α 平方增长）。① 是任何方案都要付的真实代价。

## 三、引擎剔除路径与 AABB 可替换性（已查证）

| 剔除环节 | 判定数据 | 手动 OBB 能否替换 |
|---|---|---|
| 视锥剔除（主 pass + 阴影 pass，`FrustumCull`/SceneVisibility.cpp） | AABB + Sphere | ✅ `SetHiddenInGame` 跳过收集 |
| 距离剔除（SceneVisibility.cpp L826-870） | AABB Origin | ✅ 自算桶中心距离更准 |
| **遮挡剔除**（SceneOcclusion.cpp，HZB/GPU query） | **AABB**（`AddPrimitive` 入参） | ❌ **死区**：视锥内的桶仍按 AABB 判定 |
| **LOD 距离选择**（渲染线程） | **AABB Origin** | ❌ 死区（影响小） |
| **渲染侧 Bounds 数据**（`Scene.PrimitiveBounds[]`） | 恒 AABB | ❌ 只能绕（隐藏）不能改 |

`SetHiddenInGame` 消费链（组件→渲染）：`SetHiddenInGame` (SceneComponent.cpp L3301) → `OnHiddenInGameChanged` → `MarkRenderStateDirty` → 代理重建 `bDrawInGame(Proxy->IsDrawnInGame())` (PrimitiveSceneInfo.cpp L310) → RayTracing.cpp L942 / RendererScene.cpp L5095 跳过收集 draw command。

## 四、影响评估（真实配置校准）

**实际蓝图配置**：Radius = 50000cm（500m）、NumLat = 121、NumLon = 450（代码默认值 8000/13/36 已被蓝图覆盖，评估必须以蓝图为准）。

**分桶推导**（S = INSTANCES_PER_BUCKET = 2048）：fLat = 23 格（23°）、fLon = 88 格（70.4°），桶 = 6×6 = **36 桶**。

| 量 | 数值 |
|---|---|
| 桶地表尺寸 | 经向 614m × 纬向 200m（赤道） |
| 弧高（经度向 70.4°） | 91m |
| 桶弦半（经度向） | 288m |
| AABB 方向错位 | 切向 + 径向投影叠加 → 1.3~2 倍 |
| 总实例（密铺） | 54k |

**影响程度结论：当前影响低（可接受）**，理由：

1. 桶数仅 36，膨胀只影响边界桶的提前进入，绝对值小
2. 最坏情况（高空全可见）54k 实例 = ISM 单次 draw call，GPU ~1-2ms（含阴影 ~2-4ms），非 60fps 杀手
3. 贴地视角（相机 2m，地平线仅 45m）精确可见 1-2 桶，膨胀后 2-4 桶——超量 ~2 倍但绝对值仅 1.5k-6k 实例
4. 项目已知瓶颈在 Mass 并行更新 + Debug 下 ISM Validate 全量遍历（CPU 侧），**不在渲染剔除侧**

**影响升级条件**（届时需重新评估）：实例规模 ×4 以上；更新瓶颈消除后渲染成为主要成本；INSTANCES_PER_BUCKET 调小导致桶数上百；上 Nanite/重材质。

## 五、方案 A：手动 OBB 剔除（重方案，待命）

**状态**：设计完成（v1），**未实施**，当前取舍下不采用，条件满足时升级。

- 游戏线程 `FTickableGameObject` 实现手动视锥判定，每桶法线对齐 OBB + SAT 与相机 6 平面测试，视锥外 `SetHiddenInGame(true)`
- OBB 由球面配置 O(1) 公式推导（桶索引 → 中心/三轴/半尺寸），零存储零遍历
- 10Hz Tick + 0.3s hysteresis 状态机防闪烁；跳过空桶（与 PR-6 停用机制兼容）
- 成本：~200 行 + Tick/状态机/线程考量；覆盖视锥 + 距离 + 阴影；遮挡死区无法覆盖
- 文件改动：ISMManager.h/cpp + CellManagerComponent 注入

## 六、方案 B：角跨度约束（轻方案，推荐实施）

**状态**：设计定稿（v1），**待实施**。

### 6.1 设计

`FISMSphereBucketConfig` 新增两字段（≤0 = 不约束，平面测试台不传字段行为完全不变）：

```cpp
float TotalLatSpanDeg = 0.f;         // 纬度总跨度 = MaxLat - MinLat（球面场景注入 120）
float MaxBucketAngularSpanDeg = 0.f; // 桶最大角跨度（度），建议 30
```

`ConfigureSphereBuckets` 面积校验后追加 min 约束段（经度恒按 360° 计算）：

```
DegPerLatCell = TotalLatSpanDeg / LatCells   // = 120/120 = 1°/格
DegPerLonCell = 360.0f / LonCells            // = 360/450 = 0.8°/格
LatBlockSize = min(LatBlockSize, MaxBucketAngularSpanDeg / DegPerLatCell)
LonBlockSize = min(LonBlockSize, MaxBucketAngularSpanDeg / DegPerLonCell)
```

CellManagerComponent 注入处 +2 行：`TotalLatSpanDeg = PlanetConfig->MaxLat - PlanetConfig->MinLat;`、`MaxBucketAngularSpanDeg = 30.0f;`

### 6.2 真实配置效果（R=500m、121×450）

| 档位 | 块（格） | 桶数 | 弧高 | 桶 AABB 约 | 每桶实例（密铺） |
|---|---|---|---|---|---|
| 现状 | 23×88 | 36 | 91m | 758×200×91m | 1500 |
| **30° 上限** | 23×37 | **78** | **16.6m** | 272×210×17m | ~692 |
| **15° 上限** | 15×18 | **200** | **4.3m** | 130×135×4.3m | ~270 |

- 弧高降 5~20 倍（∝α² 二次收益）；桶 AABB 从"巨人盒"收敛到贴合桶
- 尾块合规（30°：尾块 6 格 4.8°；15°：整除）；面积校验在先、B 只缩小 → 永不超 2048
- 收益：遮挡查询恢复有效（AABB 厚度 4~17m）；引擎侧（LOD/距离/屏幕尺寸）全部变准；**消不掉方向错位 1.3~2 倍**（但当前影响低，无需处理）
- 副作用可接受：桶数 36→78→200，组件数远低于背书上限（C 实验 5120 组件）；每桶实例下降 → 增量更新局部性更好
- 实施成本：~15 行，零新机制，风险极低

### 6.3 验证方式

`DebugPrintBucketStats` 输出块大小/桶数变化（36 → 78 → 200）+ 包围盒尺寸对比（弧高收敛）；调节 `MaxBucketAngularSpanDeg` 观察分桶结果与目视盒体。

## 七、方案取舍与决策（2026-08-21）

**结论**：当前选**方案 B**，方案 A 文档保留待命。

| 维度 | 方案 A（手动 OBB） | 方案 B（角跨度约束） |
|---|---|---|
| 成本 | ~200 行 + Tick/状态机/线程考量 | ~15 行，零机制 |
| 消除方向错位（1.3-2×） | ✅ | ❌（消不掉） |
| 压缩弧高/盒体尺寸 | 部分（法线对齐） | ✅ 5-20 倍 |
| 引擎侧（遮挡/LOD/距离）受益 | ❌（绕开而已） | ✅ 全部变准 |
| 风险 | 中 | 极低 |
| 当前配置下解决"盒体太大"观感 | ✅ | ✅ |

**决策依据**（重要决策，记忆 c8c39ad6）：
1. 真实配置（R=500m、121×450、36 桶、54k 实例）下膨胀影响程度低（§四）：渲染非瓶颈、绝对量小
2. 方案 A 成本高，收益在当前规模下有限（消掉的主要是影响本就低的错位）
3. 方案 B 以 15 行成本换弧高降 5-20 倍 + 遮挡恢复 + 引擎侧全受益，性价比高
4. 被拒路径：Sphere 剔除（外接球比 AABB 大 32-43%，数学必然更保守）、OBB 绘制（用户明确拒绝，保留 AABB 绘制）

**升级条件**（触发时实施方案 A，仍为纯增量改动）：实例规模 ×4 以上；更新瓶颈消除后渲染成为主要成本；桶数上百；上 Nanite/重材质。

## 八、Nanite 网格下的 AABB 剔除生效性（2026-08-21 查证）

**结论先行**：分两场景——**PIE/编辑器下 AABB 完全生效**；**打包版下 Nanite 组件走"始终可见"机制跳过 CPU 剔除**（AABB 不参与，膨胀自动豁免）；**SetHiddenInGame 两环境均有效**（消费点从 CPU 移到 GPU 端）。

### 8.1 背景

项目网格体基本为 Nanite（§四 影响升级条件中的"上 Nanite/重材质"已实际发生），需确认桶 AABB 在引擎剔除路径中是否仍参与（§三 路径是否被 Nanite 绕过）。

### 8.2 引擎证据链（UE 5.5.4 源码查证）

| 环节 | 证据 |
|---|---|
| 代理标记 | NaniteSceneProxy.h L238-239：`bIsNaniteMesh = true; bIsAlwaysVisible = SupportsAlwaysVisible();` |
| SupportsAlwaysVisible | NaniteResources.cpp L619-654：**WITH_EDITOR 恒 false**（L621-624）；打包版 L651-652 返回 true（条件：无 CustomDepth、默认光照通道、无静态光照、非蒙皮） |
| 始终可见开关 | RendererScene.cpp L145-148：`GVisibilitySkipAlwaysVisible = 1` 默认开启 |
| 始终可见区段 | RendererScene.cpp L6297-6357（`#if !WITH_EDITOR`）：构建 PrimitivesAlwaysVisible 偏移区段 |
| 无条件置位 | SceneVisibility.cpp L690：`UpdateAlwaysVisible` 对区段内组件直接置可见位，跳过全部可见性测试 |
| 截断 CPU 测试 | SceneVisibility.cpp L3350-3357：`NumTestedPrimitives` 截断；L1123-1127 / L1291-1294 跳过 draw command 收集 |
| FrustumCull 无特判 | SceneVisibility.cpp L711/L770/L818：对 Nanite **无特殊分支**，统一 AABB 测试（此即 PIE 下 AABB 生效的路径） |

### 8.3 分场景结论

| 场景 | 桶 AABB 剔除 | 原因 |
|---|---|---|
| PIE / 编辑器 | ✅ 100% 生效 | `SupportsAlwaysVisible()` 恒 false → 走普通 FrustumCull，距离/遮挡剔除同路径 |
| 打包版（Shipping） | ❌ 跳过（豁免） | PrimitivesAlwaysVisible 机制跳过全部 CPU 可见性测试，渲染由 GPU-driven Nanite 管线接管 |

打包版下膨胀自动豁免：桶 AABB 再大也不参与 CPU 剔除，无多画代价；代价转移到 GPU 逐实例剔除（按实例 LocalBounds）。

### 8.4 SetHiddenInGame（空桶停用）两环境均有效

消费链从 CPU 移到 GPU：

`SetHiddenInGame` → `MarkRenderStateDirty` → 代理重建 `IsDrawnInGame()=false` → `VisibleInGame` 参数（PrimitiveSceneProxy.cpp L847）→ GPUScene → NaniteInstanceCulling.usf L90 读 VisibilityFlags、**L161 `if (!bVisibleInGame) return false`**（注释"Partially implements FPrimitiveSceneProxy::IsShown()"）

→ PR-6 空桶停用在 PIE 和打包版都生效，只是打包版由 GPU 剔除消费。

### 8.5 对方案决策的影响

1. **方案 B（§六）不受影响**：PIE 验证仍需 AABB 行为（且 B 压缩盒体让 CPU 剔除更准）；打包版 Nanite 网格直接豁免
2. §七 升级条件中"上 Nanite/重材质"的回应：Nanite 网格的桶 AABB 膨胀在打包版自动豁免；**非 Nanite 网格仍受影响**
3. 分桶收益转移：PIE 靠桶 AABB 粗剔除降渲染线程提交；打包版靠 GPU 逐实例剔除（桶级 AABB 不再参与）
4. 与既有结论一致：分桶仍必需（渲染线程提交量与实例总数成正比，Nanite 无法替代）

## 九、相关文件与记忆索引

- 方案 A 设计：记忆「自建OBB剔除方案(v1)」（97cb971c）
- 方案 B 设计：记忆「角跨度约束方案（方案B）v1 定稿」（908d75b3）
- 取舍决策：记忆「球面AABB膨胀修复取舍」（c8c39ad6）
- 检索偏好：引擎源码检索优先 ue-codegraph（引擎索引可靠），项目源码以磁盘直读为准（记忆 c139fcb3）
- Nanite 协同：记忆「Nanite与分桶在压力场景下的协同作用」（2ee4959a）、「作物网格体支持Nanite及剔除机制差异」（61d8b97a）
