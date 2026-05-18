# CTS Code Structure Research

## Sources Read

- `.trellis/spec/project-constraints.md`
- `.trellis/spec/backend/index.md`
- `.trellis/spec/backend/directory-structure.md`
- `.trellis/spec/backend/database-guidelines.md`
- `.trellis/spec/backend/error-handling.md`
- `.trellis/spec/backend/logging-guidelines.md`
- `.trellis/spec/backend/quality-guidelines.md`
- `.trellis/spec/guides/code-reuse-thinking-guide.md`
- `.trellis/spec/guides/cross-layer-thinking-guide.md`
- Existing CTS task designs for fast STA, optimization migration, search quality, and runtime bottleneck.

## Implementation Outcome Snapshot

Final state after the original implementation pass plus the follow-up runtime-boundary pass:

- Full iCTS checker:
  - command: `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS`;
  - result: `In-scope findings: 0`.
- Default fast tests:
  - `ctest --test-dir build -N -R icts` discovers 15 iCTS tests;
  - `ctest --test-dir build -R icts --output-on-failure` passes 15/15.
- Mechanical compliance:
  - old license wording `MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.` count: 0;
  - missing `@author Dawn Li (dawnli619215645@gmail.com)` count: 0.
- File-size outcome:
  - `flow/optimization/Optimization.cc` was split into focused files below 600 lines;
  - the default `test/flow/FlowTest.cc` runner was split into focused files below 600 lines;
  - remaining historical files over 600 lines are preserved as a follow-up semantic-refactor backlog rather than split mechanically in this pass.
- Fallback wording outcome:
  - production `fallback` wording under `src/operation/iCTS/source` is removed;
  - schema diagnostic wording now uses degraded terminology for report/probe paths;
  - remaining test fallback wording belongs to opt-in/manual real-tech fallback coverage and should be renamed or redesigned when that manual suite is refactored.

## Guideline Findings

1. Backend specs explicitly apply to `src/operation/iCTS/`.
2. Source layering is `api/`, `source/`, and `test/`; source categories are `database/`, `utils/`, `module/`, and `flow/`.
3. The documented flow in `directory-structure.md` is `setup -> synthesis -> instantiation -> evaluation -> report`.
4. Current code and recent Trellis task designs use `setup -> synthesis -> optimization -> instantiation -> evaluation -> report`.
5. Module code should receive explicit options/config instead of reading runtime singletons directly; flow/database/adapter/test boundaries may read singleton state.
6. Hot name-based queries should use maintained indexes, not vector-scan fallbacks.
7. Report-only data should be narrow and typed. Broad snapshots that duplicate queryable CTS/iDB state are forbidden unless there is a clear stage contract.
8. Logging fallback/auto-derivation should be labeled at the decision point. Unrecoverable missing required resources should be `LOG_FATAL`, not silent fallback.
9. New and touched iCTS `.cc`/`.hh` files need the required license block and Doxygen author line.

## Metrics

### File Size

Files over 600 lines:

```text
2100 src/operation/iCTS/source/flow/optimization/Optimization.cc
1919 src/operation/iCTS/test/module/characterization/CharacterizationRealTechExactRegressionTest.cc
1593 src/operation/iCTS/test/flow/FlowTest.cc
1525 src/operation/iCTS/source/flow/synthesis/htree/analytical_solver/AnalyticalSolver.cc
1337 src/operation/iCTS/source/flow/synthesis/htree/HTree.cc
1273 src/operation/iCTS/source/database/adapter/sdc/SdcClockReader.cc
1129 src/operation/iCTS/source/database/io/Wrapper.cc
1121 src/operation/iCTS/source/database/adapter/sdc/ClockTraceResolver.cc
1002 src/operation/iCTS/source/database/adapter/fast_sta/FastStaDmpCeff.cc
990  src/operation/iCTS/source/flow/evaluation/qor/QorEvaluation.cc
831  src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc
806  src/operation/iCTS/source/utils/logger/Schema.cc
732  src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc
703  src/operation/iCTS/test/flow/synthesis/TopologyVisualizationSupport.cc
686  src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc
671  src/operation/iCTS/source/module/routing/bound_skew_tree/BoundSkewTreeBalance.cc
649  src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc
619  src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc
608  src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc
```

Severity:

- Critical: `Optimization.cc` is a 2100-line flow file containing local types, search policy, topology indexing, route-tree injection, mutation application, reports, logs, and the stage facade.
- High: `HTree.cc`, `AnalyticalSolver.cc`, SDC adapter files, `Wrapper.cc`, `QorEvaluation.cc`, and `FastStaDmpCeff.cc` exceed 1000 lines or are close to it and mix responsibilities.
- Medium: oversized tests and visualization support files hide test intent and slow review.

### constexpr Usage

`constexpr` appears in 75 files and 389 matched lines:

- `source/`: 202 matched lines.
- `test/`: 187 matched lines.

Largest source concentrations:

```text
42 src/operation/iCTS/source/utils/visualization/core/SvgCommon.hh
25 src/operation/iCTS/source/flow/optimization/Optimization.cc
14 src/operation/iCTS/source/module/topology/fast_clustering/FastClusteringInternal.hh
12 src/operation/iCTS/source/module/routing/bound_skew_tree/Components.hh
11 src/operation/iCTS/source/database/adapter/fast_sta/FastStaDmpCeff.cc
6  src/operation/iCTS/source/flow/synthesis/htree/HTree.cc
5  src/operation/iCTS/source/module/topology/clustering/Clustering.cc
5  src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc
5  src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc
5  src/operation/iCTS/source/database/adapter/fast_sta/FastStaTypes.hh
```

Classification:

- Keep local with comments where needed:
  - sentinel IDs such as `kInvalidFastStaNodeId`;
  - unit conversions such as `kMilliOhmPerOhm`;
  - numerical method invariants in `FastStaDmpCeff.cc`, if they are proven OpenSTA-aligned;
  - small structural constants in geometry/routing helpers.
- Move to explicit options/config or a typed policy object:
  - `Optimization.cc` search budgets and thresholds:
    - `kMaxOptimizationIterations`
    - `kMaxOptimizationTrials`
    - `kMaxFrontierSinks`
    - `kMaxBatchActions`
    - `kMaxBatchTrialsPerIteration`
    - `kTrialProgressInterval`
    - `kInitialDetailedTrials`
    - `kSlowTrialLogThresholdS`
    - `kScalableNodeThreshold`
    - `kScalableBufferThreshold`
    - `kMaxScalableBatchActions`
    - `kMaxScalableExactTrialsPerIteration`
    - `kTargetWindowShrinkRatio`
    - `kMinBranchPurity`
    - `kMaxOppositeViolationRatio`
    - rank-step, path-count, prefix-length, segment-length, and batch-size arrays.
  - `HTree.cc` analytical shortlist/beam-size and root-driver clock-period constants.
  - `FastClusteringInternal.hh` heuristic round/window/weight constants.
  - `Clustering.cc` default max ratio/iteration/convergence constants if they are user-tunable CTS behavior.
- Remove or replace with explicit failure:
  - `FastStaChar.cc` `kCharDbuPerUmFallback = 1000`; DBU is a required unit contract.
  - implicit routing layer default 1 in `FastStaBuilder`/`Config` paths unless confirmed as a project-wide default.
- Test-only constants are mostly acceptable as fixtures, but local machine paths in benchmark tests are not acceptable.

### Fallback Usage

`fallback` wording appears in 182 matched source lines and 60 matched test lines.

Largest source concentrations:

```text
31 src/operation/iCTS/source/flow/synthesis/htree/HTree.cc
15 src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc
13 src/operation/iCTS/source/flow/synthesis/htree/solution/SolutionReport.cc
13 src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc
13 src/operation/iCTS/source/flow/report/visualization/drawing/Drawing.cc
10 src/operation/iCTS/source/flow/synthesis/trace/layout/ClockLayoutBuilder.cc
10 src/operation/iCTS/source/database/adapter/sta/STAAdapterCellQuery.cc
7  src/operation/iCTS/source/module/analytical_characterization/AnalyticalCharacterization.cc
7  src/operation/iCTS/source/flow/report/visualization/svg/SvgVisualization.cc
6  src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc
```

Preliminary classification:

- Likely allowed, but should be renamed as degraded diagnostics:
  - visualization/report route drawing using pin-to-pin segments when a routed tree is unavailable;
  - display styling for degraded internal segments.
- Likely allowed only as explicit opt-in:
  - H-tree global candidate selection when no strict boundary-feasible solution exists;
  - source-trunk segment boundary fallback.
- Likely should become an explicit config requirement or failure:
  - auto-derived `wirelength_unit_um` from strongest buffer height;
  - `FastStaChar` DBU fallback to 1000;
  - routing layer default 1 when config is absent;
  - adapter query chains that say "caller may fallback" without returning typed provenance.
- Naming-only cleanup:
  - `Optimization.cc` local `fallback_id` is just an emergency seed for frontier sink selection. Rename to `seed_sink_id` or similar if the behavior remains.

### License and Author

Files with old wording:

- 103 iCTS `.cc`/`.hh` files contain `MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.`
- Required wording is `MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.`

Files missing required author line:

```text
src/operation/iCTS/test/common/realtech/RealTechSetupSupport.hh
src/operation/iCTS/test/common/types/TestDataTypes.cc
src/operation/iCTS/test/module/topology/topology_gen/TopologyGenShared.hh
```

Notes:

- `RealTechSetupSupport.hh` and `TopologyGenShared.hh` are compatibility include shims with no full header block.
- `TestDataTypes.cc` has a Doxygen file comment but omits the author line.

### Test Structure

Facts:

- iCTS test tree has 95 `.cc`/`.hh` files and about 14.3k lines.
- There are multiple flow/synthesis/realtech executables and slow-regression variants.
- `ICTS_BUILD_SLOW_REALTECH_TESTS` is `ON` in the current build cache.
- `src/operation/iCTS/test/CMakeLists.txt` defines `ICTS_BUILD_SLOW_REALTECH_TESTS` with default `OFF`, and the shared `icts_add_test_executable()` helper calls `add_test(NAME ... COMMAND ...)`.
- Not all real-tech targets are guarded the same way:
  - `icts_test_flow_synthesis_realtech`, `icts_test_flow_synthesis_htree_realtech`, and `icts_test_module_characterization_realtech` are created by default.
  - `icts_test_flow_synthesis_htree_realtech_regression`, `icts_test_module_characterization_realtech_regression`, and `icts_test_module_topology_fast_clustering_realtech_benchmark` are guarded by `ICTS_BUILD_SLOW_REALTECH_TESTS`.
- `ctest -N -R icts` reports `Total Tests: 0`, despite `add_test()` in the helper function and existing Ninja targets.
- Representative test executables build and pass when run directly:
  - `./bin/icts_test_flow --gtest_color=no`: 26 passed.
  - `./bin/icts_test_database_adapter_fast_sta --gtest_color=no`: 7 passed.
  - `./bin/icts_test_flow_synthesis --gtest_color=no`: 14 passed.
  - `./bin/icts_test_flow_synthesis_htree --gtest_color=no`: 8 passed.
  - `./bin/icts_test_module_characterization --gtest_color=no`: 17 passed.

Problems:

- Test registration is broken or not propagated to the active build tree.
- Flow tests are split across `flow`, `flow/synthesis`, `flow/synthesis/htree`, and real-tech variants; the normal flow validation path is not obvious.
- Real-tech tests skip frequently based on local assets and environment. Some benchmark support headers contain hard-coded local paths:
  - `/nfs/share/home/huangzhipeng/...`
  - `/home/liweiguo/project/ecc-tools-dev/...`
  - `/home/liweiguo/pdk/...`
- `FastClusteringRealTechBenchmarkDiscovery.cc` resolves `ICTS_REALTECH_PDK_DIR` and `PDK_DIR`, but still falls back to `/home/liweiguo/pdk/icsprout55-pdk` and `/home/liweiguo/pdk`.
- Oversized tests such as `CharacterizationRealTechExactRegressionTest.cc` and `FlowTest.cc` are difficult to maintain.

### Data Structure and Boundary Issues

1. `ClockNetwork`, `ClockLayout`, `ClockDAG`, `FastStaClockContext`, and `Optimization.cc::TopologyIndex` all represent CTS topology or related role metadata.
2. Some duplicated concepts use different enum names:
   - `ClockNetwork::DomainKind`
   - `SinkDomainKind`
   - `LayoutInstRole`
   - `ClockNetwork::InstRole`
   - `LayoutNetRole`
   - `ClockNetwork::NetRole`
3. `ClockLayout` is a report/visualization projection, but it is also consumed by fast STA optimization context construction. That broadens a report projection into an optimization input.
4. `FastStaClockContext` is public and mutable through `FastStaAdapter::mutableClockContext`. This makes the adapter facade less authoritative and lets flow code inspect adapter internals.
5. `Optimization.cc` defines its own local data model for buffer masters, cap/slew baseline, actions, topology, window stats, runtime profile, and summaries. Some of these are flow report data, some are solver state, and some are adapter-derived data.
6. Empty or disabled module placeholders exist:
   - `source/module/buffer_sizing/` is empty.
   - `source/module/buffering`, `source/module/drv`, and `source/module/report` contain empty CMake files and are commented out in `source/module/CMakeLists.txt`.

Implication:

- The current structure solves immediate feature needs, but CTS semantics are split across too many ad-hoc projections. Refactoring should converge on one committed CTS topology view plus narrow stage-specific views, not add another broad model.

### Singleton and Adapter Boundary Issues

Direct singleton usage under `source/module/`:

```text
STA_ADAPTER_INST:
src/operation/iCTS/source/module/analytical_characterization/AnalyticalCharacterization.cc
src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc
src/operation/iCTS/source/module/characterization/CharBuilderFeasibility.cc
src/operation/iCTS/source/module/characterization/CharBuilderStaSampling.cc
src/operation/iCTS/source/module/routing/router/Router.cc
src/operation/iCTS/source/module/topology/cluster_constraints/ClusterConstraintEvaluator.cc

WRAPPER_INST:
src/operation/iCTS/source/module/routing/router/Router.cc
src/operation/iCTS/source/module/topology/cluster_constraints/ClusterConstraintEvaluator.cc
```

No `CONFIG_INST` or `DESIGN_INST` use was found under `source/module/`, which is good.

Concern:

- Module code still reaches into adapters/external tool data. This may be acceptable for legacy module boundaries, but it conflicts with the current backend guideline that module code should operate on explicit CTS types/options.

### Parameter List Issues

Representative long call signatures:

- `Topology::formClock(Clock&, std::size_t, ClockLayout&, SynthesisTraceSummary&, DomainStatusTable&, CharacterizationLibrary&, std::size_t, const std::vector<ClockDistributionContext>&)`
- `ClockDistribution::prepare(Clock&, std::size_t, SinkDomainKind, const std::vector<Pin*>&, std::size_t, DomainStatusTable&, ClockDistributionContext&, const ClockDistributionRootBufferSpec*)`
- `Optimization.cc::tryBatch(...)`
- `Optimization.cc::findBestBatchTrial(...)`
- `Optimization.cc::findBestScalableBatchTrial(...)`
- `ClockLayoutBuilder.cc::makeSegment(...)`
- `ClockLayoutBuilder.cc::appendPinToPinSegments(...)`
- `ClockLayoutBuilder.cc::appendClockNetworkSegments(...)`
- `ClockLayoutBuilder.cc::makeLayoutNet(...)`
- `ClockLayoutBuilder.cc::makeLayoutInst(...)`

Pattern:

- Many long parameter lists are repeated bundles: clock identity, sink-domain metadata, layout topology metadata, optimization trial context, cap/slew baselines, and target policy.
- The right fix is narrow context structs with CTS names, not generic "Context" objects that hide ownership.

### Optimization.cc Detailed Issues

`Optimization.cc` currently owns all of the following:

- stage setup and schema sections;
- runtime profiling;
- buffer master inventory;
- route-tree cache building;
- fast STA context construction and route-tree injection;
- cap/slew baseline collection;
- solver state capture;
- exact batch candidate generation;
- scalable batch candidate generation;
- topology index construction;
- arrival window and score calculation;
- batch trial application and restore;
- final power update;
- committed design mutation and pin renaming;
- ClockLayout mutation;
- summary and profile emission.

This is too much for a single flow translation unit. It also makes it hard to decide which constants are policy, which are implementation detail, and which are logging/debug controls.

### CMake and Directory Issues

- `source/flow/optimization/CMakeLists.txt` has one target and one `.cc`. If split, update this target first and rebuild before behavioral changes.
- `source/module/CMakeLists.txt` has commented-out module targets and empty directories. These should be either removed or restored with real ownership.
- `database/adapter/fast_sta` already follows a coherent file split, but `FastStaDmpCeff.cc` still exceeds 1000 lines and needs internal splitting or stronger comments around numerical contracts.

## Preliminary Severity List

1. Critical: `flow/optimization/Optimization.cc` is structurally overloaded and hard-codes search policy.
2. Critical: flow spec does not document the current optimization stage.
3. High: algorithmic fallback policy is inconsistent and can hide invalid CTS constraints.
4. High: CTS topology semantics are represented in overlapping structures without one clear source of truth.
5. High: tests are not registered through `ctest` in the active build tree.
6. High: multiple source files exceed 1000 lines and block review.
7. Medium: module code directly reads STA/Wrapper adapter state.
8. Medium: long parameter lists indicate missing stage contract structs.
9. Medium: 103 license wording mismatches and 3 missing author lines are mechanical compliance drift.
10. Medium: local hard-coded paths in tests make some tests non-portable.

## Runtime Semantic Boundary Audit

Follow-up request date: 2026-05-18.

### Guideline Applied

Backend `error-handling.md` and `logging-guidelines.md` classify severity by continuation safety:

- use `LOG_FATAL` / `LOG_FATAL_IF` when a required pointer or resource is missing and execution cannot continue without misleading or unsafe results;
- use `LOG_ERROR` plus a safe return only when the caller can continue safely;
- use `LOG_WARNING` for optional, skippable, or report-only degraded behavior.

For CTS algorithm paths, DBU-per-micron, a positive RC routing layer, initialized STA/iDB adapter state, and speculative mutation restore are required invariants. Returning `0`, clamping DBU to `1`, or returning zero RC from unavailable infrastructure can produce plausible but invalid CTS data, so those patterns should be tightened at algorithm/adapter boundaries.

### Scan Notes

- Pre-implementation source still had DBU/routing prerequisite checks in fast STA, routing, topology, H-tree, cluster constraints, STA RC-tree installation, QoR, and report/visualization paths.
- Pre-implementation source had literal or phrase fallback wording in schema compatibility, visualization degraded segments, and BST topology normalization. Test fallback wording remains in real-tech/manual fallback coverage.
- No existing iCTS death-test usage was found with `EXPECT_DEATH`, `ASSERT_DEATH`, `EXPECT_EXIT`, or `ASSERT_EXIT`; fatal behavior tests will need either new death-test convention approval or top-level validation through setup/stage failure where possible.

### Candidate Classification

| Classification | Path / Function | Current Behavior | Expected Direction |
|---|---|---|---|
| Fatal candidate | `database/adapter/fast_sta/FastStaBuilder.cc::applyRuntimeOptions` | DBU `<= 0` logs error, stores `context.dbu_per_um = 0`, and continues. | Convert DBU and routing layer to required preconditions with `LOG_FATAL_IF`; assign valid values directly. |
| Fatal candidate | `database/adapter/fast_sta/FastStaBuilder.cc::resolveRoutingLayer` | Missing routing layer logs error and returns `0`. | Return only a positive layer or fatal. Missing RC layer makes later fast STA RC invalid. |
| Fatal candidate | `database/adapter/fast_sta/FastStaParasitics.cc::queryWireResistanceOhm` / `queryWireCapacitancePf` | Uses `std::max(context.dbu_per_um, 1)` before RC conversion. | Remove DBU clamp in algorithm path and guard `context.dbu_per_um > 0` and `context.routing_layer > 0` before RC queries. |
| Fatal candidate | `database/adapter/sta/STAAdapterRcTree.cc::resolveDbUnit` / `resolveRoutingLayer` / RC query helpers | Missing DBU/routing layer logs error and returns `0`; callers divide by resolved DBU and query RC with layer `0`. | RC-tree installation should require valid units/layer at the install boundary. Keep per-net missing STA terminals as warnings, but global unit/layer absence should not continue. |
| Fatal candidate | `module/routing/router/Router.cc::QueryArcParasitics` / `ResolveRoutingLayer` | Missing DBU returns `{0.0, 0.0}`; missing layer returns `0` and still calls STA RC. | RC-tree construction requires DBU/layer. Fatal or explicit typed failure before constructing RC; no zero-RC sentinel from infrastructure failure. |
| Fatal or typed failure | `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc::BuildBstParameters` | Missing DBU/routing layer returns partially default `BSTParameters`. | Prefer `std::optional<BSTParameters>` or fatal at exact-cap constraint setup. Do not let zero DBU/layer reach BST electrical constraints. |
| Fatal or typed failure | `flow/synthesis/topology/sink/SinkLoadClustering.cc::buildClusteringConfigFromRuntimeConfig` | Missing routing layer logs error and stores `routing_layer = 0`. | If sink clustering uses exact/electrical constraints, fail setup before clustering; otherwise disable the exact RC feature explicitly. |
| Fatal or typed failure | `flow/synthesis/htree/region/SinkLoadRegion.cc::BuildLeafElectricalConfig` | Missing routing layer logs error and stores `routing_layer = 0`. | Sink-load-region legality checks should either require layer at setup or return a typed failure that rejects the candidate. |
| Fatal or typed failure | `flow/synthesis/htree/compensation/RootDriverCompensation.cc::ResolveRoutingLayer` / `DbuToUm` | Missing layer/DBU returns `0`; wire-cap query can silently become zero. | If root-driver compensation is enabled, missing units/layer should fail the compensation path explicitly or fatal; optional disabled compensation should skip before querying RC. |
| Fatal or typed failure | `database/adapter/fast_sta/FastStaChar.cc::resolveDbuPerUm` / `buildContext` | Missing DBU logs error and returns an empty context. | Production characterization requires DBU. If char-only tests need no iDB, test setup should provide synthetic DBU or the builder should expose an explicit synthetic context path. |
| Typed stage failure candidate | `flow/synthesis/htree/HTree.cc::build` | Missing DBU sets `failure_reason = "dbu_per_micron_unavailable"` and returns failed `BuildResult`. | This is structurally clearer than sentinel continuation. Confirm whether the user wants top-level fatal instead of per-clock typed failure for global prerequisites. |
| Typed stage failure candidate | `flow/synthesis/topology/trunk/SourceTrunkSegment.cc::build...` | Missing DBU sets `failure_reason` and returns failure. | Keep typed failure if source-trunk synthesis can rollback and report the failed clock; fatal only if global setup should fail-fast. |
| Typed stage failure candidate | `flow/synthesis/topology/Topology.cc::collectSourceTrunkLengthsUm` | Missing DBU logs error and returns empty additional characterization lengths. | Replace empty-vector sentinel with explicit optional/result, or pass already validated DBU from the H-tree/topology build boundary. |
| Precondition cleanup | `flow/synthesis/htree/characterization/wirelength/WirelengthGrid.cc` and `flow/synthesis/htree/plan/Plan.cc` | Uses `std::max(dbu_per_um, 1)` while deriving requested lengths. | Once callers validate DBU, use the passed positive value directly and add precondition fatal if the helper can be called independently. |
| Precondition cleanup | `module/topology/TopologyGen.cc::NormalizeDbuPerUm` and `TopologyGen::BuildOptions::dbu_per_um = 1` | Normalizes invalid DBU to `1` for reporting/length conversion. | Split algorithm preconditions from report formatting. Topology generation called from synthesis should receive a validated DBU; report-only summaries may keep labeled degraded behavior. |
| Facade boundary | `database/io/Wrapper.cc::queryDbUnit` | Shared query facade logs error and returns `0`. | Keep fallible facade because report paths use it, but algorithm callers must immediately translate invalid DBU to fatal or typed failure. |
| Facade boundary | `database/adapter/sta/STAAdapterWireRc.cc::queryWireResistance` / `queryWireCapacitance` | Missing STA IDB adapter logs error and returns `0.0`. | Either make these fatal for all callers or add required-call wrappers for algorithms while keeping fallible probe/report APIs. |
| Keep degraded | `flow/evaluation/qor/QorEvaluation.cc` | Missing DBU marks wirelength metrics degraded and continues. | Keep warning/degraded reporting; ensure no degraded value feeds synthesis/optimization. |
| Keep degraded | `flow/synthesis/trace/distance/TopologyDistanceReport.cc` | Missing DBU skips report and writes status. | Keep as report-only degraded/skip behavior. |
| Keep degraded | `flow/report/visualization/drawing`, `flow/report/visualization/gds`, `database/design/ClockLayout` | Clamps DBU/widths to render stable visualization. | Keep isolated to visualization/report projection; rename wording to degraded rather than fallback where practical. |
| Wording cleanup | `flow/report/visualization/drawing/Drawing.cc` | Log says design view "falls back" to driver-to-load segments. | Rename to "uses degraded driver-to-load segments" or equivalent. |
| Wording or fatal cleanup | `module/routing/bound_skew_tree/BSTRouter.cc::NormalizeTopoTypeForBuild` | Wrong topology mode logs "Falling back" and normalizes to GreedyDist. | Prefer fatal because wrong API/topo mode is caller misuse; if compatibility is needed, rename as explicit normalization and document why. |
| Wording cleanup | `utils/logger/Schema.hh/.cc::DiagnosticLevel::kFallback` | Schema enum/string preserves `fallback`. | Consider `kDegraded` unless report compatibility requires preserving the string. |

### Proposed Modification Waves

1. Boundary helpers or local required resolvers:
   - add narrow `requireDbuPerUm(owner)` and `requireRoutingLayer(owner)` helpers where the same boundary repeats;
   - avoid a broad global utility unless duplication becomes real across adapter boundaries;
   - keep `wire_width` as optional `library_default` unless the user confirms otherwise.
2. First behavior wave:
   - update `FastStaBuilder`, `FastStaParasitics`, `Router`, and `STAAdapterRcTree` so algorithm/RC construction cannot continue with invalid DBU/layer/adapter state;
   - remove DBU clamps from fast STA and routing algorithm paths after the builder boundary validates inputs.
3. Second behavior wave:
   - update cluster constraint, sink clustering, sink-load-region, root-driver compensation, topology length estimation, and H-tree wirelength helpers to use fatal or typed failure instead of partial default structs/empty vectors;
   - preserve `HTree::BuildResult` / `SourceTrunkSegment::BuildResult` style only if per-clock failure is confirmed as desired.
4. Facade/API cleanup:
   - decide whether `STAAdapter::queryWireResistance` / `queryWireCapacitance` become fatal directly or gain required wrappers for algorithm callers;
   - keep `Wrapper::queryDbUnit` fallible as a shared database query facade.
5. Wording cleanup:
   - replace production "falls back"/"Falling back" logs with "degraded", "normalized", or explicit policy wording;
   - evaluate whether `DiagnosticLevel::kFallback` should be renamed to `kDegraded`.
6. Tests:
   - update tests that currently expect graceful missing-DBU or missing-routing-layer behavior;
   - add death tests only after confirming death-test policy, otherwise test top-level setup/stage failure and typed result paths.

### Confirmed Decisions

- Global DBU/routing-layer absence is process-fatal at first algorithm use, including paths that previously returned typed per-clock failure for those global prerequisites.
- `wire_width` remains optional and uses the technology/library default when unspecified.
- Char-only tests may provide explicit synthetic DBU so production characterization can fatal on missing DBU.
- STA wire RC query facades remain fallible for report/probe paths, with required wrappers for algorithm calls.

### Semantic Boundary Implementation Outcome

- Added required wire RC wrappers:
  - `STAAdapter::queryRequiredWireResistance`
  - `STAAdapter::queryRequiredWireCapacitance`
- Converted required DBU/routing-layer/unit/adapter states to `LOG_FATAL` in fast STA, STA RC-tree installation, routing, cluster constraints, sink clustering, sink-load-region, root-driver compensation, H-tree/source-trunk, topology length estimation, characterization, analytical characterization, and related precondition helpers.
- Preserved fallible `STAAdapter::queryWireResistance` / `queryWireCapacitance` for report/probe diagnostics.
- Preserved optional `wire_width` as `library_default`.
- Added explicit `FastStaCharTopologySpec::dbu_per_um` injection for char-only/unit contexts.
- Removed production `fallback` wording from iCTS source in favor of degraded/normalized/explicit policy language.
- Fixed the fast STA `ClockLayout` build order so layout segments are converted to RC only after runtime DBU/routing-layer options are validated in the fast STA context.
- Requested iCTS dev script passed after rebuilding `iEDA`:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Full iCTS checker passed after the follow-up changes:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result: `In-scope findings: 0`.
