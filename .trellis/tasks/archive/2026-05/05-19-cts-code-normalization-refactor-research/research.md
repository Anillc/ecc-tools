# Research: CTS Code Normalization Refactor

## Scope

This research covers `src/operation/iCTS`, with emphasis on:

- CTS external API and root flow orchestration.
- Flow stages and sub-flows.
- Database, adapter, and committed-design data models.
- Algorithm modules under `source/module`.
- FastSTA structure and adapter boundary.
- CMake target shape and folder hierarchy.
- Naming semantics and generic terms.

The initial research created the architecture map and problem inventory. After user review, the parent task moved into implementation through child
tasks.

## Implementation Closure Notes

Closure scan date: 2026-05-20.

- All 9 child tasks are marked completed under this parent, including the fanout-4 optimization runtime child that was split out during validation.
- Source/test text scans found no remaining matches for the prohibited copied-state and generic terms recorded in `implement.md`.
- Four residual rollback-style FlowTest case names were renamed to CTS behavior terms and revalidated with `icts_test_flow`.
- Generic path scan found only `source/database/design/ClockNetwork.hh/.cc`. This is intentionally kept because the user confirmed
  `clock_network` is acceptable when it matches the database clock-network semantics.
- `Network` remains as a local LEMON min-cost-flow graph variable in `module/topology/mcf/MinCostFlow.hh`; it is not a CTS clock-net/domain name.
- Current source inventory: 343 source `.cc/.hh` files, 59,328 total source lines, no source file above 600 lines.
- Current test inventory: 108 test `.cc/.hh` files, 20,722 total test lines. `FastSTATest.cc` is 664 lines and remains a test-side follow-up risk.
- Focused builds/tests, full built iCTS ctest coverage, and `ics55_dev` fanout 4/fanout 32 binary validation passed during child execution.
- Final full `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` passed with `0` in-scope findings after the parent
  follow-up fixes.
- No commit or archive has been made for this parent task.

The sections below preserve the initial evidence and issue map that drove the child-task split.

## Evidence Collected

### Project Rules

Relevant Trellis rules:

- iCTS backend code lives under `src/operation/iCTS`.
- Required source categories under `source/` are `database/`, `utils/`, `module/`, and `flow/`.
- Flow contract is `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
- `source/flow/` root should expose only `Flow.hh/.cc` and build metadata.
- Stage root directories should expose facade files such as `Synthesis.hh/.cc`, `Report.hh/.cc`, and keep helpers under responsibility folders.
- Algorithm code under `source/module/` should receive explicit options/data instead of reading `CONFIG_INST`.
- iDB access belongs inside `Wrapper`; iSTA access belongs inside `STAAdapter`.
- Evaluation, report, and visualization should be readonly consumers of committed CTS results.
- Avoid broad copied-state objects that duplicate queryable CTS/iDB state without a clear stage contract.
- Names should use CTS/EDA semantics rather than generic backend/service terms.

### Initial Inventory

Initial `src/operation/iCTS` source/test inventory:

- 436 `.cc` / `.hh` files.
- 78,865 `.cc` / `.hh` lines.
- No `.cc` / `.hh` file exceeded 600 lines at initial research time.
- Largest direct source directories by local `.cc` / `.hh` lines:
  - `source/module/routing/bound_skew_tree`: 5,450 lines / 21 files.
  - `source/database/adapter/fast_sta`: 4,949 lines / 28 files.
  - `source/database/adapter/sdc`: 3,072 lines / 14 files.
  - `source/module/characterization`: 2,755 lines / 20 files.
  - `source/database/adapter/sta`: 2,408 lines / 10 files.
  - `source/database/design`: 2,281 lines / 12 files.
  - `source/module/topology/fast_clustering`: 2,079 lines / 12 files.

Largest near-threshold files:

- `src/operation/iCTS/source/database/adapter/sdc/ClockTracePins.cc`: 592 lines.
- `src/operation/iCTS/test/database/adapter/fast_sta/FastSTATest.cc`: 589 lines.
- `src/operation/iCTS/source/module/routing/bound_skew_tree/BoundSkewTreeBalance.cc`: 581 lines.
- `src/operation/iCTS/test/flow/FlowSdcTraceTest.cc`: 574 lines.
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc`: 573 lines.
- `src/operation/iCTS/source/flow/synthesis/htree/embedding/Embedding.cc`: 567 lines.
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentLibrary.hh`: 563 lines.
- `src/operation/iCTS/source/database/design/ClockDAG.cc`: 541 lines.
- `src/operation/iCTS/source/database/adapter/fast_sta/FastStaParasitics.cc`: 426 lines.
- `src/operation/iCTS/source/database/adapter/fast_sta/FastStaTypes.hh`: 382 lines.

The previous file-size cleanup largely succeeded. The remaining risk is semantic breadth, not immediate line-count violation.

## Current Functional Map

### API Layer

Files:

- `src/operation/iCTS/api/CTSAPI.hh`
- `src/operation/iCTS/api/CTSAPI.cc`

Responsibilities:

- Provides singleton external entry point `CTS_API_INST`.
- `CTSAPI::init(config_file, work_dir)` resets global CTS state, runs setup, and emits runtime setup.
- `CTSAPI::runCTS()` delegates to `FLOW_INST.runCTS()`.
- `CTSAPI::report(save_dir)` delegates to `FLOW_INST.report(save_dir)`.
- `CTSAPI::outputSummary()` converts internal `QorSummary` to `ieda_feature::CTSSummary`.

Boundary:

- API owns external call surface only.
- It currently coordinates singleton reset for `Config`, `Design`, `Wrapper`, `Flow`, and `SchemaWriter`.
- It does not own internal stage behavior, which is correct.

Risk:

- API reset directly knows every singleton it must reset. This is acceptable today but should not grow; future lifecycle consolidation could move the reset list to a CTS runtime/session owner.

### Root Flow

Files:

- `source/flow/Flow.hh`
- `source/flow/Flow.cc`

Responsibilities:

- Owns root CTS lifecycle state:
  - `SynthesisTraceSummary _run_summary`
  - `ClockLayout _clock_layout`
  - `EvaluationState _evaluation_state`
  - `InstantiationResult _instantiation_result`
  - `CharacterizationLibrary _char_library`
  - setup/evaluation readiness flags
- Runs full CTS API flow:
  1. reset runtime metrics;
  2. require setup;
  3. `readData`;
  4. synthesis + optimization through `run`;
  5. `instantiate`;
  6. `evaluate`;
  7. emit runtime and key results.
- Rebuilds `ClockDAG` after synthesis and after optimization.
- Blocks instantiation/evaluation if the committed topology is not a valid DAG.

Boundary:

- Correctly stays at root orchestration.
- It does own some reporting glue (`emitKeyResults`) and output summary conversion.
- It directly decides when optimization runs after synthesis.

Issue:

- Lifecycle names are ambiguous:
  - `runCTS` means full API flow.
  - `run` means synthesis plus optimization, not full CTS.
  - `readData` is part of setup/read boundary but lives on root `Flow`.
  - `instantiate` is private wrapper around `Instantiation::run`.
  - `evaluate` is private wrapper around `Evaluation::run`.
- There is no uniform stage interface or typed stage context/result contract.

### Setup

Files:

- `source/flow/setup/Setup.hh`
- `source/flow/setup/Setup.cc`

Responsibilities:

- Load runtime config.
- Resolve and create work, visualization, and statistics directories.
- Open `cts.log` / detail report writer.
- Initialize iDB wrapper from `dmInst`.
- Initialize STA adapter.
- Emit runtime setup/config/RC report.

Boundary:

- Owns startup validation and external runtime initialization.
- Correct place for `CONFIG_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST`, and schema setup.

Issue:

- Exposes `initialize` and `emitRuntimeSetup`, while other stages expose `run`.
- Returns `bool` rather than a typed `SetupResult`, so root `Flow` only knows `setup_ready` and must invent failure state later.

### Clock-Data Read

Files:

- Root caller: `source/flow/Flow.cc`
- Main implementation: `source/flow/instantiation/design_conversion/DesignConversionClockData.cc`
- Adapter implementation: `source/database/io/WrapperClockReader.cc`
- SDC tracing: `source/database/adapter/sdc/*`

Responsibilities:

- Convert configured clock net pairs or SDC-derived clock data into CTS `Design`.
- Read clock sources, clock nets, loads, insts, pins, and net topology from iDB through `Wrapper`.
- Populate `Design`, `Clock`, `Inst`, `Pin`, `Net`.

Boundary:

- Semantically this is flow setup clock-data read work, not instantiation. It currently lives under `flow/instantiation/design_conversion` because the
  same conversion module also owns temporary-to-committed CTS design conversion.

Issue:

- `readData` calls `DesignConversion::readClockData()` from an `instantiation/design_conversion` directory. This creates semantic confusion:
  reading external clock data is pre-synthesis setup work, while instantiation is post-synthesis materialization.
- Candidate folder direction: move clock-data read to `flow/setup/clock_data/`, leaving `flow/instantiation/design_conversion` for temporary CTS
  object commit after synthesis.

### Synthesis

Files:

- `source/flow/synthesis/Synthesis.hh/.cc`
- `source/flow/synthesis/distribution/*`
- `source/flow/synthesis/topology/*`
- `source/flow/synthesis/htree/*`
- `source/flow/synthesis/trace/*`

Responsibilities:

- Iterate clocks from `Design`.
- Reset prior clock topology.
- Resolve source/source-net and skip/fail invalid clocks.
- Partition sinks into hard-macro and regular sink domains.
- Build per-clock `ClockLayout` projections.
- For each non-empty sink domain:
  - prepare `ClockDistributionContext`;
  - build downstream H-tree topology;
  - commit inserted CTS objects into `Design`;
  - merge layout projection.
- Build source-to-root trunk connecting clock source to sink-domain roots.
- Update `SynthesisTraceSummary`.

Boundary:

- `Synthesis::run` is a stage facade and delegates topology details.
- `Topology` is the sub-flow for clock topology formation.
- `HTree` is a topology-family synthesis engine inside flow/synthesis because it mutates/produces CTS topology objects for the current flow.

Algorithm summary:

1. `Synthesis::run` obtains DBU and clocks.
2. For each clock, `synthesizeClock` partitions sinks and prepares per-domain contexts.
3. `Topology::formClock` builds sink-domain H-trees, then source trunk.
4. Each H-tree:
   - builds spatial topology from loads;
   - ensures characterization data;
   - builds segment frontier catalogs;
   - explores depth candidates;
   - filters sink-load region coverage;
   - selects global topology candidate;
   - resolves root-driver compensation;
   - builds inserted inst/pin/net embedding;
   - emits summary.

Issues:

- `Topology.cc` uses a local `ClockTopologyFormation` class to hold a sub-flow with implicit phases, but that pattern is not shared by other sub-flows.
- `HTree::BuildResult` is very broad: it stores topology, characterization metadata, depth-search counters, analytical-mode counters, root-driver compensation, inserted objects, root pins/nets, and report context. This is a real stage result but needs sub-results by CTS concept.
- `HTree::BuildOptions` mixes policy, runtime config overrides, characterization library, logging labels, object naming, and algorithm mode flags.
- `HTree::LogContext`, `Topology::BuildOptions`, and `ClockDistributionContext` are local context bags with some semantic names, but there is no shared stage context pattern.

### Optimization

Files:

- `source/flow/optimization/Optimization.hh/.cc`
- `source/flow/optimization/model/OptimizationTypes.hh`
- `source/flow/optimization/options/*`
- `source/flow/optimization/preparation/*`
- `source/flow/optimization/candidate/*`
- `source/flow/optimization/state/*`
- `source/flow/optimization/solver/*`
- `source/flow/optimization/mutation/*`
- `source/flow/optimization/report/*`

Responsibilities:

- Post-synthesis fixed-topology buffer sizing.
- Build FastSTA clock context for each clock.
- Inject route trees into FastSTA.
- Collect resizable buffer candidates.
- Collect cap/slew baselines.
- Use exact or scalable solver depending on graph size.
- Apply accepted master changes to committed `Design` and `ClockLayout`.
- Report per-clock optimization summary/profile.

Boundary:

- Structure is already closer to the desired pattern than other areas: facade plus subdirectories by responsibility.
- Optimization is a formal flow stage between synthesis and instantiation.

Algorithm summary:

1. Validate optimizer-owned `OptimizationOptions`.
2. Collect legal buffer master info from runtime buffer list and STA/Liberty.
3. Build route-tree cache from committed CTS nets.
4. For each clock:
   - build FastSTA context;
   - inject route trees;
   - collect optimizable buffers;
   - collect cap/slew baselines;
   - choose exact or scalable solver;
   - solve sizing actions with FastSTA timing/cap/slew/power feedback;
   - apply accepted mutations to `Design` and `ClockLayout`;
   - erase FastSTA context.

Issues:

- Optimization internals still use `FastSTA::queryClockContext` and `FastSTA::mutableClockContext`, coupling them to the full `FastStaClockContext` model.
- `OptimizationTypes.hh` still contains many concepts in one header: state, summaries, mutation, topology index, arrival windows, action scoring, route-tree cache. The folder is named `model`, but the file is still a mixed internal type bucket.
- `Optimization::run` takes `CharacterizationLibrary&` and immediately casts it unused. This is API drift from an earlier design.

### Instantiation

Files:

- `source/flow/instantiation/Instantiation.hh/.cc`
- `source/flow/instantiation/idb_conversion/*`
- `source/flow/instantiation/design_conversion/*`

Responsibilities:

- Write committed CTS clock topology from `Design` into iDB through `Wrapper`.
- Return status for attempted/design_ready/idb_conversion_done/clock_count.

Boundary:

- Post-synthesis materialization belongs here.
- iDB writeback is adapter/database-owned through `Wrapper`.

Issue:

- `Instantiation::run` currently wraps only `IdbConversion::run`, while `design_conversion/` also contains pre-synthesis clock-data import and synthesis inserted-object commit helpers. The directory name overclaims.
- `InstantiationResult.design_conversion_done = idb_result.attempted`; this field is semantically weak because no design conversion is performed in `Instantiation::run`.

### Evaluation

Files:

- `source/flow/evaluation/Evaluation.hh/.cc`
- `source/flow/evaluation/qor/*`

Responsibilities:

- Evaluate final committed CTS results.
- Optionally refresh full-design STA timing.
- Compute QoR metrics, wirelength, buffer area/counts, path buffer stats, root probe metrics, timing records.
- Keep evaluation state for report reuse.

Boundary:

- Readonly consumer after instantiation.
- May call `STAAdapter` to refresh/query full-design timing and RC metrics.

Issues:

- `Evaluation` exposes both stage lifecycle and utility methods (`evaluate`, `outputSummary`, `hasEvaluationResult`, `reset`).
- `QorEvaluationRootProbe.cc` and `QorEvaluationMetrics.cc` are relatively broad and may need subfolders such as `timing/`, `wirelength/`, `root_path/`, and `summary/` if evaluation grows.

### Report

Files:

- `source/flow/report/Report.hh/.cc`
- `source/flow/report/export/*`
- `source/flow/report/overview/*`
- `source/flow/report/qor/*`
- `source/flow/report/visualization/*`

Responsibilities:

- Resolve result paths.
- Reuse or run evaluation.
- Write QoR statistics files.
- Emit visualization outputs such as SVG/GDS.
- Emit report runtime and artifact status.

Boundary:

- Correctly separate from evaluation and visualization details.
- Visualization is under report, matching spec.

Issue:

- Report's `run(save_dir, evaluation_ready, refresh_sta_timing, clock_layout, evaluation_state)` has a parameter list that exposes too much root-flow
  state. A CTS-specific parameter object such as `ClockQorReportState` or `ClockReportArtifacts` would be clearer than a generic input object.

### Database / Data Model

Files:

- `source/database/design/Design.hh/.cc`
- `source/database/design/Clock.hh`
- `source/database/design/ClockDAG.hh/.cc`
- `source/database/design/ClockLayout.hh/.cc`
- `source/database/design/Inst.hh`, `Pin.hh`, `Net.hh`

Responsibilities:

- `Design`: owns final `Clock`, `Inst`, `Pin`, and `Net` objects and maintains name indexes.
- `Clock`: stores clock identity, source/source net, original loads, and final membership views.
- `ClockDAG`: design-owned readonly graph projection for committed CTS topology traversal and path stats.
- `ClockLayout`: report/visualization projection with roles, sink domains, synthesis phases, routed/flyline segments, and layout insts/nets.

Boundary:

- Ownership direction is mostly aligned with spec.
- `ClockLayout` is explicitly documented as report/visualization projection.

Issues:

- `ClockLayout` has become an input to FastSTA optimization (`FastSTA::buildClockContext(clock, clock_layout, clock_index)`), so the report projection is also feeding algorithm setup. This is historically accepted for layout parasitics, but it should be made explicit as a narrow `ClockRouteGeometryView` or `ClockNetRouteView` rather than using a broad layout/report projection as an algorithm input.
- `Design::get_clocks/get_insts/get_pins/get_nets` return vectors by value. This can be acceptable for copied iteration but should be reviewed on
  huge designs and named as views/copies if retained.

### Wrapper and STAAdapter

Files:

- `source/database/io/Wrapper.hh/.cc`
- `source/database/io/WrapperClockReader.cc`
- `source/database/io/WrapperClockWriter.cc`
- `source/database/adapter/sta/STAAdapter.hh/.cc`
- `source/database/adapter/sta/STAAdapter*.cc`

Responsibilities:

- `Wrapper`: owns iDB boundary, iDB/CTS cross-reference maps, clock import, clock writeback, geometry queries, core-boundary checks.
- `STAAdapter`: owns iSTA/iPower setup, full-design timing, propagated clocks, STA-backed RC/Liberty/pin queries, root-driver costs, and STA RC-tree installation.

Boundary:

- External-tool access is mostly contained in these adapters.
- `STAAdapter` has broad but real responsibility because CTS needs STA-backed technology, Liberty, pin, and timing data.

Issues:

- File names such as `STAAdapterInternal`, `WrapperClockWriterInternal`, and `WrapperClockWriterSupport` describe implementation status, not CTS semantics.
- Suggested semantic names:
  - `STAAdapterInternal` -> split into `StaClockTreeTiming`, `StaWireRcQuery`, or `StaLibertyCellQuery` depending on content.
  - `WrapperClockWriterSupport` -> `ClockNetMembershipRestore`, `ClockWritebackObjectMap`, or `ClockWritebackNameMap`.
  - `WrapperClockWriterInternal` -> `ClockWritebackPlan` / `ClockWritebackIdbAccess` / `ClockNetMembershipRestore`.

### FastSTA

Files:

- `source/database/adapter/fast_sta/FastSta.hh/.cc`
- `FastStaTypes.hh/.cc`
- `FastStaBuilder.hh/.cc`
- `FastStaClockTree.hh/.cc`
- `FastStaLiberty.hh/.cc`
- `FastStaParasitics.hh/.cc`
- `FastStaTiming.hh/.cc`
- `FastStaPower.hh/.cc`
- `FastStaIncremental.hh/.cc`
- `FastStaDmpCeff*.cc/.hh`
- `FastStaChar.hh/.cc`
- `FastStaReport.hh/.cc`

Responsibilities:

- CTS fast timing/power/legality facade.
- Builds per-clock fast timing context from committed CTS clock topology and layout route geometry.
- Converts Liberty and STA-backed data into fast lookup data.
- Builds RC parasitic data and reduces to pi/Elmore.
- Propagates fast timing, slew, cap, and power.
- Provides incremental buffer master changes.
- Provides characterization sampling context for segment char builder.

Boundary:

- Prior task intentionally kept FastSTA under `database/adapter/fast_sta`.
- It is conceptually a CTS fast timing engine sitting at the boundary of committed CTS design, STA-backed Liberty/RC data, and optimization.

Issues:

- `FastSta.hh` facade exposes `queryClockContext` and `mutableClockContext`, so callers can bypass the facade and depend on internal vectors/maps.
- `FastStaTypes.hh` is a broad mixed model file. It includes:
  - identity types;
  - geometry point and point hash;
  - RC segments/nodes/edges/pi model;
  - DMP algorithm enums and driver/load results;
  - Liberty axes/tables/arcs/cells;
  - timing and power summaries;
  - dirty region and mutation requests;
  - cap/slew status;
  - clock context;
  - characterization topology spec and sample result.
- CMake target `icts_source_database_adapter_fast_sta` is one library for all internals. It does not express submodule boundaries.
- `FastSTA` singleton owns both clock contexts and char contexts as the same `FastStaClockContext` vector type, which blurs normal-run and characterization lifecycle semantics.
- The directory name `fast_sta` is acceptable as CTS shorthand, but submodule names should be CTS-timing semantic rather than implementation-mechanics names.

### Algorithm Modules

Key modules:

- `source/module/characterization`
- `source/module/analytical_characterization`
- `source/module/topology`
- `source/module/routing`
- `source/module/timing`

Responsibilities:

- `characterization`: enumerate buffering patterns, build segment characterization, sample FastSTA timing/power, store segment chars.
- `analytical_characterization`: build analytical models/fits for H-tree evaluation.
- `topology`: clustering, fast clustering, min-cost flow helpers, topology generation.
- `routing`: FLUTE/SALT/BST/CBS routing, local legalization, RCTree conversion.
- `timing`: pure RCTree timing propagation.

Boundary:

- `TimingEngine` is clean: pure `RCTree` update/evaluation.
- `TopologyGen` is relatively clean but includes reporting helpers and default-fast-clustering facade.
- `Router` is a facade over routing implementations plus RC-tree conversion.

Issues:

- Several `source/module/` files call `STA_ADAPTER_INST`, `WRAPPER_INST`, or `CONFIG_INST` directly:
  - `module/analytical_characterization/AnalyticalCharacterization.cc`
  - `module/characterization/CharBuilderConfig.cc`
  - `module/characterization/CharBuilderFeasibility.cc`
  - `module/characterization/CharBuilderStaSampling.cc`
  - `module/routing/router/Router.cc`
  - `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc`
- Some direct access may be historical and practical, but the preferred boundary is explicit CTS options/data. The module layer should not decide
  runtime config defaulting or external-tool setup.
- `Router::buildRCTree(clock_tree)` exposes a no-options overload, but implementation requires explicit routing layer and fatals without it. This public API contract is misleading.
- `CharBuilder.hh` is a broad framework header with many private helpers and state fields. Its `.cc` is only a translation-unit anchor while behavior is split across many `CharBuilder*.cc` files. This solves line count but not interface breadth.

## Naming / Semantic Problems

### Generic Terms Found

Source files with generic names include:

- `FastStaDmpCeffInternal.hh`
- `ClockTraceResolverInternal.hh`
- `STAAdapterInternal.hh/.cc`
- `WrapperClockWriterInternal.hh`
- `WrapperClockWriterSupport.cc`
- `QorEvaluationInternal.hh`
- `OptimizationTypes.hh`
- `AnalyticalSolverInternal.hh`
- `AnalyticalSolverRequest.cc`
- `RootDriverCompensationInternal.hh`
- `BSTRouterInternal.hh`
- `BSTTypes.hh`
- `ClusterConstraintTypes.hh`
- `FastClusteringInternal.hh`

Test files also have many `Support` and `Internal` helpers. Tests can tolerate more helper naming, but long-term test readability would improve with domain names such as `RealTechFixture`, `TopologyCaseBuilder`, `VisualizationGolden`, or `CharacterizationFrontierFixture`.

### Classification

Acceptable generic naming:

- A narrow private implementation header under a stable domain folder can use `Internal` temporarily, if it only declares non-exported helper functions for that domain.

Problematic generic naming:

- `Support` files that contain concrete domain logic.
- `Internal` files with multiple unrelated responsibilities.
- `Request` / `Response` names where the payload is actually a domain concept such as H-tree search data, clock trace query data, root-driver
  compensation data, or stage report fields.
- `Types` files that become cross-domain buckets.
- Broad copied-state wording for data that is actually a Liberty cell view, timing model, writeback backup, or report metric sample.

Recommended replacement direction:

- Name by CTS object, stage, and responsibility:
  - `ClockNetMembershipRestore`
  - `ClockWritebackObjectMap`
  - `StaClockTreeTiming`
  - `StaWireRcQuery`
  - `StaLibertyCellQuery`
  - `FastStaClockTree`
  - `FastStaLibertyModel`
  - `FastStaClockNetParasitic`
  - `FastStaTimingState`
  - `FastStaPowerState`
  - `OptimizationClockSizingProblem`
  - `OptimizationTopologyWindow`
  - `AnalyticalHTreeSearchData`
  - `RootDriverCompensationState`

## CMake / Target Boundary Findings

Good patterns:

- `flow/optimization` already has subtargets for `model`, `options`, `report`, `preparation`, `candidate`, `state`, `solver`, and `mutation`.
- `flow/synthesis/htree` already has many semantic subdirectories and targets.
- `flow/report/visualization` has subdirectories for `drawing`, `gds`, `svg`, `layer`, and `writer`.

Problems:

- `database/adapter/fast_sta` is still one target compiling all internals. It should be split by semantic submodule.
- `module/characterization` compiles many `CharBuilder*.cc` files under one target with one large header. It likely needs semantic subfolders and internal headers.
- `database/adapter/sdc` has many related files but one adapter target. That may be acceptable, but `ClockTracePins.cc` is near the 600-line boundary and the SDC clock trace responsibility should be reviewed.
- Public include directories are often broad at folder roots. This is common in the repo but weakens encapsulation when internal headers live in the same include root.

## Problem Inventory

### P0 / Blocker

No immediate blocker found for planning. No implementation was started.

### P1 / Architecture Risks

1. Flow stages lack one explicit lifecycle contract.

Evidence:

- `Setup::initialize`, `Setup::emitRuntimeSetup`.
- `Synthesis::run`.
- `Optimization::run`.
- `Instantiation::run`.
- `Evaluation::run`, `Evaluation::evaluate`, `Evaluation::outputSummary`, `Evaluation::reset`.
- `Report::run`.
- `Flow::runCTS`, `Flow::readData`, `Flow::run`, `Flow::instantiate`, `Flow::evaluate`, `Flow::report`.

Impact:

- Stage responsibilities are learned by reading implementation instead of interface.
- Shared behavior such as runtime metric, schema stage, diagnostics, setup gating, report sections, and typed result state is duplicated or manually shaped.

Recommendation:

- Introduce a stage pattern conceptually, even if not a virtual base class:
  - `prepare/init`: validate required clock data, resolve config/options, build stage-local state.
  - `run/execute`: perform the stage mutation or readonly evaluation.
  - `report`: emit concise report and diagnostics.
  - `result`: return a typed result with `status`, `reason`, counters, and stage-specific payload.

2. `readData` lives under semantically confusing instantiation/design-conversion code.

Evidence:

- `Flow::readData()` calls `DesignConversion::readClockData()`.
- Clock import code lives under `flow/instantiation/design_conversion`.

Impact:

- Clock-data read and post-synthesis materialization are separate CTS phases but share a folder.

Recommendation:

- Split pre-synthesis clock-data read from post-synthesis design commit:
  - `flow/setup/clock_data`
  - `flow/instantiation/design_commit` for inserted CTS object commit
  - `flow/instantiation/idb_writeback` for iDB projection

3. FastSTA facade does not isolate internal timing model.

Evidence:

- `FastSTA::queryClockContext`.
- `FastSTA::mutableClockContext`.
- Optimization preparation/candidate/state/solver uses `FastStaClockContext` directly.

Impact:

- FastSTA cannot freely reorganize internal data layout.
- Optimization becomes coupled to vectors/maps and node/net ID internals.

Recommendation:

- Keep `FastSta.hh/.cc` as facade.
- Replace broad context exposure with narrow query/update APIs and typed views:
  - `FastStaClockTreeView`
  - `FastStaBufferView`
  - `FastStaNetTimingView`
  - `FastStaRouteInjection`
  - `FastStaClockSizingEdit`

4. Module layer still reaches into runtime singletons and adapters.

Evidence:

- Direct singleton touch points appear in module characterization, analytical characterization, routing, topology cluster constraints.

Impact:

- Module algorithms own runtime policy accidentally.
- Unit testing and reuse require initialized global runtime.

Recommendation:

- Move singleton reads to flow/database boundary.
- Pass explicit `RoutingTechnology`, `CharacterizationOptions`, `ClusterElectricalParams`, `RCTreeBuildOptions`, or `LibertyBufferCatalog`.

### P2 / Maintainability Risks

5. Large headers are still semantic buckets.

Evidence:

- `FastStaTypes.hh` groups many independent domains.
- `CharBuilder.hh` exposes a large framework private surface.
- `HTree.hh` has broad `BuildResult` and `BuildOptions`.
- `OptimizationTypes.hh` groups multiple internal optimization concepts.

Impact:

- Hard to infer ownership, lifecycle, or valid use.
- Small changes pull broad dependencies into many compilation units.

Recommendation:

- Split into domain-named model headers:
  - FastSTA: `clock_id`, `clock_tree`, `liberty_model`, `clock_net_parasitic`, `timing_state`, `power_state`, `segment_char_state`.
  - HTree: `sink_domain_build`, `depth_search_result`, `root_compensation_result`, `embedding_result`, `report_summary`.
  - Optimization: `clock_sizing_problem`, `sizing_action`, `legality_check`, `runtime_profile`, `topology_window`.

6. CMake target granularity is inconsistent.

Evidence:

- Optimization has subtargets.
- FastSTA has one target for 15 `.cc` files.
- Characterization and SDC adapters are broad single targets.

Impact:

- Harder to enforce dependency visibility.
- Internal headers are exposed through broad include dirs.

Recommendation:

- Split large modules by responsibility and express dependencies via target links, not broad include-path reuse.

7. Generic naming obscures CTS semantics.

Evidence:

- `Internal`, `Support`, `Request`, `Types`, and broad copied-state wording were present in source/test names and symbols at initial research time.

Impact:

- Users cannot infer responsibility from file names.
- Refactor reviews focus on mechanics rather than CTS concepts.

Recommendation:

- Rename only with a concept map. Do not do blind mechanical renames.

### P3 / Cleanup Opportunities

8. Public no-options overloads can hide required contracts.

Evidence:

- `Router::buildRCTree(clock_tree)` exists, but RC construction fatals unless routing layer is explicitly provided.

Recommendation:

- Remove misleading overload or make it valid by resolving options at flow boundary before calling the module.

9. Test helper naming is service-style.

Evidence:

- Many files under `test/**/support` and `*Support.*`.

Recommendation:

- Rename gradually when touching tests:
  - `Fixture`, `CaseBuilder`, `Golden`, `ArtifactWriter`, `RealTechAsset`, `Scenario`.

10. Near-threshold files need semantic follow-up, not line-count-only splitting.

Evidence:

- Several 500-600 line files are still cohesive enough to compile, but some hide multiple responsibilities.

Recommendation:

- Review near-threshold files by responsibility before they grow:
  - SDC clock trace pin resolution.
  - Bound-skew-tree balancing.
  - Source trunk segment synthesis.
  - H-tree embedding.
  - FastSTA parasitic network construction/reduction.

## Recommended Child Task Split

1. Flow lifecycle contract and stage result normalization.
2. Clock-data read vs instantiation directory split.
3. FastSTA facade and subtarget split.
4. Module singleton-boundary cleanup.
5. Semantic naming cleanup for source files and internal types.
6. Large header concept split.
7. Test helper naming and fixture organization.

## Open Product / Scope Decision

The main unresolved decision is whether to prioritize this as a behavior-preserving architecture refactor only, or allow API-breaking internal
interface cleanup in the same campaign. The recommended default is behavior-preserving first, with internal API changes allowed only when needed to
make boundaries enforceable.
