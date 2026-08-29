# ISMManager 代码 Review 检查清单

**版本**: v1.3
**更新日期**: 2026-08-22
**分支**: MassCrop
**关联**: 《ISMManager技术文档.md》（v2.2）、《ISMManager空间分桶重构方案.md》、《球面AABB剔除精度问题与解决方案.md》
**用途**: 辅助手动 Review `UISMManager`（`Source/ProjectB/Public/Subsystem/ISMManager.h` + `Private/Subsystem/ISMManager.cpp`）。每条含**定位 / 现状 / 为什么重要 / 怎么验证 / 确认结论空白**，逐条手动核对后填写结论（✅ 已核对 / ⚠️ 需讨论 / ❌ 待修复）。

> **v1.3 修订**（2026-08-22）：清理 `GetTotalBuckets`（仅日志调用的模糊语义接口，推导式内联至 `DebugPrintBucketStats`，技术文档 §11.4 同步）；全文行号按当前磁盘代码重标定。
> **v1.2 修订**（2026-08-22）：`NumCustomDataFloats` 已收敛为首次写入生效（挪入新组 `if` 块，与组件创建时固化值对齐），P1-2 重复块同步；新增 P2-5（`GetBucketComponent` 只读接口线程边界，已决策维持现状）；全文行号同步至当前代码。
> **v1.1 修订**（2026-08-22）：同步接口收敛——首桶接口（`GetOrCreateISMComponent` / `GetISMComponent` / `GetFirstBucketComponent`）已全部删除，统一 `GetOrCreateBucket` / `GetBucketComponent`（首桶传 0）；`GetOrCreateBucket` 已修正为入口即派发（P0-1 部分缓解）；全文行号同步至当前代码。

---

## 〇、全局速查（文件行号 → 模块）

| 模块 | 头文件行号 | 源文件行号 |
|---|---|---|
| 生命周期 Initialize/Deinitialize/Tick/GetStatId | .h L176-180 | .cpp L25-44、L523-537 |
| 组件管理（CreateISMComponent/GetOrCreateBucket/GetBucketComponent） | .h L183-197 | .cpp L54-90、L374-416 |
| 球面分桶配置 ConfigureSphereBuckets | .h L200-207 | .cpp L101-149 |
| 内部映射 GetBucketKey | .h L269-270 | .cpp L157-168 |
| 批量添加 BatchAddInstances | .h L210-225 | .cpp L179-267 |
| 批量移除 BatchRemoveInstances | .h L226-230 | .cpp L273-305 |
| CustomData 批量/单值 | .h L231-234 | .cpp L310-362 |
| 调试 DebugPrintBucketStats | .h L238 | .cpp L425-456 |
| AABB 线框绘制（Draw/ByPath/Clear） | .h L257-261 | .cpp L462-515 |
| AABB 交互封装（SetDebugAABB/Input/Draw） | .h L245-253、L289 | .cpp L539-672 |

---

## 一、P0 检查点（线程 / 数据一致性，风险最高）

### P0-1 批量接口的线程派发缺口
- **定位**: 头文件声明 `.h L165-168`（“创建/修改接口可在任意线程调用（非游戏线程自动派发到游戏线程并同步等待）”）；实现 `BatchAddInstances` `.cpp L192`（`MeshToBucketGroups.FindOrAdd`）、L245/264（`BucketInstanceCounts +=`）；`BatchRemoveInstances` `.cpp L277`（`MeshToBucketGroups.Find`）、L297（计数递减）。仅 `GetOrCreateBucket` `.cpp L378-388`（入口即派发，已修正）内部做了 `IsInGameThread()` 派发。
- **现状**: `BatchAddInstances` / `BatchRemoveInstances` **整个函数体没有**做 `IsInGameThread()` 判断与派发；但它们在**调用线程**直接访问非线程安全的 `UPROPERTY TMap<FISMMeshKey,FISMBucketGroup>` 与 `TArray<int32> BucketInstanceCounts`。内部虽会经由 `GetOrCreateBucket` 派发，但派发发生前（L192 的 FindOrAdd、L245 的计数递增）已在自己线程上动了共享数据。
- **为什么重要**: 若实际调用方是 `Mass` 并行处理器或其它 worker 线程，会在非游戏线程并发读写管理器核心 MAP + 数组 → 数据竞争 / 崩溃。这是整套管理器最脆弱的一环。
- **怎么验证**: ① 用 ue-codegraph `find_callers(BatchAddInstances/BatchRemoveInstances)` 查调用方，确认是否落在 `Mass` processor 的并行循环；② 在 `BatchAddInstances` 函数头（`.cpp L179`）加 `UE_LOG(LogTemp, Warning, TEXT("BatchAdd MG=%d"), IsInGameThread())`（或断点比对 `IsInGameThread()`），运行期观察是否出现过 False。
- **确认结论**: ________

### P0-2 同桶后续追加不更新 Bounds
- **定位**: `BatchAddInstances` 单桶 `.cpp L242-247`、分桶 `.cpp L256-263`，都仅在 `bWasEmpty`（计数为 0）时调用 `UpdateBounds()`；注释明示“运行期同桶追加跳过，避免每帧全量重算”。
- **现状**: 桶从"空→非空"时显式计算 Bounds；但桶已含实例后再 Add，**不再更新 Bounds**。若新增实例位置超出旧 Bounds 范围，会被引擎视锥/距离剔除误裁剪（渲染消失，数据仍在）。
- **为什么重要**: 分桶模式下同一桶会反复 Add（尤其密铺增长），Bounds 停留在"首次非空"时的范围；这正是记忆「增量 API AddInstancesById 不自动更新 Bounds 致剔除失效」的已知坑，此处是否真正覆盖到位是 **P0-2 的核心**。
- **怎么验证**: ① 构造用例：向同桶先 Add 2 个远位实例 → 再向**同一桶** Add 3 个更远的实例 → 观察新增 3 个是否渲染消失；② 临时去掉 `if (bWasEmpty)` 条件、每次 `UpdateBounds()`，对比是否消失；③ 若确认误剔，权衡"触发式更新"（累计 N 次或每桶超 S 时才重算）替代"每帧全量重算"。
- **确认结论**: ________

---

## 二、P1 检查点（静默失败 / 代码重复 / 时序依赖）

### P1-1 CustomData 长度校验静默失败
- **定位**: 单桶 `.cpp L226-232`（`if (CustomData.Num() == Expected)` 才调用 `SetCustomDataById`）、分桶 `.cpp L256-266`（同样条件）；`BatchUpdateInstanceCustomData` `.cpp L327`（`if (CustomData.Num() != Expected) return;`
- **现状**: `CustomData` 长度与 `Transforms.Num() × NumCustomDataFloats` 不匹配时**无条件跳过**（实例照常添加、只是不设 CustomData），**无任何日志/警告**。
- **为什么重要**: 排查"材质无反应 / 数据对不上"时零反馈；且长度错误不致中断添加，导致"实例已加、CustomData 缺失"的松耦合状态。`FloatsPerInstance` 依赖创建时固化的 `Group.NumCustomDataFloats`，调用方若传错 floats 会造成系统性偏移。
- **怎么验证**: 审查所有调用方传入的 `NumCustomDataFloats` 与 `CustomData` 计算是否一致；加 `UE_LOG(LogTemp, Warning, ...)` 观察运行期是否触发过。
- **确认结论**: ________

### P1-2 新组继承配置逻辑两段重复（v1.1：三处→两处；v1.2：`NumCustomDataFloats` 已并入块内）
- **定位**: `BatchAddInstances` `.cpp L194-200`、`GetOrCreateBucket` `.cpp L395-401`，两处均重复 `Group.bDisableBuckets = Params.bDisableBuckets; Group.CachedParams = Params; Group.BucketingThreshold = BucketingThreshold; Group.NumCustomDataFloats = NumCustomDataFloats;`。
- **现状**: 完全相同的“新组继承全局配置”块写了两遍（原第三处 `GetOrCreateISMComponent` 已随接口收敛删除）。注：`NumCustomDataFloats` 已从无条件赋值收敛为首次写入生效（与组件创建时 `SetNumCustomDataFloats` 的固化值对齐，避免后续调用传不同值时组记录与组件错位）。
- **为什么重要**: DRY 违背。未来若新增继承字段、或改继承策略，漏改任一处 → 不同路径行为不一致（如某路径阈值继承失效 → 恒走单桶）。属于"改了 A 处漏 B 处"的典型隐患。
- **怎么验证**: 逐一比对两处字段是否完全一致；考虑抽私有 Helper（如 `InitGroupFromParams(FISMBucketGroup&, ...)`）统一承载。
- **确认结论**: ________

### P1-3 预创建组不覆盖全局阈值（时序依赖）
- **定位**: `.cpp L194`、`.cpp L395` 仅 `if (Group.Buckets.Num() == 0)` 才继承全局 `BucketingThreshold`；全局阈值修改入口 `SetBucketingThreshold` `.h L207`。
- **现状**: 新组在首次创建时继承全局 `BucketingThreshold`；之后全局改为新值（`SetBucketingThreshold`）**不会**更新已存在的组。
- **为什么重要**: 若流程是"先为某 Mesh 建组（继承阈值 T1）→ 后调 `SetBucketingThreshold(T2)`"，该 Mesh 仍用 T1。若 T1 被设计为非正整数（≤0 视为强制单桶），会静默导致分桶失效。需确认**所有调用方是否都在 `SetBucketingThreshold` 之后**才首次 Add。
- **怎么验证**: 梳理 `SetBucketingThreshold` 相对所有 BatchAdd 调用方的时序；或考虑将组阈值改为"运行时实时读取全局值"，消除固化快照。
- **确认结论**: ________

---

## 三、P2 检查点（边界 / 已知取舍）

### P2-1 GridCoords 长度不符静默返回
- **定位**: `.cpp L188` `if (!GridCoords.IsEmpty() && GridCoords.Num() != Transforms.Num()) return AllGlobalIds;`
- **现状**: `GridCoords` 长度与 `Transforms` 不一致时静默返回空数组，**一个实例都不加**，无日志。
- **为什么重要**: 调用方若算错 GridCoords 长度，整批添加静默失败（无实例、无报错），只能在产出端发现"东西没生成"。属低概率但难排查的静默失败。
- **怎么验证**: 加 `UE_LOG` 或调用方 `ensure(GridCoords.Num() == Transforms.Num())`。
- **确认结论**: ________

### P2-2 越界 GridCoords 被 clamp 挤入同桶
- **定位**: `.cpp L162-163` `FMath::Clamp(GridCoords.X/Y, 0, Cells-1)`；头文件 `.h L269` 注释“越界 clamp 保确定性，实例不丢失（包围盒略扩展）”。
- **现状**: 越界坐标被 clamp 到边界格，多个不同越界坐标会落入同一桶（如 X 越到 -5 与 -100 都变 0）。
- **为什么重要**: 边界桶实例数可能激增，破坏"面积校验保证每桶 ≤ S"的容量假设（面积校验针对合法坐标生效，clamp 会绕过它）。需确认业务侧是否保证坐标在 `[0, Cells-1]` 内；若越界是高概率，应"丢弃并告警"而非 clamp。
- **怎么验证**: 确认调用方 `GridCoords` 生成逻辑是否已 clamp/校验；压测时观察边界桶计数是否异常膨胀。
- **确认结论**: ________

### P2-3 停用桶上的 CustomData / 单值更新是否安全
- **定位**: `BatchRemoveInstances` `.cpp L297-302`（计数归零 → `SetHiddenInGame(true)` + `UpdateBounds`）；`GetOrCreateBucket` `.cpp L407-411`（复活 → `SetHiddenInGame(false)`）；`BatchUpdateInstanceCustomData` `.cpp L343-345`、`UpdateInstanceCustomData` `.cpp L350-362` 直接按句柄定位桶、**未检查桶是否 `IsHiddenInGame()`**。
- **现状**: 停用/复活与 `BucketInstanceCounts` 判定一致（PR-6）；但对"已停用（Hidden）"的桶仍可 `SetCustomDataById` / `SetCustomDataValueById`，未加保护分支。
- **为什么重要**: 停用桶仅 `SetHiddenInGame(true)`（组件常驻复用，D-4），数据仍可写入、复活时生效——通常无害；但需确认引擎对 Hidden 组件调用 ById 更新是否安全、是否会被跳过。属于“想清楚即可”的半取舍点。
- **怎么验证**: 确认引擎 `SetCustomDataById` 对 `SetHiddenInGame(true)` 组件的处理；或代码中明确注释该约定。
- **确认结论**: ________

### P2-4 Nanite 打包版剔除豁免（已知取舍）
- **定位**: 头文件 `FISMComponentParams::bDisallowNanite` `.h L82`（作物类无 Nanite 支持置 true，走传统渲染路径）；相关结论已文档化（《球面AABB剔除精度问题与解决方案.md》§八）。
- **现状**: 支持 Nanite 的网格在打包版走"始终可见"机制（跳 CPU AABB 剔除、GPU 逐实例剔除接管）；`bDisallowNanite=true` 的网格走传统路径、可被桶 AABB 剔除。PIE 下无论是否 Nanite 都走 AABB 剔除。
- **为什么重要**: 分桶对 Nanite 网格在**打包版**的剔除收益有限（GPU 接管）；PIE 下仍生效。需确认各 Mesh 的 `bDisallowNanite` 赋值是否符合预期（作物→true，其余→false?）。
- **怎么验证**: 核对各调用方 `FISMComponentParams` 中 `bDisallowNanite` 的赋值；对照《球面AABB剔除精度问题与解决方案.md》§八 结论。
- **确认结论**: ________

### P2-5 GetBucketComponent 只读接口线程边界（已决策：维持现状）
- **定位**: `GetBucketComponent` `.h L190-197`（声明 + 线程约束注释）、`.cpp L79-90`（定义 + 注释）；类注释线程模型 `.h L165-168`；当前唯一调用方 `AFarmlandActor` 压力测试（Tick 路径，游戏线程）。
- **现状**: 只读接口不做派发、无锁裸读，**仅限游戏线程**；跨线程调用会与游戏线程写操作（`FindOrAdd` / `SetNum` / 赋值）数据竞争，且返回组件指针的增量用法（`UpdateInstanceTransform` 等）本身亦要求游戏线程。
- **为什么重要**: 若未来出现跨线程调用方（如 Mass 并行 processor 直取桶组件做增量更新）会直接踩中；与 P0-1 同根源。已决策维持现状：契约写在注释/文档（《ISMManager技术文档》§3.2/§9-8），跨线程需求的正确姿势 = 游戏线程取一次指针缓存使用（AFarmlandActor 即此模式）。
- **怎么验证**: 新增 `GetBucketComponent` 调用方时确认其线程上下文；若确需跨线程，重新评估“入口即派发”或“调用方缓存”两方案。
- **确认结论**: ________

---

## 四、使用建议

1. **逐条核对**：按 P0 → P1 → P2 顺序逐行走读，每条在「确认结论」填写 ✅ / ⚠️ / ❌。
2. **配合检索**：引擎源码检索优先 ue-codegraph（引擎索引可靠），项目源码以磁盘直读 `Get-Content -Encoding UTF8` 为准（记忆 c139fcb3）。
3. **不编译直接交付**：按用户偏好，本清单仅供手动 Review，无需构建验证（记忆 1b8e4c88）。
4. **结论回填**：Review 完成后，可将 ❌ 项转化为具体修复任务；如需，我可在本清单 §5 追加"修复建议"栏。
