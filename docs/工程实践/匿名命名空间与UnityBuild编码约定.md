# C++ 内部链接与 Unity Build 编码约定

> 本文面向本项目的 C++ 开发约定：解释"为什么 .cpp 里的辅助函数要写成 `static` 或匿名 namespace"，
> 并给出写法选择判据与常见陷阱。结论先行，便于速查。

---

## 一、结论先行

1. **UE 项目的硬性约束**：.cpp 中"仅本文件使用"的自由函数与全局变量，**必须**用 `static` 或匿名 namespace 赋予内部链接——否则在 Unity Build 下可能直接编译失败
2. **等价关系**：匿名 namespace ≡ 顶层 `static` 自由函数；**≠ 类的静态成员函数**（后者是外部链接）
3. **定位**：这是**正确性与封装实践**（防冲突 + 信息隐藏），**不是性能优化**（运行时零影响）

---

## 二、基础：TU 与链接属性

### 2.1 翻译单元（Translation Unit, TU）

```
一个 .cpp 文件 + 它 include 的所有头文件展开 = 一个翻译单元
```
编译器以 TU 为单位独立编译，每个 TU 产出一个 `.obj`。10 个 .cpp = 10 个 TU = 10 次编译调用。

### 2.2 外部链接 vs 内部链接

| 链接属性 | 含义 | 默认情况 |
|---|---|---|
| **外部链接** | 符号导出到全局符号表，可被其他 TU 引用 | C++ 函数/全局变量的**默认** |
| **内部链接** | 符号仅在本 TU 可见，不导出 | 需要显式声明 |

```cpp
// 外部链接（默认）——符号进入 .obj 符号表
void Helper() {}

// 内部链接——不导出符号
namespace { void Helper() {} }     // 匿名 namespace（现代推荐写法）
static void Helper() {}            // 顶层 static（等价的老式写法）
```

### 2.3 符号冲突（常规构建下的经典问题）

```cpp
// FarmUtils.cpp
void UpdateMovement(...) { ... }   // 忘记 static

// Satellite.cpp（完全无关的系统）
void UpdateMovement(...) { ... }   // 忘记 static，同签名
```

两个 TU 编译各自都能通过，但**链接阶段**发现同名外部符号 → `LNK2005 重复定义`。

> 注意：冲突条件是"**同名 + 同签名**"（名字修饰后符号相同）；同名不同参数是合法重载，不直接冲突，但跨文件"意外重载"仍是隐患。

---

## 三、Unity Build：UE 的编译加速与配套约定

### 3.1 机制

UE 默认开启（UBT 的 `bUseUnityBuild = true`）。构建时生成"Unity 文件"，将同模块的多个 .cpp **拼接**成一个 TU 编译：

```
常规构建： GridUtils.cpp → 编译、Satellite.cpp → 编译（N 个文件 N 次编译）
Unity 构建： Unity.1.cpp = #include "GridUtils.cpp"
                          #include "Satellite.cpp"
                          ...（几十个 cpp 拼成一个 TU，一次编译）
```

**目的**：大幅减少编译调用次数（UE 大项目编译优化的核心手段）。

### 3.2 副作用（关键）

多个 .cpp 现在处于**同一编译单元**：

```cpp
// Unity 文件（同一 TU）里：
void UpdateMovement(...) {...}   // 来自 GridUtils.cpp
void UpdateMovement(...) {...}   // 来自 Satellite.cpp
// → 同一 TU 内重定义 → 直接编译错误 ❌
```

**原本"跨 TU 的链接期问题"，变成"同 TU 的编译期错误"**——爆得更早、更必炸。

### 3.3 因此形成的 UE 约定

> Epic 官方编码规范：.cpp 中不需要跨文件使用的自由函数/全局变量，必须用 `static` 或匿名 namespace。

引擎自身代码大量遵循此约定（这也是引擎源码里随处可见这类 `static` 函数的原因）。

---

## 四、写法选择判据

### 4.1 五种写法对比

| 写法 | 链接属性 | 头文件改动 | 冲突风险 | 适用性 |
|---|---|---|---|---|
| **匿名 namespace（.cpp 内）** | 内部 | 无 | 无 | ✅ 单文件实现细节首选 |
| 顶层 `static` 自由函数 | 内部 | 无 | 无 | ✅ 等价（老式写法） |
| 类普通成员函数 | 外部（类限定） | +1 行声明 | 无 | 需要访问类状态时 |
| 类 `static` 成员函数 | 外部（类限定） | +1 行声明 | 无（符号含类名） | 需类作用域但无 this 时 |
| 顶层自由函数（无 static） | **外部** | 无 | **有** | ❌ 禁止 |

### 4.2 选择判据（三个问题）

```
Q1: 函数依赖类的状态（需要 this / 私有成员 / 基类能力）吗？
    ├─ 是 → 类成员函数（普通成员）
    └─ 否 → 继续 Q2
Q2: 函数被多个文件（多个 TU）复用吗？
    ├─ 是 → 头文件中的公共函数（inline 或声明+实现）
    └─ 否 → 继续 Q3
Q3: 仅单个 .cpp 使用，且无类状态依赖？
    → 匿名 namespace 自由函数（本文件实现细节首选）
```

### 4.3 项目实例对照

```cpp
// 单消费者 + 纯函数 → .cpp 匿名 namespace
// SatelliteOrbitMovementProcessor.cpp
namespace { void UpdateOrbitMovement(FMassOrbitFragment& Orbit, float DeltaTime) {...} }

// 多消费者（注册组件 + 渲染处理器）→ 头文件 inline 公共函数
// SatelliteMassFragments.h
FORCEINLINE FVector ComputeOrbitWorldPosition(...) {...}
```

---

## 五、常见陷阱

### 5.1 头文件里写匿名 namespace（反模式）

```cpp
// ❌ 错误：头文件中的匿名 namespace
namespace { void Helper() {} }
```

每个 include 它的 .cpp 都会获得**一份独立副本**：
- 代码膨胀（同一实现被编译进每个 TU）
- "改一处不生效"的错觉（各 TU 的副本相互独立）
- 正确做法：匿名 namespace 只用**在 .cpp 里**

### 5.2 术语混淆："static 方法" ≠ 内部链接

```cpp
static void Foo() {}                    // 顶层 static：自由函数，内部链接 ✅
namespace { void Foo() {} }             // 等价 ✅
class C { static void Foo(); };         // 类静态成员函数：外部链接（符号含类名 C::Foo）
```

只有**顶层 `static` 自由函数**与匿名 namespace 等价；类的静态成员函数**不能**用来保护"仅文件内"的符号。

### 5.3 全局变量同样受约束

`.cpp` 顶层的全局变量默认外部链接 → 同样会引发跨 TU 冲突，同样需要 `static` / 匿名 namespace。（`const` 变量默认内部链接，是例外。）

### 5.4 Unity 报错行号

Unity 构建下的重定义错误，行号可能指向拼接生成的中间文件（Intermediate 下的 Unity 文件），而非你的源文件——排查时以错误信息中的原始文件名为准。

### 5.5 同名匿名函数也会在 Unity 下“意外重定义”（2026-10-06 实例）

匿名 namespace 防的是【跨 TU 链接冲突】，但【防不了 Unity 合并后的同 TU 重定义】：
同一翻译单元中的多个匿名 namespace 块属于**同一个匿名命名空间**——两个 .cpp 的
`namespace {}` 里定义了同名同签名函数，被 Unity 拼进同一 TU 后即 `C2084: 函数已有主体`。

```
实例：NormalizeAngleDegrees360 曾在 SatelliteStateTreeTasks.cpp 与
SatelliteManagerComponent.cpp 各持一份（实现完全相同）——某次 Unity 分组抖动（新增文件/
Adaptive Unity 重排）后两者拼入同一 unity 文件 → 编译阻断（后续“未声明标识符”均为连锁报错）。
修复：提取为 UPlanetBlueprintFunctionLibrary 公共函数（单一实现）。
```

**结论**：跨文件重复的小工具（哪怕都放匿名 namespace）仍是隐患——
同一模块内出现**第二份相同实现**时，应立即提取为公共函数（见 4.2 Q2：被多个文件复用 → 头文件公共函数），
而不是再复制一份匿名副本。

---

## 六、澄清：它不是"性能优化"

| "优化"语义 | 结论 |
|---|---|
| 运行时性能 | **无影响**（调用开销、内存布局、执行速度均相同） |
| 构建期 | 收益极小（符号表少几条、链接略快），核心价值是**防错** |
| 编译器优化机会 | 理论上有（内部链接 → 更激进的内联/去虚化），实际可忽略：函数定义在同一 .cpp 内编译器本就可见；UE 未开 LTO 时差异仅存在于跨 TU 场景，而匿名 ns 函数本就单 TU 使用 |

**正确定位**：健壮性/封装实践（durability practice），归类上接近"防错"与"信息隐藏"，而非"优化"。

---

## 七、速查表

```
新建一个 .cpp 辅助函数，先问三件事：

1. 需要 this / 类状态？          → 类成员函数
2. 被多个文件复用？               → 头文件公共函数（inline）
3. 仅本文件 + 无类状态依赖？      → 匿名 namespace { ... }（首选）
                                    或顶层 static（等价）

绝对禁止：.cpp 顶层裸写自由函数/全局变量（无 static、无匿名 ns）
绝对避免：头文件里写匿名 namespace
```

---

## 八、参考

- Epic C++ Coding Standard：内部实现使用 `static` 或匿名 namespace
- 引擎实例：`MassStateTreeProcessors.cpp`（`UE::MassBehavior::ForEachEntityInChunk` 被 3 个 Processor 类共用，为命名空间自由函数）、`MassDebugger.cpp`（匿名 namespace 辅助函数）
- 项目实例：`SatelliteOrbitMovementProcessor.cpp` / `SatelliteMassFragments.h`
