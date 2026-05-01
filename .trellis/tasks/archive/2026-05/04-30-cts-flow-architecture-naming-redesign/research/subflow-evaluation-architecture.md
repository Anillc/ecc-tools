# Research: subflow evaluation architecture

- Query: Inspect the proposed `evaluation` subflow for task `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`; cover current `src/operation/iCTS/source/flow/evaluation`, `stage/CTSClockTreeEvaluationStep.*`, feature summary integration, STA timing refresh, statistics writer, and tests; determine responsibilities, stable result/data boundaries, whether `Evaluation.hh/.cc` is enough, and what secondary folders are justified.
- Scope: internal
- Date: 2026-04-30

## Findings

### Short Conclusion

`evaluation/Evaluation.hh` and `evaluation/Evaluation.cc` are enough as the root public entry pair, but not enough as the entire subflow if the current behavior is preserved. The root pair should expose the facade and stable evaluation result/state boundary. The current statistics writer is already a separate artifact-output responsibility and should live below a secondary `evaluation/statistics/` folder if it remains owned by evaluation.

The current `ClockTreeEvaluator.cc` is 548 lines and combines physical metric collection, optional STA timing refresh/query, schema summary emission, compatibility-summary projection, and statistics report writing. A minimal redesign can keep physical/timing helpers private in `Evaluation.cc` and move only statistics output to `statistics/`. A broader cleanup can justify `evaluation/physical/` and `evaluation/timing/`, but those folders should not be created unless the refactor actually extracts meaningful code and tests into them.

### Files Found

- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh` - current evaluation facade plus `ClockTreeSummary`, `ClockTreeEvaluationOptions`, and `ClockTreeEvaluationState`.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` - current evaluator implementation for final buffer/area metrics, net wirelength/HPWL, optional STA refresh, timing/skew summaries, statistics validity, and statistics file writing.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh` - current `CTSStatistics` data model plus writer facade.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc` - writes `wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt`; also emits statistics tables into schema logs.
- `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt` - builds `icts_source_flow_evaluation` from `ClockTreeEvaluator.cc` and `CTSStatisticsWriter.cc`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.hh` - thin stage result and stage entry wrapper for evaluation readiness.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc` - stage logging/runtime wrapper around `ClockTreeEvaluator::evaluate`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh` - report stage interface that accepts and may reuse evaluation state.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc` - resolves report/statistics directories, reuses or rebuilds evaluation, writes statistics, and emits visualization/GDS artifacts.
- `src/operation/iCTS/source/flow/FlowManager.hh` - owns persistent `ClockTreeEvaluationState` and `_evaluation_ready`.
- `src/operation/iCTS/source/flow/FlowManager.cc` - calls evaluation after writeback, reuses evaluation in report, and uses evaluation summary in final key results.
- `src/operation/iCTS/api/CTSAPI.hh` - external feature-summary API declaration.
- `src/operation/iCTS/api/CTSAPI.cc` - maps flow evaluation summary into `ieda_feature::CTSSummary`.
- `src/feature/database/feature_icts.h` - external feature summary fields currently consumed by the feature API.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` - full-design timing refresh, propagated-clock setup, timing update/report, timing metric query, and latency/skew query implementation.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc` - installs exact clock-net RC trees into STA from CTS routing trees.
- `src/operation/iCTS/test/flow/FlowManagerTest.cc` - current direct tests touching evaluation log contract and API summary reset behavior.
- `src/operation/iCTS/test/flow/CMakeLists.txt` - flow test target wiring; no separate evaluation test target exists.

### Current Code Volume

Measured with `wc -l` on 2026-04-30:

| File | Lines | Assessment |
|---|---:|---|
| `ClockTreeEvaluator.hh` | 110 | Public facade plus summary/state/options data. |
| `ClockTreeEvaluator.cc` | 548 | Large enough to hide distinct physical, timing, summary, and statistics responsibilities. |
| `CTSStatisticsWriter.hh` | 73 | Separate report data model and writer facade. |
| `CTSStatisticsWriter.cc` | 166 | Enough independent output logic to justify not burying it in `Evaluation.cc`. |
| `CTSClockTreeEvaluationStep.hh/.cc` | 92 total | Thin orchestration wrapper; can be absorbed by `Flow` or a high-level stage facade during architecture cleanup. |
| `CTSClockTreeReportStep.hh/.cc` | 174 total | Report orchestration, not evaluation internals. |

### Evaluation Responsibilities

The evaluation subflow should own:

- Readonly evaluation of committed CTS topology in `Design` and `Clock` after synthesis/instantiation/writeback.
- Final physical/QoR metrics: final clock buffer count, final buffer area, clock-member buffer count, routed clock-net wirelength, HPWL by top/trunk/leaf role, and DBU-to-um conversion.
- Optional timing refresh over the committed/writeback design: refresh full-design STA context, install exact clock-net RC trees, set propagated clocks, update timing, query timing slack/frequency, and query clock latency/skew.
- A stable result/state object for flow/report/API consumers.
- Statistics data production and, if retained in evaluation, statistics report file output.
- Schema/log summaries for evaluation outcomes, or a narrow summary callback used by the report layer.

The evaluation subflow should not own:

- Creating, committing, rolling back, or writing CTS topology objects to iDB. That remains synthesis/instantiation responsibility.
- Clock-tree visualization model construction or SVG/GDS writing. That belongs in report/visualization.
- Public feature API types. Evaluation may produce internal summary data; `CTSAPI` should translate that to `ieda_feature::CTSSummary`.
- Report-root/save-dir policy. Current report step resolves save/statistics/visualization directories; that should stay outside evaluation.
- Raw iSTA or iDB access. All timing and iDB interactions should continue through `STAAdapter` and `Wrapper`.

### Stable Result and Data Boundaries

Recommended stable boundary:

- `Evaluation.hh` should expose the main facade, a coarse `EvaluationState`, a coarse `EvaluationSummary` or `EvaluationResult`, and narrow options such as `refresh_sta_timing`.
- `EvaluationState` should remain flow-owned, reset when new data/synthesis invalidates it, and reused by report when ready.
- `EvaluationSummary` should be the stable data consumed by `Flow` and `CTSAPI`. It can retain compatibility aliases temporarily, but the real field names should stay semantic: final buffer count/area, clock-tree wirelength, timing records, and latency/skew records.
- `Statistics` should be a secondary data boundary used by statistics/report output. It should not become a public feature API unless a real external consumer needs it.
- Physical/timing intermediate types such as clock-net measurements, role classification, RC-tree build details, and STA query failure conditions should stay private to implementation files or secondary internal headers.

Current code already approximates this:

- `ClockTreeSummary` contains stable evaluation fields and compatibility aliases for feature consumers (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:35`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:72`).
- `ClockTreeEvaluationOptions` currently has the single side-effect gate `refresh_sta_timing` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:85`).
- `ClockTreeEvaluationState` pairs summary with `CTSStatistics` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:90`).
- `ClockTreeEvaluator` exposes the right coarse operations: evaluate, output summary, readiness, statistics writing, and reset (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:96`).

### Code Patterns

#### Flow and Stage Integration

- `FlowManager` owns `_evaluation_state` and `_evaluation_ready`, which is the correct persistence boundary for reuse by report and feature API (`src/operation/iCTS/source/flow/FlowManager.hh:68`).
- `FlowManager::readData()` and `FlowManager::run()` reset evaluation state before new flow data/synthesis can invalidate it (`src/operation/iCTS/source/flow/FlowManager.cc:87`, `src/operation/iCTS/source/flow/FlowManager.cc:97`).
- `FlowManager::evaluate()` passes `_writeback_result.writeback_done` as the `refresh_sta_timing` flag, tying STA refresh to successful writeback instead of letting evaluation guess writeback readiness (`src/operation/iCTS/source/flow/FlowManager.cc:115`).
- `FlowManager::report()` passes the current readiness, writeback status, clock-tree view, and evaluation state into the report step, allowing report to reuse or rebuild evaluation (`src/operation/iCTS/source/flow/FlowManager.cc:120`).
- `CTSClockTreeEvaluationStep::run()` is only runtime/schema orchestration around `ClockTreeEvaluator::evaluate` and readiness checking (`src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:31`).
- `CTSClockTreeReportStep::run()` decides whether to reuse evaluation state and only rebuilds evaluation if needed (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:86`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:97`).

#### Feature Summary Integration

- `CTSAPI::outputSummary()` maps from `FLOW_MANAGER_INST.outputSummary()` into the external feature summary, preserving `ieda_feature` as the API boundary (`src/operation/iCTS/api/CTSAPI.cc:93`).
- `buildFeatureSummary()` copies compatibility fields and timing records from the flow summary into `ieda_feature::CTSSummary` (`src/operation/iCTS/api/CTSAPI.cc:41`).
- `ieda_feature::CTSSummary` currently exposes legacy/generic fields such as `buffer_num`, `buffer_area`, `clock_path_min_buffer`, `max_clock_wirelength`, and `clocks_timing` (`src/feature/database/feature_icts.h:25`).
- Evaluation keeps compatibility aliases but warns not to use alias names in `cts.log` until real path/depth traversal exists (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:72`).
- Alias synchronization maps real evaluation metrics into feature-compatible fields (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:102`).

Boundary decision: keep feature-summary translation in `CTSAPI.cc`, not in `evaluation/`. `Evaluation` should not include `feature_icts.h`.

#### STA Timing Refresh and RC Boundary

- The evaluator computes `should_refresh_sta` from `WRAPPER_INST.is_design_ready()` and `options.refresh_sta_timing`, then refreshes full-design STA context and sets propagated clocks only when allowed (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:450`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:452`).
- Exact RC trees are installed into STA per measured clock net when timing refresh is active (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:288`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:298`).
- The evaluator calls `STA_ADAPTER_INST.updateTiming()`, `reportTiming()`, and latency/skew query only after refresh is enabled (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:499`).
- Timing records are queried through `STA_ADAPTER_INST.queryClockTimings()` and copied into evaluation summary records (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:331`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:339`).
- `STAAdapter::updateTiming()` has a hard precondition that full-design context exists and emits a fatal error if refresh was not performed first (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:199`).
- `STAAdapter::refreshFullDesignTimingContext()` converts DB to the timing netlist, loads configured SDC, builds the graph, initializes RC trees, and configures worst-path count (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:224`).
- `STAAdapter::setPropagatedClocks()` mutates STA SDC clock state, not CTS `Design` state (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:241`).
- `STAAdapter::installClockNetRcTree()` resets and rebuilds the STA RC tree for a CTS net by name; raw iSTA objects stay inside the adapter implementation (`src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc:159`).

Boundary decision: optional timing refresh is an adapter/timing side effect, but it is still evaluation-owned because it prepares timing metrics over committed CTS results. It must remain gated by writeback/design readiness and must not write CTS objects back to iDB.

#### Statistics Writer Boundary

- `CTSStatistics` holds wirelength, HPWL, cell-stat, and lib-cell-distribution data (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh:47`).
- `CTSStatisticsWriter::writeReports()` owns filesystem creation and writes the three statistics reports (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:131`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:145`).
- `CTSStatisticsWriter::emitLogTables()` emits the same statistics as schema tables (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:155`).
- `ClockTreeEvaluator::writeStatistics()` guards invalid statistics, resolves fallback `CONFIG_INST.get_statistics_dir()`, delegates report writing, and optionally emits log tables (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:525`).
- `ClockTreeEvaluator::evaluate()` currently writes statistics immediately after computing evaluation (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:512`).
- `CTSClockTreeReportStep::run()` also writes statistics when report runs, using the report-specific statistics directory and no duplicate log tables (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:103`).

Boundary decision: the writer is a separate responsibility. If architecture keeps statistics output under evaluation, put it under `evaluation/statistics/`. If the project later makes `report/` own all artifact output, the writer should move under `report/statistics/` while `evaluation` only returns `Statistics`.

#### Tests

- `FlowManagerTest.EmptyAPIRunEmitsConciseMainLogContract` verifies that the main log contains `## Evaluation Summary`, `CTS Evaluation Summary`, and key result fields, while excluding stale/misleading evaluation/writeback text and raw internal field names (`src/operation/iCTS/test/flow/FlowManagerTest.cc:275`).
- The same test checks the stage summary block for `CTSEvaluation Evaluate CTS clock tree Summary` and status fields (`src/operation/iCTS/test/flow/FlowManagerTest.cc:315`).
- `FlowManagerTest.ResetAPIClearsEvaluationSummary` verifies that evaluation summary is populated after evaluation and cleared through `CTSAPI::resetAPI()` (`src/operation/iCTS/test/flow/FlowManagerTest.cc:574`).
- `src/operation/iCTS/test/flow/CMakeLists.txt` builds only `icts_test_flow_manager` at the flow root and adds H-tree/synthesis subdirectories; there is no dedicated evaluation test target (`src/operation/iCTS/test/flow/CMakeLists.txt:1`).

### Proposed `evaluation/` Shape

Recommended minimal architecture:

```text
src/operation/iCTS/source/flow/evaluation/
  Evaluation.hh
  Evaluation.cc
  statistics/
    Statistics.hh
    StatisticsWriter.hh
    StatisticsWriter.cc
```

In this shape:

- `Evaluation.hh` replaces `ClockTreeEvaluator.hh` as the folder's primary entry.
- `Evaluation.cc` owns orchestration plus private physical/timing helpers if they remain local.
- `statistics/Statistics.hh` owns the current `CTSStatistics`, `CTSCellStats`, and `CTSLibCellDistribution` data if those types need to be included outside `StatisticsWriter.hh`.
- `statistics/StatisticsWriter.*` owns statistics report files and schema statistics tables.

Recommended broader architecture only if extraction is performed:

```text
src/operation/iCTS/source/flow/evaluation/
  Evaluation.hh
  Evaluation.cc
  physical/
    PhysicalMetrics.hh
    PhysicalMetrics.cc
  timing/
    TimingEvaluation.hh
    TimingEvaluation.cc
  statistics/
    Statistics.hh
    StatisticsWriter.hh
    StatisticsWriter.cc
```

Use the broader shape only if the current 548-line evaluator is split into coherent translation units:

- `physical/` is justified if final buffer/area, clock-net role classification, routed wirelength, HPWL, and RC-tree measurement/install logic are moved out of `Evaluation.cc`.
- `timing/` is justified if full-design STA refresh, propagated-clock setup, timing update/report, TNS/WNS/frequency projection, and latency/skew projection are moved out of `Evaluation.cc`.
- `statistics/` is justified now because there is already a separate writer pair and three report formats.

Do not create these secondary folders now:

- `feature/`: feature API mapping belongs in `api/CTSAPI.cc`.
- `sta/`: the business concept is timing evaluation; raw STA details belong in `database/adapter/sta`.
- `rc_tree/`: exact RC installation is an implementation detail of timing/physical evaluation, not a user-visible subflow.
- `schema/` or `logging/`: schema/log emission should stay a narrow output function or move to report, not become a peer architecture folder.
- `stage/`: thin stage wrappers should be absorbed into the top-level flow/report orchestration during the redesign, not recreated under evaluation.

### `Evaluation.hh/.cc` Sufficiency

`Evaluation.hh/.cc` is sufficient for:

- The public folder entry point required by the task architecture rule.
- Coarse options/state/result boundaries.
- Keeping small physical/timing helper functions private while the subflow remains modest.
- A low-risk first rename from `ClockTreeEvaluator` to `Evaluation` or `CTSEvaluation`.

`Evaluation.hh/.cc` is not sufficient for:

- Statistics report writing if the folder-root rule says only the primary entry pair should be at the root.
- Long-term readability if physical metrics, timing refresh, schema output, and report writing all remain in one 500+ line `.cc`.
- Isolating tests for statistics formatting or timing-refresh gating.

Practical recommendation:

1. First pass: create root `Evaluation.hh/.cc` and `evaluation/statistics/` only. Keep physical/timing helpers private unless the edit already touches them heavily.
2. Second pass, if needed: extract `physical/` and `timing/` once tests or code volume make those boundaries concrete.
3. Preserve `FlowManager` ownership of evaluation state and `CTSAPI` ownership of feature projection.

### Related Specs

- `.trellis/spec/backend/directory-structure.md` - defines `source/flow/evaluation/` as readonly evaluation over committed CTS results and requires CTS flow alignment with physical-design stages.
- `.trellis/spec/backend/database-guidelines.md` - says only synthesis/writeback may commit CTS-created topology, while evaluation/report/visualization are readonly consumers; also requires external-tool access to stay inside `Wrapper` and `STAAdapter`.
- `.trellis/spec/backend/quality-guidelines.md` - naming, include, dependency visibility, and forbidden broad snapshot guidance.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - relevant because evaluation crosses flow, database, Wrapper, STAAdapter, feature API, and report outputs.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - relevant before extracting helpers/folders from the current evaluator.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md` - requires re-analysis of each proposed subflow and favors a root entry file aligned with each folder.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md` - current proposal names `evaluation/Evaluation.hh` and `evaluation/Evaluation.cc`, with evaluation owning readonly QoR/timing/physical summaries from instantiated topology.

### External References

- No new external sources were needed for this artifact. The question is local-code architecture specific.
- Existing task research already records relevant external CTS/report terminology in `industry-cts-flow-terminology.md` and `open-source-cts-comparison.md`.

## Caveats / Not Found

- No source code was modified.
- No dedicated `src/operation/iCTS/test/flow/evaluation/` tests were found. Current evaluation coverage is indirect through `FlowManagerTest`.
- No current `Evaluation.hh` or `Evaluation.cc` files exist. The current root public entry is `ClockTreeEvaluator.hh/.cc`.
- The current evaluator writes statistics both during `evaluate()` and again during `report()` when requested. A future report/evaluation split should decide whether immediate statistics writing remains an evaluation side effect or moves entirely to `report`.
- The current `ClockTreeSummary` compatibility aliases intentionally do not represent real source-to-sink path/depth traversal. The redesign should not rename those aliases into user-facing log fields until real traversal metrics exist.
