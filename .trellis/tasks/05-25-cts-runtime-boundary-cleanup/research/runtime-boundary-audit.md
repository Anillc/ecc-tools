# CTS runtime-boundary cleanup audit

Date: 2026-05-25

## Scope

Audited current iCTS code for the class of problems raised by `FastSTA::buildClockContext`:

- repeated stable runtime dependencies passed as per-call algorithm arguments;
- duplicated public overloads whose production semantics are not both used;
- private helper APIs that still move `Config` / `Wrapper` / `STAAdapter` / `FastSTA` / `SchemaWriter` as long parameter lists;
- broad `Config` or `Wrapper` dependencies where a derived policy or environment fact is enough.

## Confirmed hotspots

### FastSTA clock context construction

Files:

- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.cc`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.cc`
- `src/operation/iCTS/source/flow/optimization/Optimization.cc`

Findings:

- `FastSTA` exposes both no-route and route-geometry `buildClockContext` overloads.
- `FastStaBuilder` mirrors both overloads.
- Production code only calls the route-geometry overload from `Optimization.cc`.
- Tests register synthetic contexts directly and do not require the no-route public overload.
- `Config`, `STAAdapter`, and `Wrapper` are stable runtime environment facts for one `FastSTA` instance, but are repeated as per-call build
  arguments.
- `Wrapper` is only used to query DBU-per-micron; a full `Wrapper&` is unnecessary below the flow boundary.

Required cleanup:

- introduce a typed `FastStaEnvironment` or equivalent bound runtime policy;
- replace duplicated public build overloads with one `FastStaClockBuildInput`-based API;
- remove or make private the no-route build path unless a concrete production flow and timing model is defined;
- ensure route geometry / route tree injection semantics are not ambiguous.

### Topology HTree input builders

Files:

- `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkBranch.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunk.cc`

Findings:

- Private helpers still expose long runtime plumbing:
  - `BuildSinkHtreeInput(config, design, wrapper, sta_adapter, fast_sta, reporter, ...)`;
  - `BuildTopSegmentInput(config, wrapper, sta_adapter, fast_sta, reporter, ...)`;
  - `BuildTopHtreeInput(config, design, wrapper, sta_adapter, fast_sta, reporter, ...)`.
- These helpers mostly assemble `HTree::Input`, `SourceTrunkSegment::Input`, characterization runtime input, and policy from the same stable runtime
  group.
- The long helper signatures are private, but they preserve the same readability problem inside topology orchestration.

Required cleanup:

- introduce a local topology runtime/environment binding or reuse the enclosing `Topology::Input` directly;
- derive characterization runtime input and HTree/segment configs once per topology build path;
- keep per-call data explicit: root net, source pin, root input, load role, source-to-root phase.

### Runtime Config deep reads in adapter/service APIs

Files:

- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`
- `src/operation/iCTS/source/database/adapter/sta/*`
- `src/operation/iCTS/source/database/adapter/fast_sta/*`

Findings:

- `STAAdapter` still exposes several methods taking broad `Config`, for example configured RC, source cap limit, full timing refresh, clock-net RC
  install, pin slew limit, and configured unit-RC reporting.
- Some of these names are explicit (`queryConfigured...`, `emitConfigured...`) and may remain temporarily acceptable, but the architectural target is
  to derive typed STA/FastSTA policies near the flow boundary.
- FastSTA currently stores `STAAdapter*` inside every `FastStaClockContext`. That is acceptable only if the environment binding makes the lifetime
  explicit and prevents mixed-runtime context construction.

Required cleanup:

- for this task, remove the broad Config/Wrapper dependency from FastSTA clock context construction;
- audit `STAAdapter` Config-taking methods and either move them to typed policies or explicitly mark them as adapter-bound configured operations;
- do not leave ambiguous Config deep reads inside newly refactored FastSTA/optimization paths.

### Optimization per-clock context lifetime

Files:

- `src/operation/iCTS/source/flow/optimization/Optimization.cc`
- `src/operation/iCTS/source/flow/optimization/preparation/OptimizationPreparation.cc`
- `src/operation/iCTS/source/flow/optimization/solver/OptimizationSolver.*`

Findings:

- `Optimization.cc` owns the per-clock FastSTA context id and must erase it on multiple branches.
- This is correct but scattered. It is a good candidate for a per-clock `ClockOptimizationRun` or scoped context handle after FastSTA environment
  cleanup.

Required cleanup:

- centralize FastSTA context lifetime where practical;
- at minimum, ensure the new FastSTA build API makes context ownership and erase responsibility obvious.

## Not every match is a defect

The following are acceptable unless local review finds broad runtime plumbing:

- algorithm-local `Config` types such as `HTree::Config`, `ClusterConfig`, `BSTRoutingConfig`, `AnalyticalDominanceConfig`;
- report helpers that take `SchemaWriter&` as the actual output sink;
- adapter functions that query one explicit domain fact from `STAAdapter` or `Wrapper`;
- stage-level `Input` structs holding runtime-owned dependencies at the flow boundary.

The cleanup target is unclear ownership and repeated stable environment plumbing, not removing every occurrence of the word `Config`.

## Implementation classification and cleanup record

### FastSTA clock context construction

Status: cleaned in this task.

- Replaced the two public `FastSTA::buildClockContext(...)` overloads with a single `FastStaClockBuildInput` API.
- Added `FastStaEnvironment` and `FastSTA::bindEnvironment(...)` so stable runtime facts are bound once:
  `STAAdapter*`, DBU-per-micron, routing layer, wire width, root input slew, max-cap policy, max sink transition, and STA refresh policy.
- Converted `FastStaBuilder` to consume `FastStaEnvironment` plus `FastStaClockBuildInput` instead of `Config / STAAdapter / Wrapper`.
- Kept the builder's no-route internal branch only as a semantics-preserving implementation option behind one public input contract. Production
  optimization still passes route geometry explicitly.
- Removed `Wrapper` from FastSTA clock-context construction; DBU crosses as a narrow integer environment fact.

### Optimization per-clock FastSTA lifetime

Status: cleaned in this task.

- `Optimization` now derives `FastStaEnvironment` once at the stage boundary and binds it to `FastSTA`.
- Per-clock calls pass only `Clock` and route geometry through `FastStaClockBuildInput`.
- Context erase is centralized with a local RAII guard so skip/failure branches do not each own cleanup.

### Topology HTree input builders

Status: cleaned for the in-scope private helpers.

- `BuildSinkHtreeInput` now consumes the enclosing `Topology::Input` plus explicit per-call facts (`root_net`, clustering/load role).
- `BuildTopSegmentInput` and `BuildTopHtreeInput` now consume the enclosing `SourceTrunkInput` plus explicit per-call facts (`clock_source`,
  `root_input`, `source_net`).
- The helper signatures no longer repeat `Config / Design / Wrapper / STAAdapter / FastSTA / SchemaWriter` as separate private plumbing.

### STAAdapter configured operations

Status: partially narrowed and classified.

- Added narrow STAAdapter input/policy types for the FastSTA/Topology paths:
  `StaTimingRefreshConfig`, `ClockSourceDriveCapLimitInput`, and `PinSlewLimitInput`.
- `FastStaBuilder` and `SourceTrunk` now call those narrow APIs instead of passing broad `Config`.
- `FastStaEnvironment` carries a FastSTA-owned `FastStaTimingRefreshPolicy` rather than exposing STAAdapter policy types through the FastSTA facade.
- Existing `Config` overloads are retained as compatibility shims and for explicitly configured adapter operations outside this task's primary
  path. They should not be copied into new algorithm/module APIs.
- Remaining Config-taking STAAdapter methods are classified as adapter-bound configured operations for now:
  initialization, configured unit RC report, configured route segment RC query, full-design timing refresh, and iDB RC-tree installation. Further
  reduction belongs to the later structural optimization task if it requires wider flow/report/evaluation edits.

### Additional same-class sweep cleanup

Status: cleaned where local and low-risk.

- `Wrapper::traceSdcClocks` no longer takes broad `Config`; it now consumes `SdcClockTraceInput` with SDC data, max fanout, and reporter.
- `OptimizationPreparation::CollectClockSizingBufferMasters` no longer takes broad `Config`; it now consumes `ClockSizingMasterQueryInput` with the
  candidate master list and STA adapter.
- `SinkLoadClustering` now derives explicit sink-clustering electrical and buffer-master policies before invoking private helpers.
- `TopologyDistanceReport` no longer passes broad Config through its private path-resolution helper.
- `STAAdapterRcTree` now derives a local `ClockNetRcTreeInstallPolicy` once before per-edge RC queries.

### PRD recheck cleanup

Status: cleaned after acceptance recheck.

- `PrepareSinkTreeLoads` still accepted `const Config& flow_config` even though it only needed the sink-clustering decision, fanout/cap policy,
  route-segment RC, object prefix, buffer masters, root loads, build object, and STA adapter. It now consumes `SinkTreeLoadPreparationInput` plus
  `SinkTreeLoadPreparationPolicy`, and the enclosing topology stage derives that policy once.
- `ClusterLeafDistanceReportInput` still carried `Config*` and `Wrapper*` only to resolve `log_file` and DBU-per-micron. It now carries
  `log_file` and `dbu_per_um` directly, keeping the report helper narrow.
- These changes close the same-class residual found during the PRD acceptance scan: helper APIs should not pass broad runtime services when the
  implementation only needs scalar environment facts or a small policy.

### Final residual classification

Allowed residuals after the sweep:

- `CTSAPI::getInst()` / `CTS_API_INST` remains the external API singleton boundary.
- `Flow` stores `CTSRuntime&` because `Flow` is the runtime owner/orchestrator boundary, not a lower algorithm.
- Test runtime helpers may expose `CTSRuntime&` for fixture setup.
- `SchemaForward.hh` is the central shared place that forward declares the CTS-level `SchemaWriter`. Report writer types and helpers now live
  directly under `icts`; CTS source should not expose or use a separate report namespace.
- Public `STAAdapter` methods with `Configured` in the name remain compatibility/configured adapter operations:
  `queryConfiguredClockRouteSegmentRc`, `emitConfiguredUnitWireRcReport`, and `installClockNetRcTree(const Config&, ...)`.
- `STAAdapter::refreshFullDesignTimingContext(const Config&)`, `queryClockSourceDriveCapLimit(const Config&, ...)`, and
  `queryPinSlewLimit(const Config&, ...)` remain compatibility overloads that forward to narrower typed inputs.
