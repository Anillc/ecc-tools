# iCTS 代码规范化大规模重构调研

> **状态**：Phase 1 Planning · 仅完成 prd.md，等待 design.md / implement.md 阶段
> **代码范围**：`src/operation/iCTS/`（source + api + external_libs + test，共 436 个 C++ 文件）
> **调研材料**：见同目录 `research/{flow,module,database,cross-cutting}/` 30 份 markdown

---

## 1. Goal

为 iCTS 建立**统一的代码规范**，使 flow / module / database / utils / api 五层在以下维度都"按 CTS 业务语义自然组织"：

1. **行为范式统一**：每个 sub-flow / sub-module 的入口、阶段、状态、返回值、错误信号都遵循同一套契约。
2. **命名 CTS 语义化**：消除互联网/Web/SaaS/通用框架风格命名（`Internal` / `Snapshot` / `Support` / `Request` / `Wrapper` / `Helper` / `Engine` / `Polish` / `Draft` / `Desc` / `Manager` 等），改为 CTS / EDA 领域术语。
3. **模块物理边界清晰**：大模块（`fast_sta` / `bound_skew_tree` / `CharBuilder` / `FastClustering` / `optimization` / `htree` 等）按高内聚低耦合拆分为子目录 + 独立 CMake target，对外只暴露门面 .hh。
4. **`.hh` / `.cc` 分布合理**：消灭"为了拆分 .cc 而存在的 `*Internal.hh`"现象，公开头不再暴露算法私有数据/方法/嵌套类型。
5. **依赖与分层正确**：utils → database 反向依赖、module 紧耦合 database/adapter/fast_sta、跨 sub-flow / sub-module 直接 include 等违例必须治理。

本任务的产出是**调研产物 + 重构方案**，本任务**不直接执行重构**——执行交由后续 child task（见第 7 节）。

---

## 2. iCTS 代码现状摘要（功能、职责、语义、算法、流程）

完整证据见 `research/` 目录；本节给出"用户问 PRD 时可以直接读"的浓缩版。

### 2.1 层次结构

```
src/operation/iCTS/
├── api/                    1 类 (CTSAPI)，5 个对外方法，单例
├── external_libs/          3 个 .cmake，命名误导（90% 是 iEDA 内部 target 而非"外部库"）
├── source/
│   ├── database/           数据持有 + 适配器（9 子目录）
│   │   ├── adapter/        fast_sta (28 文件) / sta (12) / sdc (15)
│   │   ├── characterization/   header-only，CTS 特征化数据底座
│   │   ├── config/         Config 单例，30+ 字段混合算法参数 + 文件路径
│   │   ├── design/         Design 单例 + Clock/ClockDAG/ClockLayout/ClockNetwork 四件套
│   │   ├── io/             Wrapper 单例，iDB ↔ CTS 桥接（命名最严重）
│   │   ├── qor/            QorSummary POD
│   │   ├── routing/        Steiner tree POD
│   │   ├── spatial/        Point/Rect/Region/Tree（Tree 反向依赖 design::Pin）
│   │   └── timing/         RCTree POD（疑似无消费者）
│   ├── flow/               CTS 主流程编排（Flow 单例 + 6 sub-flow）
│   │   ├── Flow.hh/.cc     单例编排，硬编码 setup→run→instantiate→evaluate→emitKeyResults
│   │   ├── setup/          配置加载 + STA 初始化（命名 initialize/emitRuntimeSetup，无 run）
│   │   ├── synthesis/      时钟树综合（htree + topology + distribution + trace 子树）
│   │   ├── instantiation/  iDB 实例化（薄包装 IdbConversion，含跨 sub-flow 的 DesignConversion）
│   │   ├── optimization/   后综合优化（8 个子目录，namespace icts::optimization_internal）
│   │   ├── evaluation/     QoR 评估（5 个静态入口）
│   │   └── report/         报告 + 可视化（反向调用 Evaluation::run）
│   ├── module/             算法模块（5 子目录）
│   │   ├── routing/        8 个子目录（router/bst/cbs/flute/salt/local_legalization/helper/database）
│   │   ├── topology/       6 个子目录（topology_gen / clustering / cluster_constraints / fast_clustering / kmeans / mcf）
│   │   ├── timing/         单类 TimingEngine（最精简、最干净）
│   │   ├── characterization/   平铺 20 文件，CharBuilder 一类拆 11 .cc
│   │   └── analytical_characterization/  平铺 6 文件（未被顶层 module aggregator 链接）
│   └── utils/              通用工具（geometry / graph / logger / visualization）
└── test/                   测试，含 31 个 *Support* 文件
```

### 2.2 数据流与编排

CTS 主流程（`Flow::runCTS`）：

```
CTSAPI::init
  → Setup::initialize           // 写 CONFIG_INST + WRAPPER_INST + STA_ADAPTER_INST
  → FLOW_INST.setSetupReady     // 跨层注入 bool

CTSAPI::runCTS → FLOW_INST.runCTS
  → readData                    // 调 DesignConversion（位于 instantiation/，跨 sub-flow）
  → run                          // Synthesis::run + DAG 重建 + Optimization::run + DAG 重建
  → instantiate                 // Instantiation::run → IdbConversion::run
  → evaluate                    // Evaluation::run
  → emitKeyResults              // schema 表汇总

CTSAPI::report → FLOW_INST.report
  → 串联 ResultExport / Overview / Evaluation::run(可能再调) / QorReport / Visualization
```

数据载体：`ClockLayout`（layout 投影）、`CharacterizationLibrary`（HTree 缓存类，但被 Flow 单例持有）、`EvaluationState`、`SynthesisTraceSummary`、5 类 `XxxResult` 结构体。

### 2.3 关键算法核心（无需重构其语义，仅治其外形）

- **H-tree analytical solver**（`flow/synthesis/htree/analytical_solver/`）：闭式分析求解 + 候选筛选；`AnalyticalSolverRequest` / `Result` 命名不正。
- **Bounded Skew Tree（BST / DME）**（`module/routing/bound_skew_tree/`）：~92 方法的 mega-class `BoundSkewTree`，分散到 7 .cc；附带 `GeomCalc` 静态类 + 4 .cc。
- **Fast Clustering**（`module/topology/fast_clustering/`）：10 .cc + `FastClusteringInternal.hh` 共享 namespace。
- **Buffering characterization**（`module/characterization/CharBuilder`）：1 类 26 方法 + 11 .cc；公开 `FastStaTypes.hh` include 形成 module → fast_sta 紧耦合。
- **DMP Effective-Capacitance Driver Model**（`database/adapter/fast_sta/dmp_ceff/`）：4 .cc 实现 `DmpSolver` + Newton-Raphson + LU，私有类型被迫挤进 `FastStaDmpCeffInternal.hh`。
- **Optimization 8 阶段**（`flow/optimization/`）：Preparation → CapBaseline → SlewBaseline → Solver(Exact/Scalable) → Mutation → Report，全部 free function。
- **K-means 与 Min-Cost-Flow**（`module/topology/kmeans/`、`module/topology/mcf/`）：通用模板，但 `BoundSkewTree::kMeansPlus` 不复用 KMeans 模板而是手写一份。

---

## 3. 问题清单

> **来源标记**：`[U]` 用户在 PRD 中点名 · `[A]` 调研补充

每条问题都给出：标题 · 证据（路径/行号） · 影响 · 处理优先级（高/中/低）。

### P1 · flow 层缺统一范式 `[U]`

**证据**：
- 6 个 sub-flow 入口动词混用：`initialize`/`run`/`evaluate`/`build`/`emit`（`flow/setup/Setup.hh`、`flow/synthesis/Synthesis.hh` 等）。
- 返回类型 7 种各异：`bool` / `void` / `SynthesisTraceSummary` / `OptimizationResult` / `EvaluationResult` / `InstantiationResult` / `ReportResult` / `IdbConversionResult`。
- 错误信号四套并存：`SynthesisOutcome` 枚举 / `bool success` / `evaluation_ready` / `LOG_FATAL_IF`。
- **没有任何 sub-flow 把 init/run/report 显式拆为三阶段**，全部塞进单个 `run`（`Optimization::run` 145 行硬编码 8 阶段）。
- Schema stage 名 `Synthesis.cc:200` 用 `"CTSFlow"`（不一致，应为 `"Synthesis"`）。

**影响**：sub-flow 之间无法共享生命周期管理、错误恢复、日志、Schema 输出策略；新增 sub-flow 没有可参照范式；测试要逐个 sub-flow 手工 reset 状态。

**优先级**：**高**

### P2 · module / flow 子模块层次与命名不统一 `[U]`

**证据**：
- module 子模块粒度：`topology/` 6 子目录、`routing/` 8 子目录、`characterization/` / `analytical_characterization/` / `timing/` 全平铺零子目录（`module/01_top_level_cmake_and_architecture.md`）。
- `analytical_characterization` 子目录被 `add_subdirectory` 但**未被顶层 `icts_source_module` INTERFACE aggregator 链接**（`module/CMakeLists.txt:13-23`）。
- `module/routing/router/CMakeLists.txt:1-4` 把 `icts_source_module_routing` 这个名字定义在 `router/` 子目录里，同时打包了整个 routing/，命名误导。
- `module/routing/database/` 是空目录（仅 INTERFACE 转发），名字与 `database/` 顶层冲突。
- flow 层 namespace 与目录不对齐：同一物理目录混排 `icts` / `icts::htree` / `icts::optimization_internal` / `icts::qor_evaluation` / `icts::visualization` 等 6+ 种 namespace（`flow/05-file-organization-and-cmake.md`）。

**优先级**：**高**

### P3 · 互联网化用词扩散 `[U]`

> 用户原文：`snapshot / Internal / Support / Request / Response` 等没有具体业务语义。

**全局扫描结果**（`cross-cutting/04-naming-scan.md`）：

| 关键词 | source 内文件数 | namespace 数 | test 内文件数 |
|---|---|---|---|
| `Internal`（.hh/.cc 后缀） | 10 (`*Internal.hh`) + 1 (`STAAdapterInternal.cc`) | 9 个 `*_internal` 或等价私有 namespace | — |
| `Snapshot`（struct/方法名） | 2 struct + 18+ 处 `snapshot*` 函数 | — | — |
| `Support` | 1（`WrapperClockWriterSupport.cc`） | — | **31** |
| `Wrapper` | 4 (`Wrapper.hh/.cc/WrapperClock*`) | — | — |
| `Request` | 3 个 struct + 1 个 .cc | — | — |
| `Response` | **0** | — | — |
| `Manager/Handler/Service/Provider/Factory` | **0** | **0** | — |
| `Helper` | 2（`PinLocationHelper.hh/.cc`） | — | — |
| `Util` | 0 | 0 | — |

**关键 `*Internal.hh` 清单**（10 个）：
1. `database/adapter/fast_sta/FastStaDmpCeffInternal.hh` (169 行，含 `DmpSolver` 64 字节级私有类)
2. `database/adapter/sta/STAAdapterInternal.hh` (95 行) + `.cc` (539 行)
3. `database/adapter/sdc/ClockTraceResolverInternal.hh` (155 行，50+ 自由函数)
4. `database/io/WrapperClockWriterInternal.hh` (67 行)
5. `module/topology/fast_clustering/FastClusteringInternal.hh` (152 行)
6. `module/routing/bound_skew_tree/BSTRouterInternal.hh` (38 行，被 CMake PUBLIC 暴露)
7. `flow/evaluation/qor/QorEvaluationInternal.hh` (71 行)
8. `flow/synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` (206 行)
9. `flow/synthesis/htree/compensation/RootDriverCompensationInternal.hh` (116 行)
10. `flow/optimization/model/OptimizationTypes.hh` 配合 `namespace icts::optimization_internal` 13 个文件

**关键 `snapshot` 集中点**（`database/02-naming-issues.md`）：
- `FastStaLiberty.cc` 6 处函数名（`snapshotTable` / `snapshotDelayTable` / `snapshotPowerTable` / `snapshotPowerArcTables` / `snapshotBufferCellFromLibCell`）
- `FastStaBuilder.cc` 4 处（`snapshotClockData` / `snapshotSinkPinCaps` + 注释）
- `io/WrapperClockWriter*` 4 处（`IdbNetPinSnapshot` struct + `SnapshotIdbNetPins` 函数 + 字段）
- `utils/logger/Schema.hh:72` `RuntimeMetricSnapshot`

**优先级**：**高**

### P4 · 大模块内部设计不合理（fast_sta 案例） `[U]`

**证据**（`database/01-fast_sta-deep-dive.md`、`database/04-fast_sta-refactor-proposal.md`）：
- `database/adapter/fast_sta/` 全平铺 28 个文件、4949 行，无子目录。
- 13 个 .hh 全部通过 `target_include_directories(... PUBLIC ${ICTS_DATABASE_ADAPTER_FAST_STA})` 暴露给外部。
- **门面破例**：`flow/optimization/preparation/OptimizationPreparation.cc:271` 直接调用 `FastStaBuilder::injectNetRouteTree`，绕过 `FastSta.hh`。
- **API 过度暴露**：`FastSta.hh` 21 个 public API 中 5 个未被外部使用（`changeBufferMaster` 单点 / `rebuildClockContext` / `querySinkArrival` / `queryNodeSlew` / `queryNetLoad` / `queryArea` / `queryClockIds`）。
- `FastStaDmpCeffInternal.hh` 是为了让 4 个 .cc 共享 `DmpSolver` 类被迫拆出的副产物。
- `FastStaTypes.hh` 单文件 382 行容纳 24 个 struct + 6 enum + 5 alias。

**类似问题集中点**：
- `module/routing/bound_skew_tree/`：22 文件 / 5450 行 / `BoundSkewTree` 单类 92 方法 / 11 个私有嵌套类型暴露在 .hh
- `module/characterization/`：20 文件 / `CharBuilder` 单类 26 方法分散在 11 .cc / 6 个私有嵌套 struct 在 .hh
- `module/topology/fast_clustering/`：10 .cc + Internal.hh
- `flow/optimization/`：8 子目录 × 平均 1 .cc 但全部 namespace `icts::optimization_internal`

**优先级**：**高**

### P5 · 大模块声明在 .hh，实现散落到 .hh+多个子目录 .cc `[U]`

**Mega-class 切片清单**（`module/10_implementation_scattering.md`）：
| 类 | 头文件 | 方法数 | .cc 文件数 | 共享方式 |
|---|---|---|---|---|
| `icts::bst::BoundSkewTree` | `BoundSkewTree.hh` (371 行) | ~92 | 7 | 全部 private method 暴露在头文件 |
| `icts::CharBuilder` | `CharBuilder.hh` (231 行) | 26 | 11 | private nested + members 暴露在头文件 |
| `icts::FastClustering` 系列 | `FastClustering.hh` + `FastClusteringInternal.hh` | 40+ free fn | 10 | namespace 共享 |
| `icts::bst::GeomCalc` | `GeomCalc.hh` (155 行) | 30+ static | 4 (`GeomCalc{,Line,PointRegion,TransformedRect}.cc`) | 静态类拆分 |
| `BSTRouter` 适配器 | `BSTRouter.hh` (49 行) | 2 | 3 | `*Export.cc` + `*BinaryTopology.cc` 拆出 |
| `FastSta` `DmpSolver` | `FastStaDmpCeffInternal.hh` (169 行) | ~25 | 4 | namespace 共享 |
| `STAAdapter` | `STAAdapter.hh` | 30+ | 7 (`STAAdapter{Cell,Clock,Internal,Rc,RootDriver,Timing,WireRc}.cc`) | namespace + Internal.hh 共享 |
| `ClockTraceResolver` | `ClockTraceResolver.hh` | 2 + 50 自由函数 | 4 | namespace 共享 |

**核心模式**：每个 .cc 是同一个类的"章节"，文件名是 `ClassNameTopic.cc`（`CharBuilderBuild.cc` / `BoundSkewTreeBalance.cc` 等），没有任何"阶段类"/"组件"抽象——文件命名约定本身就是设计缺失的信号。

**优先级**：**高**

### P6 · utils 反向依赖 database `[A]`

**证据**（`cross-cutting/01-utils-layer.md`、`cross-cutting/06-dependency-graph.md`）：
- `utils/geometry/Geometry.hh` 引用 `database/spatial/Point.hh`、`Rect.hh`、`Region.hh`
- `utils/visualization/CMakeLists.txt`: `target_link_libraries(icts_source_utils_visualization PUBLIC icts_source_database_design icts_source_database_spatial)`，且代码 `#include "design/Pin.hh"`、`#include "spatial/Tree.hh"`

**影响**：utils 命名说自己是底层但实际坐在 database 上面，分层文档失真，未来想把 utils 抽出复用基本不可能。

**优先级**：中

### P7 · module/characterization 紧耦合 database/adapter/fast_sta `[A]`

**证据**（`cross-cutting/06-dependency-graph.md`）：
- `module/characterization/CharBuilder.hh:37` `#include "adapter/fast_sta/FastStaTypes.hh"`（公开头依赖）
- `CharBuilderSlewSampling.cc:33` 与 `CharBuilderCircuit.cc:35` 直接 `#include "adapter/fast_sta/FastSta.hh"` 并调用 `FAST_STA_INST`

**影响**：`fast_sta` 实际是 module 算法模块，被错放在 `database/adapter/` 之下；CharBuilder 的任何 include 者都隐式带入 fast_sta 类型。

**优先级**：中

### P8 · 7 个全局单例 + 隐式 `resetAPI` 契约 `[A]`

**证据**（`cross-cutting/07-other-cross-cutting.md`）：
- 单例清单：`FLOW_INST` / `CONFIG_INST` / `WRAPPER_INST` / `STA_ADAPTER_INST` / `FAST_STA_INST` / `DESIGN_INST` / `SCHEMA_WRITER_INST` + `CTS_API_INST`
- `CTSAPI::resetAPI()` 手工列举 5 个 reset（CONFIG/DESIGN/WRAPPER/FLOW/SCHEMA_WRITER），**遗漏 STA_ADAPTER 与 FAST_STA**
- 无机制保证新增单例同步 reset；任何 .cc include 单例头即获访问权
- 多 clock-domain / 并行流程不可行

**优先级**：中

### P9 · CMake target 数量爆炸但内容稀疏 `[A]`

**证据**（`cross-cutting/03-cmake-structure.md`、`flow/05-file-organization-and-cmake.md`）：
- source 总计 88 个 add_library；30+ 个 target 只有 0~1 个 .cc
- `flow/optimization/` 8 子目录 × 平均 1 .cc/target；`flow/synthesis/htree/` 10 子目录 × 平均 1 .cc/target
- 顶层 `option(DEBUG_ICTS_XXX OFF)` 在每个 CMakeLists.txt 内重复 `-g3 -O0 -fno-inline` 块（~880 行重复模板）
- 50 个 CMakeLists.txt 在 flow 层
- `module/routing/router/CMakeLists.txt:1-4` 的 target 名 `icts_source_module_routing` 实际承担整个 routing 聚合，命名误导

**优先级**：中

### P10 · 大头文件携带 inline 实现与算法私有细节 `[A]`

**证据**（`cross-cutting/05-large-headers.md`）：
| 行数 | 文件 | 问题 |
|---|---|---|
| 563 | `flow/synthesis/htree/segment_pruning/SegmentLibrary.hh` | 13 类型 + 含 `LOG_FATAL_IF` 的 inline 大方法 |
| 382 | `database/adapter/fast_sta/FastStaTypes.hh` | 24 struct + 6 enum 大杂烩 |
| 371 | `module/routing/bound_skew_tree/BoundSkewTree.hh` | 11 私有 nested 类 + ~80 私有方法 + 全部数据成员 |
| 349 | `database/routing/SteinerTree.hh` | inline accessor 过多 |
| 347 | `module/routing/bound_skew_tree/Components.hh` | 5 类一文件（Point/Area/Match/Interval/TransformedRect） |
| 323 | `utils/visualization/core/SvgCommon.hh` | 常量 + 4 struct + 6 inline 函数混合 |
| 319 | `module/characterization/Frontier.hh` | 模板类大集合 |

**优先级**：中

### P11 · 数据结构与算法重复实现 `[A]`

**证据**（`module/09_cross_module_coupling.md`）：
- **Point**：`icts::Point<T>`（utils 模板）+ `icts::bst::Point`（含 delay 字段）—— 2 套
- **Manhattan distance**：`geometry::Manhattan`、`bst::Geom::distance`、`fast_clustering::CalcManhattanDistance` —— 3 套
- **Bounding box**：`bst::BoundingBox` vs `fast_clustering::Bounds` —— 2 套
- **K-means**：`topology/kmeans/KMeans.hh` 模板 vs `bst::BoundSkewTree::kMeansPlus`（手写）—— 2 套
- **Cluster evaluation**：`Clustering::ClusterElectricalEvaluation` vs `cluster_constraints::ConstraintEvaluation` —— 1-to-1 重命名
- **三层 forwarding**：`TopologyGen::fastClustering` → `Clustering::fastClustering` → `FastClustering::run`
- **`BuildOptions/BuildResult` 名字撞车**：`HTree::BuildOptions` / `Topology::BuildOptions` / `SourceTrunkSegment::BuildOptions` 共存

**优先级**：中

### P12 · 跨 sub-flow / sub-module 边界破裂 `[A]`

**证据**（`flow/06-additional-issues.md`、`module/09_cross_module_coupling.md`）：
- `report/Report.cc:62` 反向调 `Evaluation::run`（如果 evaluation 没做就在 report 阶段补做）
- `synthesis/distribution/CMakeLists.txt:12` PRIVATE link `icts_source_flow_instantiation_design_conversion`
- `synthesis/topology/Topology.cc:42` include `instantiation/design_conversion/DesignConversion.hh` 并调 4 次
- `module/topology/cluster_constraints/` 同时 include `module/timing/`、`module/routing/{bound_skew_tree,local_legalization,router,helper}/`
- `Flow.hh:29-32` 公共契约依赖 sub-sub-flow 类型（`CharacterizationLibrary` 等 5 类）

**优先级**：中

### P13 · 264 次 `LOG_FATAL_IF` 替代错误处理 `[A]`

**证据**（`cross-cutting/07-other-cross-cutting.md`）：
- `LOG_FATAL_IF` 跨 source 出现 264 次；`throw / try / catch` 仅 3 处（全在 `database/config/Config.cc`，用于 json 解析）
- 没有区分"程序内部不变量"vs"运行时输入错误"
- 错误恢复路径基本不存在

**优先级**：低（不在本次必须修复范围内，但应记入 spec）

### P14 · external_libs 命名误导、test 内 31 个 Support 文件 `[A]`

**证据**（`cross-cutting/07-other-cross-cutting.md`、`cross-cutting/04-naming-scan.md`）：
- `external_libs/` 三个 .cmake 文件，但 90% 链接的是 iEDA 内部 target（idm / idb / ista-engine / log / usage / feature_db）；只有 gtest 和 pthread 是真外部
- `test/**/*Support*` 共 31 个文件（test/common/realtech/support、test/module/characterization/support 等），test 本身已成 mini 项目，需独立规范

**优先级**：低

### P15 · design/ 内 Clock 系列四件套枚举重复 `[A]`

**证据**（`database/03-submodule-overview.md`）：
- `ClockLayout.hh:36-77` 的 `SinkDomainKind` / `LayoutInstRole` / `LayoutNetRole` 
- `ClockNetwork.hh:42-65` 的 `DomainKind` / `InstRole` / `NetRole`
- 两套各持 `ToString()`；几乎 1-to-1 等价
- `ClockDAG` vs `ClockNetwork` 边界不清；`timing/RCTree.hh` 似乎无消费者

**优先级**：低

### P16 · namespace 与目录层级不对齐 `[A]`

**证据**（`flow/05-file-organization-and-cmake.md`、`cross-cutting/07-other-cross-cutting.md`）：
- 同一目录混排 `icts` 与 `icts::xxx_internal`
- `icts::htree::analytical_solver` 嵌套两层，目录是 `flow/synthesis/htree/analytical_solver/`（flow/synthesis 被吞）
- `icts::optimization_internal` 13 文件，`icts::sta_adapter_internal` 多文件
- `icts::visualization::detail` / `icts::fast_clustering` / `icts::fast_sta_dmp` 等多套"事实私有 namespace"

**优先级**：低

---

## 4. 重构目标（每条都对应至少一个上面的问题）

| ID | 目标 | 关联问题 |
|---|---|---|
| G1 | flow 层引入统一范式（init/run/report 或等价契约），所有 sub-flow 遵循同一生命周期 | P1, P12 |
| G2 | 消灭所有 `*Internal.hh` 文件与 `*_internal` namespace；私有共享放进 .cc 匿名 namespace 或子目录 `detail/` + PRIVATE include | P3 |
| G3 | 把 `fast_sta/` 拆为 11 个子目录 + 11 个 cmake target，对外仅暴露 `FastSta.hh` + `types/` | P4 |
| G4 | 拆分 `BoundSkewTree` / `CharBuilder` / `FastClustering` mega-class，按阶段/组件抽象（不再是文件名章节） | P5 |
| G5 | 全部 `snapshot* / Wrapper / Helper / Engine / Polish / Draft / Desc / Request` 改为 CTS 业务语义命名 | P3 |
| G6 | module 子模块目录粒度对齐（要么都有子目录，要么都平铺）；CMake aggregator 命名清晰 | P2, P9 |
| G7 | utils 不再反向依赖 database；module/characterization 与 fast_sta 解耦或正名 | P6, P7 |
| G8 | 收敛 CMake target（从 88 收敛到 ~30），抽出公共 `icts_apply_debug_flags()` 宏 | P9 |
| G9 | 大 .hh（>300 行）拆分；公开头不再暴露算法私有 nested type / 私有方法 | P10 |
| G10 | 治理 7 单例的 reset 契约（统一 `IResettable` / 注册表）；spec 中固化规则 | P8 |
| G11 | 去除重复实现（Point / Manhattan / Bounding box / K-means / Cluster evaluation） | P11 |
| G12 | namespace 与目录层级对齐（每条 namespace 路径 ↔ 物理目录路径） | P2, P16 |

---

## 5. 范围与边界

### 5.1 必须做（本次重构 in scope）

- 整个 `src/operation/iCTS/source/` 内的代码、CMake、头文件、命名空间
- `src/operation/iCTS/api/CTSAPI.{hh,cc}` 的对外名字与方法风格（不改 API 数量）
- `src/operation/iCTS/external_libs/*.cmake` 的命名与组织（不改外部依赖）
- `src/operation/iCTS/test/` 内 31 个 `*Support*` 文件的重命名（不改测试逻辑）

### 5.2 不做（out of scope）

- 算法本身的正确性、性能优化、收益验证（仅做结构化改造）
- 删除 `LOG_FATAL_IF` 改用异常或返回码（P13 留作 spec 沉淀）
- 把 fast_sta 从 `database/adapter/` 物理移到 `module/`（P7 在 design.md 中评估，可能保留在 adapter）
- 修改 iEDA 其它模块（idm / idb / ista-engine / iSTA / iPlacer 等）的代码

### 5.3 兼容性约束

- **对外 API 不变**：`CTSAPI::{init, runCTS, report, resetAPI, outputSummary}` 5 个方法签名保持兼容（参数列表、返回类型）
- **CTSSummary / QorSummary 字段**保持兼容（feature 层依赖）
- **Tcl/Python binding 入口**（`src/interface/tcl/tcl_icts/`、`src/interface/python/py_icts/`）只可重新链接，不可改入口符号
- **配置文件 schema**（CTSConfig JSON）保持兼容

---

## 6. 验收标准

### 6.1 物理验收（grep-able）

- [ ] `find src/operation/iCTS/source -name '*Internal.hh'` 返回为空（含 .cc）
- [ ] `grep -rn 'namespace.*_internal' src/operation/iCTS/source` 返回为空
- [ ] `grep -rn 'snapshot' src/operation/iCTS/source --include='*.hh' --include='*.cc'` 仅保留少量注释用法（≤3 处，必须有 spec 说明）
- [ ] `find src/operation/iCTS/source -name 'Wrapper*.hh' -o -name 'Wrapper*.cc'` 返回为空
- [ ] `grep -rEn 'class .*(Helper|Manager|Handler|Service|Provider|Factory)\b' src/operation/iCTS/source` 返回为空
- [ ] `grep -rn 'Request' src/operation/iCTS/source --include='*.hh' | grep -v '//.*Request'` 返回为空
- [ ] `find src/operation/iCTS/source/database/adapter/fast_sta -maxdepth 1 -name '*.hh'` 仅返回 `FastSta.hh`
- [ ] `flow/optimization/preparation/OptimizationPreparation.cc` 不再直接调 `FastStaBuilder::*`，改走 `FastSta::*` 门面
- [ ] CMake target 数量从 88 收敛到 ≤35（统计 `add_library` 出现次数）
- [ ] `find src/operation/iCTS/source -name '*.hh' | xargs wc -l | awk '$1>300'` 仅保留有 spec 豁免说明的文件

### 6.2 结构验收

- [ ] 6 个 sub-flow 全部实现统一范式（命名待 design.md 决定）；新增 sub-flow 模板存在 spec 文档
- [ ] `fast_sta/` 至少拆为 `types/` + `liberty/` + `parasitics/` + `dmp_ceff/` + `builder/` + `clock_tree/` + `incremental/` + `timing/` + `power/` + `report/` 10 个子目录（具体命名以 design.md 为准）
- [ ] `BoundSkewTree` / `CharBuilder` / `FastClustering` 三大 mega-class 不再用文件名章节切片；对外只暴露门面 .hh
- [ ] utils 不再 link database（除 `geometry/` 与 `visualization/` 的处理见 design.md）

### 6.3 行为验收（不破坏现有功能）

- [ ] `bash build.sh` 全量构建成功
- [ ] `iCTS/test/` 下全部现有测试通过（不增加新失败用例）
- [ ] `FlowTest.cc` 端到端 CTS run 完成、QorSummary 关键指标（skew / power / area）与重构前同输入数据保持 bit-identical
- [ ] 5 个 CTSAPI 公共方法签名不变

### 6.4 文档与 spec 验收

- [ ] `.trellis/spec/backend/icts-architecture.md` 描述新的层级模型与 sub-flow 范式
- [ ] `.trellis/spec/backend/icts-naming-convention.md` 列出禁用词汇表 + CTS 语义替代映射
- [ ] `.trellis/spec/guides/icts-refactor-checklist.md` 描述新增 sub-flow / sub-module 的检查项
- [ ] 每个子任务（child task）完成后归档进 `.trellis/tasks/` 并 link 回本任务

---

## 7. 子任务拆分建议（父子任务结构）

> 本调研任务（`05-19-icts-code-standardization-refactor`）是**调研 + 总体规划**的 PRD-only 父任务。
> 实施环节按**独立可验证的交付物**拆为以下 child task（顺序见 implement.md）。

| Child Slug 建议 | 范围 | 优先验证手段 |
|---|---|---|
| `icts-naming-convention-spec` | 写 `.trellis/spec/` 命名规范 + 重构 mapping 表（不动代码） | spec 完整性 review |
| `icts-flow-paradigm-unification` | 统一 6 sub-flow 范式 + 修复跨 sub-flow 反向调用 | FlowTest + Schema 输出比对 |
| `icts-fast-sta-decomposition` | fast_sta 11 子目录拆分 + 命名规范化 + 修复 OptimizationPreparation 破例 | FastSTATest + Optimization 端到端 |
| `icts-bst-decomposition` | bound_skew_tree mega-class 拆分 + GeomCalc 整合 + Components 拆分 | BST router 单测 + 端到端 |
| `icts-char-builder-decomposition` | CharBuilder mega-class 拆分 + 解耦 fast_sta 紧耦合 | CharBuilder 单测 |
| `icts-fast-clustering-decomposition` | FastClustering 拆分 + naming Polish/Draft 替换 | FastClustering 单测 |
| `icts-database-naming-cleanup` | io/Wrapper → IdbBridge 系列重命名 + STAAdapterInternal / ClockTraceResolverInternal 拆分 | 编译 + 现有测试 |
| `icts-cmake-consolidation` | CMake target 收敛（88 → ≤35） + DEBUG_ICTS_xxx 宏抽出 + utils → database 治理 | 增量编译 + target 计数 |
| `icts-large-headers-split` | SegmentLibrary.hh / FastStaTypes.hh / SvgCommon.hh / Frontier.hh 拆分 | 编译时间 + include 关系图 |
| `icts-singleton-reset-contract` | 7 单例统一 IResettable 接口或注册表 + 修复 resetAPI 漏 reset | resetAPI 单测 + 多次 init 测试 |
| `icts-test-support-naming` | test/ 内 31 个 *Support* 文件按业务语义重命名 | 测试编译通过 |

**依赖关系**（在各 child task 的 prd.md 内显式声明）：
- `icts-naming-convention-spec` 是其它所有 task 的依赖（先约定规则）
- `icts-flow-paradigm-unification` 不依赖具体 mega-class 拆分（可并行）
- `icts-fast-sta-decomposition` 必须在 `icts-char-builder-decomposition` 之前（CharBuilder 公开依赖 FastStaTypes，先稳定 fast_sta 边界）
- `icts-cmake-consolidation` 应该在 mega-class 拆分之后（保证 target 边界稳定）

---

## 8. 风险

| 风险 | 缓解策略 |
|---|---|
| 命名大改导致大量 PR review 噪声 | 每个 child task 单独 PR，命名变更与逻辑变更分开 commit |
| `FastStaClockContext` 同时承载真实时钟与特征化测试电路，类型重命名可能影响调用方 | 在 `icts-fast-sta-decomposition` 中先做调用方调研，保留类型别名做过渡 |
| 删除 7 个未使用 API 可能影响下游 binding | 先做 `grep -rn` 全仓库扫描确认无 Tcl/Python 调用，再删除 |
| Mega-class 拆分破坏 inline cache、影响性能 | 拆分前做 baseline benchmark，拆分后 bit-identical 验证 + 性能回归测试 |
| 跨 sub-flow / sub-module 边界破裂修复改动面大 | 按"先内后外"顺序：先拆 module，再治理 flow 内部，最后跨 sub-flow 调用 |
| 单例 reset 契约统一可能要求新增 fixture | `icts-singleton-reset-contract` 单独成 child task，避免与其它任务并发 |

---

## 9. 调研附录

完整证据在同目录 `research/` 下 30 份 markdown：

- `research/flow/01-overview.md` ~ `07-summary.md`
- `research/module/01_top_level_cmake_and_architecture.md` ~ `11_summary_top5_issues.md`
- `research/database/00-summary.md` ~ `04-fast_sta-refactor-proposal.md`
- `research/cross-cutting/01-utils-layer.md` ~ `07-other-cross-cutting.md`

后续 `design.md` 将基于上述问题清单与目标，给出技术方案；`implement.md` 给出分阶段、可回滚的执行清单。
