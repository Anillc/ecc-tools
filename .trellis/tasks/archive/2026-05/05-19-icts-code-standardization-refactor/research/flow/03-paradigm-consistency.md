# Research: 范式一致性与 init/run/report 模式适配度

- **Query**: 哪些 sub-flow 已经接近"init/run/report"范式？哪些完全不符合？签名/错误处理/日志是否一致？
- **Scope**: internal
- **Date**: 2026-05-19

## "init / run / report" 三阶段范式适配度评分

| Sub-flow | init | run | report | 状态 |
|---|---|---|---|---|
| **Setup** | `initialize` 全部职责 | — | `emitRuntimeSetup`（与 Flow 由 API 跨调用） | 只有 init/report，无 run |
| **Synthesis** | 嵌在 `run` 内（`clock_layout.reset()`、`set_design_dbu_per_um`） | `run` | 嵌在 `run` 内（`emitSynthesisOverview`、`SCHEMA_WRITER_INST.emitRuntimeMetricTable` 隐式） | 三段杂糅在一个 `run` 函数里 |
| **Instantiation** | 无（直接转发） | `run`（仅转调 `IdbConversion::run`） | 嵌在 `IdbConversion.cc` 内 | 全部 1 段 |
| **Optimization** | 嵌在 `run` 内（`ValidateOptimizationOptions`、`BuildRouteTreeCache` 等准备工作） | `run` | 嵌在 `run` 内（per-clock `EmitClockSummary` + 末尾 `EmitKeyValueTable`） | 145 行单函数三段杂糅 |
| **Evaluation** | `evaluate` 内的 `ClearSummary/ClearStatistics` 完成 init | `run`（含 `beginStage` 与 `evaluate` 调用） | 嵌在 `qor_evaluation::EmitEvaluationSummary` | run 兼 init |
| **Report** | `ResultExport::resolvePaths`（外部前置）+ `Overview::emitReportMode`（report 内首段） | `run` 主体 | `QorReport::write` + `Visualization::emit` | run 内串联 |

> **没有一个 sub-flow 把 init / run / report 拆成显式三阶段**。所有"准备"和"汇总写表"都被塞进同一个 `run` 函数。

## 入口签名一致性问题

1. **入口动词混用**：Setup 用 `initialize` + `emitRuntimeSetup`，其余用 `run`，Synthesis 内部下钻又出现 `build`（HTree、Topology）、`form` (`Topology::formClock`)、`emit`（Visualization）等。
2. **第一形参类型不齐**：
   - Synthesis/Optimization：`ClockLayout&`（输入兼输出）。
   - Evaluation：`EvaluationState&`。
   - Report：先 `const std::string& save_dir`，然后多个 bool + 两个引用。
   - Instantiation：无形参。
   - Setup：两个字符串。
3. **返回类型不齐**：
   - `SynthesisTraceSummary`（无 `success` bool，仅有 `outcome` 枚举）。
   - `OptimizationResult`（自带 `success` bool）。
   - `EvaluationResult`（只有 `evaluation_ready` 一个 bool）。
   - `InstantiationResult`（6 个 bool 字段，多状态）。
   - `ReportResult`（2 个 bool）。
   - `IdbConversionResult`（与 InstantiationResult 高度重叠）。
4. **错误信号方式不齐**：
   - Synthesis 用 `SynthesisOutcome` 枚举（kFinished / kFailed / kNoOp）。
   - Optimization 用 `bool success` + `bool optimized` 双信号。
   - Evaluation 仅 `evaluation_ready`。
   - Report 仅 `report_success` + `evaluation_ready`。
   - Setup 仅返回 `bool`，错误细节只在 `CONFIG_INST.get_last_error()` 与日志中。

## 日志/错误处理一致性

| 维度 | Setup | Synthesis | Instantiation | Optimization | Evaluation | Report |
|---|---|---|---|---|---|---|
| `beginRuntimeMetric` 名称 | 无（在 Flow 内开） | `synthesis` | `instantiation` | `optimization` | `evaluation` | `report` |
| `beginStage` 名称 | 无（仅 `## Runtime Setup` 段） | `CTSFlow` | `Instantiation` | `Optimization` | `Evaluation` | `Report` |
| `emitSection` 数量 | 4 | 5 | 0 | 7 | 0 | 1 |
| `schema::EmitDiagnostic` 使用 | 0 | 0 | 0 | 0 | 2（qor） | 0 |
| `LOG_FATAL_IF` 使用 | 1 (`Setup.cc:91`) | 1 (`Synthesis.cc:200`) | 0 | 0 | 0 | 1 (`Report.cc:47`) |
| 失败回滚 | 仅日志 | 通过 `Topology::resetClockTopology` | 通过 `WrapperWriteResult.rollback_done` 字段 | `(void) FastSTA::eraseClockContext` per-iter | 通过 `qor_evaluation::ClearSummary` | 无（依赖 caller 处理） |

- 报表 Section 名（`emitSection` 字符串）由各 sub-flow 自由约定（`## Runtime Setup` / `## Synthesis Overview` / `## Optimization Overview` / `## Evaluation Overview` / `## Report Overview`），没有统一前缀或注册表。
- Stage 名 `CTSFlow`（Synthesis）与 `Optimization`/`Evaluation` 不一致——Synthesis 使用了**子流程概念混淆**的名字（`CTSFlow` 让人误以为是整体）。
- 错误诊断有 `schema::EmitDiagnostic`、`LOG_ERROR`、`LOG_WARNING`、`LOG_FATAL_IF` 四套调用方式同时存在，且 sub-flow 之间偏好不同。

## 阶段概念混淆点

1. **Topology 类名涵盖 sub-sub-flow**：`synthesis/topology/Topology.hh` 提供 `static formClock(Clock&, ..., const std::vector<ClockDistributionContext>&)`（Topology.hh:144-146）——这是被 `Synthesis::run` 直接调用的高层入口，而非真正"拓扑形成"的低层操作。
2. **`SynthesisTrace::reset` 是孤儿**：`synthesis/trace/SynthesisTrace.hh` 暴露 `static reset(SynthesisTraceSummary&)`，但没有任何 .cc 调用它——`Synthesis.cc` 直接 `summary = SynthesisTraceSummary{}` 自行重置（Synthesis.cc:202）。
3. **Setup 与 Flow 的边界跨 API 层**：`CTSAPI::init` 必须先 `Setup::initialize`，再把布尔结果通过 `FLOW_INST.setSetupReady` 回填 Flow，再调 `outputRuntimeSetup`——这条链路有 3 个调用者参与，不是 sub-flow 内部闭环。
4. **Report 调用 Evaluation**：`Report.cc:62` 调用 `Evaluation::run` 来补做 evaluation。Report 与 Evaluation 之间形成"如果上一阶段没做，就在 report 阶段补做"的隐式回退——这破坏阶段顺序假设。
5. **Optimization 内嵌 per-clock micro-flow**：`Optimization::run` 内手写每个 clock 的 12 步 free function 串联（见 02-subflow-patterns.md 第 4 节），与 Synthesis 的"per-clock synthesizeClock 静态闭包"形式不一致，又与"声明式 stage list"不一致。

## 概念混淆：sub-flow 内嵌入其他 sub-flow

| 案例 | 文件:行 | 说明 |
|---|---|---|
| Report 调用 Evaluation | `Report.cc:62` | 阶段顺序假设被破坏 |
| Instantiation 仅转发 IdbConversion | `Instantiation.cc:32` | "instantiation" 名字与 "idb 转换" 实质重叠 |
| design_conversion 被 readData/Topology/Instantiation 三处使用 | `Flow.cc:153`, `Topology.cc:42`, `Instantiation.cc:26` | 它放在 instantiation 目录下但跨多个 sub-flow 调用，归属不清 |
| Synthesis::run 内部用 Topology::formClock 把 HTree+SourceTrunk+SinkBranch 串成"per-clock pipeline" | `Synthesis.cc:156` + `Topology.cc:150-373`（`class ClockTopologyFormation` 私有 helper） | 真正的子流程被埋在 .cc 匿名命名空间内的辅助类中 |

## Caveats / 不确定项

- "init/run/report" 用户的目标范式没有正式定义，本研究仅对照用户在 PRD 中的描述进行评估。
- 是否需要把 "init" 显式上提（让 caller 控制）取决于是否希望保留懒初始化（如 `CharacterizationLibrary::ensure` 当前在 `HTree::build` 内部按需触发）。
