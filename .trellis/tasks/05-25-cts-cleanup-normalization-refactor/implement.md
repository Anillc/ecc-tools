# CTS Cleanup And Normalization Refactor Implementation Plan

## Current State

Planning is complete enough to start after user approval. The task is still `planning`; do not run implementation until `task.py start`.

Do not run `ecc_dev_tools` during this task until the user explicitly requests it.

## Phase 0: Baseline

- [ ] Confirm working tree only contains this task plus intentional untracked temporary files.
- [ ] Confirm prior refactor/archive commits remain in history.
- [ ] Build the current baseline target set if needed before edits:
  - `ninja -C build icts_source_flow`
  - `ninja -C build icts_source_database_adapter_fast_sta`
  - `ninja -C build icts_source_flow_synthesis_htree`
- [ ] Do not run `ecc_dev_tools`.

## Phase 1: FastSTA Public Surface Cleanup

- [ ] Remove unused `FastStaCharSegmentSpec`.
- [ ] Remove public `registerClockContext` from `FastSTA`.
- [ ] Replace `FastSTATest.cc` context injection with test-local context construction or real `buildClockContext` setup.
- [ ] Move `queryClockContext` and `mutableClockContext` out of public facade; keep as private helpers in `FastSta.cc` or `ContextStore`.
- [ ] Remove public `rebuildClockContext` if no production external caller remains.
- [ ] Remove unused public scalar queries:
  - `querySinkArrival`
  - `queryNodeSlew`
  - `queryNetLoad`
  - `queryArea`
  - `queryClockIds`
- [ ] Remove public `changeBufferMaster` facade if no production caller needs single-edit facade semantics.
- [ ] Remove no-count `injectNetRouteTree` facade overload if all production callers use the counted overload.
- [ ] Keep lower-level FastSTA internal helpers private to the FastSTA target; tests that intentionally cover internals should use test helpers and
  private target links rather than facade-only test seams.
- [ ] Rebuild:
  - `ninja -C build icts_source_database_adapter_fast_sta`
  - `ninja -C build icts_test_database_adapter_fast_sta`
- [ ] Run:
  - `ctest --test-dir build -R '^icts_test_database_adapter_fast_sta$' --output-on-failure`

## Phase 2: Flow Runtime Header Consolidation

- [ ] Move `CTSRuntime` definition into `Flow.hh`.
- [ ] Delete `CTSRuntime.hh`.
- [ ] Update production includes:
  - `CTSAPI.cc`
  - `Flow.cc`
- [ ] Update test includes:
  - `test/main.cc`
  - `test/common/CTSTestRuntime.*`
  - flow tests and fixtures that directly include `CTSRuntime.hh`
  - real-tech fixtures that access `CurrentRuntime()`
- [ ] Keep `CTSAPI.hh` using forward declarations where possible.
- [ ] Audit `Flow.hh` public methods:
  - keep API-facing lifecycle methods;
  - make test-only partial-stage methods private if feasible;
  - if partial-stage tests need direct access, add a `test/flow` driver/helper instead of keeping production public surface for tests.
- [ ] Rebuild:
  - `ninja -C build icts_source_flow icts_test_flow`
- [ ] Run:
  - `ctest --test-dir build -R '^icts_test_flow$' --output-on-failure`

## Phase 3: HTree Contract Folding

- [ ] Move `HTreeContracts.hh` contents into `HTree.hh`.
- [ ] Prefer outward use of `HTree::Input`, `HTree::Config`, `HTree::Output`, `HTree::Summary`, and `HTree::Build`.
- [ ] Replace `HTreeLoadRole` with `HTree::LoadRole` or a temporary compatibility alias removed before task completion.
- [ ] Remove direct includes of `synthesis/htree/HTreeContracts.hh`.
- [ ] Delete `HTreeContracts.hh`.
- [ ] Update `Topology.hh` to include only `HTree.hh`.
- [ ] Decide whether `HTree::buildWithDiagnostics` remains production public:
  - keep if real-tech experiment/report tools are considered supported diagnostic APIs;
  - otherwise move diagnostics access to tests or a narrowly named diagnostic helper.
- [ ] Review `HTree::Output` vs `HTree::Diagnostics`:
  - ordinary output should keep only design payload and data consumed by topology;
  - diagnostics/report-only metrics should stay under diagnostic build or reporter/observer code.
- [ ] Rebuild:
  - `ninja -C build icts_source_flow_synthesis_htree icts_test_flow_synthesis_htree`
- [ ] Run:
  - `ctest --test-dir build -R '^icts_test_flow_synthesis_htree$' --output-on-failure`

## Phase 4: Topology And Synthesis Trace Cleanup

- [ ] Update `Topology.hh` after HTree contract folding.
- [ ] Review `Topology::buildSourceTrunk`:
  - make private/internal if it is only a topology implementation step;
  - move direct tests to a test helper or higher-level `Topology::formClock` scenario if needed.
- [ ] Review `Topology::Output` duplication of `HTree::Output` and inserted-object vectors; remove redundant transport if topology can absorb HTree
  payload internally.
- [ ] Review `ClockTopologyInput`, `TopologyInput`, and `SourceTrunkInput` for broad repeated runtime pointers; bind frequent collaborators in
  implementation builders when it reduces public signature noise.
- [ ] Privatize or fold trace helpers where possible:
  - `TopologyBuildTrace.hh`
  - `TopologyDistanceReport.hh`
  - `DomainStatus.hh`
  - layout adapter/build helper headers that are not real external contracts.
- [ ] Rebuild:
  - `ninja -C build icts_source_flow_synthesis icts_source_flow_synthesis_topology icts_test_flow_synthesis`
- [ ] Run:
  - `ctest --test-dir build -R '^icts_test_flow_synthesis$' --output-on-failure`

## Phase 5: Optimization Header Visibility

- [ ] Keep `Optimization.hh` as the only external optimization stage facade.
- [ ] Audit and tighten includes for:
  - `OptimizationPolicy.hh`
  - `OptimizationState.hh`
  - `OptimizationReport.hh`
  - `OptimizationCandidates.hh`
  - `OptimizationPreparation.hh`
  - `OptimizationSolver.hh`
- [ ] Fold single-translation-unit helpers into `.cc` files where this reduces public/private header surface.
- [ ] Keep multi-translation-unit helpers as internal headers, but ensure CMake dependencies and include directories are `PRIVATE`.
- [ ] Rebuild:
  - `ninja -C build icts_source_flow_optimization`
- [ ] Run optimization-related tests if present; otherwise rely on full `icts_test_*` in final validation.

## Phase 6: Characterization And Topology Module Cleanup

- [ ] Keep `Characterization.hh` and `CharBuilder.hh` as characterization module contracts.
- [ ] Make `CharBuilderImpl.hh` and related builder/circuit/pattern helper headers private implementation details.
- [ ] Remove root facade includes of pruning/table internals when callers do not need them.
- [ ] Keep database characterization headers under `source/database/characterization` as stable domain objects.
- [ ] Review `TopologyGen.hh` for internal-only static helpers:
  - `calcLeafCount`
  - `calcMaxDepth`
  - `buildFullTree`
  - `embedPositions`
  - report helper methods
- [ ] Move internal-only `TopologyGen` helpers private or file-local if tests can use behavior-level assertions instead.
- [ ] Keep `KMeans.hh`, `MinCostFlow.hh`, and `FastClustering.hh` as private/lower-level algorithm headers unless they are explicitly supported
  module contracts.
- [ ] Rebuild:
  - `ninja -C build icts_source_module_characterization icts_source_module_topology`
- [ ] Run focused module tests:
  - `ctest --test-dir build -R '^icts_test_module_characterization' --output-on-failure`
  - `ctest --test-dir build -R '^icts_test_module_topology' --output-on-failure`

## Phase 7: Report / Visualization / Utility Cleanup

- [ ] Keep `Report.hh` as the report stage facade.
- [ ] Verify `QorReport.hh`, `ReportExport.hh`, `Overview.hh`, `Visualization.hh`, `SvgVisualization.hh`, `GdsVisualization.hh`, `LayerPolicy.hh`,
  and `Drawing.hh` are only consumed inside report/visualization targets or intentional tests.
- [ ] Review `SchemaForward.hh`:
  - replace with local `class SchemaWriter;` forward declarations where easy and clearer;
  - keep only if it is documented as a utility forward header with real value.
- [ ] Review `RootedTreeLCA.hh`:
  - keep if intended as reusable utility with test coverage;
  - remove if obsolete and only test-owned.
- [ ] Rebuild:
  - `ninja -C build icts_source_flow_report icts_source_utils_logger`

## Phase 8: CMake Visibility Pass

- [ ] Remove public include directories that exist only for private implementation headers.
- [ ] Ensure every `PUBLIC` target dependency appears in the owning public header.
- [ ] Convert helper-target links to `PRIVATE` where public headers do not name helper types.
- [ ] Prefer target links over duplicated include paths.
- [ ] Rebuild all changed iCTS source targets.

## Phase 9: Final Validation

- [ ] Confirm no `_INST` uses exist except `CTS_API_INST`.
- [ ] Confirm `CTSRuntime.hh` and `HTreeContracts.hh` are gone or explicitly justified if retained.
- [ ] Confirm no production header exposes APIs used only by tests.
- [ ] Confirm no empty `Input`, `Config`, `Output`, or `Summary` wrappers were added.
- [ ] Run targeted builds from all changed phases.
- [ ] Run full iCTS tests:
  - `ctest --test-dir build -R '^icts_test_' --output-on-failure`
- [ ] Do not run `ecc_dev_tools` unless the user explicitly requests it.

## Review Gate Before Handoff

- [ ] Re-run include/user scans for FastSTA, Flow/Runtime, HTree, Topology, Optimization, and utility forward headers.
- [ ] Review every remaining root behavior directory with more than one root header and classify it as:
  - stable data-model exception;
  - intentional lower-level facade;
  - remaining cleanup item.
- [ ] Verify the final diff does not change `CTSAPI` behavior or algorithm QoR intentionally.
- [ ] Update PRD acceptance checkboxes only after code and tests converge.

