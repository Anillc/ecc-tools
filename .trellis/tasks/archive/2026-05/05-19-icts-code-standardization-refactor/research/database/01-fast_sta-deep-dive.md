# Research: fast_sta Deep Dive

- **Query**: 深入剖析 `source/database/adapter/fast_sta/` 的文件清单、接口分布、内部依赖、命名问题、.hh/.cc 分布合理性
- **Scope**: internal (`src/operation/iCTS/source/database/adapter/fast_sta/`)
- **Date**: 2026-05-19

---

## 1.1 完整文件清单

目录绝对路径：`src/operation/iCTS/source/database/adapter/fast_sta/`
总计 28 个源文件（13 个 .hh + 14 个 .cc + 1 个 CMakeLists.txt）≈ 4949 行，逐文件行数：

### 按职责分类

| 职责类别 | 文件 | 行数 | 内容 |
|---|---|---|---|
| **门面 (Facade)** | `FastSta.hh` | 103 | 单例 `FastSTA`，21 个公共静态 API |
| | `FastSta.cc` | 389 | 转发到内部子系统，日志/计时包装 |
| **共享数据类型** | `FastStaTypes.hh` | 382 | 24 个 struct / 6 个 enum / 5 个 id type alias / 5 个 kInvalid 常量 |
| | `FastStaTypes.cc` | 122 | 仅 `FastStaLibertyTable::lookup` 实现（多维表插值） |
| **初始化** | `FastStaBuilder.hh` | 48 | `buildClockContext` × 2 + `injectNetRouteTree` |
| | `FastStaBuilder.cc` | 234 | 注入 STA 句柄、Config、ClockLayout |
| | `FastStaClockTree.hh` | 45 | `buildFromClock` / `buildFromClockLayout` / `applyLayoutParasitics` |
| | `FastStaClockTree.cc` | 202 | 从 `Clock`/`ClockLayout` 构建节点/网络 |
| | `FastStaLiberty.hh` | 40 | `snapshotBufferCell` |
| | `FastStaLiberty.cc` | 356 | 从 iSTA `LibCell` 拷贝表格、单位换算 |
| **特性化** | `FastStaChar.hh` | 40 | `buildContext` / `setLoad` / `runSample` |
| | `FastStaChar.cc` | 351 | 单 buffer 链拓扑构造 + 单点采样 |
| **DMP Ceff 求解器**（5 个文件） | `FastStaDmpCeff.hh` | 43 | `calcDriverTiming` / `calcLoadDelaySlew` / `calcInputPortDelaySlew` |
| | `FastStaDmpCeff.cc` | 200 | 负载侧波形 vl/vl0、阈值穿越查找 |
| | `FastStaDmpCeffInternal.hh` | 169 | `namespace fast_sta_dmp` 暴露 `DmpSolver` 类 + 8 个自由函数 + 常量 |
| | `FastStaDmpCeffShared.cc` | 197 | `DmpExp` / `FindRoot` / 阈值/derate 工具 + 表查询 |
| | `FastStaDmpCeffSolver.cc` | 247 | `DmpSolver` 构造/`solve()`/`solveCap/Pi/ZeroC2`/`initPi/ZeroC2` |
| | `FastStaDmpCeffEquations.cc` | 391 | `DmpSolver::y/y0/dy/evalPi/evalOnePole/newtonRaphson/lu*` 等 |
| **增量更新** | `FastStaIncremental.hh` | 45 | `changeBufferMaster` × 2 + `changeBufferMasterIncremental` |
| | `FastStaIncremental.cc` | 227 | BFS 标脏域、buffer master 替换 |
| **寄生** | `FastStaParasitics.hh` | 50 | `updateNetLoads` × 2 + `buildNetParasiticFromSegments/RouteTree` + `reduceToPiElmore` |
| | `FastStaParasitics.cc` | 426 | RC 邻接 + 矩量法 Pi/Elmore reduction（最长文件） |
| **时序传播** | `FastStaTiming.hh` | 40 | `update` / `updateRegion` / `calcSkew` |
| | `FastStaTiming.cc` | 310 | 拓扑队列传播 + skew 汇总 |
| **功耗** | `FastStaPower.hh` | 40 | `update` / `updateRegion` |
| | `FastStaPower.cc` | 170 | 开关功耗 + 内部功耗 + 漏功耗求和 |
| **报告** | `FastStaReport.hh` | 38 | 仅 1 个函数 `logClockSummary` |
| | `FastStaReport.cc` | 44 | 1 个时钟摘要日志函数 |

### "对外接口" vs "内部实现" 划分

唯一的"对外接口"是 `FastSta.hh` 中的 `FastSTA` 单例类。然而，CMake 配置使整个目录 `${ICTS_DATABASE_ADAPTER_FAST_STA}` 通过 `PUBLIC` 暴露（`CMakeLists.txt:34-37`），导致所有 13 个 .hh 都对外可见，外部代码可以直接 include 任何子模块的头文件。

外部调用的实际证据：
- `flow/optimization/preparation/OptimizationPreparation.cc:271` 直接调用 `FastStaBuilder::injectNetRouteTree` —— **绕过门面**
- `flow/optimization/state/OptimizationState.cc:71` `flow/optimization/model/OptimizationTypes.hh:79` 等使用 `FastStaSlewRole`、`FastStaNodeKind`、`FastStaClockContext` 等类型 —— Types 必须可见

---

## 1.2 FastSta.hh / FastSta.cc 分析

### `FastSTA` 类暴露的 21 个 public 静态接口

| 序号 | API | FastSta.hh 行 | 外部调用？ |
|---|---|---|---|
| 1 | `buildClockContext(clock)` | 62 | 否（外部用 layout 版本） |
| 2 | `buildClockContext(clock, layout, idx)` | 63 | 是 (Optimization.cc:101) |
| 3 | `rebuildClockContext(clock_id)` | 64 | 否 |
| 4 | `eraseClockContext(clock_id)` | 65 | 是 |
| 5 | `clear()` | 66 | 仅测试 (FastSTATest.cc:218-219) |
| 6 | `buildCharContext(spec)` | 68 | 是 (CharBuilderCircuit.cc:86) |
| 7 | `eraseCharContext(id)` | 69 | 是 |
| 8 | `setCharLoad(id, load)` | 70 | 是 |
| 9 | `runCharSample(id, slew)` | 71 | 是 (CharBuilderSlewSampling.cc:49) |
| 10 | `changeBufferMaster(...)` 单点版本 | 73 | 否 |
| 11 | `changeBufferMasters(...)` 批量 | 74 | 是 (OptimizationSolver.cc:108) |
| 12 | `changeBufferMastersTimingOnly` | 75 | 是 (OptimizationSolver.cc:117) |
| 13 | `updateTiming(clock_id)` | 76 | 是 |
| 14 | `updatePower(clock_id)` | 77 | 是 |
| 15 | `querySinkArrival(...)` | 79 | 否 |
| 16 | `querySkew(clock_id)` | 80 | 是 (OptimizationState.cc:149) |
| 17 | `queryNodeSlew(...)` | 81 | 否 |
| 18 | `queryNetLoad(...)` | 82 | 否 |
| 19 | `queryCapStatus(...)` | 83 | 是 (OptimizationState.cc:50) |
| 20 | `querySlewStatus(...)` | 84 | 是 |
| 21 | `queryPower(clock_id)` | 85 | 是 |
| 22 | `queryArea(clock_id)` | 86 | 否 |
| 23 | `queryClockContext(clock_id)` | 87 | 是 |
| 24 | `mutableClockContext(clock_id)` | 88 | 是 (OptimizationPreparation.cc:239) |
| 25 | `queryClockIds()` | 89 | 否 |

**真正需要对外的 API 共 16 个**。`queryNodeSlew`、`queryNetLoad`、`queryArea`、`querySinkArrival`、`queryClockIds`、`changeBufferMaster` 单点、`rebuildClockContext` 这 7 个未被外部使用，可下沉到 cc 内部 namespace 或删除。`clear()` 仅测试代码用，建议移到 friend `FastStaTestAccess`。

### 数据成员（`FastSta.hh:97-100`）

```cpp
std::vector<FastStaClockContext> _clock_contexts;
std::vector<bool> _clock_context_valid;
std::vector<FastStaClockContext> _char_contexts;
std::vector<bool> _char_context_valid;
```

注意：clock 上下文与 char 上下文用同一 `FastStaClockContext` 类型，但 char 走的是 `FastStaChar::buildContext()` 构造的"线形 buffer 链"特殊形状，导致 `FastStaClockContext` 这一名字混淆了"真实时钟"与"特性化测试电路"。

---

## 1.3 内部组件依赖关系

### DMP Ceff 五件套

```
FastStaDmpCeff.hh         (公共 3 个静态方法接口)
   |
   v
FastStaDmpCeff.cc         (vl/vl0/findVlCrossing/applyThresholdAdjust + Facade 转发)
   |
   v
FastStaDmpCeffInternal.hh (namespace fast_sta_dmp + DmpSolver 类全声明 + 8 个自由函数)
   |
   +---FastStaDmpCeffShared.cc    (OutputThreshold/InputThreshold/SlewDerate/DmpExp/FindRoot)
   +---FastStaDmpCeffSolver.cc    (DmpSolver 构造 + solveCap/solvePi/solveZeroC2 + initPi/initZeroC2 + makeResult)
   +---FastStaDmpCeffEquations.cc (DmpSolver::y/dy/evalPiEquations/newtonRaphson/luDecomp/luSolve/findDriverDelaySlew/vo/v0/voCrossingUpperBound + allFinite)
```

`FastStaDmpCeffInternal.hh` (169 行) 是为了让 `DmpCeff.cc` / `DmpCeffShared.cc` / `DmpCeffSolver.cc` / `DmpCeffEquations.cc` 共享同一个 `DmpSolver` 类与工具函数。`DmpSolver` 是一个 64 字节级别的私有求解器对象，本不该出现在 .hh 公共目录中。

### 顶层关系（Builder 视角）

```
FastSta (Facade)
 │
 ├─> FastStaBuilder         ── 构造 FastStaClockContext
 │     ├─> FastStaClockTree         ── 拷贝 Clock/ClockLayout 拓扑
 │     ├─> FastStaLiberty           ── 从 iSTA LibCell 拷贝表格
 │     └─> FastStaParasitics        ── 计算 net 负载
 │
 ├─> FastStaChar           ── 构造"特性化电路"上下文
 │     ├─> FastStaLiberty
 │     ├─> FastStaParasitics
 │     ├─> FastStaTiming
 │     └─> FastStaPower
 │
 ├─> FastStaIncremental    ── 增量改 buffer master + BFS 标脏
 │     └─> FastStaLiberty
 │
 ├─> FastStaTiming         ── 拓扑序传播
 │     ├─> FastStaParasitics
 │     └─> FastStaDmpCeff
 │
 ├─> FastStaPower          ── 开关 + 内部 + 漏功耗
 │
 ├─> FastStaParasitics     ── RC reduction
 │     └─> STAAdapter      (wire R/C 单位查询)
 │
 ├─> FastStaDmpCeff        ── 驱动/负载延迟与 slew
 │     └─> namespace fast_sta_dmp (DmpSolver)
 │           ├─ FastStaDmpCeffSolver   (求解流程)
 │           ├─ FastStaDmpCeffEquations(Newton-Raphson + LU)
 │           └─ FastStaDmpCeffShared   (查表 + DmpExp + FindRoot)
 │
 └─> FastStaReport         ── 仅日志摘要
```

### 各文件职责

- **FastStaBuilder**: 协调 ClockTree → Liberty → Parasitics 三步初始化，加日志
- **FastStaChar**: 特性化测试电路（命名为 `cts_char_source/Y` 等约定名）的构造与样本
- **FastStaClockTree**: 从 `Clock` 与 `ClockLayout` 拷贝节点/网络拓扑
- **FastStaIncremental**: buffer master 替换 + BFS 求 `FastStaDirtyRegion`
- **FastStaLiberty**: 把 iSTA `LibCell` 表格 / 阈值 / 漏功耗"拍快照"成 `FastStaLibertyCell`
- **FastStaParasitics**: RC 段/路由树 → 矩量法 Pi 模型 + Elmore（最大文件 426 行）
- **FastStaPower**: 静态 + 内部 + 开关功耗 + 漏功耗
- **FastStaReport**: 单一日志函数
- **FastStaTiming**: 拓扑 BFS 传播；驱动用 DMP，负载用 wire delay + slew
- **FastStaTypes**: 全局类型集合（24 struct + 6 enum + 5 alias + lookup 单实现）

---

## 1.4 命名问题

### `FastStaDmpCeffInternal.hh` 的 "Internal"

含义：该头文件声明了 `namespace icts::fast_sta_dmp`，仅被另外 4 个 cc 文件（DmpCeff.cc / DmpCeffShared.cc / DmpCeffSolver.cc / DmpCeffEquations.cc）include。"Internal" 用于阻止外部使用——但仅依赖文件名约定，缺乏物理隔离。

为什么需要存在：因为 `DmpSolver` 类（96 行成员声明）跨 3 个 .cc 文件实现（Solver、Equations），所以必须把类声明放在 .hh 里。这是一个"为了拆分 .cc 而被迫拆出的内部 .hh"。

### "DmpCeff" 缩写

- **DMP** = Driver Model with Pi Load（OpenSTA 沿用 Synopsys "DMP" 论文术语，参见 Gupta-Krauter-Underwood `Modeling the Output Driver` 文献，CCS/ECSM 之前的工业标准 reduction 算法）
- **Ceff** = Effective Capacitance（有效电容，将 Pi 模型等效为单电容用以查 NLDM 表）
- 合在一起 "DmpCeff" 对 CTS 领域专家是清晰的，但对一般阅读者完全不透明

CTS 语义化命名候选：`DriverModel`、`PiLoadSolver`、`EffectiveCapacitanceSolver`、`OpenStaCompatibleDriver`

### 其他命名问题

| 当前 | 问题 | CTS 语义化建议 |
|---|---|---|
| `FastSta.hh` 单例 + `FAST_STA_INST` 宏 | "Fast" 是相对说法（与谁 fast？答：相对 iSTA 完整 STA） | `CtsTimingFacade` / `CtsLightweightStaFacade` |
| `FastStaTypes.hh` 单大堆 | 24 个 struct 全堆在一个文件，无明显分类 | 按子领域拆为 `Types/Topology.hh`、`Types/Liberty.hh`、`Types/Parasitic.hh`、`Types/Timing.hh`、`Types/Power.hh` |
| `FastStaDmpCeffShared.cc` 的 "Shared" | 同样的互联网化命名（≈ `Common`/`Util`） | 直接按内容命名：`FastStaDmpCeffNumerics.cc`、`FastStaDmpCeffLibertyLookup.cc` |
| `FastStaDmpCeffEquations.cc` | "Equations" 描述了内容，但只是 DmpSolver 的方法实现 | 合理但应改文件名为 `FastStaDmpCeffSolverNewton.cc` 等更具体 |
| `FastStaSlewRole` | "Role" 暗示用户/角色，但实际是节点类型 | `FastStaSlewKind` (与 `FastStaNodeKind` 对齐) |
| `FastStaDirtyRegion` | "Dirty Region" 是图形学/缓存术语 | `FastStaInvalidatedScope` / `FastStaAffectedSubgraph` |
| `snapshotBufferCell` / `snapshotClockData` | "snapshot" 互联网化 | `extractBufferCell` / `captureBufferCell` / `materializeBufferCell` |
| `FastStaLiberty` 的注释 "Liberty data snapshots extracted" | 自带 "snapshot" 描述 | 用 "Liberty data captured" 即可 |

---

## 1.5 .hh / .cc 分布合理性

### .hh 文件行数排序

| 文件 | 行数 | 评估 |
|---|---|---|
| `FastStaTypes.hh` | **382** | 超出 200 行阈值（警示）。但都是 POD struct，移到 .cc 不现实；应按子领域拆为多个 Types 头 |
| `FastStaDmpCeffInternal.hh` | **169** | 暴露 `DmpSolver` 类（64 字节级私有对象）+ 17 个常量/函数，本应只是 .cc 局部 |
| `FastSta.hh` | 103 | 单例 + 21 API 静态方法 + 4 个 vector 数据成员，可接受 |
| 其他 .hh | 38-50 | 都是单类 + ≤3 个静态方法，合理 |

### "Internal" 文件本应是 .cc 私有结构

`FastStaDmpCeffInternal.hh` 的存在原因纯粹是技术性的——`DmpSolver` 实现拆分到了 3 个 cc 文件，所以类声明必须共享。这是 .cc 切分策略的副作用，**不是真正的 API 设计**。

更好的替代方案：

1. **方案 A（推荐）**：把 `DmpSolver` 整个写在一个 .cc 文件里（约 700 行），让 `Internal.hh` 完全消失
2. **方案 B**：把 dmp_ceff 独立为子目录 / 子 cmake target，`Internal.hh` 改名为 `DmpSolver.hh` 并仅在子目录内部使用，对外只导出 `FastStaDmpCeff.hh` 的三个静态方法

### .cc 文件行数排序

| 文件 | 行数 | 评估 |
|---|---|---|
| `FastStaParasitics.cc` | **426** | 最大。RC adjacency + 矩量法 reduction，建议拆出 `FastStaParasiticsReduction.cc` |
| `FastStaDmpCeffEquations.cc` | 391 | DmpSolver 求解步骤，已是拆分后 |
| `FastSta.cc` | 389 | 门面转发 + 日志包装，可压缩到 ~200 行 |
| `FastStaLiberty.cc` | 356 | Liberty 表拷贝，已合理 |
| `FastStaChar.cc` | 351 | 特性化拓扑构造（240+ 行 buildContext），可拆 builder/sample |
| `FastStaTiming.cc` | 310 | 时序传播，合理 |
| `FastStaDmpCeffSolver.cc` | 247 | DmpSolver 入口，已合理 |
| `FastStaBuilder.cc` | 234 | 初始化协调，合理 |
| `FastStaIncremental.cc` | 227 | BFS 标脏，合理 |
| `FastStaDmpCeffShared.cc` | 197 | 数值工具，已合理 |
| `FastStaDmpCeff.cc` | 200 | 负载波形 + 入口转发，合理 |
| 其他 | < 200 | 合理 |

---

## 关键观察总结

1. **门面破例**：`FastStaBuilder::injectNetRouteTree` 被 `flow/` 层直接调用（`OptimizationPreparation.cc:271`），绕过了 `FastSta.hh` 门面
2. **类型暴露不可避免**：`FastStaSlewRole` / `FastStaNodeKind` / `FastStaClockContext` 等在 `flow/optimization/` 多处被引用，是事实上的对外类型
3. **Internal.hh 是 .cc 拆分的副作用**，不是 API 设计
4. **CMakeLists.txt:33-42** 的 `target_include_directories` 用 `PUBLIC ${ICTS_DATABASE_ADAPTER_FAST_STA}` 等价于把整个目录公开，外部 include 任何子文件不会被构建系统拦截
5. 21 个公共 API 中有 7 个未被外部使用，存在"过度暴露"
6. `FastStaTypes.hh` 单文件 382 行容纳 24 个 struct，是高内聚还是耦合在一起，存争议
