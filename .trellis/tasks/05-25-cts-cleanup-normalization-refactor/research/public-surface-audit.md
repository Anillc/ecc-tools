# iCTS Public Surface Audit

## Scope

This audit covers `src/operation/iCTS/source` and mirrored iCTS tests. It was performed after the committed runtime-boundary refactor
`6c437f228 refactor(iCTS): explicit CTS runtime boundaries`.

No implementation changes or `ecc_dev_tools` checks were run during this audit.

## Inventory

- iCTS source/header files under `source`: 353.
- iCTS source headers under `source`: 185.
- iCTS test source/header files under `test`: 111.
- Singleton scan result: only `CTS_API_INST` / `CTSAPI::getInst()` remains in iCTS, which matches the current singleton exception.
- Active untracked temporary files intentionally excluded from task work: `a.out`, `tmpkwi7mspm.cc`.

## Method

The audit used:

- header/include enumeration for all iCTS `*.hh` / `*.cc`;
- public-method call-site search for key facades;
- root-directory header inventory for behavior directories;
- standalone helper-header scan for names ending in `Contracts`, `Runtime`, `State`, `Report`, `Policy`, `Impl`, and `Forward`;
- manual inspection of the main facades and boundary headers.

Mechanical matches were not treated as final proof by themselves because common method names such as `run`, `build`, and `reset` create
cross-module noise. The findings below are the manually reviewed conclusions that should drive implementation.

## FastSTA Facade

Header: `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh`

The facade currently exposes both service APIs and context internals. The production-facing surface should be narrowed to building clock/char
contexts, applying optimization edits, updating timing/power, injecting committed route trees, and querying aggregate timing/power/state data.

Confirmed production-facing APIs:

- `bindEnvironment`
- `buildClockContext`
- `eraseClockContext`
- `reset`
- `buildCharContext`
- `eraseCharContext`
- `setCharLoad`
- `runCharSample`
- `changeBufferMasters`
- `changeBufferMastersTimingOnly`
- `updateTiming`
- `updatePower`
- `injectNetRouteTree` with `FastStaClockNetRcTreeCounts&`
- `queryClockGraphProfile`
- `queryClockAnalysisStatus`
- `queryClockTreeTopology`
- `collectClockSizingBuffers`
- `collectClockSinkArrivals`
- `queryClockNodeArrival`
- `querySkew`
- `queryCapStatus`
- `querySlewStatus`
- `queryPower`

Facade cleanup candidates:

- `registerClockContext`: used by `FastSTATest.cc` to inject contexts; remove from production facade and replace with test-local construction.
- `queryClockContext`: only used by `FastSta.cc`; move behind private implementation.
- `mutableClockContext`: only used by `FastSta.cc`; move behind private implementation.
- `rebuildClockContext`: only defined and internally reachable, no production external call; remove unless a real caller is introduced.
- `querySinkArrival`: no production external call; remove or keep private only if reused by another public query.
- `queryNodeSlew`: no production external call; remove or keep private only if reused by another public query.
- `queryNetLoad`: no production external call; remove or keep private only if reused by another public query.
- `queryArea`: no production external call; callers use `queryPower().area_um2`; remove.
- `queryClockIds`: no production external call; remove.
- `changeBufferMaster`: no production external facade call; the single-edit operation exists on the lower incremental helper and can remain internal.
- `injectNetRouteTree` without counts: no production external call; remove the facade overload or make the counted overload own all external use.
- `FastStaCharSegmentSpec`: no source or test uses; remove.

FastSTA internal headers are already mostly compiled as `PRIVATE`, but tests include multiple implementation headers directly. The production facade
should not expose these seams. If lower-level algorithms need direct tests, put the test-only factories/helpers in `test/database/adapter/fast_sta`
and link against the internal targets without adding public facade APIs.

## Flow Runtime Boundary

Headers:

- `src/operation/iCTS/source/flow/Flow.hh`
- `src/operation/iCTS/source/flow/CTSRuntime.hh`

`CTSRuntime.hh` has one production include (`Flow.cc`) plus API/test includes. It is a root flow-boundary type, not an algorithm contract. Keeping it
as a peer root header violates the desired root behavior-directory contract that `source/flow/` exposes `Flow.hh/.cc` only.

Required cleanup:

- fold the `CTSRuntime` definition into `Flow.hh`;
- delete `CTSRuntime.hh`;
- update `CTSAPI.cc`, `test/main.cc`, `CTSTestRuntime`, and tests to include `Flow.hh` or a test runtime helper instead of `CTSRuntime.hh`;
- keep `CTSAPI.hh` as a forward-declaration boundary where possible;
- ensure `CTSRuntime` remains a flow/API boundary owner only and is not passed into algorithms.

Flow facade audit:

- `runCTS`, `emitReports`, `outputSummary`, `reset`, and `setSetupReady` are used by `CTSAPI`.
- `readClockData`, `runSynthesis`, `runOptimization`, `evaluateClockTree`, `outputRuntimeSetup`, and `outputRunSummary` are primarily stage-level
  methods used by `Flow.cc` and tests. These should be reviewed as test-only public surface. The likely direction is to make stage methods private
  or expose them through a narrower test driver/helper if tests need partial-stage coverage.

## HTree Contract Boundary

Headers:

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTreeContracts.hh`

`HTreeContracts.hh` is included by HTree itself, topology, trace/layout, topology-build tracing, and many HTree implementation submodules. It is
currently acting as both:

- the public HTree contract (`Input`, `Config`, `Output`, `Summary`, `Build`, `DiagnosticBuild`);
- internal diagnostic and implementation data transport (`Diagnostics`, level plans, inserted-level maps, compensation details).

Required cleanup:

- fold public HTree contracts into `HTree.hh`;
- remove direct includes of `HTreeContracts.hh`;
- delete `HTreeContracts.hh` when all users include `HTree.hh`;
- prefer `HTree::Input`, `HTree::Config`, `HTree::Output`, `HTree::Summary`, and `HTree::Build` as the outward-facing names;
- convert the standalone `HTreeLoadRole` into `HTree::LoadRole` or keep a tightly scoped alias only during migration;
- review whether `buildWithDiagnostics` should remain a production public API or become test-only/internal. Current production code calls
  `buildWithDiagnostics` only from `HTree::build`; tests and real-tech experiment utilities call it for deep assertions and artifacts.

Potential follow-up cleanup inside HTree output:

- `HTree::Output` carries design payload plus trace/report details such as levels and best characterization pattern.
- `HTree::Diagnostics` carries a large amount of report-only data. This is acceptable as a diagnostic build shape, but production callers should
  not need to transport diagnostic state through ordinary output.
- During implementation, keep `HTree::Build` focused on committed design payload and minimal success summary; keep heavy diagnostic data reachable
  only through the diagnostic build path or an HTree reporter/observer interface.

## Topology Boundary

Header: `src/operation/iCTS/source/flow/synthesis/topology/Topology.hh`

`Topology.hh` currently includes both `HTree.hh` and `HTreeContracts.hh`; after HTree contract folding it should include only `HTree.hh`.

Cleanup candidates:

- `Topology::buildSourceTrunk` is production-called from `Topology.cc` and test-called directly. If it is not a real external topology entry, make it
  private/internal and move tests to `Topology::formClock` or a test helper.
- `Topology::resetClockTopology` has production and test callers; keep if it is a real module service, otherwise migrate tests through flow-level
  cleanup helpers.
- `ClockTopologyInput`, `TopologyInput`, and `SourceTrunkInput` contain broad repeated runtime pointers. This is not the primary removal target for
  this task, but the implementation should check if private stage builders can bind repeated objects instead of widening the public header.
- `Topology::Output` duplicates `HTree::Output` and extracted inserted object vectors. Review whether callers need both forms or whether topology
  should absorb HTree output internally and expose only topology-owned design payload.

## Optimization Boundary

Headers:

- `Optimization.hh`
- `policy/OptimizationPolicy.hh`
- `state/OptimizationState.hh`
- `report/OptimizationReport.hh`
- `candidate/OptimizationCandidates.hh`
- `preparation/OptimizationPreparation.hh`
- `solver/OptimizationSolver.hh`

The root `Optimization.hh` is a reasonable facade with `OptimizationInput` and `OptimizationSummary`.

The policy, state, report, candidate, preparation, and solver headers are internal to `source/flow/optimization`. They are not currently external
facade contracts, but CMake/include visibility makes them easy to include outside their owning implementation. The task should tighten visibility:

- keep `Optimization.hh` as the only external include for the optimization stage;
- ensure helper headers are included only by `optimization/` implementation files and tests that intentionally cover internals;
- make internal CMake links/include dirs `PRIVATE` unless helper types appear in a public header;
- consider folding tiny helpers into `Optimization.cc` when they are used by one translation unit only.

## Report / Visualization Boundary

`Report.hh` is the root facade. Subheaders such as `QorReport.hh`, `ReportExport.hh`, `Overview.hh`, `Visualization.hh`, `SvgVisualization.hh`,
`GdsVisualization.hh`, `LayerPolicy.hh`, and `Drawing.hh` are implementation slices under the report stage.

Cleanup direction:

- keep `Report.hh` as the external stage contract;
- verify no non-report stage includes report implementation helpers directly;
- keep visualization implementation headers private to the visualization target unless tests intentionally cover them;
- do not collapse stable format writer/data-model headers only for cosmetic reasons.

## Synthesis Trace Boundary

Headers:

- `SynthesisTrace.hh`
- `trace/domain_status/DomainStatus.hh`
- `trace/topology_build/TopologyBuildTrace.hh`
- `trace/distance/TopologyDistanceReport.hh`
- `trace/layout/*`

`SynthesisTrace.hh` is a stable summary type used by `Flow.hh`, `Synthesis.hh`, and tests. It can remain public if `Flow::outputRunSummary` or
`Synthesis::run` returns it.

Other trace helpers should be treated as synthesis implementation details:

- `DomainStatus.hh` is included by synthesis/distribution/topology and tests; check if it can be hidden behind `SynthesisTrace.hh` or stage-local
  code.
- `TopologyBuildTrace.hh` moves HTree build payload into topology output; after HTree cleanup, consider making it private to topology/trunk/sink
  implementation or folding it into `Topology.cc`.
- `TopologyDistanceReport.hh` is used from sink branch implementation only; candidate for local privatization or `.cc` fold.

## Characterization Module Boundary

The characterization module has a root facade `Characterization.hh`, but several low-level helper headers are also included outside their immediate
subdirectories:

- `builder/CharBuilderImpl.hh`
- `builder/CharFeasibilityChecker.hh`
- `builder/CharTopologyPlanner.hh`
- `circuit/CharCircuitBuilder.hh`
- `pattern/CharPatternEnumerator.hh`
- `pattern/CharPatternStorage.hh`
- `pruning/*`
- `table/*`

Some of these are genuine internal algorithm data structures shared across characterization submodules. The implementation should not force all of
them into the root facade. Instead:

- keep `Characterization.hh` / `CharBuilder.hh` as the external algorithm contracts;
- make `CharBuilderImpl.hh` a private implementation detail and remove any outward include requirement;
- ensure the root facade does not include pruning/table internals merely for convenience;
- keep stable database characterization objects under `source/database/characterization` as separate domain headers.

## Topology Module Boundary

Root facade: `source/module/topology/TopologyGen.hh`.

Candidate cleanup:

- `KMeans.hh` and `MinCostFlow.hh` are included only by clustering implementation. They can stay as private algorithm headers or be moved under a
  private subfolder; they should not be treated as public module contracts.
- `FastClustering.hh` is included by `TopologyGen.cc`, `Clustering.cc`, and tests. It is a lower-level algorithm facade; keep if tests and module
  composition need it, but do not expose it through broad include roots unnecessarily.
- `ClusterConstraintEvaluation.hh` and `ClusterConstraintEvaluator.hh` are used by clustering and fast clustering. Keep as internal shared module
  contracts unless a higher-level caller needs them directly.
- `TopologyGen` public helpers such as `calcLeafCount`, `calcMaxDepth`, `buildFullTree`, `embedPositions`, and report helpers appear to be used
  internally only. These should be moved private or to file-local helpers if no tests require the exact public entry.

## Utility Boundary

`SchemaForward.hh` is included widely to forward-declare `SchemaWriter`. It is not a behavior facade. During implementation, check whether simple
headers can forward declare `class SchemaWriter;` directly and include `Schema.hh` only in `.cc` files. If keeping `SchemaForward.hh`, document it
as an intentional utility forward header rather than a stage contract.

`RootedTreeLCA.hh` has no production source users and one test. It should be reviewed: if it is intended as reusable utility, add a real production
caller or keep it as utility with tests; if it is obsolete, remove it.

## CMake Visibility Findings

Many targets expose broad `PUBLIC` include directories for entire source roots, for example:

- `icts_source_flow` exposes `${ICTS_FLOW}`;
- `icts_source_flow_synthesis_htree` exposes `${ICTS_FLOW}`;
- `icts_source_flow_synthesis_topology` exposes `${ICTS_FLOW}`;
- `icts_source_database_adapter_fast_sta` exposes the FastSTA root while internal subtargets use private internal include directories.

The broad include roots make implementation headers easy to include from unrelated modules. The cleanup should:

- keep root facade include paths public only when the facade header requires them;
- move child/submodule include directories to `PRIVATE` wherever possible;
- prefer rooted includes through the owning facade for external callers;
- update tests deliberately when they need private helper coverage.

## Risk Areas

- `HTreeContracts.hh` folding has a large include fanout and will likely create compile-order and include self-sufficiency issues.
- Moving Flow stage methods private can break partial-flow tests; test helpers should be prepared before removing public methods.
- FastSTA tests currently rely on context injection. Removing `registerClockContext` requires test-local context construction or migration to real
  `buildClockContext` flows.
- CMake visibility tightening can surface hidden include dependencies. Make changes in small phases and rebuild affected targets after each phase.

