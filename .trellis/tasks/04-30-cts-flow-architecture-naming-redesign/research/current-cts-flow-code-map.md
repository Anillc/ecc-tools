# Research: current CTS flow code map

- Query: Deeply inspect local CTS flow code under `src/operation/iCTS/source/flow` and related API, test, and CMake entry points. Identify each current directory/subflow's core function, business semantics, primary structs/classes, entry points, dependencies, and responsibility boundaries.
- Scope: internal
- Date: 2026-04-30

## Findings

### Context

- Artifact target: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/current-cts-flow-code-map.md`.
- At inspection time, `.trellis/.current-task` still contained the older duplicate-date path, but the user explicitly corrected the output path and instructed not to write to the old path.
- Relevant Trellis specs establish the expected CTS flow shape as `read data -> synthesis/writeback -> evaluation -> report`, place external entry points under `api/`, place flow orchestration under `source/flow/`, and keep evaluation/report/visualization as readonly consumers except for report file output (`.trellis/spec/backend/directory-structure.md`, `.trellis/spec/backend/database-guidelines.md`, `.trellis/spec/backend/quality-guidelines.md`).

### Files Found

#### API and Top-Level Build Entry Points

- `src/operation/iCTS/CMakeLists.txt` - Defines iCTS root options and adds `external_libs`, `source`, `api`, and `test`.
- `src/operation/iCTS/source/CMakeLists.txt` - Aggregates `database`, `utils`, `flow`, and `module` into the `icts_source` interface target.
- `src/operation/iCTS/api/CMakeLists.txt` - Builds `icts_api` from `CTSAPI.cc` and links `icts_source`.
- `src/operation/iCTS/api/CTSAPI.hh` - Stable external API facade for CLI, flow, reset/init, and feature summary calls.
- `src/operation/iCTS/api/CTSAPI.cc` - Delegates external calls into `FlowManager`, resets global singletons, and initializes run setup.

#### Flow Root

- `src/operation/iCTS/source/flow/CMakeLists.txt` - Adds all flow subdirectories and builds `icts_source_flow`.
- `src/operation/iCTS/source/flow/FlowManager.hh` - Singleton flow orchestrator and owner of run/evaluation/view/writeback state.
- `src/operation/iCTS/source/flow/FlowManager.cc` - Implements the top-level CTS lifecycle, runtime summaries, reporting, summary output, and reset.

#### `run_setup/`

- `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt` - Builds `icts_source_flow_run_setup`.
- `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.hh` - Static setup facade.
- `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc` - Initializes config, work/log/report directories, schema writer, iDB wrapper, and STA adapter.

#### `stage/`

- `src/operation/iCTS/source/flow/stage/CMakeLists.txt` - Builds `icts_source_flow_stage` and links synthesis, utils, database, module, evaluation, netlist, and visualization.
- `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.hh` - Read-data stage interface.
- `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc` - Runs clock data loading through `ClockNetEditor`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.hh` - Synthesis-stage interface returning `CTSClockTreeRunSummary`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc` - Iterates clocks, invokes the per-clock driver, records status, and marks synthesis completion.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.hh` - Writeback result and stage interface.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc` - Writes committed CTS clock data back to iDB through `Wrapper`.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.hh` - Evaluation result and stage interface.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc` - Runs `ClockTreeEvaluator` and returns readiness.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.hh` - Report result and stage interface.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc` - Emits statistics plus SVG/GDS reports, rebuilding evaluation when needed.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeRunSummary.hh` - Mutable run-level counters and synthesis metrics summary.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.hh` - Per-clock synthesis driver result and facade.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc` - Coordinates one clock: rollback, sink partitioning, domain preparation, downstream synthesis, source-to-root synthesis, and view merge.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh` - Sink-domain root-buffer spec, partition, context, and builder interface.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc` - Partitions macro/regular sinks and prepares root buffers plus downstream nets.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.hh` - Per-clock transaction boundary for rollback, synthesis, commit, and source-to-root wiring.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc` - Implements rollback, sink-domain commit, downstream H-tree synthesis, and source-to-root synthesis.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisStatusTable.hh` - Sink-domain status enum and status-table appender.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisStatusTable.cc` - Emits per-clock/per-domain status rows.

#### `synthesis/`

- `src/operation/iCTS/source/flow/synthesis/CMakeLists.txt` - Builds `icts_source_flow_synthesis` and links database, clock-tree view, module, H-tree, and utils.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh` - Main synthesis facade and aggregate option/result types.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - Dispatches sink-tree and source-to-root build calls to internal synthesizers.
- `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.hh` - Internal sink-tree build function declaration.
- `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc` - Validates root net, guards root-net side effects, prepares clustered/direct loads, runs `HTreeBuilder`, and records metrics.
- `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.hh` - Internal source-to-root build function declaration.
- `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc` - Selects source-to-root segment vs top H-tree based on root-input count.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisHtreeOptions.hh` - Option builders from `ClockSynthesis` options to H-tree/segment options.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisHtreeOptions.cc` - Resolves drive/load/slew constraints and H-tree build options.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh` - Synthesis-local net editing helpers and side-effect guards.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc` - Creates temporary buffers/nets, reconnects nets, and restores side effects on failure.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.hh` - Sink-tree load-preparation result and clustering hook.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc` - Optional clustering, cluster-buffer object construction, and H-tree sink preparation.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.hh` - Result-metric transfer helpers.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeSynthesisMetrics.cc` - Moves temporary H-tree/segment objects into synthesis results and records topology-level metadata.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.hh` - Adapter from synthesis result data into clock-tree view inputs.
- `src/operation/iCTS/source/flow/synthesis/ClockTreeViewAdapter.cc` - Converts inserted object topology levels into `ClockTreeView` input records.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisReporter.hh` - Cluster leaf-distance report interface.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisReporter.cc` - Emits cluster leaf-distance tables and reports.

#### `htree/`

- `src/operation/iCTS/source/flow/htree/CMakeLists.txt` - Builds `icts_source_flow_htree` and links database, module, topology, clustering, and utils.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - Main H-tree facade with build options, level plans, inserted object metadata, and build result.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - End-to-end H-tree topology generation, characterization, candidate search, selection, object construction, and logging.
- `src/operation/iCTS/source/flow/htree/HTreeBuilderInternal.hh` - Internal declarations for H-tree helper passes.
- `src/operation/iCTS/source/flow/htree/HTreeBuildOptions.cc` - Resolves boundary/build options from runtime config and caller options.
- `src/operation/iCTS/source/flow/htree/HTreeCandidateTypes.hh` - Candidate, depth-summary, and filter-result structs.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationTypes.hh` - Characterization grid and resolved build option structs.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationCache.hh` - Buffer strength and port lookup caches.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc` - Adapts and runs characterization for topology/requested lengths.
- `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.hh` - Reusable characterization cache facade around `CharBuilder`.
- `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.cc` - Builds/reuses characterization data keyed by runtime/derived options.
- `src/operation/iCTS/source/flow/htree/HTreeClockTopologyBuildContext.hh` - Object-name and build-context state for H-tree materialization.
- `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc` - Builds level plans, characterization length indices, candidate depth plans, and depth candidates.
- `src/operation/iCTS/source/flow/htree/HTreeSegmentCandidateFrontier.cc` - Synthesizes segment candidate frontier sets by composing characterized segment patterns.
- `src/operation/iCTS/source/flow/htree/HTreePatternRegistry.hh` - Pattern registries, pattern composition keys, topology pattern nodes, and combiners.
- `src/operation/iCTS/source/flow/htree/HTreeTopologyAssembly.cc` - Assembles H-tree topology candidate frontiers and selects delay/power candidates.
- `src/operation/iCTS/source/flow/htree/HTreeTopologyDepthEvaluation.cc` - Evaluates one topology depth candidate.
- `src/operation/iCTS/source/flow/htree/HTreeTopologyDepthSearch.cc` - Searches across depth candidates.
- `src/operation/iCTS/source/flow/htree/HTreeSinkLoadProfileTypes.hh` - Sink-load electrical profile legality data structures.
- `src/operation/iCTS/source/flow/htree/HTreeSinkLoadProfile.cc` - Filters candidates by sink-load profile coverage and legality.
- `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc` - Materializes selected H-tree pattern into temporary `Inst`/`Pin`/`Net` objects.
- `src/operation/iCTS/source/flow/htree/HTreeSynthesisSummary.cc` - Emits H-tree synthesis summary tables.
- `src/operation/iCTS/source/flow/htree/HTreeLogging.cc` - Shared H-tree logging/table helpers.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.hh` - Single source-to-root segment builder facade and result/options structs.
- `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc` - Builds/direct-connects one source-to-root segment using segment characterization.

#### `netlist/`

- `src/operation/iCTS/source/flow/netlist/CMakeLists.txt` - Builds `icts_source_flow_netlist` and links clock-tree view, database, design, and utils.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh` - Static facade for flow netlist mutation and final commit.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc` - Reads clock data, partitions sinks, creates root buffers/downstream nets, reconnects source nets, and commits inserted objects into `Design`.

#### `clock_tree_view/`

- `src/operation/iCTS/source/flow/clock_tree_view/CMakeLists.txt` - Builds `icts_source_flow_clock_tree_view`.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh` - Typed readonly CTS metadata model for net roles, inst roles, sink domains, phases, and per-clock records.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc` - Implements view reset, clock/net/inst lookup, insertion, and enum string conversion.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewSynthesisInput.hh` - Narrow synthesis-to-view input structs.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.hh` - Builder facade for sink insts, sink-domain views, source-to-root views, and merges.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc` - Builds/merges typed view records from synthesis topology metadata.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.hh` - Visualization-ready normalized segment/inst/logic/pin model.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc` - Builds visualization model from view data plus `Design` fallbacks.

#### `evaluation/`

- `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt` - Builds `icts_source_flow_evaluation`.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh` - Evaluation summary/state/options and evaluator facade.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc` - Counts buffers/area, classifies/measures clock nets, optionally refreshes STA, writes statistics, and emits evaluation summary.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh` - Statistics data model and writer facade.
- `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc` - Writes `wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt`.

#### `visualization/`

- `src/operation/iCTS/source/flow/visualization/CMakeLists.txt` - Builds static `icts_source_flow_visualization`.
- `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.hh` - SVG visualization result and writer entry point.
- `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc` - Emits design/flyline SVG reports from `ClockTreeVisualizationModel`.
- `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.hh` - GDS visualization result and writer entry point.
- `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc` - Emits design/flyline GDS and layer property files.
- `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh` - Minimal GDSII library/path/boundary/text model and writer facade.
- `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.cc` - Binary GDS and `.lyp` layer property writer implementation.
- `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh` - Semantic layer policy for GDS visualization.
- `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.cc` - Assigns GDS layers/colors by role, phase, sink domain, clock, and topology level.

#### Test Entry Points

- `src/operation/iCTS/test/CMakeLists.txt` - Defines base test targets and `icts_add_test_executable`.
- `src/operation/iCTS/test/flow/CMakeLists.txt` - Builds `icts_test_flow_manager` and adds flow H-tree/synthesis tests.
- `src/operation/iCTS/test/flow/FlowManagerTest.cc` - Covers API/flow log contracts, macro/regular sink-domain semantics, rollback, source-to-root failure, and API reset summary behavior.
- `src/operation/iCTS/test/flow/htree/CMakeLists.txt` - Builds H-tree unit, real-tech smoke/matrix, and optional regression tests.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc` - Unit coverage for H-tree degenerate/early-exit cases.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` - Real-tech smoke tests for H-tree builder.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechMatrixTest.cc` - Real-tech H-tree experiment matrix tests.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechBranchBufferRegressionTest.cc` - Slow branch-buffer regression test gated by CMake option.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeSupport.*` - Real-tech setup and H-tree smoke support.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechMatrixSupport.cc` - ARM9 H-tree matrix runner and report writer.
- `src/operation/iCTS/test/flow/htree/HTreeVisualization*.cc|.hh` - Test artifact SVG/rendering support for H-tree cases.
- `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt` - Builds synthesis unit and real-tech tests.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc` - Unit coverage for invalid inputs, ownership/commit behavior, config, and source-to-root cases.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` - Real-tech synthesis smoke tests.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisNonClusteredRealTechSmokeTest.cc` - Non-clustered real-tech synthesis smoke tests.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechMatrixTest.cc` - Real-tech synthesis experiment matrix tests.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechMatrixSupport.cc` - Synthesis matrix runner and report writer.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTech*Assertions.cc` - GTest assertions for real-tech artifact, cluster, H-tree, and selection validation.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechClusterValidation.cc` - GTest-free cluster-buffer connectivity validation.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.*` - Test artifact and SVG support for synthesis cases.

### Current Flow Entry Points and Call Chain

- Public entry is `CTSAPI`, a singleton-style static facade with `runCTS`, `report`, `resetAPI`, `init`, and `outputSummary` (`src/operation/iCTS/api/CTSAPI.hh:35`, `src/operation/iCTS/api/CTSAPI.hh:44`).
- `CTSAPI::runCTS()` delegates directly to `FLOW_MANAGER_INST.runCTS()`; `report()` delegates to `FLOW_MANAGER_INST.report(save_dir)` (`src/operation/iCTS/api/CTSAPI.cc:67`, `src/operation/iCTS/api/CTSAPI.cc:72`).
- `CTSAPI::resetAPI()` resets config, design, wrapper, flow manager, and schema writer; `init()` resets then calls `CTSRunSetup::initialize(...)` and emits runtime setup (`src/operation/iCTS/api/CTSAPI.cc:77`, `src/operation/iCTS/api/CTSAPI.cc:86`).
- `FlowManager` exposes `runCTS`, `readData`, `run`, `evaluate`, `report`, `outputRuntimeSetup`, `outputSummary`, `outputRunSummary`, and `reset` (`src/operation/iCTS/source/flow/FlowManager.hh:46`).
- `FlowManager` owns the current run summary, clock-tree view, evaluation state, writeback result, runtime-setup emission flag, and evaluation-ready flag (`src/operation/iCTS/source/flow/FlowManager.hh:68`).
- Main execution order is hard-coded in `FlowManager::runCTS()`: reset runtime metrics, begin stage, `readData()`, `run()`, `writeback()`, `evaluate()`, then emit runtime/key results (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`).
- `readData()` clears run/view/evaluation/writeback state and calls `CTSClockDataLoadStep::run()` (`src/operation/iCTS/source/flow/FlowManager.cc:87`).
- `run()` clears evaluation/writeback state and invokes `CTSClockTreeSynthesisStep::run(_clock_tree_view)` (`src/operation/iCTS/source/flow/FlowManager.cc:97`).
- `writeback()` calls `CTSClockTreeWritebackStep::run()` only when synthesis succeeded, then marks the view writeback state and folds writeback status into run success (`src/operation/iCTS/source/flow/FlowManager.cc:105`).
- `evaluate()` calls `CTSClockTreeEvaluationStep::run(...)`; `report()` delegates to `CTSClockTreeReportStep::run(...)`, which may reuse or rebuild evaluation (`src/operation/iCTS/source/flow/FlowManager.cc:115`, `src/operation/iCTS/source/flow/FlowManager.cc:120`).
- `outputSummary()` intentionally returns an empty summary until `_evaluation_ready` is true (`src/operation/iCTS/source/flow/FlowManager.cc:163`).

### Directory/Subflow Responsibility Map

| Directory | Core function | Business semantics | Primary entry points | Primary structs/classes | Dependencies and boundary |
|---|---|---|---|---|---|
| `api/` | External CTS facade | Public CLI/flow/feature calls, lifecycle reset/init | `CTSAPI::runCTS`, `CTSAPI::report`, `CTSAPI::init`, `CTSAPI::outputSummary` | `CTSAPI` | Depends on `icts_source`; does not own internal stages (`src/operation/iCTS/api/CMakeLists.txt:5`). |
| `source/flow/` | Root flow orchestration | Stage ordering, run/evaluation state, runtime summary | `FlowManager::runCTS`, `readData`, `run`, `writeback`, `evaluate`, `report` | `FlowManager` | Publicly links stage/evaluation and privately links DB/module/netlist/visualization/run_setup/utils (`src/operation/iCTS/source/flow/CMakeLists.txt:21`). |
| `run_setup/` | Runtime setup | Config, work dir, `cts.log`, visualization/statistics dirs, iDB and STA adapter initialization | `CTSRunSetup::initialize`, `emitRuntimeSetup` | `CTSRunSetup` | May touch config/wrapper/STA/schema. No synthesis behavior (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:53`). |
| `stage/` | Stage sequencing plus per-clock coordination | Clock iteration, sink-domain split, transaction/rollback, writeback/evaluation/report wrappers | `CTSClockDataLoadStep::run`, `CTSClockTreeSynthesisStep::run`, `ClockTreeSynthesisDriver::run`, `ClockTreeSynthesisTransaction::*`, writeback/eval/report steps | `CTSClockTreeRunSummary`, `ClockSinkDomainContext`, `ClockTreeSynthesisDriver`, `ClockTreeSynthesisTransaction`, `ClockTreeSynthesisStatusTable` | Boundary between flow lifecycle and synthesis/netlist/evaluation/visualization internals. Coordinates but does not implement H-tree search. |
| `synthesis/` | Generic clock-tree synthesis assembly | Validates root/source nets, optional sink clustering, H-tree/source-to-root dispatch, temporary object ownership | `ClockSynthesis::build`, `ClockSynthesis::buildSourceToRoot` | `ClockSynthesis::{BuildOptions, BuildResult, SourceToRootBuildOptions, SourceToRootBuildResult}`, side-effect guards | Publicly exposes database/view/module/H-tree. It builds temporary objects and returns owned results; commit happens in stage/netlist. |
| `htree/` | H-tree algorithm pipeline | Topology generation, characterization, segment/frontier composition, depth search, candidate filtering/selection, temporary object materialization | `HTreeBuilder::build`, `SourceToRootSegmentBuilder::build`, `CharacterizationLibrary::ensure` | `HTreeBuilder::{BuildOptions, LevelPlan, BuildResult}`, `CharacterizationLibrary`, pattern/candidate/profile structs | Algorithmic internals depend on topology/characterization modules and database types, but do not commit final objects into `Design`. |
| `netlist/` | Flow netlist mutation and commit boundary | Read clocks, partition macro/regular sinks, insert root buffers/downstream nets, reconnect source net, validate/commit objects into `Design` | `ClockNetEditor::*` | `ClockNetEditor` | Owns mutation helpers for `Design`/`Clock` final membership. This is the narrow commit boundary. |
| `clock_tree_view/` | Typed readonly metadata store | Captures CTS net/inst roles, sink domain, phase, topology levels for report/visualization | `ClockTreeViewBuilder::*`, `ClockTreeVisualizationModelBuilder::build` | `ClockTreeView`, `ClockTreeViewNet`, `ClockTreeViewInst`, `ClockTreeViewSegment`, visualization model structs | Report-only typed data. May use `Design` fallbacks for visualization model, but does not write CTS topology. |
| `evaluation/` | Readonly final CTS evaluation | Counts final buffers/area, classifies clock nets, route/RC/timing metrics, statistics reports | `ClockTreeEvaluator::evaluate`, `writeStatistics`, `outputSummary` | `ClockTreeSummary`, `ClockTreeEvaluationState`, `CTSStatistics`, `CTSStatisticsWriter` | Readonly over committed CTS results, except optional STA timing/RC updates and file output. |
| `visualization/` | Readonly file writers | Emits SVG and GDS views from visualization model | `EmitClockTreeSvgVisualizations`, `EmitClockTreeGdsVisualizations` | `ClockTreeGdsWriter`, `ClockTreeVisualizationLayerPolicy`, visualization result structs | Consumes clock-tree view/model and writes report artifacts only. |
| `test/flow/` | Flow regression coverage | Verifies API/log contracts, transaction rollback, ownership, H-tree/synthesis behavior | CMake test executables and GTest files | Test fixtures/helpers | Mirrors flow/H-tree/synthesis responsibilities; real-tech tests are separately gated. |

### Subflow Semantics and Boundaries

#### `run_setup/`

- `CTSRunSetup::initialize()` initializes `CONFIG_INST`, resolves/creates work, visualization, and statistics directories, opens `cts.log`, initializes `WRAPPER_INST` from `dmInst->get_idb_builder()`, and initializes `STA_ADAPTER_INST` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:53`).
- `emitRuntimeSetup()` emits path/config/wire-RC setup reports via schema/config/STA adapter helpers (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:86`).
- Responsibility boundary: setup owns runtime environment and external adapter readiness. It must not synthesize trees, mutate per-clock topology beyond initialization, or write reports other than runtime setup.

#### `stage/`

- `CTSClockDataLoadStep::run()` starts `read_data`, emits input/clock sections, and delegates clock ingestion to `ClockNetEditor::readClockData()` (`src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:31`).
- `CTSClockTreeSynthesisStep::run()` resets the view, sets DBU, iterates `DESIGN_INST.get_clocks()`, calls `ClockTreeSynthesisDriver::run` for each clock, aggregates success/skipped/failed/domain/sink counters, and marks synthesis complete (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:63`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:82`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:101`).
- `ClockTreeSynthesisDriver::run()` is the per-clock coordinator. It rolls the clock back, resolves source net, partitions sinks, appends sink insts into a per-clock view, creates one `CharacterizationLibrary`, prepares non-empty macro and regular domains, synthesizes each downstream domain, synthesizes source-to-root, then merges the per-clock view into the global view (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:44`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:49`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:70`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:108`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:119`).
- `ClockSinkDomainBuilder` models sink-domain preparation with `ClockSinkDomainRootBufferSpec`, `ClockSinkDomainPartition`, and `ClockSinkDomainContext`; context captures the domain, object prefix, sinks, root buffer pins, and downstream net (`src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:41`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:48`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.hh:55`).
- `ClockSinkDomainBuilder::partitionSinkDomains()` delegates macro/regular sink classification to `ClockNetEditor`; `prepare()` creates root buffer and downstream net, populating the context (`src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:47`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:55`).
- `ClockTreeSynthesisTransaction` is the per-clock safety boundary. It can rollback, collect root inputs, synthesize/commit sink domains, and synthesize source-to-root (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.hh:42`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.hh:49`).
- Rollback restores the clock source net to original clock loads and clears CTS membership (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:119`).
- Sink-domain commit first builds a pending clock-tree view, then commits inserted objects through `ClockNetEditor::commitInsertedObjects`; on failure it reconnects the downstream net, records failure, and rolls back (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`).
- Downstream synthesis uses `ClockSynthesis::build` with options carrying object prefix, sink-clustering flag, shared characterization library, source-to-root lengths, and log context (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:178`).
- Source-to-root synthesis uses `ClockSynthesis::buildSourceToRoot`; successful results are adapted into `ClockTreeView`, committed through `ClockNetEditor`, and recorded in the run summary (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:216`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:254`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:257`).
- Writeback is separated from synthesis object commit: `CTSClockTreeWritebackStep::run()` calls `WRAPPER_INST.writeClocks(clocks)` when wrapper design is ready (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:48`).
- Evaluation and report steps are wrappers around lower-level consumers: evaluation calls `ClockTreeEvaluator::evaluate`, while report writes statistics plus SVG/GDS and may rebuild evaluation (`src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:31`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:75`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:103`).
- Responsibility boundary: `stage/` owns orchestration, per-clock transaction decisions, status rows, and stage-level logging. It does not own the H-tree algorithm and should not own low-level netlist commit mechanics beyond calling `ClockNetEditor`.

#### `synthesis/`

- `ClockSynthesis` is the facade. `BuildOptions` holds sink clustering, object prefix, characterization library, additional characterization lengths, and log context; `BuildResult` owns H-tree result, optional clustering result, cluster-buffer metadata, synthesis metrics, and temporary inserted objects (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:43`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:46`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:89`).
- Source-to-root uses `SourceToRootBuildOptions`, `SourceToRootStage`, and `SourceToRootBuildResult`, with either `kSegment` or `kHTree` as the selected implementation path (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:55`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:62`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:111`).
- `ClockSynthesis.cc` is intentionally thin: sink-tree build delegates to `clock_synthesis::BuildSinkTree`; source-to-root delegates to `clock_synthesis::BuildSourceToRootTree` (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:36`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:41`).
- `BuildSinkTree()` validates the root driver and loads, guards root-net side effects, prepares clustered/direct H-tree sinks, invokes `HTreeBuilder::build`, restores on failure, records metrics, and returns temporary owned objects without committing them (`src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:54`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:57`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:66`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:77`).
- `BuildSourceToRootTree()` validates roots, guards source-net side effects, reconnects source net to root inputs, uses `SourceToRootSegmentBuilder` for one root input, and uses `HTreeBuilder` for multiple root inputs (`src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:37`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:60`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:62`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:76`).
- Optional sink clustering is resolved by `PrepareSinkTreeLoads()`: disabled mode passes original root loads through, enabled mode runs `TopologyGen::defaultFastClustering`, creates cluster buffers/sink nets, and returns cluster-buffer inputs as H-tree sinks (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:180`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:187`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:193`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisSinkClustering.cc:205`).
- `ClockSynthesisNetEditor` provides synthesis-local helpers and RAII-like side-effect guards (`RootNetSideEffectGuard`, `SourceNetSideEffectGuard`) to restore borrowed net/pin state when builds fail (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:52`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.hh:73`).
- Responsibility boundary: `synthesis/` assembles clock-tree build results and manages temporary objects and temporary net side effects. Final commit into `Design` belongs to `ClockNetEditor` called from `stage/`.

#### `htree/`

- `HTreeBuilder` is the main algorithm facade. `BuildOptions` controls branch buffer forcing, top input slew, depth exploration, topology root location, characterization library, root-driver sizing, topology load interpretation, log context, and object prefix (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:45`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:57`).
- `HTreeBuilder::BuildResult` records topology, level plans, best char/pattern, characterization metadata, depth/candidate metrics, load profile data, fallback information, temporary inserted objects, and root pins/nets (`src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:101`).
- `HTreeBuilder::build()` validates root driver and loads, builds topology through `TopologyGen::build`, rejects degenerate no-H-tree topologies, runs characterization, resolves build options, builds level plans/depth candidates, synthesizes segment entry sets, searches depth candidates, filters candidate pools by sink-load profile coverage, selects the best entry, materializes the chosen topology pattern, optionally validates/applies root-driver sizing, logs summary, and returns temporary objects (`src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:81`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:95`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:104`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:127`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:143`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:150`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:164`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:173`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:178`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:185`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:252`, `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:281`).
- `CharacterizationLibrary` caches reusable `CharBuilder` results by request key and reports whether a request was reused (`src/operation/iCTS/source/flow/htree/CharacterizationLibrary.hh:34`, `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.hh:37`, `src/operation/iCTS/source/flow/htree/CharacterizationLibrary.cc:53`).
- `RunCharacterizationFlow()` collects topology level lengths plus caller-supplied source-to-root lengths, adapts the characterization grid, reports grid decisions, and ensures characterization is available (`src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:76`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:80`, `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc:124`).
- `BuildLevelPlans()` derives per-topology-level average segment lengths and aligned length indices; `ResolveDepthCandidates()` picks a target depth or a window of deepest candidates (`src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc:177`, `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc:296`).
- `SynthesizeSegmentEntrySets()` composes base segment chars into required length-index frontiers (`src/operation/iCTS/source/flow/htree/HTreeSegmentCandidateFrontier.cc:334`).
- `EvaluateCandidateBuild()` assembles topology pattern frontiers, applies boundary and sink-load filters, selects best candidates, and marks boundary fallback when needed (`src/operation/iCTS/source/flow/htree/HTreeTopologyAssembly.cc:341`).
- `FilterSinkLoadProfileLegalEntries()` and `FilterGlobalEntriesBySinkLoadProfileCoverage()` ensure candidates cover real sink-load electrical profiles (`src/operation/iCTS/source/flow/htree/HTreeSinkLoadProfile.cc:367`, `src/operation/iCTS/source/flow/htree/HTreeSinkLoadProfile.cc:388`).
- `BuildClockTreeObjects()` traverses selected topology levels bottom-up, materializes segment objects into temporary `Inst`/`Pin`/`Net` containers, connects the root net, and prunes redundant single-load leaf buffers (`src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:472`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:524`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:561`, `src/operation/iCTS/source/flow/htree/HTreeClockTreeObjectBuilder.cc:565`).
- `SourceToRootSegmentBuilder` is a single-segment top-level builder. It direct-connects zero-length source/root pairs, otherwise resolves characterization boundary indices, synthesizes the needed segment frontier, selects a segment entry, and materializes source-to-root segment objects (`src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.hh:42`, `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc:352`, `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc:365`, `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc:423`, `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc:436`, `src/operation/iCTS/source/flow/htree/SourceToRootSegmentBuilder.cc:462`).
- Responsibility boundary: `htree/` owns clock-tree topology/characterization/search/materialization internals. It should not own per-clock flow policy, sink-domain splitting, writeback to iDB, or final `Design` commit.

#### `netlist/`

- `ClockNetEditor` exposes the flow netlist mutation surface: read clock data, partition sinks, make sink-domain prefixes, add root buffers, reconnect nets, connect downstream nets, restore/reuse source nets, and commit inserted objects (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh:39`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh:44`).
- `readClockData()` reads clock/net pairs from config netlist or `WRAPPER_INST.collectClockNetPairs()`, calls `WRAPPER_INST.readClocks`, and emits design clock distribution summary (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:300`).
- `partitionClockSinks()` classifies sinks by macro block vs regular inst (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:327`).
- `addRootBufferForSinkDomain()` creates a root buffer at a sink-domain location with a minimum-drive buffer or explicit root-buffer spec (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:354`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:370`).
- `reconnectNet()` rewires a net and corresponding pin back-pointers (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:379`).
- `connectSinkDomainDownstreamNet()` creates a domain downstream net; `restoreClockSourceNetToClockLoads()` returns source net to original clock loads; `reuseClockSourceNetAsSourceToRootBuffers()` reconnects source net to root-buffer inputs (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:411`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:417`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:430`).
- `commitInsertedObjects()` validates duplicate/name collisions before moving temporary `Inst`/`Pin`/`Net` ownership into `DESIGN_INST` and adding committed inst/net membership to the `Clock` (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:444`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:502`).
- Responsibility boundary: `netlist/` is the final mutation/commit layer for `Design` and `Clock` membership. It is deliberately narrower than synthesis and H-tree.

#### `clock_tree_view/`

- `ClockTreeView` defines narrow typed metadata: net roles (`kClockSource`, `kSourceToRoot`, `kDownstream`, `kSinkTree`), inst roles (`kLogicCell`, `kClockSource`, `kClockLoad`, `kRootBuffer`, `kHTreeBuffer`, `kSourceRootBuffer`), synthesis phases, sink domains, and view modes (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:35`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:44`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:55`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:64`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:72`).
- View records are explicit and per-clock: segments, nets, insts, and clocks (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:79`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:95`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:110`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:126`).
- `ClockTreeView` stores synthesis/writeback completion flags but is otherwise readonly metadata for consumers (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:135`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:142`).
- `ClockTreeViewBuilder` appends sink insts, direct sink-domain metadata, full sink-domain view records, source-to-root view records, and merges views (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.hh:46`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:200`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:217`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:231`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:263`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeViewBuilder.cc:285`).
- `ClockTreeVisualizationModelBuilder::build()` consumes `ClockTreeView` and uses `DESIGN_INST` fallbacks when design/flyline segments or inst geometry are incomplete (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.hh:83`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeVisualizationModel.cc:330`).
- Responsibility boundary: `clock_tree_view/` owns typed reporting/visualization metadata, not final topology ownership or commit.

#### `evaluation/`

- `ClockTreeSummary` holds evaluation/feature summary metrics and compatibility aliases for existing feature consumers (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:35`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:72`).
- `ClockTreeEvaluationState` pairs summary and `CTSStatistics`; `ClockTreeEvaluator` exposes evaluate, summary, readiness, statistics write, and reset methods (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:90`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:96`).
- `ClockTreeEvaluator::evaluate()` clears state, optionally refreshes STA timing and propagated clocks, counts final clock buffers/area, measures/classifies clock source and inserted nets, updates timing/latency/skew when requested, syncs compatibility aliases, marks statistics/summary ready, emits summary, and writes statistics (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:443`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:450`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:452`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:459`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:483`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:499`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:506`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:509`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:512`).
- `CTSStatistics` separates top/trunk/leaf/total/max wirelengths, HPWL equivalents, cell stats, and library cell distribution (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh:47`).
- `CTSStatisticsWriter::writeReports()` creates the statistics directory and writes `wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt` (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:131`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:145`).
- Responsibility boundary: `evaluation/` is readonly over committed CTS topology, except optional STA timing/RC updates and report-file output.

#### `visualization/`

- SVG visualization builds a `ClockTreeVisualizationModel`, rejects empty clock/model data, writes `cts_design.svg` and `cts_flyline.svg`, emits report status rows, and returns aggregate success (`src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.hh:34`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:427`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:430`, `src/operation/iCTS/source/flow/visualization/ClockTreeSvgVisualization.cc:445`).
- GDS visualization builds design/flyline libraries from the model, emits GDS binaries and layer property files, then reports success (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.hh:34`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:170`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:186`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsVisualization.cc:191`).
- `ClockTreeGdsWriter` owns the minimal file-format model and writer entry points; `ClockTreeVisualizationLayerPolicy` maps semantic roles to GDS layer keys/properties (`src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh:35`, `src/operation/iCTS/source/flow/visualization/ClockTreeGdsWriter.hh:78`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:37`, `src/operation/iCTS/source/flow/visualization/ClockTreeVisualizationLayerPolicy.hh:47`).
- Responsibility boundary: `visualization/` should stay a readonly artifact writer from `ClockTreeVisualizationModel` and not embed synthesis/evaluation policy.

### Current Business Semantics

- The current implementation is not a single monolithic CTS "synthesis" step. It is a staged flow: setup, read clocks, per-clock sink-domain synthesis, commit/writeback, evaluation, and reporting.
- Per clock, sinks are split into `hard_macro` and `regular` domains. Each non-empty domain gets a root buffer and a downstream net; those downstream roots are then connected to the clock source by a source-to-root path.
- Downstream domains use H-tree synthesis when they have at least the minimum synthesis sink count; single-sink domains are represented as direct root-buffer/downstream-net views without H-tree synthesis (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:181`).
- Source-to-root synthesis is top-level glue between clock source and downstream root buffer inputs: single root input uses a characterized segment, multiple root inputs use H-tree (`src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:62`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:76`).
- Synthesis/H-tree results own temporary algorithm-created objects through `std::unique_ptr`; successful stage transactions commit them into `Design`; failures destruct temporary objects and rollback net/member state (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:104`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:502`).
- iDB writeback is later and distinct from internal `Design` commit. Internal commit builds the CTS `Design` model; writeback calls `WRAPPER_INST.writeClocks` to push to iDB (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:48`).
- `ClockTreeView` is a reporting/visualization metadata side channel. It records typed roles and phases at synthesis time and is used later by visualization; final `Design` remains the topology owner.

### CMake and Dependency Boundaries

- iCTS root CMake adds external libs, source, API, and tests in that order (`src/operation/iCTS/CMakeLists.txt:44`).
- `icts_source` is an interface aggregator linking database, utils, flow, and module (`src/operation/iCTS/source/CMakeLists.txt:11`, `src/operation/iCTS/source/CMakeLists.txt:13`).
- Flow subdirectories are declared in a fixed set: `clock_tree_view`, `evaluation`, `htree`, `netlist`, `visualization`, `run_setup`, `stage`, and `synthesis` (`src/operation/iCTS/source/flow/CMakeLists.txt:1`).
- `icts_source_flow` compiles only `FlowManager.cc`, exposes `${ICTS_FLOW}`, publicly links evaluation/stage, and privately links database, module, netlist, visualization, run_setup, and utils (`src/operation/iCTS/source/flow/CMakeLists.txt:19`, `src/operation/iCTS/source/flow/CMakeLists.txt:21`).
- `stage` publicly links `synthesis` and `utils` and privately links database/module/evaluation/netlist/visualization, reflecting its orchestration role (`src/operation/iCTS/source/flow/stage/CMakeLists.txt:14`).
- `synthesis` publicly links database, clock-tree view, module, and H-tree, and privately links utils (`src/operation/iCTS/source/flow/synthesis/CMakeLists.txt:14`).
- `htree` privately links database, module, topology, topology clustering, and utils (`src/operation/iCTS/source/flow/htree/CMakeLists.txt:25`).
- `clock_tree_view` publicly exposes database spatial types and privately uses database, routing module, and utils (`src/operation/iCTS/source/flow/clock_tree_view/CMakeLists.txt:8`).
- `evaluation` privately links database, module, timing module, and utils (`src/operation/iCTS/source/flow/evaluation/CMakeLists.txt:7`).
- `visualization` publicly links clock-tree view and database spatial, and privately uses database, routing module, and utils (`src/operation/iCTS/source/flow/visualization/CMakeLists.txt:10`).
- `netlist` privately links clock-tree view, database, database design, and utils (`src/operation/iCTS/source/flow/netlist/CMakeLists.txt:3`).
- `run_setup` privately links `idm`, database, and utils (`src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:3`).

### Test Coverage Map

- Test infrastructure links test executables to `icts_test_base`, `icts_source`, `icts_api`, and external test libs via `icts_add_test_executable` (`src/operation/iCTS/test/CMakeLists.txt:34`).
- Flow tests build `icts_test_flow_manager`, then add H-tree and synthesis test subdirectories (`src/operation/iCTS/test/flow/CMakeLists.txt:1`, `src/operation/iCTS/test/flow/CMakeLists.txt:13`).
- `FlowManagerTest` covers empty run callability, API run log contract, macro/regular sink-domain terminology, separate downstream nets for mixed macro/regular single-sink domains, repeated preparation rollback, root-buffer/downstream-net failure rollback, inserted-object commit failure, source-to-root rollback, and API reset summary behavior (`src/operation/iCTS/test/flow/FlowManagerTest.cc:268`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:275`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:344`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:369`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:415`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:450`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:476`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:502`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:546`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:574`).
- H-tree unit tests cover empty loads, explicit build options with empty loads, missing root driver, and single-load early exit (`src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc:55`, `src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc:78`, `src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc:114`, `src/operation/iCTS/test/flow/htree/HTreeBuilderTest.cc:135`).
- H-tree CMake builds unit, real-tech, matrix, and optional slow regression executables (`src/operation/iCTS/test/flow/htree/CMakeLists.txt:1`, `src/operation/iCTS/test/flow/htree/CMakeLists.txt:9`, `src/operation/iCTS/test/flow/htree/CMakeLists.txt:35`).
- Synthesis unit tests cover invalid root inputs, failure side-effect safety, final-name collision rejection, object ownership, sink-clustering config report, empty source-to-root roots, same-location direct connect, and top-level IO drive-cap behavior (`src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:133`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:177`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:202`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:232`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:288`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:334`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:356`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:378`).
- Synthesis CMake builds unit and real-tech smoke/matrix test executables (`src/operation/iCTS/test/flow/synthesis/CMakeLists.txt:1`, `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt:7`).

### Responsibility Boundary Summary

- API boundary: `CTSAPI` is the stable external facade. It should stay thin and lifecycle-oriented.
- Flow root boundary: `FlowManager` owns top-level stage order and persistent run/view/evaluation/writeback state.
- Setup boundary: `run_setup/` owns config, output paths, logging/schema initialization, and external adapter initialization.
- Stage boundary: `stage/` owns flow sequencing below `FlowManager`, per-clock/sink-domain policy, transaction rollback, status summaries, and calls into synthesis/netlist/evaluation/report consumers.
- Netlist boundary: `netlist/` owns clock data ingestion, sink classification, net rewiring, root/downstream net creation, and final commit of temporary CTS objects into `Design`.
- Synthesis boundary: `synthesis/` owns generic build assembly around H-tree/source-to-root builders, optional sink clustering, side-effect guards, and transfer of temporary objects/results.
- H-tree boundary: `htree/` owns algorithmic topology, characterization, candidate frontier/depth search, sink-load legality filtering, and temporary object materialization.
- View boundary: `clock_tree_view/` owns typed readonly metadata shared by report and visualization; it is not a final object owner.
- Evaluation boundary: `evaluation/` owns readonly metrics over committed CTS results plus statistics file output and optional STA timing/RC updates.
- Visualization boundary: `visualization/` owns readonly SVG/GDS artifact generation from the visualization model.

### Naming/Architecture Observations for Later Redesign

- Current `stage/` mixes stage wrappers (`CTSClockTreeEvaluationStep`, `CTSClockTreeReportStep`) with per-clock synthesis transaction internals (`ClockTreeSynthesisDriver`, `ClockTreeSynthesisTransaction`). This is semantically coherent as orchestration, but the directory name is broad and hides the per-clock/sink-domain transaction layer.
- Current `synthesis/` is not the H-tree algorithm itself; it is an assembly layer that wraps H-tree, source-to-root segment/H-tree selection, clustering, temporary net editing, metrics transfer, and view adaptation.
- Current `htree/` has the highest internal density and mixes facade, characterization cache/flow, candidate search, pattern registries, sink-load legality, materialization, and single source-to-root segment builder. These are separable algorithm subdomains.
- `netlist/ClockNetEditor` is a critical boundary despite its small directory: it is the only layer that both prepares flow netlist objects and commits temporary algorithm objects into final `Design`.
- `clock_tree_view/` is already a strong semantic name and maps well to the spec rule for narrow typed report-only data.
- `evaluation/` and `visualization/` are semantically aligned with their readonly consumer roles.

### External References

- None. This artifact is an internal code map only; no web or external documentation lookup was used for this local inspection.

### Related Specs

- `.trellis/spec/project-constraints.md` - iCTS-wide constraints, singleton usage, terminology, logging, and validation.
- `.trellis/spec/backend/directory-structure.md` - Layer placement and expected CTS flow framework.
- `.trellis/spec/backend/database-guidelines.md` - Singleton boundaries, ownership rules, final `Design` ownership, temporary algorithm object ownership, and readonly evaluation/report rules.
- `.trellis/spec/backend/quality-guidelines.md` - CTS semantic naming, dependency visibility, and narrow typed behavioral concepts.
- `.trellis/spec/backend/logging-guidelines.md` - Runtime/schema logging responsibilities.
- `.trellis/spec/backend/error-handling.md` - No-exception and log-level/safe-return conventions.

## Caveats / Not Found

- No source code, spec files, CMake files, tests, git metadata, or old duplicate-date task path were modified.
- This inspection did not run builds or tests; it maps architecture and entry points from code.
- The code map focuses on `src/operation/iCTS/source/flow` and related API/test/CMake entry points. It references database/module dependencies by boundary but does not deeply map all `source/database` or `source/module` internals.
- `.trellis/.current-task` still pointed at the old duplicate-date task directory when checked, but the requested output path was the corrected task directory.
