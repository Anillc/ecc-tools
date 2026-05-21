# W5 baseline counts (2026-05-20)

Captured immediately before W5 (flow paradigm unification) work begins.

## 1. Sub-flow entry signatures (current)

```
Setup::init(const std::string&, const std::string&)  -> bool
Setup::report()                                       -> void
Setup::run()                                          -> SubFlowOutcome             // [legacy]

Synthesis::init(ClockLayout&, CharacterizationLibrary&)                -> bool
Synthesis::run(ClockLayout&, CharacterizationLibrary&)                 -> SynthesisTraceSummary
Synthesis::report(const SynthesisTraceSummary&)                        -> void

Optimization::init(ClockLayout&, CharacterizationLibrary&)             -> bool
Optimization::run(ClockLayout&, CharacterizationLibrary&)              -> OptimizationResult
Optimization::report(const OptimizationResult&)                        -> void

Instantiation::init()                                                  -> bool
Instantiation::run()                                                   -> InstantiationResult
Instantiation::report(const InstantiationResult&)                      -> void

Evaluation::init(EvaluationState&)                                     -> bool
Evaluation::run(EvaluationState&, bool)                                -> EvaluationResult       // overload
Evaluation::run(EvaluationState&, const EvaluationOptions&)            -> EvaluationResult
Evaluation::report(const EvaluationState&)                             -> void

Report::init(const std::string&, bool, const EvaluationState&)         -> bool
Report::run(const std::string&, bool, const ClockLayout&, EvaluationState&) -> ReportResult
Report::report(const ReportResult&)                                    -> void
```

Observation: every sub-flow already exposes `init` / `run` / `report` after Session 62's
range work, but `run()` return type is non-uniform: 5 different `XxxResult` structs +
1 `SubFlowOutcome` enum (Setup only). User feedback 3 explicitly flags this as the
half-finished contract that motivates W5.

## 2. SubFlowOutcome references (source only)

```
src/operation/iCTS/source/flow/SubFlowOutcome.hh:18: * @file SubFlowOutcome.hh
src/operation/iCTS/source/flow/SubFlowOutcome.hh:35:enum class SubFlowOutcome
src/operation/iCTS/source/flow/setup/Setup.cc:35:#include "SubFlowOutcome.hh"
src/operation/iCTS/source/flow/setup/Setup.cc:117:auto Setup::run() -> SubFlowOutcome
src/operation/iCTS/source/flow/setup/Setup.cc:119:  return SubFlowOutcome::kFinished;
src/operation/iCTS/source/flow/setup/Setup.hh:28:#include "SubFlowOutcome.hh"
src/operation/iCTS/source/flow/setup/Setup.hh:41:  static auto run() -> SubFlowOutcome;
```

Only Setup consumes it. W5b deletes the file, W5c migrates the remaining 5 sub-flows.

## 3. CTSFlow schema stage reference (already fixed in earlier wave)

```
src/operation/iCTS/source/flow/Flow.cc:93   beginStage("CTSReadData", ...)
src/operation/iCTS/source/flow/Flow.cc:224  beginStage("CTS", ...)
src/operation/iCTS/source/flow/synthesis/Synthesis.cc:207  beginStage("Synthesis", ...)
```

W5d ("CTSFlow" → "Synthesis") is a no-op: the stage label is already "Synthesis"
at Synthesis.cc:207. Nothing to change.

## 4. Flow orchestration touchpoints (Flow.cc consumers)

`Flow.cc` is the orchestrator: `runReadData` / `runSynthesis` / `runInstantiation`
/ `runEvaluate` are file-local helpers inside the anonymous namespace and they
each call the corresponding sub-flow `init`/`run`/`report`. Each helper threads
its sub-flow's `XxxResult` struct back into `FlowImpl` state for downstream
sub-flows to consume. After W5 the `XxxResult` payload stays on `FlowImpl`; only
the `run()` return value collapses to `FlowStageOutcome`.

`Flow::report` does the same for Report::run.

## 5. Files in scope (W5)

- new: `flow/interface/FlowInterface.hh`, `flow/interface/FlowInterface.cc`,
  `flow/interface/CMakeLists.txt`
- modified: `flow/CMakeLists.txt`, `flow/Flow.cc`, `flow/FlowImpl.hh` (Optimization
  result side-state), `flow/setup/Setup.hh`/`.cc`, `flow/synthesis/Synthesis.hh`/`.cc`,
  `flow/optimization/Optimization.hh`/`.cc`, `flow/instantiation/Instantiation.hh`/`.cc`,
  `flow/evaluation/Evaluation.hh`/`.cc`, `flow/report/Report.hh`/`.cc`
- deleted: `flow/SubFlowOutcome.hh`
- CMake link adjustments: each sub-flow now `PUBLIC` links `icts_source_flow_interface`
  so its public `FlowStageOutcome` return type is visible.
