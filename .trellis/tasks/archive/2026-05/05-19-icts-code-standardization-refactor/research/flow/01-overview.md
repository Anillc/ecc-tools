# Research: flow 层顶层入口与编排概览

- **Query**: 调研 iCTS `src/operation/iCTS/source/flow/` 顶层 `Flow.hh/.cc` 的职责、对外接口、编排方式
- **Scope**: internal
- **Date**: 2026-05-19

## 顶层目录结构

```
src/operation/iCTS/source/flow/
├── Flow.hh / Flow.cc            # 单例编排入口（FLOW_INST 宏）
├── CMakeLists.txt               # 顶层 add_subdirectory + icts_source_flow target
├── setup/                       # 配置/路径/STA 初始化（无 run）
├── synthesis/                   # 时钟树综合（含 distribution/htree/topology/trace）
├── instantiation/               # iDB 实例化（含 design_conversion/idb_conversion）
├── optimization/                # 后综合优化（含 8 个子目录）
├── evaluation/                  # 时序/QoR 评估（含 qor）
└── report/                      # 报告/可视化（含 export/overview/qor/visualization）
```

## Flow 类（src/operation/iCTS/source/flow/Flow.hh）

- 通过 `FLOW_INST` 宏暴露为单例：`#define FLOW_INST (icts::Flow::getInst())`（Flow.hh:36, Flow.hh:41-45）。
- 构造/拷贝/移动均被禁用，仅可通过 `getInst()` 取静态实例。
- 持有的状态成员（Flow.hh:70-77）：
  - `SynthesisTraceSummary _run_summary`
  - `ClockLayout _clock_layout`
  - `EvaluationState _evaluation_state`
  - `InstantiationResult _instantiation_result`
  - `CharacterizationLibrary _char_library`
  - `bool _runtime_setup_emitted`
  - `bool _setup_ready`
  - `bool _evaluation_ready`

### 对外接口（Flow.hh:40-56）

| 公共方法 | 返回类型 | 调用方 |
|---|---|---|
| `getInst()` | `Flow&` | `FLOW_INST` 宏所有使用方 |
| `runCTS()` | `void` | `CTSAPI::runCTS()`（api/CTSAPI.cc:69） |
| `readData()` | `bool` | `runCTS` 内部 |
| `run()` | `void` | `runCTS` 与 `FlowTest`（test/flow/FlowTest.cc:60,159） |
| `evaluate()` | `void` | `runCTS` 与 `FlowTest`（test/flow/FlowTest.cc:160,385） |
| `report(save_dir)` | `void` | `CTSAPI::report()`（api/CTSAPI.cc:74） |
| `outputRuntimeSetup()` | `void` | `CTSAPI::init()`（api/CTSAPI.cc:94） |
| `outputSummary()` | `QorSummary` | `CTSAPI::outputSummary()`（api/CTSAPI.cc:99） |
| `outputRunSummary()` | `SynthesisTraceSummary` | `FlowTest`（test/flow/FlowTest.cc:62,162） |
| `setSetupReady(bool)` | `void`（inline setter） | `CTSAPI::init()`（api/CTSAPI.cc:90） |
| `reset()` | `void` | `CTSAPI::resetAPI()`（api/CTSAPI.cc:82） |

### 私有阶段函数（Flow.hh:67-68）

- `instantiate()`（私有）—— `runCTS` 内部调用 `Instantiation::run()`。
- `emitKeyResults(elapsed_s, peak_vmem_mb)`（私有）—— 末尾结果聚合。

## Flow.cc 编排逻辑（Flow.cc:80-137）

`runCTS()` 是主入口，硬编码线性顺序：

```
runCTS
 ├── 1. 重置 runtime metric，开启 "CTS" stage
 ├── 2. 检查 _setup_ready（由 CTSAPI 通过 setSetupReady 注入）
 ├── 3. readData()         // Flow.cc:139-162
 ├── 4. run()              // Flow.cc:164-194 → Synthesis::run + DAG 重建 + Optimization::run + DAG 重建
 ├── 5. instantiate()      // Flow.cc:196-204 → Instantiation::run
 ├── 6. evaluate()         // Flow.cc:206-216 → Evaluation::run
 └── 7. emitKeyResults()   // Flow.cc:235-266
```

注意：`report(save_dir)` 不在 `runCTS` 内部，而是由外部 API 单独调用。

## 与现有模式的关系

- **没有使用任何模板方法 / pipeline / state machine 抽象**。
  - 没有公共抽象基类（如 `IFlowStage`），各 sub-flow 是相互独立的命名类型；
  - 阶段编排是裸的、过程式的 `if/return` 链；
  - 状态依赖通过成员变量传递，错误传播靠手动检查 `SynthesisOutcome` 与 `instantiation_done` 等布尔。
- **Schema 日志阶段**通过 `SCHEMA_WRITER_INST.beginStage()` 显式划分 (Flow.cc:84, 150)，每个 sub-flow 内部各自 begin/finish/skip/failed，但没有统一的封装。

## 与外部 API 的契约

`src/operation/iCTS/api/CTSAPI.cc`（仅 102 行）是 Flow 的唯一外部消费者：

```
CTSAPI::init       → Setup::initialize（非 FLOW_INST 内部）+ FLOW_INST.setSetupReady + FLOW_INST.outputRuntimeSetup
CTSAPI::runCTS     → FLOW_INST.runCTS
CTSAPI::report     → FLOW_INST.report
CTSAPI::resetAPI   → 多个单例 reset（含 FLOW_INST.reset）
CTSAPI::outputSummary → buildFeatureSummary(FLOW_INST.outputSummary())
```

## 关键证据文件

| 文件 | 用途 |
|---|---|
| `src/operation/iCTS/source/flow/Flow.hh` (80 行) | 单例声明 + 全部公共接口 |
| `src/operation/iCTS/source/flow/Flow.cc` (293 行) | 编排实现 + emitKeyResults 大表 |
| `src/operation/iCTS/source/flow/CMakeLists.txt` (47 行) | 顶层 target 装配 |
| `src/operation/iCTS/api/CTSAPI.cc` (102 行) | 唯一外部入口，用 FLOW_INST 宏 |
| `src/operation/iCTS/test/flow/FlowTest.cc` | 测试通过 FLOW_INST 调用 run/evaluate 等子阶段 |

## Caveats / 不确定项

- Flow 既"持有所有阶段输出"又"控制阶段编排"，与"持纯编排，状态由阶段返回值汇总"的模式存在一定混合。
- `report` 不在 `runCTS` 内编排，使得整体执行链断开为两步：`init → runCTS → report`，原因猜测是用户希望先 commit 实例再决定 save_dir，但 PRD 未明确。
- `_setup_ready` 的写者是 `CTSAPI::init`（在 Setup 阶段之后注入），不是 `Setup` 本身写入；导致 Setup 与 Flow 之间的契约通过 API 层中转。
