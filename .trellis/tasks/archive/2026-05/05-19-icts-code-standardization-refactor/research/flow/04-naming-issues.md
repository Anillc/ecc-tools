# Research: 互联网化命名痕迹（重点扫描）

- **Query**: 扫描 flow 层中 `snapshot`、`Internal`、`Support`、`Request`、`Response`、`Manager`、`Handler`、`Helper`、`Util`、`Wrapper` 等互联网化命名
- **Scope**: internal
- **Date**: 2026-05-19

## 总体结论

flow 层中**未发现** `Manager`、`Handler`、`Helper`、`Util`、`Support`、`snapshot/Snapshot` 直接作为类/文件/接口的命名（数据库层 `database/io/Wrapper.hh`、`WrapperClockWriterSupport.cc`、`FastStaBuilder.cc` 有 snapshot 用词，但**不在 flow 层**）。

flow 层主要的"互联网化"命名集中在两个模式：

1. **`*Internal.hh` 文件**——4 处。
2. **`Request` / `Response`** 结构体命名（来自 H-tree 解析求解器与 segment_pruning）——大量。

`Wrapper` 一词只作为 **include** 出现（`io/Wrapper.hh` 是 database/io 模块），并非 flow 层自定义。

---

## 1. `Internal` 命名痕迹

四个 `*Internal.hh` 私有头文件，均把"对模块内部其他 .cc 共享的契约"放进单独头：

| 文件路径 | 命名空间 | 函数/类型数 | 共享方 |
|---|---|---|---|
| `src/operation/iCTS/source/flow/evaluation/qor/QorEvaluationInternal.hh` | `icts::qor_evaluation` | 12 个 free function + `enum class ClockNetRole` + `struct ClockNetMeasurement` | QorEvaluation.cc / QorEvaluationMetrics.cc / QorEvaluationRootProbe.cc |
| `src/operation/iCTS/source/flow/optimization/model/OptimizationTypes.hh` | `icts::optimization_internal` | 21 个结构体 + 2 个 enum + 1 个 type alias | optimization 8 个子目录的所有 .cc |
| `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` | `icts::htree::analytical_solver` | 20+ 个 free function + 12 个结构体 + 1 个 enum | analytical_solver 目录下 9 个 .cc |
| `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensationInternal.hh` | `icts::htree` | 6 个结构体 + 1 个 free function | RootDriverCompensation.cc / RootDriverCompensationLoad.cc |

**进一步证据**（命名空间也用 `_internal` 后缀）：

```
icts::optimization_internal     ← 11 个文件使用
icts::qor_evaluation            ← 4 个文件使用（仅 evaluation/qor）
icts::htree::analytical_solver  ← 14 个文件使用
```

**问题**：
- 用户明确要求避免 `Internal` 后缀，但 flow 层有 4 处头文件 + 1 个 namespace 是 `optimization_internal`、1 个 namespace 是 `qor_evaluation`（含义就是 internal）。
- 这种"internal 头 + 共享 free function"是为了支持把单一 .cc 拆成多个 .cc 时共享私有定义——可以替换为更明确的、按职责命名的子模块（如 `CapBaseline.hh`、`SlewBaseline.hh`、`SizingActionGenerator.hh`、`RouteTreeCache.hh`），而不是聚拢成一个 `*Internal.hh`。

### 命名建议（CTS 业务语义）

| 当前 | 建议 |
|---|---|
| `evaluation/qor/QorEvaluationInternal.hh` (`icts::qor_evaluation`) | 按职责拆分：`qor/ClockNetMeasurement.hh`、`qor/ClockNetStatistics.hh`、`qor/PathDepthStats.hh`、`qor/EvaluationSummaryEmitter.hh`；统一在 `icts::qor` 命名空间 |
| `optimization/model/OptimizationTypes.hh` | 拆为按主题：`optimization/buffer/BufferMaster.hh`、`optimization/baseline/CapSlewBaseline.hh`、`optimization/action/SizingAction.hh`、`optimization/profile/RuntimeProfile.hh`、`optimization/topology/TopologyIndex.hh`、`optimization/state/FastState.hh`、`optimization/summary/ClockOptimizationSummary.hh` |
| `synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh` | 按职责拆：`AnalyticalCacheKey.hh`、`AnalyticalRequestContract.hh`（**注意：Request 也要改名**）、`PartialAnalyticalCandidate.hh`、`UnitModelRef.hh`、`FunctionalComposeContext.hh` |
| `synthesis/htree/compensation/RootDriverCompensationInternal.hh` | 按职责拆：`RootClosureLoadEstimate.hh`、`RootDriverCompensationCache.hh`、`RootDriverCompensationState.hh` |
| `namespace optimization_internal` | `namespace optimization`（统一对外） |
| `namespace qor_evaluation` | `namespace qor`（或合并入 `icts`） |

---

## 2. `Request` / `Response` 命名痕迹（重点）

`Request` 在 flow 层非常密集，**全部位于 H-tree 解析求解器和 segment_pruning 内部**：

| 类型 / 文件 | 数量级 | 含义（业务语义） |
|---|---|---|
| `AnalyticalSolverRequest` (`AnalyticalSolver.hh:61`) | 40+ 处引用 | 解析求解器的输入参数集合（拓扑、模型、配置） |
| `AnalyticalSolverResult` (`AnalyticalSolver.hh`) | 与 `Request` 配对使用 | 解析求解器输出（候选 + 诊断） |
| `AnalyticalValidationRequest` (`AnalyticalValidation.hh:43`) | 多处 | 候选验证的输入参数 |
| `SegmentFrontierRequest` (`SegmentLibrary.hh:194`, `SegmentPruning.hh:40`, `MakeHTreeSegmentFrontierRequest`) | 多处 | 段前沿剪枝的输入参数 |
| `CharacterizationLibrary::RequestKey` (`CharacterizationLibrary.hh:54-68`) | 私有 | 缓存键 |

**没有发现** `Response`、`Reply`、`Receiver` 等配对词；都是 `Request → Result`。

### 命名建议（CTS 业务语义）

| 当前 | 建议 |
|---|---|
| `AnalyticalSolverRequest` | `AnalyticalSolverInput` / `AnalyticalSolverContext` / `HTreeAnalyticalConfig` |
| `AnalyticalValidationRequest` | `CandidateValidationInput` / `CandidateValidationContext` |
| `SegmentFrontierRequest` | `SegmentFrontierQuery` / `SegmentFrontierConstraints` |
| `MakeHTreeSegmentFrontierRequest(...)` | `MakeSegmentFrontierConstraints(...)` |
| `CharacterizationLibrary::RequestKey` | `CharacterizationLibrary::OptionsKey` / `BuildKey` |

> Web 化的 `Request/Response` 暗示了"客户端-服务器异步交互"，而 CTS 这里完全是同步函数调用，所以建议改成 `Input` / `Context` / `Query` / `Constraints` 这类与硬件 EDA 习惯对齐的词。

---

## 3. `snapshot` 命名痕迹

- **flow 层**：仅一次出现，**类型来自 utils**：
  - `src/operation/iCTS/source/flow/Flow.cc:117` → `schema::SchemaWriter::RuntimeMetricSnapshot total_metric;`（`utils/logger/SchemaScope.cc:120` 实现，不属于 flow 层）。
- **flow 外**（database/io、database/adapter/fast_sta）有大量 `snapshot/Snapshot` 使用：
  - `FastStaBuilder.cc:93`：注释"FastSTA snapshots CTS-owned topology"
  - `FastStaBuilder.cc:119`：`snapshotClockData`
  - `FastStaBuilder.cc:157`：`snapshotSinkPinCaps`
  - `WrapperClockWriter.cc:163`：`SnapshotIdbNetPins`、`IdbNetPinSnapshot`
  - `WrapperClockWriterInternal.hh:42,58,65`：`IdbNetPinSnapshot`、`net_pin_membership_by_name`
  - `FastStaLiberty.hh:21,37`：`snapshotBufferCell`

**结论**：flow 层本身已经几乎清除 snapshot 命名，主要遗留集中在 database 层。这超出本调研范围，但需提醒用户：**真正的 snapshot 问题在 database/io 与 database/adapter/fast_sta**。

---

## 4. `Support` 命名痕迹

flow 层**无**类/文件以 `Support` 命名。

flow 外：
- `src/operation/iCTS/source/database/io/WrapperClockWriterSupport.cc`（database 层）。
- 测试文件 `src/operation/iCTS/test/flow/FlowTestSupport.hh`。

---

## 5. `Manager` / `Handler` / `Helper` / `Util` / `Wrapper`

| 关键字 | flow 层匹配 |
|---|---|
| `Manager` | 无 |
| `Handler` | 无 |
| `Helper` | 仅出现在文件 doc 注释中（`@brief Internal helper contracts for ...`），不是类/函数名 |
| `Util` | 无 |
| `Wrapper` | 仅作为 `#include "io/Wrapper.hh"`（database 层定义），不是 flow 层自定义 |

> `io/Wrapper.hh` 提供 `class Wrapper`（在 database/io），被 flow 层多处 include（见 02 文件 grep 结果）。建议在重构 database 层时考虑改名（如 `IdbAdapter`、`ClockIo`），但**这超出 flow 层范围**。

---

## 命名痕迹汇总表（仅 flow 层内）

| 关键字 | 出现位置 | 计数 | 建议处理优先级 |
|---|---|---|---|
| `Internal`（文件后缀） | 4 个文件 | 4 | 高（拆分） |
| `_internal`（命名空间） | `icts::optimization_internal`、`icts::htree::analytical_solver`（隐式 internal） | 2 个 namespace | 高（重命名） |
| `Request` | `AnalyticalSolverRequest`、`AnalyticalValidationRequest`、`SegmentFrontierRequest`、`RequestKey` | 30+ 处 | 高（业务语义化） |
| `Response` | 无 | 0 | — |
| `snapshot` | `RuntimeMetricSnapshot`（来自 utils） | 1 | 低（flow 外） |
| `Support` | 无 | 0 | — |
| `Manager/Handler/Helper/Util/Wrapper` | 无（flow 内） | 0 | — |

---

## Caveats / 不确定项

- "互联网化"是主观判断，本研究按用户在 PRD 中列出的关键词进行扫描。
- `Result` 是 CTS 业务里常用词（如 `BuildResult`、`SolverResult`），通常不算互联网化，保留即可——但若与新的 `Input/Context` 配对，建议保持一致性（`Input → Result` 而非 `Request → Result`）。
- `_internal` 命名空间不是文件名问题，但属于"内部约定外泄到 API"，建议在重构时同步去除。
