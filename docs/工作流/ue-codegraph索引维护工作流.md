# ue-codegraph 索引维护工作流

> 本文档记录 ProjectB 开发中 ue-codegraph 代码索引的完整工作流：索引双源机制、三条刷新通道的可靠性实测结论、以及 AI 编辑代码后的强制约定。
> 结论状态：**定稿**（2026-08-31 实测验证）。本文档为后续所有涉及代码检索/编辑会话的必备依据。
> **v2.0 扩展（2026-10-02）**：第八节补齐 Rider + UE AI 全工作流（工具链分工、PIE 调试闭环、视觉验证、红线清单、故障处理）；一~七节结论不变。

---

## 一、背景与问题

- 项目使用 ue-codegraph（MCP）做代码检索（`search_symbols` / `search_code` / `find_callers` 等）。
- 早期经验：项目索引可能过期，曾出现"检索结果与磁盘不一致"。
- 实测发现：索引的**更新机制存在多条通道，可靠性差异巨大**，且存在"幽灵符号"污染问题。
- 目的：固化一套可靠、低 token 消耗的索引维护工作流，避免误判与污染。

---

## 二、索引的双数据源（关键认知）

ue-codegraph 的索引内容来自**两个独立来源**：

| 数据源 | 内容 | 刷新手段 | 谁触发 |
|---|---|---|---|
| **磁盘文件** | 真实代码符号 | `reindex_file`（MCP）/ `ue-graph reindex`（CLI）/ 用户保存 | AI 或用户 |
| **变更文件记录** | Qoder 会话中每次编辑的 diff 文本（存于 `local.db`） | **只有用户在 IDE 接受/拒绝变更后清除** | 用户 |

> **推论**：即使磁盘代码已删除/还原，只要变更记录里还留有旧 diff，索引就可能残留旧符号（幽灵符号）。`reindex_file` 只能刷磁盘源，**无法清除变更记录源**。

---

## 三、三条刷新通道的实测结论（2026-08-31）

| 通道 | 触发方式 | 可靠性 | 实测证据 |
|---|---|---|---|
| **用户手动保存** | Rider 保存 → 脚本调用 `ue-graph reindex <file>` | ✅ **可靠** | `TsetNew1111` 手写后检索命中，方法/签名/位置全部正确 |
| **File Watcher 自动检测** | 外部文件变更自动捕获 | ⚠️ 不可靠 | 新增符号偶尔捕获（`Marker5` 命中），改名/删除不同步（`Marker6` 5 秒后仍搜不到） |
| **AI 调 `reindex_file`** | MCP 工具主动刷新 | ✅ 对磁盘源可靠 | `Marker3→Marker4` 改名后 reindex，旧名消失、新名出现 |
| **reindex 清变更记录** | — | ❌ 无效 | `Marker5` 磁盘已删，reindex 后仍在索引；用户拒绝变更后消失 |

**保存脚本细节**（`F:\ue-codegraph-mcp\reindex-file.bat`）：
- Rider 保存时以 `%~1` 传入文件路径，仅对源文件（`.cpp/.cxx/.cc/.h/.hpp/.inl`）执行 `ue-graph reindex`。
- 全程写日志到 `F:\ue-codegraph-mcp\watcher.log`，可据此核对"保存是否真的触发了刷新"。

---

## 四、幽灵符号判定与排查

幽灵符号 = **索引里有、磁盘上没有** 的符号（来自变更文件记录残留）。

| 判定方法 | 操作 | 结论 |
|---|---|---|
| 单符号验证 | `search_symbols` 得文件+行号 → 磁盘直读对比 | 磁盘无 → 幽灵 |
| 批量扫描 | 遍历可疑符号（`Test`/`TEMP` 前缀、近期变更文件）逐个对比 | 输出幽灵清单 |
| 源头分析 | 解析 `local.db` 变更记录（`fullDiffText`/`fileStatus`），列出已删除/已拒绝但仍被索引的符号 | 最彻底 |

**清除手段**：在 IDE 中接受/拒绝对应变更记录，幽灵自动消失（实测 `Marker5` 拒绝后索引即干净）。

---

## 五、最终工作流约定（必须遵守）

### AI 侧（每次编辑后）
1. 用 SearchReplace/Write 编辑项目文件（磁盘变更 + 产生变更记录）。
2. **立即调用 `reindex_file`** 刷新该文件；头文件改动需连带 reindex 引用它的文件。
3. 验证/检索一律用 ue-codegraph（`search_symbols` / `search_code`）。

### 用户侧
4. 在 IDE 中接受/拒绝变更 → 清除变更文件记录 → 消除幽灵符号风险。

### 兜底
5. 磁盘直读（`Get-Content -Encoding UTF8`）**仅当**索引结果明显可疑或冲突时使用，作为最终裁决；不作为例行验证（避免额外 token 消耗）。

---

## 六、tokens 效率原则

- 验证与检索**默认走 ue-codegraph**，不例行磁盘直读。
- 磁盘直读降级为"最后手段"，只在索引与预期冲突、无法裁决时出现。
- 编辑 → reindex → 检索 三步闭环即可覆盖绝大多数场景。

---

## 七、风险与注意事项

1. **File Watcher 不可作为依赖**：新增偶尔生效、改名/删除不同步，容易造成"以为刷新了实际没有"的误判。
2. **reindex 清不掉变更记录幽灵**：编辑器外改代码（AI 编辑）尤其要配合用户侧"接受/拒绝变更"。
3. **`Last indexed` 时间戳不更新属正常**：它记录全量批次时间，增量 reindex 不改变，不代表索引未更新。
4. **引擎源码索引长期稳定**：`unreal-engine-*` codebase 可直接信任，无需上述流程。

---

## 八、Rider + UE AI 全工作流（v2.0 完善版）

> 2026-10-02 基于全量实测（jetbrains-rider 内置 MCP 93 工具、ue-codegraph 16 工具）固化。本节与一~七节共同构成完整工作流契约。

### 8.1 工具链分工与优先级

| 工具链 | 定位 | 负责范围 |
|---|---|---|
| ue-codegraph | UE 语义分析 | 符号/宏与 specifier、蓝图暴露与接口、调用方/被调方、文件依赖、C++ 继承树、项目聚焦检索、索引维护 |
| jetbrains-rider | IDE 与编辑器侧 | 实时诊断、VCS、终端、重构、格式化、调试全套（xdebug）、UE 编辑器联动（ue_*）、截图、资产查询 |

- 语义问题优先 codegraph（仅项目、无引擎噪音）；需实时诊断/精确定位/编辑器状态时用 rider；均不可用或结果可疑时降级磁盘直读（仅作最终裁决）。
- 前提：Rider 运行中；`ue_*` 类工具还需 UE 编辑器运行且 RiderLink 已连接（先 `ue_health` 检查）。

**常用速查**：

- 读资产属性用 `find_default_value_overrides`（`get_asset_properties` 恒空不可用）；找派生蓝图用 `get_class_hierarchy`（rider 版查蓝图子类，codegraph 同名工具查 C++ 继承树）。
- `search_assets` 的 query 为按下划线分词的整词匹配（"FarmlandActor"✓ / "Farmland"✗）；按类找资产优先用 baseClass。
- IDE 终端执行：`execute_terminal_command`；动态调用任意工具：`execute_tool`（"工具名 --param value"）。
- 代码检索注意 `codebase_id`：项目 = 7，默认 1 是引擎区；索引库中 [5] 为旧配置残留可忽略。

### 8.2 代码修改闭环

编辑（SearchReplace/Write）→ **立即 `reindex_file`**（头文件连带）→ codegraph 检索验证 → 用户在 IDE 接受/拒绝变更（清幽灵符号）。

实时检查：`get_file_problems` / `lint_files`（支持多文件 + min_severity）；需要 hook 式质量门时用 `post_edit_quality_check`（reformat=false + run_inspections=true 为只读分析）。

### 8.3 PIE 运行时调试闭环（实测通过）

| 步骤 | 操作 | 要点 |
|---|---|---|
| 1 | 断点设在 PIE 世界必然重初始化的路径（如 `UISMManager::Initialize`） | 勿选编辑器/游戏共用 Tick——编辑器世界会先命中 |
| 2 | `ue_play(action=play)` 启动 PIE | 返回动作前状态；状态串 Idle/Play/Pause |
| 3 | `xdebug_control_session(WAIT_FOR_PAUSE)` | PIE 实例与编辑器世界实例是不同对象 |
| 4 | 现场检查：get_stack / get_frame_values / get_value_by_path / evaluate_expression | 读成员可靠；调引擎函数可能因优化失败（Identifier not available） |
| 5 | 移除断点 + RESUME → `ue_status` 确认 playState=Play | 运行中可再次 PAUSE 抓帧（健康态常停在 FFrameEndSync::Sync） |
| 6 | `take_screenshot`（editor_window）看运行画面 → `ue_play(stop)` 收尾 | — |

调试会话由运行配置 "ProjectB" 拉起（ProjectBEditor，约 56 秒启动）。

### 8.4 视觉验证闭环（截图即眼睛）

- 三模式实测：**editor_window 最可靠**；asset_preview 需 Long package path（缓存空→透明图，用 forceLive=true）；**viewport 模式不稳定**（启动期/PIE 中均可能失败），一律改用 editor_window。
- **灰面纠正法**：forceLive 渲染发灰 ≠ 缺材质（NeverFlush 不等异步加载）——先 `ue_execute_python` 显式 load_asset 各材质槽，再重截即上色。
- **远程打开任意资产**：`unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([asset])` → 截图查看。StateTree 等非蓝图资产的结构化数据在 5.5 Python 层不可读（EditorData 为 protected 属性），视觉读取是唯一可行通道。
- 截图落盘：`<项目>/Saved/Screenshots/WindowsEditor/RiderMCP/`。

### 8.5 红线清单（必须遵守）

1. **`find_call_chain` 永久禁用**——任何调用必冻结 ue-codegraph 实例（两轮复现）；调用链分析改用 `find_callers`/`find_callees` 逐级展开。
2. **`build_solution_start` 严禁无参调用**——对 UE 项目等于构建整个 .sln 的 150+ 引擎目标；需要构建用 `filesToRebuild` 指定文件或走日常 UBT 流程。
3. **所有 MCP 调用逐条串行**——Python 通道工具（`ue_execute_python`、`ue_export_blueprint_nodes` 等）与其他调用并行时必冻结编辑器（2/2 复现）。
4. **`xdebug_ignore_exception` 勿调用**——.NET 专用但在 UE 项目也真实生效，会持久修改异常断点设置且无精细撤销。
5. **蓝图内容不读文本**——导出噪声大且无必要；需要时以截图查看（配合 8.4）。
6. 调试用断点均标 agent 归属并用后即删（`xdebug_remove_breakpoint` 默认只删 agent，无损往返已验证）。

### 8.6 故障处理手册

**RiderLink 冻结**（识别：`ue_*` 调用 40504 超时 + 编辑器窗口无响应 + 调试会话 paused 于 RiderLink/RD 源码）：

- 断点静音无效、RESUME 无效（同一地址复停）；唯一解 = **STOP 结束调试会话**（编辑器进程随之关闭）→ 重启编辑器，RiderLink 自动重连。

**ue-codegraph 失联**（识别：全部调用 40504 超时、进程假死/内存缓慢增长）：在 MCP 面板刷新重拉实例；同机多客户端各自持有独立实例（按父进程区分，互不影响）。

**连接配置**：jetbrains-rider 走 SSE 直连（端口由 Ktor 动态分配，Rider 重启后如失联，对照 idea.log 重新探测并更新 mcp.json）。

### 8.7 环境前提速查

- Rider 2026.2.3+（内置 MCP Server 需在 Settings | Tools | MCP Server 启用）
- ue-codegraph：独立 Node 服务（stdio 接入）；索引 codebase = 项目 [7] + 引擎五区（[1]~[4]、[6]；[5] 为旧残留）
- UE 5.5.4 源码版；编辑器需运行且已装 RiderLink

---

## 九、版本记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-08-31 | 定稿：双源机制、三通道实测、AI 编辑后强制 reindex 约定、幽灵符号判定 |
| v2.0 | 2026-10-02 | 扩展第八节：Rider + UE AI 全工作流（工具链分工 / PIE 调试闭环 / 视觉验证 / 红线清单 / 故障手册） |
