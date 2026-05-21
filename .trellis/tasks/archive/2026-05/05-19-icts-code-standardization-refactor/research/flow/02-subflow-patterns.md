# Research: 各 sub-flow 范式扫描

- **Query**: 逐个剖析 setup/synthesis/instantiation/optimization/evaluation/report 六个 sub-flow 的入口类、阶段划分、输入输出契约、状态持有方式
- **Scope**: internal
- **Date**: 2026-05-19

## 速览表（六个 sub-flow 入口一览）

| Sub-flow | 入口文件 | 入口类 | 主要入口签名 | 返回类型 | 阶段划分 |
|---|---|---|---|---|---|
| setup | `setup/Setup.hh` | `class Setup`（构造禁用） | `static initialize(config_file, work_dir) -> bool` + `static emitRuntimeSetup() -> void` | `bool` / `void` | 只有 init / 二段：initialize + emitRuntimeSetup |
| synthesis | `synthesis/Synthesis.hh` | `class Synthesis`（构造禁用） | `static run(ClockLayout&, CharacterizationLibrary&) -> SynthesisTraceSummary` | `SynthesisTraceSummary` | 一段：单 `run` |
| instantiation | `instantiation/Instantiation.hh` | `class Instantiation`（构造禁用） | `static run() -> InstantiationResult` | `InstantiationResult` | 一段：单 `run` 转发到 `IdbConversion::run` |
| optimization | `optimization/Optimization.hh` | `class Optimization`（构造禁用） | `static run(ClockLayout&, CharacterizationLibrary&) -> OptimizationResult` | `OptimizationResult` | 一段：单 `run`，内部分钟级 8 个子模块手动串联 |
| evaluation | `evaluation/Evaluation.hh` | `class Evaluation`（构造禁用） | `static run(EvaluationState&, ...) -> EvaluationResult` + 4 个静态方法 | `EvaluationResult` | 多入口：run / evaluate / outputSummary / hasEvaluationResult / reset |
| report | `report/Report.hh` | `class Report`（构造禁用） | `static run(save_dir, evaluation_ready, refresh_sta_timing, ClockLayout&, EvaluationState&) -> ReportResult` | `ReportResult` | 一段：单 `run`，内部串联 ResultExport/Overview/QorReport/Visualization |

> 共同点：六个 sub-flow 都是**静态门面类**（`Foo() = delete`），不持有任何成员状态；状态由 caller（即 `Flow` 单例）以引用形参传入。

## 1. Setup（最不规范）

- 入口文件：`src/operation/iCTS/source/flow/setup/Setup.hh`（40 行）
- 入口类：`class Setup`（构造禁用，全部静态）
- **没有 `run`**。有两个独立静态函数：
  - `initialize(config_file, work_dir) -> bool`（Setup.cc:54-96）—— 加载 `CONFIG_INST`、创建运行目录、初始化 `WRAPPER_INST` 与 `STA_ADAPTER_INST`。
  - `emitRuntimeSetup() -> void`（Setup.cc:98-114）—— 打印 Runtime Setup / Configuration / WireRC 三段表格。
- 输入：配置文件路径、工作目录字符串。
- 输出：`bool`（仅表示 init 是否成功）；副作用：写多个 `CONFIG_INST` setter、初始化全局单例。
- 状态：完全靠改写 `CONFIG_INST`、`WRAPPER_INST`、`STA_ADAPTER_INST` 等全局单例。
- 子目录：**无**。

## 2. Synthesis

- 入口文件：`src/operation/iCTS/source/flow/synthesis/Synthesis.hh`（41 行）
- 入口类：`class Synthesis`（构造禁用）
- 入口签名：`static auto run(ClockLayout& clock_layout, CharacterizationLibrary& char_library) -> SynthesisTraceSummary`
- 内部不分阶段，靠 `Synthesis.cc` 内部匿名命名空间的 `synthesizeClock`（Synthesis.cc:94-161）等内嵌函数遍历 clocks。
- 子目录（已经形成 sub-sub-flow 风格）：
  - `synthesis/distribution/` → `class ClockDistribution`（静态：`partitionSinkDomains` + `prepare`）
  - `synthesis/htree/` → `class HTree` + 10 个再下层目录（characterization/analytical_solver/compensation/constraint/embedding/plan/region/segment_pruning/solution/topology_pruning/wirelength）
  - `synthesis/topology/` → `class Topology` + sink/buffer/trunk 子目录
  - `synthesis/trace/` → `class SynthesisTrace`（仅 `reset`） + distance/domain_status/layout/topology_result
- 状态：通过 `clock_layout` 与 `char_library` 形参累加产出；通过 `DESIGN_INST` 修改全局 clock list 上的拓扑。
- 输入：`ClockLayout&`（当作输出累加器）+ `CharacterizationLibrary&`（被 `Topology` 内部 `ensure`）。
- 输出：`SynthesisTraceSummary`（包含 outcome、计数、domain_status 行列表）。

> Synthesis 内部最接近一个 sub-flow，但子目录划分粒度差异大：`distribution` 只有 1 个 .cc，`htree` 有 10 多个子目录。

## 3. Instantiation

- 入口文件：`src/operation/iCTS/source/flow/instantiation/Instantiation.hh`（48 行）
- 入口类：`class Instantiation`（构造禁用）
- 入口签名：`static auto run() -> InstantiationResult`
- `Instantiation.cc:30-41` 只是一个薄包装，**直接调用 `IdbConversion::run()` 并把字段映射到自己 6 个字段的 `InstantiationResult`**。
- 子目录：
  - `instantiation/design_conversion/` → `class DesignConversion`（11 个静态方法，512 行 .cc）— **不是被 Instantiation 直接调用**，而是被 `Flow.cc::readData` 和 `synthesis/topology/Topology.cc` 调用。
  - `instantiation/idb_conversion/` → `class IdbConversion`（仅 `static run`）
- **设计混乱**：`design_conversion` 在 `instantiation/` 目录下，但实际上是被 Flow 的 `readData` 和 Synthesis 的 Topology 同时消费——它"在哪个 sub-flow"是模糊的。

## 4. Optimization（最复杂）

- 入口文件：`src/operation/iCTS/source/flow/optimization/Optimization.hh`（50 行）
- 入口类：`class Optimization`（构造禁用）
- 入口签名：`static auto run(ClockLayout&, CharacterizationLibrary&) -> OptimizationResult`（注意第二个参数在 `Optimization.cc:54` 实际被忽略 `(void) characterization_library;`）
- `Optimization.cc:52-197` 是一个 145 行的 `run` 函数，内部硬编码 8 个阶段的 free function 调用：

```
Optimization::run
 ├── DefaultOptimizationOptions / ValidateOptimizationOptions
 ├── CollectBufferMasterInfos              (preparation)
 ├── BuildRouteTreeCache                   (preparation)
 ├── For each clock:
 │    ├── FastSTA::buildClockContext       (database/adapter/fast_sta)
 │    ├── CaptureGraphProfile              (preparation)
 │    ├── InjectRouteTrees                 (preparation)
 │    ├── CollectOptimizableBuffers        (preparation)
 │    ├── CollectCapBaseline / CollectSlewBaseline (preparation)
 │    ├── ShouldUseScalableSolver          (solver)
 │    ├── SolveClock / SolveClockScalable  (solver)
 │    ├── ApplyMutations                   (mutation)
 │    ├── EmitClockSummary / EmitClockProfile (report)
 │    └── FastSTA::eraseClockContext
 └── 最终 EmitKeyValueTable 总结
```

- 8 个子目录（全部用 `namespace icts::optimization_internal`）：
  - `optimization/candidate/` → `OptimizationCandidates.hh/cc`、`OptimizationScalableCandidates.cc`（同一头文件、两份 cc 实现）
  - `optimization/model/` → 仅 `OptimizationTypes.hh`（230 行，只有结构体）
  - `optimization/mutation/` → `OptimizationMutation.hh/cc`（注意：与 `OptimizationMutation` 结构体重名，仅靠 namespace 区分函数）
  - `optimization/options/` → `OptimizationOptions.hh/cc`（默认值常量 + ValidateOptimizationOptions）
  - `optimization/preparation/` → `OptimizationPreparation.hh/cc`（8 个 free function）
  - `optimization/report/` → `OptimizationReport.hh/cc`（EmitClockSummary/EmitClockProfile + 单位格式化）
  - `optimization/solver/` → `OptimizationSolver.hh/cc`（470 行 .cc）
  - `optimization/state/` → `OptimizationState.hh/cc`（CaptureState/TargetMet/...）
- 状态：通过 `FastSTA` 全局上下文管理；mutation 通过 `ApplyMutations` 改写 `ClockLayout`。
- **接近"模块拆分"但仍然不是 sub-flow**：每个子目录暴露的是一组自由函数，而非阶段类。

## 5. Evaluation

- 入口文件：`src/operation/iCTS/source/flow/evaluation/Evaluation.hh`（48 行）
- 入口类：`class Evaluation`（构造禁用）
- **多入口**（Evaluation.hh:40-45）：
  - `run(EvaluationState&, bool refresh_sta_timing) -> EvaluationResult`
  - `run(EvaluationState&, const EvaluationOptions& options) -> EvaluationResult`
  - `evaluate(EvaluationState&, const EvaluationOptions&) -> void`
  - `outputSummary(const EvaluationState&) -> QorSummary`
  - `hasEvaluationResult(const EvaluationState&) -> bool`
  - `reset(EvaluationState&) -> void`
- `Evaluation.cc` 是 `QorEvaluation` 的薄转发层（76 行，每个函数 1-3 行）。真正实现在 `QorEvaluation`。
- 子目录：`evaluation/qor/`（唯一）—— `QorEvaluation.hh/cc` + `QorEvaluationInternal.hh` + `QorEvaluationMetrics.cc` + `QorEvaluationRootProbe.cc`。
  - `namespace icts::qor_evaluation` 持有 12 个内部 free function（QorEvaluationInternal.hh:56-68）。
- 状态：通过 `EvaluationState`（持有 `QorSummary summary` + `Qor statistics`）显式传递。
- **唯一暴露多个静态入口**的 sub-flow（其它都只有 1 个 `run`）。

## 6. Report

- 入口文件：`src/operation/iCTS/source/flow/report/Report.hh`（48 行）
- 入口类：`class Report`（构造禁用）
- 入口签名：`static auto run(save_dir, evaluation_ready, refresh_sta_timing, ClockLayout&, EvaluationState&) -> ReportResult`
- `Report.cc:44-85` 串联：
  1. `ResultExport::resolvePaths(save_dir)` → 报告/可视化/统计目录
  2. `Overview::emitReportMode(...)` → 写 Markdown 段
  3. （若需要）`Evaluation::run(...)` → 复用 evaluation sub-flow，**这是 Report 调用 Evaluation 的反向依赖**
  4. `QorReport::write(...)`
  5. `Visualization::emit(...)`
- 子目录：
  - `report/export/` → `class ResultExport`（仅 `resolvePaths`）
  - `report/overview/` → `class Overview`（仅 `emitReportMode`）
  - `report/qor/` → `class QorReport`（`write`）+ `class QorFiles`（`writeReports` + `emitLogTables`）
  - `report/visualization/` → `class Visualization`（`emit`）+ 三个子目录 `drawing/`, `gds/`, `svg/`，并且 gds 内部再分 `layer/` + `writer/`。
- 状态：完全无；通过 `evaluation_state` 与 `clock_layout` 形参传入。

## 各 sub-flow 接口签名风格对比

| Sub-flow | 入口动词 | 第一参数类型 | 是否吐 Result |
|---|---|---|---|
| Setup | `initialize` + `emitRuntimeSetup` | 字符串 | `bool` / `void` |
| Synthesis | `run` | `ClockLayout&`（被写入） | `SynthesisTraceSummary` |
| Instantiation | `run` | 无 | `InstantiationResult` |
| Optimization | `run` | `ClockLayout&`（被写入） | `OptimizationResult` |
| Evaluation | `run` / `evaluate` / `outputSummary` / `hasEvaluationResult` / `reset` | `EvaluationState&` | `EvaluationResult` 或 void/bool |
| Report | `run` | 多个参数 | `ReportResult` |

> Synthesis 与 Optimization 都以 `ClockLayout&` 作为输入兼输出，但 Synthesis 返回 `SynthesisTraceSummary` 而 Optimization 返回 `OptimizationResult`——**结果结构体字段名不可比**。

## Caveats / 不确定项

- `Synthesis::run` 实际把 `valid` 状态写在 `clock_layout`（`clock_layout.markSynthesisComplete(summary.success)` Synthesis.cc:250），但 `Optimization` 不写 `markOptimizationComplete`。
- `Optimization::run` 把 `OptimizationResult.success` 设为 `true` 默认，意图是"未运行也算成功"，与其他 sub-flow 不同。
- 子目录中文件以 `Snake_case` 命名（如 `idb_conversion`）但类名是 `PascalCase`，这种映射只是约定，未在 CMakeLists 或 spec 中明文。
