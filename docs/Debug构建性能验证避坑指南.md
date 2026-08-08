# Debug 构建下性能验证避坑指南

> 项目：ProjectB（分支 MassCrop，UE 5.5.4 自编译源码版）
> 背景：增量 ISM 组件重构期间，引擎原生增量路径"每次调用 ~80μs"的性能瓶颈根因定位复盘。
> 结论先行：**Debug 构建产出的任何性能绝对值都不可信，性能验证必须用 Development 或 DebugGame。**

---

## 一、核心结论（TL;DR）

1. 本次排查的"80μs/调用瓶颈"是 **Debug 构建专属校验税**，不是引擎增量路径的真实成本：
   - Debug 下：76.8μs/次调用（5000 实例）
   - Development/DebugGame 下：0.046μs/次调用（**×1688 倍差**，同一份代码）
2. 根因：`DO_GUARD_SLOW=1`（仅 `UE_BUILD_DEBUG` 启用）把 `FInstanceAttributeTracker::Validate()` 的 **O(实例总数) 全量遍历**编译进了引擎，每次 `MarkIndex` 触发 **4 次 Validate**。
3. 引擎原生增量路径本身无瓶颈（ID 查找/标记链/数组写全 O(1)，实测 ~0.35μs/调用级）。
4. **唯一可靠的判据是项目 DLL 文件名**（`UnrealEditor-ProjectB-Win64-Debug.dll`）；CRT 依赖判断法是陷阱。

---

## 二、Debug 构建的性能失真来源（为什么 Debug 慢）

| # | 失真源 | 机制 | 影响量级 |
|---|--------|------|---------|
| 1 | **DO_GUARD_SLOW=1** | O(N) 校验被编译进引擎（本次根因） | 单次调用数十~数百 μs，与实例总数线性 |
| 2 | 零优化编译（/Od） | 所有代码 5~20 倍慢 | 全局放大 |
| 3 | FORCEINLINE 失效 | 内联被放弃，每次调用真实函数开销 | 热路径显著 |
| 4 | 容器/迭代器调试检查 | TArray/Map 越界检查更重 | 放大 |
| 5 | 内存调试分配器 | 分配/释放填充与校验 | 分配密集场景 |
| 6 | DO_CHECK 断言成本 | check/ensure 带栈抓取路径 | 放大 |

**核心认知**：Debug 下"哪个函数慢"和发布下"哪个函数慢"可能是完全不同的两件事——O(N) 校验税、内联失效、内存填充会**扭曲热点排序**。

---

## 三、案例复盘：80μs 根因定位全过程

### 3.1 现象
微基准（每 120 帧轮换 stage，拆解 ID API 单次调用成本）：
- stage 2（变换全 API）/ stage 4（CustomData 全 API）/ stage 8（纯标记链）：**~80μs/次调用**
- stage 0/1/3/5/6/7/9（lookup / raw 直写 / math / Modify / 仅渲染脏标记）：全部 <0.5μs
- first-call ≈ per-call（每次均匀贵，无"每帧固定税"）

### 3.2 排除链（逐层剥离）
```
Modify 税（stage 7 实测 0.9μs）      → 排除 UObject::Modify
数学/查找/直写（stage 1/3/5/6）       → 全部免费
mark-chain（stage 8 = 80.8μs）        → 锁定标记链
mark-renderdirty-only（stage 9 = 0.085μs）→ 排除 MarkComponentRenderInstancesDirty
→ 真凶只剩 MarkIndex 段
```

### 3.3 源码穷尽
- `MarkIndex` / `SetNum` / `GetMaxInstanceIndex` / `MarkComponentRenderInstancesDirty`：源码全 O(1)
- `LOG_INST_DATA`：`#if 0` 禁用，排除
- **唯一 O(N) 项**：`FInstanceAttributeTracker::Validate()`（InstanceAttributeTracker.cpp L49-93，`#if DO_GUARD_SLOW` 下）——O(MaxIndex) 全量遍历
- `DO_GUARD_SLOW` 仅 `UE_BUILD_DEBUG=1` 时 =1（Build.h L242-266）

### 3.4 二进制验证反转（关键教训）
- 误判：引擎 DLL 依赖 `VCRUNTIME140.dll`（无 D）→ 以为 Development
- 真相：**UE Debug 构建默认 `bDebugBuildsActuallyUseDebugCRT=false`，不依赖 D 版 CRT** → CRT 判据无效
- 实锤：项目 Binaries 存在 `UnrealEditor-ProjectB-Win64-Debug.dll` + 满屏 `Debug.patch_*.exe` 热重载残留 → **一直在跑 Debug 配置**

### 3.5 数量级吻合与闭环
```
每次 MarkIndex → 4 次 Validate（入口 + SetNum 内 2 次 + 出口）
4 × 5000 实例 = 20000 次 GetFlags × ~3.8ns ≈ 76.8μs  ← 与实测完全吻合
Development 复测：stage 8 = 0.046μs（×1688 降幅）→ 闭环
```

### 3.6 教训
- 源码阅读确认宏开关（`DO_GUARD_SLOW`/`DO_CHECK`/`STATS`）优先于二进制猜测
- 性能异常先确认构建配置，再进入逐层拆解
- 本案例 4 轮微基准拆解若第一轮就跑 Development，答案当场就是 0.05μs，不存在"瓶颈"

---

## 四、如何识别自己正在跑 Debug（判据清单）

| 判据 | 可靠性 | 说明 |
|------|--------|------|
| 项目 DLL 名含 `Win64-Debug` | ✅ 可靠 | `UnrealEditor-ProjectB-Win64-Debug.dll` |
| 进程名 `UnrealEditor-ProjectB-Win64-Debug.exe` | ✅ 可靠 | 任务管理器 |
| 热重载残留 `Debug.patch_*.exe` | ✅ 可靠 | Binaries 目录满屏即 Debug 配置 |
| 启动日志 `LogInit: Build: ... (Debug)` | ✅ 可靠 | 日志首行 |
| CRT DLL 依赖（有无 D 后缀） | ❌ **陷阱** | UE Debug 默认不用 Debug CRT |
| 构建速度 / 内存占用 | ❌ 不可靠 | 与配置无稳定对应 |

---

## 五、构建配置选择规范

| 配置 | 引擎校验 | 优化 | 断点能力 | 用途 |
|------|---------|------|---------|------|
| **Debug** | DO_GUARD_SLOW=1 | 无 | 引擎+项目全可断 | 断点调试、数据校验；**性能数据作废** |
| **DebugGame** | 引擎无 / 项目有 | 引擎优化 | 项目可断，引擎部分 | 日常开发 + 性能观察（推荐平衡点） |
| **Development** | 无重校验 | 优化 | 部分（优化限制） | **标准性能基线** |
| **Shipping** | 全关 | 最优 | 几乎不可 | 最终验收 |

本次实测对照（同机同场景，5000 实例 mark-chain）：
- Debug：76.8μs/调用
- Development：0.046μs/调用（×1688）

---

## 六、预防措施与工作流

1. **性能验证前三问**：什么配置？（DLL 名）什么进程？（PIE / 独立 / Game 目标）数据可复现吗？（同配置对比）
2. **工作流固化**：
   ```
   逻辑调试（断点）    → Debug（或 DebugGame）
   性能基准 / 压力测试 → Development（标准） / DebugGame（折中）
   最终验收            → Shipping
   ```
3. **微基准纪律**：
   - 同构建配置内做相对对比（stage 间比较）
   - 防优化累加器（成员写承接结果，防编译器消除）
   - first-call 与 per-call 分离统计（区分固定税与均匀开销）
   - 复杂度趋势可看（N 翻倍耗时是否翻倍），绝对值不可信
4. **引擎源码排查顺序**：先确认宏开关（DO_GUARD_SLOW/DO_CHECK/STATS/WITH_EDITOR）→ 再看函数实现 → 最后二进制验证

---

## 七、相关引擎源码索引（备查）

| 文件 | 位置 | 内容 |
|------|------|------|
| `Misc/Build.h` | L242-266 | `DO_GUARD_SLOW` 定义（Debug=1，其余=0） |
| `InstancedStaticMesh/InstanceAttributeTracker.cpp` | L49-93 | `Validate()` O(N) 全量遍历（仅 DO_GUARD_SLOW 下编译） |
| `InstancedStaticMesh/InstanceAttributeTracker.h` | L244-310 / L404-408 | `MarkIndex` / `SetNum` 热路径 4 次 Validate 调用 |
| `InstancedStaticMesh/ISMInstanceDataManager.cpp` | L274-278 / L884-902 / L918-928 | `TransformChanged` → `MarkChangeHelper` → `MarkIndex` |

---

## 八、附加：Debug 下调性能路径的可选引擎补丁（已评估，未实施）

目的：仅改善 **Debug 构建下的调试体验**（调性能路径时不被 80μs 校验税污染）；正式构建（Development/Shipping）本无此税，**不补丁也不影响生产性能**。

- **方案 A（推荐，改动最小）**：`InstanceAttributeTracker.cpp` 的 `Validate()` 开头加运行时判断 `if (!GIsPlayInEditor) return;` → 独立运行（含 Game 目标）跳过 O(N) 校验，PIE 保留。只重编 Engine 模块。
- **方案 B**：头文件 `#if DO_GUARD_SLOW && WITH_EDITOR` + 同步 .cpp → 编译期裁剪，但头文件改动触发全引擎重编。
- 状态：**待决策**。若实施，记录进 `引擎补丁清单.md`。

---

*本文档对应代码：`Source/ProjectB/Private/Farm/FarmlandActor.cpp`（微基准与压力测试）。*
