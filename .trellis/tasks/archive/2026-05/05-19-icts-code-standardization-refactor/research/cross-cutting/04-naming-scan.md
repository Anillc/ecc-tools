# Research: Global Naming Scan (Internet-style / Generic Suffix)

- **Query**: 在整个 iCTS source 中扫描 Snapshot/Internal/Support/Request/Response/Manager/Handler/Helper/Util/Wrapper/Service/Provider/Factory 等"互联网化"或泛化命名作为类名/文件名
- **Scope**: internal
- **Date**: 2026-05-19

扫描根：`src/operation/iCTS/source/`，文件类型 `*.hh` / `*.cc`。

## 4.1 汇总表

| 关键词 | 文件名出现次数 | 类/struct 出现次数 | namespace 出现次数 | 严重度 |
|---|---|---|---|---|
| Snapshot | 0 | 2（嵌套子结构） | 0 | 低 |
| Internal | 10（10 个文件名） | 0（独立类名） | 17（在 9 个独立 namespace） | **高** |
| Support | 1（database/io/WrapperClockWriterSupport.cc）| 0 | 0 | 低（source 内）/ **高**（test 内 31 个） |
| Request | 1（AnalyticalSolverRequest.cc）| 3 struct | 0 | 中 |
| Response | 0 | 0 | 0 | — |
| Manager | 0 | 0 | 0 | — |
| Handler | 0 | 0 | 0 | — |
| Helper | 2（PinLocationHelper.hh/cc）| 0 | 0 | 低 |
| Util | 0 | 0 | 0 | — |
| Wrapper | 3（database/io/Wrapper.hh/cc/WrapperClockWriter*）| 1 class (Wrapper) | 0 | 中 |
| Service | 0 | 0 | 0 | — |
| Provider | 0 | 0 | 0 | — |
| Factory | 0 | 0 | 0 | — |

总体结论：iCTS source 几乎没有 Manager/Handler/Service/Provider/Factory 这些"通用框架词"。但 `Internal` 在文件名和 namespace 里被滥用。

## 4.2 详细文件清单

### Snapshot（仅 struct 层级）
2 处：
- `utils/logger/Schema.hh:72` — `struct RuntimeMetricSnapshot { double elapsed_time_s; double peak_vmem_delta_mb; };`
- `database/io/WrapperClockWriterInternal.hh:42` — `struct IdbNetPinSnapshot`（用来缓存 net→pins 的中间结果）

**CTS 语义化建议**：
- `RuntimeMetricSnapshot` → `RuntimeMetricSample` 或 `RuntimeMetricRecord`
- `IdbNetPinSnapshot` → `IdbNetPinView` 或 `IdbNetPinCache`

### Internal（10 个 .hh + 1 个 .cc，9 个 namespace）

#### 10 个文件
| 文件 | 行数 | 命名空间 |
|---|---|---|
| `database/io/WrapperClockWriterInternal.hh` | 67 | （未列） |
| `database/adapter/sta/STAAdapterInternal.hh` | 95 | `icts::sta_adapter_internal` |
| `database/adapter/sta/STAAdapterInternal.cc` | 539 | `icts::sta_adapter_internal` |
| `database/adapter/sdc/ClockTraceResolverInternal.hh` | 154 | `icts::clock_trace` |
| `database/adapter/fast_sta/FastStaDmpCeffInternal.hh` | 169 | `icts::fast_sta_dmp` |
| `flow/evaluation/qor/QorEvaluationInternal.hh` | 71 | `icts::qor_evaluation` |
| `module/topology/fast_clustering/FastClusteringInternal.hh` | 152 | `icts::fast_clustering` |
| `module/routing/bound_skew_tree/BSTRouterInternal.hh` | 38 | `icts::bst`（推测） |
| `flow/synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` | 206 | `icts::htree::analytical_solver`（推测） |
| `flow/synthesis/htree/compensation/RootDriverCompensationInternal.hh` | 116 | （未列） |

#### 9 个 `*_internal` namespace（最严重）
按出现次数：
- `icts::optimization_internal`：13 处文件（飞行优化器全套都用这个 namespace）
- `icts::sta_adapter_internal`：2 处
- 单独 namespace 但带 internal 语义的：`icts::clock_trace`、`icts::fast_sta_dmp`、`icts::fast_clustering` —— 这些虽然名字没 `_internal`，但实际作用就是各自模块的私有命名空间。

#### 问题
- "Internal" 是 SaaS / framework 风格用语，C++ 标准做法是：
  - 公开 header 留 `icts::xxx`，私有 helper 放进 anonymous namespace（`namespace {}` 在 .cc 内）。
  - 跨 cc 共享的私有 helper 走 `*_detail` namespace 或 `*_p`（PIMPL）模式。
- 这里大量 `*Internal.hh` + `icts::xxx_internal` namespace 暴露在 include path 上 —— 任何模块都能 `#include "...Internal.hh"`，跨界使用风险高。
- 用户最严重的发现：用户已经在 PRD 里点名了"互联网化用词（snapshot/Internal/Support/Request/Response）"，这是核心痛点。

#### CTS 语义化建议
| 当前 | 建议 |
|---|---|
| `STAAdapterInternal.hh` (95 行 + 539 行 .cc) | 拆分：sta_engine 配置/lookup 部分 → `StaSession.hh/.cc`；wire-rc / lib unit 转换 → `StaUnitConversion.hh/.cc`；vertex / liberty find → `StaSymbolTable.hh/.cc`。 |
| `ClockTraceResolverInternal.hh` (154 行) | `icts::clock_trace` namespace 保留；文件名改 `ClockTraceHelpers.hh` 或拆成 `ClockTracePinClassify.hh` + `ClockTraceObjectResolve.hh`。 |
| `FastStaDmpCeffInternal.hh` (169 行) | namespace `icts::fast_sta_dmp` 已经语义化；文件名建议 `DmpCeffSolverState.hh` / `DmpCeffCoefficients.hh`。 |
| `FastClusteringInternal.hh` (152 行) | 文件名 `FastClusteringWorkspace.hh` 或拆 `ClusterDraft.hh`+`BoundaryMove.hh`+`NeighborGraph.hh`。 |
| `AnalyticalSolverInternal.hh` (206 行) | 拆为 `AnalyticalSolverContext.hh` + `AnalyticalSolverState.hh`。 |
| `QorEvaluationInternal.hh` (71 行) | 文件名 `QorEvaluationHelpers.hh`（用 Helpers 比 Internal 好但仍然不够语义）→ 直接 `QorPinClassify.hh` 之类。 |
| `BSTRouterInternal.hh` (38 行) | 太小，并入 `BSTRouter.cc` 的匿名 namespace 或主头。 |
| `RootDriverCompensationInternal.hh` (116 行) | `CompensationConstraint.hh` 或类似业务名。 |
| `WrapperClockWriterInternal.hh` (67 行) | `IdbNetPinView.hh` 或 `ClockWriterContext.hh`。 |

### Support（source 内 1 个）
- `database/io/WrapperClockWriterSupport.cc` — 没有 .hh 配对，是 `WrapperClockWriter.cc` 的 partial-implementation cc。
- **test 内有 31 个 `*Support.hh/.cc`**（其中 6 个 .hh + 25 个 .cc）—— test 里大量用 Support 作为后缀。这是 test 的内部约定，部分集中在 `test/common/realtech/support/`、`test/module/characterization/support/`、`test/module/topology/topology_gen/support/`。
- 用户 PRD 里点名"Support"，这里 test 占了绝大部分。test 内的 Support 用来表示"测试帮助器"，类似 fixture，但语义并不强：用户期望改成更有领域含义的名字。

**建议**：
- source 内的 `WrapperClockWriterSupport.cc` → 直接合并进 `WrapperClockWriter.cc`，或改名 `WrapperClockWriterMaterial.cc` / `WrapperClockNetBuild.cc`（按内容定）。
- test 内：`*Support.hh/.cc` 这种"测试支撑文件"建议改用 `*Fixture.hh`（gtest 习惯）或 `*Scenario.hh`、`*Asset.hh`、`*Builder.hh`，按 31 个文件分别看。

### Request（3 处 struct）
- `flow/synthesis/htree/analytical_solver/AnalyticalValidation.hh:43` — `struct AnalyticalValidationRequest`
- `flow/synthesis/htree/analytical_solver/AnalyticalSolver.hh:61` — `struct AnalyticalSolverRequest`
- `flow/synthesis/htree/segment_pruning/SegmentLibrary.hh:194` — `struct SegmentFrontierRequest`

加上 `AnalyticalSolverRequest.cc`（1 个 .cc，是上面 AnalyticalSolver.hh 的 Request 的实现）。

#### 问题
- "Request" 是 HTTP / RPC 风格。在 H-tree analytical solver 上下文里，本质是"求解输入"或"求解参数"。
- 没有配套的 "Response" —— 单边出现 Request 说明这个命名是"思维定势从 SaaS 带过来"，没有形成 API 范式。

**建议**：
- `AnalyticalValidationRequest` → `AnalyticalValidationInput` / `AnalyticalValidationProbe`
- `AnalyticalSolverRequest` → `AnalyticalSolverInput` / `AnalyticalSolverProblem`
- `SegmentFrontierRequest` → `SegmentFrontierQuery` / `SegmentFrontierSelector`

### Response
**0 出现**。强佐证了 Request 单边、无对称。

### Helper（2 个文件）
- `module/routing/helper/PinLocationHelper.hh/cc` — 看 .hh：`class PinLocationHelper`（推测），用来辅助 pin location 查询/缓存。

**建议**：
- `PinLocationHelper` → `PinLocationCache` 或 `PinLocationLookup`（取决于实际行为）。
- 整个 `module/routing/helper/` 目录建议改名 `module/routing/common/` 或 `module/routing/util/`。

### Wrapper（3 处文件 + 1 个 class）
- `database/io/Wrapper.hh/cc` — `class Wrapper`（单例 `WRAPPER_INST`），是 IDB ↔ iCTS 内部 design 转换的入口。
- `database/io/WrapperClockWriter.cc` / `WrapperClockReader.cc` / `WrapperClockWriterSupport.cc` / `WrapperClockWriterInternal.hh`

#### 问题
- `Wrapper` 是泛化命名，缺乏领域含义。Wrapper of what? 实际：把 iDB 数据封装成 CTS 用的 net / instance / clock。
- "wrapper" 在 EDA 里也是常用词（warpper around STA / IDB），但更精确的名字会是 `IdbBridge` / `DesignIO` / `ClockIO`。

**建议**：
- `database/io/Wrapper.hh` → `database/io/IdbBridge.hh` 或 `database/io/ClockIO.hh`
- `WrapperClockReader` → `IdbClockReader` / `ClockNetReader`
- `WrapperClockWriter` → `IdbClockWriter` / `ClockNetWriter`

### Util / Service / Provider / Factory / Manager / Handler
**全 0**。iCTS 源码很自律地没有用这些泛词作为类名/文件名。值得保留这个习惯。

## 4.3 隐藏的 Internet-style 用语（非用户列表）

扫描其他可疑词：

| 词 | 出现 | 备注 |
|---|---|---|
| `Context` | 少量（schema 的 emitSection context_sink 等） | 大多用于 sink 含义，可接受 |
| `Engine` | `module/timing/TimingEngine.hh`（class TimingEngine 来自 ista_adapter）| 名字合理（STA engine） |
| `Scope` | `RuntimeMetricScope` / `StageScope` / `SchemaScope.cc` | RAII，命名 OK |
| `Stage` | `StageScope` / `beginStage`（schema） | 流程概念，合理 |
| `Session` | 0 | — |
| `Driver` | 多（涉及 driver pin）| 领域词，OK |
| `Pipeline` | 0 | — |
| `Operator` | 0 | — |

## 4.4 结论与"最严重 5 个"命名问题

按用户原意（"互联网化用词"），最严重 5 个：

1. **`*Internal` 文件 10 个 + `*_internal` namespace 9 个**：分布广，已经成为事实标准。**这是命名层面最严重的问题。**
2. **`test/**/*Support*.hh/.cc` 共 31 个文件**：test 用 "Support" 表示 fixture/helper，大量重复且无语义。需要按领域换词。
3. **`database/io/Wrapper.hh` + `WrapperClock*` 4 个文件**：Wrapper 是泛化词，缺业务含义。
4. **`*Request` 3 个 struct（无对应 Response）**：单边出现的 RPC 风格命名。
5. **`SchemaWriter` + `RuntimeMetricSnapshot` + `emitArtifact` + `ReportSink` 等 schema/observability 用词**：utils/logger 把 SaaS 平台用语带进 EDA。

## Caveats / Not Found

- 没扫"变量名"层级（用户问题里说"作为类名/文件名"），所以可能有 `local_helper`、`req_xxx` 等变量名未涵盖。
- test/ 内的命名扫描仅做了文件名层面统计，没逐个读 31 个 Support 文件确定其内部 class 命名是否也带 Support。
