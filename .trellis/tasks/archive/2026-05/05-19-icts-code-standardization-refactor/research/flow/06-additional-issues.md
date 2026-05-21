# Research: 额外问题（耦合、重复定义、API 边界、错误传播等）

- **Query**: 自由发挥，补充 flow 层独有的其他问题
- **Scope**: internal
- **Date**: 2026-05-19

## 1. 跨 sub-flow 耦合点

### 案例 A：Report → Evaluation（反向调用）

`src/operation/iCTS/source/flow/report/Report.cc:62`：

```cpp
current_evaluation_ready
    = Evaluation::run(evaluation_state, EvaluationOptions{.refresh_sta_timing = refresh_sta_timing, .clock_layout = &clock_layout})
          .evaluation_ready;
```

Report 直接调用 Evaluation::run。这违反"阶段依赖单向"原则，让 `report/CMakeLists.txt:21` 必须 PUBLIC link `icts_source_flow_evaluation`。

### 案例 B：Topology（在 synthesis 内）→ DesignConversion（在 instantiation 内）

`synthesis/topology/Topology.cc:42` include `instantiation/design_conversion/DesignConversion.hh`，并多处调用：

- `Topology.cc:191`：`DesignConversion::commitInsertedObjects(...)`
- `Topology.cc:193`：`DesignConversion::reconnectNet(...)`
- `Topology.cc:263`：`DesignConversion::makeSinkDomainPrefix(...)`
- `Topology.cc:362`：`DesignConversion::restoreClockSourceNetToClockLoads(...)`

但 Synthesis sub-flow 应该独立于 Instantiation，这破坏了 sub-flow 边界。`DesignConversion` 实际上是一组"design 数据操作工具"，被误放到 `instantiation/` 目录下。

### 案例 C：Synthesis → ClockDistribution → DesignConversion

`synthesis/distribution/CMakeLists.txt:12` PRIVATE link `icts_source_flow_instantiation_design_conversion`——synthesis 子模块依赖 instantiation 子模块，构成实际的跨 sub-flow 库依赖。

### 案例 D：Flow.hh 公共契约依赖 sub-sub-flow 类型

`Flow.hh:29-32`：

```cpp
#include "evaluation/qor/QorEvaluation.hh"            // QorSummary, EvaluationState
#include "instantiation/Instantiation.hh"              // InstantiationResult
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"  // CharacterizationLibrary
#include "synthesis/trace/SynthesisTrace.hh"           // SynthesisTraceSummary
```

`CharacterizationLibrary` 是 htree 内部的 cache 类，却被 Flow 当作成员持有（Flow.hh:74）。这说明：

- Flow 不只是编排者，还**充当 sub-flow 间的数据载体**。
- `CharacterizationLibrary` 的作用域横跨 Synthesis 与 Optimization（虽然 Optimization::run 当前 `(void) characterization_library`）。

## 2. 数据结构重复定义

### `BuildOptions` / `BuildResult` 名字重复

| 类 | 嵌套 BuildOptions | 嵌套 BuildResult |
|---|---|---|
| `HTree` | `HTree::BuildOptions`（HTree.hh:57-75） | `HTree::BuildResult`（HTree.hh:141-213） |
| `Topology` | `Topology::BuildOptions`（Topology.hh:51-60）+ `Topology::SourceTrunkBuildOptions`（Topology.hh:62-69） | `Topology::BuildResult`（Topology.hh:98-118）+ `Topology::SourceTrunkBuildResult`（Topology.hh:120-135） |
| `SourceTrunkSegment` | `SourceTrunkSegment::BuildOptions`（SourceTrunkSegment.hh:45） | `SourceTrunkSegment::BuildResult`（SourceTrunkSegment.hh:55） |
| `Drawing` | — | `Drawing::Build`（Drawing.hh:99 `static build`） |

这是名字空间嵌套式 "Options/Result"，调用方需要写：

```cpp
HTree::BuildOptions opts;
Topology::BuildOptions topo_opts;
SourceTrunkSegment::BuildOptions trunk_opts;
```

读者要小心区分。建议提升为命名性更强的全局类型，如 `HTreeBuildOptions`、`HTreeBuildResult`、`TopologySinkBuildOptions`。

### `ClockLayoutPhase` / `SinkDomainKind` 在多处冗余引用

`enum class ClockLayoutPhase` 定义在 `database/design/ClockLayout.hh:55`，但在 flow 层 6+ 处使用并冗余声明 `enum class ClockLayoutPhase;` 前向声明（如 `report/visualization/drawing/Drawing.hh:45`、`synthesis/trace/layout/ClockLayoutBuilder.hh:59`、`synthesis/trace/layout/ClockLayoutAdapter.hh:31`）。**自身是 database 层枚举，但被多个 sub-flow 复用**，没有上层 spec 文档说明它是"sub-flow 跨阶段通用的 phase 标识"。

### `SinkDomainLayoutTopology` 在多处使用

定义在 `synthesis/trace/layout/ClockLayoutBuilder.hh:40-45`，被 `synthesis/distribution/ClockDistribution.hh:65-72` 通过 `makeLayoutTopology()` 内联使用——这构成 distribution → trace/layout 的依赖。distribution 模块为了构造 trace/layout 的输入数据，需要包含 trace/layout 头。

### `LogContext` 在 HTree 与 Topology 间共享

`HTree::LogContext`（HTree.hh:48-55）被 `Topology::BuildOptions` 与 `Topology::SourceTrunkBuildOptions` 嵌套使用（Topology.hh:59,68）。形成 Topology → HTree 的契约依赖（虽然合理，因 Topology::build 内部就是构造 HTree）。

## 3. 接口稳定性与 API 清晰度

### Flow 公共 API 信号语义混乱

`Flow::runCTS()` 是 void 返回，全部状态需通过后续的 `outputSummary()` / `outputRunSummary()` / `evaluation_ready` 等 getter 探查。这对调用方（CTSAPI）来说工作正常，但对外部测试（FlowTest）来说要手动检查多个布尔状态。

### `Setup` 是唯一不通过 Flow 接口的 sub-flow

`CTSAPI::init` 必须先 `Setup::initialize(...)`，再调 `FLOW_INST.setSetupReady(bool)`。Setup 的状态写在 CONFIG_INST 等全局单例，但同步给 Flow 是布尔——状态进出口不对称。

### `Evaluation` 公开 5 个静态方法 vs 其他 sub-flow 公开 1 个

参见 `02-subflow-patterns.md` 表格。Evaluation 暴露 run/evaluate/outputSummary/hasEvaluationResult/reset 5 个，使其难以判断哪个是"主入口"。

## 4. 错误传播路径

### `SynthesisOutcome` 三态枚举

`synthesis/trace/SynthesisTrace.hh:32-37`：

```cpp
enum class SynthesisOutcome { kFinished, kFailed, kNoOp };
```

但 Flow 在判断"是否成功"时要写：

```cpp
const bool run_success = _run_summary.outcome == SynthesisOutcome::kFinished
                      && _run_summary.success
                      && _instantiation_result.instantiation_done;
```

`success` 与 `outcome == kFinished` 同时检查，逻辑冗余。

### `OptimizationResult` 默认 success = true

`Optimization::run` 开头：

```cpp
OptimizationResult result;
result.success = true;  // 默认 true (Optimization.hh:35)
```

不一致：其他 sub-flow Result 默认 `success = false`。

### Setup 失败时的状态污染

Setup 失败（如配置无效）→ `_setup_ready = false`，但 Setup 已经修改了 `CONFIG_INST`、写过 `SCHEMA_WRITER_INST.open()` 创建了日志文件。`Setup` 本身不回滚 partial state，依赖 caller 再 `CTSAPI::resetAPI` 或 `FLOW_INST.reset` 去清理。这是 partial init 风险。

### `Evaluation::run` 返回 `EvaluationResult{ evaluation_ready }`，但 caller 还要看 `hasEvaluationResult` 区分

`Flow.cc:213-215`：

```cpp
_evaluation_ready = Evaluation::run(_evaluation_state, EvaluationOptions{...}).evaluation_ready;
```

但 `Report.cc:53` 还要再判：

```cpp
const bool reused_evaluation_state = evaluation_ready && Evaluation::hasEvaluationResult(evaluation_state);
```

`evaluation_ready` 与 `hasEvaluationResult` 双重信号——为什么不只用其中一个？

## 5. 测试覆盖与依赖注入

### Flow 单例不支持依赖注入

`Flow::getInst()` 是 static local，无法替换；测试代码（`test/flow/FlowTest.cc:60,159` 等）必须通过 `FLOW_INST` 全局宏，每个测试要先 `FLOW_INST.reset()`。

### Sub-flow 静态门面虽便于测试，但跨阶段 mock 困难

由于 `Synthesis::run` 直接 reach into `DESIGN_INST.get_clocks()`、`WRAPPER_INST.queryDbUnit()` 等全局单例，测试时无法注入 mock clocks。所有 sub-flow 测试都需要先初始化完整 design 环境。

### 没有跨 sub-flow 的契约测试

`test/flow/FlowTest.cc` 是端到端测试，没有针对 Synthesis → Optimization → Instantiation 之间数据契约的单元测试。重构时数据格式变化容易出现非预期破坏。

## 6. 注释、文档、命名一致性

### Doxygen 风格不一致

抽样：

- `Setup.hh:21`：`@brief CTS setup entry facade.`
- `Synthesis.hh:21`：`@brief CTS synthesis entry facade.`
- `Instantiation.hh:21`：`@brief CTS instantiation entry facade.`
- `Optimization.hh:21`：`@brief CTS post-synthesis optimization flow facade.`  ← 这个说"flow facade"
- `Evaluation.hh:21`：`@brief CTS evaluation entry facade.`
- `Report.hh:21`：`@brief CTS report entry facade.`

"entry facade" vs "flow facade" 用词不一致。

### 子目录 README 缺失

`flow/` 下 50 个 CMakeLists.txt 但**没有任何 README.md 或 ARCHITECTURE.md**说明每个 sub-flow 的职责和边界。读者必须阅读 .hh + .cc 才能理解阶段语义。

### 私有 helper 的位置混乱

- `SynthesisTrace::reset(SynthesisTraceSummary&)` 在 `SynthesisTrace.hh:74` 公开，但**没有 .cc 调用方**（Synthesis.cc 直接赋值空对象重置）。死代码风险。
- `using SynthesisTraceSummary = SynthesisTraceSummary;`（SynthesisTrace.hh:77）—— 这是自指 type alias，没有实际效果，应删除。

### 大量重复"semantic_owner"硬编码字符串

`IdbConversion.cc:60` 写 `{"semantic_owner", "instantiation"}`，但其他 sub-flow 没有发出 `semantic_owner` 字段。这是不一致的元数据。

## 7. Schema/runtime metric 命名分布

| sub-flow | beginRuntimeMetric 名 | beginStage 名 |
|---|---|---|
| Flow.cc | `total`, `read_data` | `CTS`, `CTSReadData` |
| Setup.cc | （无） | （无 beginStage，只有 `## Runtime Setup` section） |
| Synthesis.cc | `synthesis` | `CTSFlow` ← **不一致**，应为 `Synthesis` |
| Instantiation.cc → IdbConversion.cc | `instantiation` | `Instantiation` |
| Optimization.cc | `optimization` | `Optimization` |
| Evaluation.cc | `evaluation` | `Evaluation` |
| Report.cc | `report` | `Report` |

Synthesis 内部 `beginStage("CTSFlow", "Run CTS synthesis flow", ...)` 用了 `"CTSFlow"`，看起来像"整体 flow"，会让日志读者混淆是否是 Flow.cc 写的。

## 8. 死代码与冗余 using alias

- `synthesis/trace/SynthesisTrace.hh:77` `using SynthesisTraceSummary = SynthesisTraceSummary;` —— 自指别名。
- `synthesis/distribution/ClockDistribution.hh:86-88` 末尾：
  ```cpp
  using ClockSinkDomainRootBufferSpec = ClockDistributionRootBufferSpec;
  using ClockSinkDomainPartition = ClockDistributionPartition;
  using ClockSinkDomainContext = ClockDistributionContext;
  ```
  这些 type alias 没有被任何 .cc 引用（搜索结果为空）；它们应当被删除或替换原始类型名。

## Caveats / 不确定项

- 跨 sub-flow 耦合是历史演进的结果，不一定全部需要改正——例如 Report → Evaluation 的回退逻辑可能是用户希望保留的"补做"行为。
- 部分死代码可能是 ABI/feature 切换占位符；建议在 spec 阶段确认后再删除。
- 测试覆盖率数据需独立测试报告输出（不在本次调研范围）。
