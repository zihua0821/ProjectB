# Git 未提交改动梳理

**分支**: MassCrop  
**基准**: `43dac60`（mass:单元格和地块）  
**梳理日期**: 2026-08-04  
**改动规模**: 源码 ~15 文件（约 2000+ 行变动）+ 内容资产大量增删

> ## 📌 后续更新（2026-08-07）
> 本快照中的**增量 ISM 渲染管线已被清理**：`UIncrementalInstancedStaticMeshComponent`（含根目录副本）与 `ISMPoolManager` 源码已删除，`ISMManager` 中的对象池接口（`AcquireSlot` / `ReleaseSlot` / `PreAllocateSlots` / `FlushAllIncrementalComponents` 等）已移除。
> 架构定案：**空间分桶 + 引擎原生增量 API**（`bUseIncrementalISM` / `AddInstancesById` / `RemoveInstancesById` / `SetCustomDataValueById`）。
> 本文件正文为 2026-08-04 时的历史快照，仅作参考，不再反映当前代码状态。

---

## 一、改动概览

| 类别 | 数量 | 说明 |
|------|------|------|
| 源码修改（M） | 13 | ISM 管理器、作物渲染、农场配置等 |
| 源码新增（AM/??） | 2 | 增量 ISM 组件（.h/.cpp） |
| 内容资产修改（M） | 23 | 配置、蓝图、地图、第三方材质 |
| 内容资产删除（D） | ~80 | UltimateFarming 第三方作物网格 |
| 内容资产新增（??） | 5+ | 新网格目录、Niagara、新地图、AI 测试 |
| 文档新增（??） | 4 | Cell/Plot/ISMManager/增量ISM 技术文档 |

**核心主题**：**增量 ISM 渲染管线落地** + **ISM 性能测试台** + **作物渲染改造**。

---

## 二、源码改动详解

### 1. 增量 ISM 组件（新增，AM 状态）

**文件**：`Source/ProjectB/Public/Component/IncrementalInstancedStaticMeshComponent.h` / `Private/...cpp`

全新组件，解决普通 ISM 每次修改触发 GPU 全量重建的性能问题：

- **脏队列**：`FDirtyInstance` + `FDirtyInstanceQueue`（MPSC），游戏线程与渲染线程间传递增量变更
- **双 SceneProxy**：`FIncrementalSceneProxy`（非 Nanite）/ `FIncrementalNaniteSceneProxy`（Nanite）
- **增量接口**：`UpdateInstanceTransformIncremental` / `UpdateInstanceCustomDataIncremental` / `UpdateInstanceIncremental` / `SetInstanceVisibility` / `FlushIncrementalUpdates`
- **关键约束**：重写 `SendRenderInstanceData_Concurrent()` 为空——父类接口写入不推送渲染线程，**必须走增量接口 + Flush**
- 静态辅助函数 `ProcessDirtyInstances_RenderThread` 供两个 SceneProxy 复用

> ⚠️ 根目录还发现一个未跟踪的 `IncrementalInstancedStaticMeshComponent.cpp`（副本），疑似误放，可考虑删除或整理。

### 2. ISMManager.h / cpp（重构，M）

| 改动点 | 说明 |
|--------|------|
| `INSTANCES_PER_BUCKET` | **256 → 2048**（单桶容量扩大） |
| `CreateISMComponent` | 支持 `bUseIncrementalISM` 创建增量组件 |
| 新增对象池接口 | `AcquireSlot` / `ReleaseSlot` / `PreAllocateSlots` / `ExpandCapacity`（仅增量组件） |
| 新增刷新接口 | `FlushAllIncrementalComponents` + `DirtyIncrementalComponents` 集合 |
| 新增空闲池 | `FreeSlotIndices`（MeshKey → MPSC 空闲队列） |
| 分桶参数 | 缓存 `CachedParams`、`BucketingThreshold=4096`、`BatchExpandSize=512` |
| 代码清理 | 移除大量乱码注释，精简格式 |

### 3. CellManagerComponent.cpp（M，~90 行）

- **移除 MassRepresentation 依赖**：删除 `FMassRepresentationFragment` 写入及相关 include（`MassRepresentationSubsystem` 等）
- **初始化顺序**：`PrecreateISMComponents()` 提前到 `CreateMassCell()` 之前（先预创建组件再批量添加实例）
- **作物组件改造**：作物 ISM 改用 `bUseIncrementalISM = true`，并 `PreAllocateSlots(Mesh, TotalCells / 网格数)` 预分配槽位
- **注册唯一网格**：`ConfigSub->SetUniqueMeshes(UniqueMeshes)`（UniqueMeshes 从 TMap 改为 TArray<TPair>）
- **轮廓保持普通 ISM**（方案 A，`bUseIncrementalISM` 未设置）
- **Test()**：随机作物种植（从 `CropDataTable` 随机选作物）
- **PlantCrop**：Defer lambda 补充捕获 `this`
- `PlotTransforms` → `CellTransforms` 重命名

### 4. CropVisualSyncProcessor.cpp（M，~94 行）

**从"批量收集（未完成）"改为"即时增量池"**：

- 移除 `PendingRemoves` / `PendingAdds` 批量收集与分组逻辑（原 `BatchRemoveInstances`/`BatchAddInstances` 为注释状态）
- 改为即时调用：`ReleaseSlot`（释放旧实例）+ `AcquireSlot`（申请新实例）
- `AcquireSlot` CustomData 传 `{1.0, 0.0, 0.0}`（Opacity=1 / Disease=0 / IsLeaf=0）
- 末尾新增 `ISMManager->FlushAllIncrementalComponents()`
- 新增 `bRequiresGameThreadExecution = true`
- 新增 include：`NiagaraDataInterfaceArrayFunctionLibrary`、`PlanetConfigDataAsset`

### 5. PlotVisualSyncProcessor.cpp（M，6 行）

- **三处 `UpdateInstanceCustomData` 被注释**（Opacity / Wetness / Fertility）——地块视觉更新被禁用
- 影响：耕地后土地不显示、浇水/施肥无视觉反馈
- ⚠️ 建议确认这是**有意关闭**（调试中）还是待修复

### 6. FarmlandActor.h / cpp（重写为性能测试台，~678 行）

**从"农场核心 Actor"完全重写为 ISM 性能测试台**：

- 移除：`FarmlandComponent` / `CropManagerComponent` / `GroundMeshComponent` 及全部旧农场接口（`GetPlotInfo`、`GetPlotSaveGameData`、`HandleLeftMouseClicked`、`OutlinePlot` 等）
- 新增测试接口：
  - `RunTest_Level0_SingleInstance()` — 单实例
  - `RunTest_Level1_BatchPressure(int32 Count)` — 批量压力
  - `RunTest_Level2_CrossSwitch(int32 Count)` — 交叉切换
  - `RunTest_Level3_FullPressure()` — 全压力
  - `RunTest_Level4_SteadyState()` — **多帧稳态压力测试**
  - `CleanupTest()`
- Level 4 蓝图可配置参数：`TestMeshArray` / `Level4TotalInstances=10000` / `Level4ChangesPerFrame=500`
- 测试状态：`TestIdsA/B`、`Level4InstanceIds`、`Level4MeshIndices`、`Level4Transforms` 等
- 常量 `TotalInstanceCount = 10000`

### 7. FarmlandConfigDataAsset.h（M）

- 新增 `CropMeshes`（作物模型数组）
- 新增 `CropNiagaraSystem`（作物粒子系统）

### 8. CropData.h（M）

- `CropCustomData` 命名空间调整：新增 `Opacity=0`，`Disease=1`、`IsLeaf=2`、`Count=3`（原来 Disease=0/IsLeaf=1/Count=2）
- 与 `AcquireSlot` 传的 `{1.0, 0.0, 0.0}` 对应（Opacity 控制作物显隐）

### 9. GlobalConfigSubsystem.h（M）

- 新增 `SetUniqueMeshes` / `GetUniqueMeshes`（`TArray<TPair<TObjectPtr<UStaticMesh>, int32>>`）
- 新增私有成员 `UniqueMeshes`
- （已有：`SetConfigs`、`SetISMAttachParent`、`MatureCropQueue`）

### 10. ProjectB.h（M）

- 新增 `static const FVector ParkedLocation(1.0e7, 1.0e7, -1.0e7)`（Nanite 实例隐藏停靠点）

### 11. ProjectB.Build.cs（M）

- 新增模块依赖：`RHI`、`RenderCore`、`Renderer`（增量 ISM 渲染线程操作需要）
- 移除模块依赖：`MassRepresentation`（已弃用）

### 12. CellManagerComponent.h（M）

- 仅换行/空行格式变化，无实质改动

---

## 三、内容资产改动

### 修改（M，23 个）

| 分组 | 文件 | 说明 |
|------|------|------|
| 游戏配置 | `DA_FarmlandConfig.uasset` | 农田配置（配合新增 CropMeshes/CropNiagaraSystem） |
| 数据表 | `DT_CropData.uasset` | 作物数据表 |
| 蓝图 | `BP_FarmlandActor.uasset` / `TestActor.uasset` | 适配 FarmlandActor 测试台改造 |
| 地图 | `StartMap.umap` | 起始地图 |
| 第三方材质 | `UltimateFarming/Materials/*`（17 个） | MI_* 作物材质、M_Foliage/M_Fruits/M_Hardsurface、MF_Plant_Disease |

### 删除（D，~80 个）

- `Content/ThirdParty/UltimateFarming/Meshes/SM_*.uasset`（全部旧作物网格）：Carrot/Corn/Grape/Pumpkin/Rose/Strawberry/Sugarcane/Sunflower/Tomatoes/Watermelon 的各阶段模型
- 仅 `SM_Grape_B.uasset` 保留（M 状态）

### 新增（??）

| 路径 | 说明 |
|------|------|
| `Content/ThirdParty/UltimateFarming/Meshes/Crops/` | 新作物网格目录（可能为迁移/整理后的网格） |
| `Content/MeshTemp/` | 占位网格目录 |
| `Content/Niagara/` | Niagara 粒子系统（`NS_CropRenderer` 等） |
| `Content/Game/AI/Test/` | AI 测试资产 |
| `Content/NewMap.umap` / `NewMap1.umap` | 新测试地图 |

---

## 四、改动主题归纳

### 主题 1：增量 ISM 渲染管线（核心）
```
新增 UIncrementalInstancedStaticMeshComponent（脏队列 + 双 SceneProxy）
    → ISMManager 支持创建增量组件 + 对象池（AcquireSlot/ReleaseSlot/PreAllocateSlots/Flush）
    → 作物渲染改走增量池（CropVisualSyncProcessor）
    → Build.cs 加 RHI/RenderCore/Renderer
    → 移除 MassRepresentation（旧渲染方案）
```

### 主题 2：ISM 性能测试台
```
FarmlandActor 重写 → Level0~4 测试（含多帧稳态压力测试，10000 实例）
配合 TestMeshArray 等蓝图可配置参数
```

### 主题 3：作物渲染增强
```
CropCustomData 新增 Opacity 通道
作物组件预分配槽位 + 增量渲染
配置新增 CropMeshes / CropNiagaraSystem（粒子）
```

### 主题 4：资源整理
```
UltimateFarming 旧网格批量删除 → 迁移至 Meshes/Crops + MeshTemp 占位
材质批量更新（配合 CustomData 布局调整）
```

---

## 五、需要关注的点

1. **Plot 视觉更新被注释**：`PlotVisualSyncProcessor` 三处更新禁用，地块视觉反馈缺失——确认是否有意
2. **根目录多余文件**：`IncrementalInstancedStaticMeshComponent.cpp`（根目录副本）建议清理
3. **CropCustomData 数量**：`AcquireSlot` 传 3 个 float，需确保数据表 `CropNumCustomDataFloats` 配置为 3，否则增量组件校验失败
4. **批量删除的网格**：删除后需确认 `DT_CropData` / `DA_FarmlandConfig` 中引用是否已更新（新网格位于 `Meshes/Crops`）
5. **FarmlandActor 角色变化**：已从农场核心 Actor 变为测试台，若地图/蓝图仍依赖其旧接口（GetPlotInfo 等）会失效——`BP_FarmlandActor.uasset` 已同步修改
6. **INSTANCES_PER_BUCKET 变化**：256 → 2048 影响分桶粒度（分桶行为与旧版本不同）

---

## 六、相关文档索引

| 文档 | 说明 |
|------|------|
| `docs/格子与作物/单元格系统技术文档.md` | Cell 系统（轮廓高亮面片） |
| `docs/格子与作物/地块系统技术文档.md` | Plot 系统（含分桶机制澄清） |
| `docs/渲染体系/ISMManager技术文档.md` | ISM 管理器（分桶 + 对象池） |
| `docs/渲染体系/历史归档/增量ISM组件技术文档.md` | 增量 ISM 组件（脏队列 + 双 SceneProxy） |
