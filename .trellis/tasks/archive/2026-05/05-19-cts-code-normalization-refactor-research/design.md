# Design: CTS Code Normalization Refactor

## Objective

Normalize the iCTS architecture around CTS semantics rather than file-size mechanics. The desired outcome is a codebase where:

- each flow stage has a predictable lifecycle shape;
- folder names describe CTS responsibilities;
- facade headers expose stable stage/module APIs;
- internal data models are split by concept;
- module algorithms receive explicit data/options instead of reading runtime singletons;
- FastSTA exposes a CTS fast timing facade, not its internal graph model;
- CMake targets express semantic dependencies.

## Non-Goals

- Do not redesign CTS QoR algorithms as part of structure cleanup.
- Do not change external `CTSAPI` behavior unless separately approved.
- Do not move FastSTA out of `database/adapter/fast_sta` during this refactor campaign unless the user approves a later location change.
- Do not rename files mechanically without a semantic target name.
- Do not introduce a generic service framework that hides CTS concepts.

## Flow Stage Responsibilities

The accepted flow order remains:

```text
setup -> synthesis -> optimization -> instantiation -> evaluation -> report
```

`setup/clock_data` is an explicit setup substage because it exists today as `Flow::readData()` and is a real boundary. It should not remain hidden
under instantiation design conversion.

Define every stage by required data, produced state/artifacts, owned behavior, allowed capabilities, and forbidden dependencies before adding
shared lifecycle conventions.

### Responsibility Matrix

| Stage | Required CTS Data | Produced State / Artifacts | Owned Behavior | Allowed Capabilities | Forbidden Dependencies |
|---|---|---|---|---|---|
| `setup` | runtime config, iDB/iSTA handles, result path config | initialized wrapper/STA boundary, schema registration, stage-ready status | validate environment, initialize external adapters, resolve runtime config into CTS options | read `CONFIG_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST` because setup owns external runtime readiness | perform synthesis/optimization, mutate final CTS clock tree |
| `setup/clock_data` | configured clock-net pairs or SDC clock definitions, iDB clock objects, pin/net topology | `Design` clocks, sources, sinks, insts, pins, nets, and clock membership | read clock data into CTS design model; report missing or invalid clock definitions | call wrapper and SDC adapter through setup boundary | create inserted clock objects, write back iDB nets, run optimization |
| `synthesis` | CTS `Design`, clock sources/sinks, topology options, buffer/root-driver data | temporary CTS topology, sink-domain structure, H-tree/source-trunk candidates, synthesis trace | build clock topology and inserted-object intent before timing optimization | call synthesis modules with explicit CTS data/options | write committed iDB objects, query mutable FastSTA clock context directly |
| `optimization` | synthesized topology, FastSTA facade, sizing options, legality limits | selected clock-sizing edits, optimized buffer masters/topology edits, optimization report data | evaluate candidates, solve sizing/legalization choices, apply timing-driven edits through facade APIs | use FastSTA clock timing queries and clock-sizing edit APIs | depend on FastSTA raw vectors/maps or broad clock context layout |
| `instantiation` | optimized CTS topology, inserted-object intent, design commit options | committed CTS `Design` objects, iDB writeback artifacts, clock-net membership restore data | materialize inserted buffers/nets, commit CTS objects, project committed design to iDB | call wrapper writeback APIs and design commit helpers | read external clock data, rerun synthesis or optimization |
| `evaluation` | committed CTS design, clock layout/route trees, STA refresh policy | QoR metrics, skew/latency/wirelength/power summaries, evaluation state | refresh/query timing when requested and aggregate QoR | call STA adapter for full-design timing/RC metrics | create report files that belong to report/export, mutate CTS topology |
| `report` | evaluation state, committed design, visualization/export options, result paths | report files, QoR tables, SVG/GDS/export artifacts, final run summary | resolve output paths and emit artifacts | reuse evaluation or request evaluation refresh through explicit stage data | own optimization/evaluation algorithms or consume broad root-flow state directly |

### Stage-Owned Data Flow

```text
setup
  -> setup/clock_data builds CTS clock design data
  -> synthesis builds inserted-clock intent
  -> optimization selects clock-sizing and topology edits
  -> instantiation commits inserted-clock objects and iDB writeback
  -> evaluation aggregates QoR on committed design
  -> report emits files and visualization artifacts
```

## Target Flow Lifecycle Pattern

The lifecycle convention is subordinate to the responsibility matrix. It exists only to make the stage boundaries visible and consistent.

### Stage Contract

Each root flow stage should follow this conceptual contract:

```text
StageState -> prepare/init -> run/execute -> report -> StageResult
```

Do not require a virtual base class unless duplication proves it is useful. A simple convention with strongly named CTS stage state/result structs is
enough.

Recommended responsibilities:

- `prepare/init`
  - Validate required singleton readiness at the boundary.
  - Resolve runtime config into explicit stage options.
  - Build narrow stage-local state.
  - Open stage metric/report scopes.
- `run/execute`
  - Perform the stage's owned mutation or readonly computation.
  - Call submodules with explicit CTS data/options.
  - Return typed failure/no-op status instead of leaking local bool semantics.
- `report`
  - Emit concise stage summary.
  - Emit detailed helper diagnostics to `cts_detail.log`.
  - Keep report field construction close to data owners.
- `StageResult`
  - Include `status`, `reason`, counters, and stage payload.
  - Use enum status rather than several loosely related booleans.

### Recommended Common Types

```cpp
enum class FlowStageStatus
{
  kFinished,
  kSkipped,
  kFailed
};

struct FlowStageResult
{
  FlowStageStatus status = FlowStageStatus::kFailed;
  std::string reason;
};
```

Stage-specific results should embed or mirror this status:

- `SetupResult`
- `ClockDataReadResult`
- `SynthesisResult` or rename existing `SynthesisTraceSummary`
- `OptimizationResult`
- `InstantiationResult`
- `EvaluationResult`
- `ReportResult`

Do not replace CTS-specific fields with a generic map. Keep domain counters typed.

### Root Flow Shape

Target root flow:

```text
Flow::runCTS
  setup gate
  readClockData()
  runSynthesis()
  runOptimization()
  instantiateClockTree()
  evaluateClockTree()
  emitReports()
  emitRunResults()
```

Rename internal helpers so their names match the stage they run:

- `Flow::run()` -> `runSynthesisAndOptimization()` only as a temporary bridge, or split into `runSynthesis()` and `runOptimization()`.
- `Flow::readData()` -> `readClockData()`.
- `Flow::instantiate()` -> `instantiateClockTree()`.
- `Flow::evaluate()` -> `evaluateClockTree()`.

## Target Folder Structure

### Flow

Recommended shape:

```text
source/flow/
  Flow.hh/.cc
  setup/
    Setup.hh/.cc
    clock_data/
      ClockDataRead.hh/.cc
      ConfiguredClockPairs.hh/.cc
      SdcClockImport.hh/.cc
  synthesis/
    Synthesis.hh/.cc
    distribution/
    topology/
    htree/
    trace/
  optimization/
    Optimization.hh/.cc
    options/
    clock_problem/           # current model pieces that describe the sizing problem
    preparation/
    candidate/
    state/
    solver/
    mutation/
    report/
  instantiation/
    Instantiation.hh/.cc
    design_commit/           # commit temporary CTS objects to Design
    idb_writeback/           # project committed Design to iDB
  evaluation/
    Evaluation.hh/.cc
    qor/
      summary/
      timing/
      wirelength/
      root_path/
  report/
    Report.hh/.cc
    export/
    overview/
    qor/
    visualization/
```

Key change:

- Move pre-synthesis clock import out of `instantiation/design_conversion`.
- Keep post-synthesis temporary object commit separate from iDB writeback.

### FastSTA

Keep current location initially:

```text
source/database/adapter/fast_sta/
```

But split internals under semantic subfolders:

```text
fast_sta/
  FastSta.hh/.cc                   # facade only
  context/
    FastStaClockState.hh/.cc       # owned per-clock timing state
    FastStaClockTree.hh/.cc        # clock-tree nodes, clock nets, and topology IDs
    FastStaSegmentCharState.hh/.cc # characterization-only timing state
  liberty/
    FastStaLibertyModel.hh/.cc
    FastStaLibertyTable.hh/.cc
  parasitic/
    FastStaClockNetParasitic.hh/.cc
    FastStaPiReduction.hh/.cc
  timing/
    FastStaTiming.hh/.cc
    FastStaDmpCeff.hh/.cc
    FastStaDmpEquation.hh/.cc
  power/
    FastStaPower.hh/.cc
  incremental/
    FastStaClockSizingEdit.hh/.cc
    FastStaChangedSubtree.hh
  report/
    FastStaReport.hh/.cc
```

Suggested CMake targets:

```text
icts_source_database_adapter_fast_sta
icts_source_database_adapter_fast_sta_context
icts_source_database_adapter_fast_sta_liberty
icts_source_database_adapter_fast_sta_parasitic
icts_source_database_adapter_fast_sta_timing
icts_source_database_adapter_fast_sta_power
icts_source_database_adapter_fast_sta_incremental
icts_source_database_adapter_fast_sta_report
```

Facade dependencies:

- `fast_sta` facade links private implementation targets.
- External callers link only the facade unless they are unit tests for an internal component.

### Characterization Module

Current `CharBuilder.hh` should be treated as a framework facade and split internally:

```text
source/module/characterization/
  CharBuilder.hh/.cc
  options/
    CharGridOptions.hh/.cc
    BufferCharacterizationCatalog.hh/.cc
  topology/
    SegmentTopologyEnumeration.hh/.cc
    BufferingPatternStorage.hh/.cc
  feasibility/
    SegmentFeasibility.hh/.cc
  sampling/
    FastStaSegmentSample.hh/.cc
    SlewLoadGridSampler.hh/.cc
  storage/
    SegmentCharStorage.hh/.cc
```

The `CharBuilder` facade should expose `init`, `build`, and query APIs. Private helper declarations should move into semantic internal headers.

### Routing Module

`Router` should be a routing facade, but RC-tree construction needs explicit technology data.

Recommended changes:

- Remove or deprecate `Router::buildRCTree(clock_tree)` with no options.
- Require `ClockRouteRcData` / `RCTreeBuildOptions` at compile-time API level.
- Move DBU resolution out of `Router.cc`; pass `dbu_per_um` explicitly or include it in `RCTreeBuildOptions`.

Target options:

```cpp
struct RCTreeBuildOptions
{
  int32_t dbu_per_um = 0;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
};
```

`Router` may still call a technology query object if one is explicitly passed, but it should not read `WRAPPER_INST` and `STA_ADAPTER_INST`
itself.

## Naming Rules

### Naming Bar

Names must state the CTS object and the owned action. Do not replace generic words with different generic words.

For source file/type names, forbid these as recommended names unless the user explicitly approves a specific exception:

- `rollback`
- `fallback`
- `Input`
- `Response`
- `Session`
- `Support`
- `Internal`
- `Types`
- broad copied-state names without a CTS object/action qualifier
- standalone `Network`

`Network` is especially risky as a new standalone CTS name because CTS already has `Net` / clock-net semantics. Use a concrete CTS object such as
`ClockTree`, `ClockNet`, `RouteTree`, `RCTree`, or `ClockDAG` only when that is the actual concept. `ClockNetwork` / `clock_network` is allowed
when it is the database clock-network concept and matches the established `database/design` model.

If a concrete CTS object/action name cannot be determined, record the unresolved name and ask the user for the naming standard before editing.

### File and Type Naming

Use CTS terms that identify:

- the stage: `ClockDataRead`, `ClockWriteback`, `HTreeDepthSearch`, `SourceTrunk`, `ClockSizing`;
- the object: `Clock`, `SinkDomain`, `RootDriver`, `ClockNet`, `RouteTree`, `RCTree`, `RoutingSegment`, `LibertyCell`;
- the action: `Build`, `Select`, `Commit`, `Restore`, `Evaluate`, `Report`, `Sample`, `Solve`.

Avoid generic names in source unless the code is truly generic infrastructure and the exception has been recorded:

- `Internal`
- `Support`
- `Request`
- `Response`
- `Types`
- broad copied-state names without CTS object/action semantics
- `Context` without a CTS qualifier

### Replacement Examples

| Current Pattern | Preferred Direction |
|---|---|
| `WrapperClockWriterSupport` | `ClockNetMembershipRestore` / `ClockWritebackObjectMap` |
| `WrapperClockWriterInternal` | `ClockWritebackPlan` / `ClockWritebackIdbAccess` / `ClockNetMembershipRestore` |
| `STAAdapterInternal` | `StaClockTreeTiming` / `StaWireRcQuery` / `StaLibertyCellQuery` |
| `FastStaTypes` | split into `FastStaClockTree`, `FastStaLibertyModel`, `FastStaClockNetParasitic`, `FastStaTimingState` |
| `OptimizationTypes` | split into `ClockSizingProblem`, `ClockSizingEdit`, `ClockSizingProfile`, `TopologyWindow` |
| `AnalyticalSolverRequest` | `AnalyticalHTreeSearchData` or `AnalyticalHTreeSolveData` |
| `QorEvaluationInternal` | `QorMetricAggregation` / `QorRootPathProbe` |

### Copied-State Naming Policy

Do not use broad copied-state names for queryable CTS, Liberty, timing, or iDB state. The name must state why the data exists and which CTS object
or algorithm owns it.

Allowed:

- A runtime metric sample if it is truly point-in-time data.

Prefer:

- `LibertyCellModel` for copied Liberty data.
- `ClockNetMembershipRestore` for pre-writeback membership data that is used to restore iDB clock-net connections.
- `TimingState` for computed timing state.
- `ReportSample` for report-only metrics.

## FastSTA Facade Boundary

Target facade surface:

- context lifecycle:
  - `buildClockTimingContext`
  - `eraseClockTimingContext`
  - `clear`
- route/parasitic injection:
  - `injectClockNetRouteTree`
  - `injectClockRouteGeometry`
- timing/power:
  - `updateTiming`
  - `updatePower`
  - `querySkew`
  - `querySinkArrivals`
  - `queryCapStatus`
  - `querySlewStatus`
  - `queryPower`
  - `queryArea`
- sizing:
  - `beginClockSizingEdit`
  - `applyBufferMasterChange`
  - `restoreClockSizingEdit`
  - `commitClockSizingEdit`
- characterization:
  - `buildSegmentCharacterizationContext`
  - `setSegmentLoad`
  - `runSegmentSample`

Avoid facade APIs that expose:

- mutable context pointers;
- raw node/net vectors;
- implementation maps;
- char contexts as normal clock contexts.

Internal tests can target submodule libraries directly instead of opening the facade.

## Module Boundary Rules

### Flow May Read Singletons

Flow/database/adapter boundaries may read:

- `CONFIG_INST`
- `WRAPPER_INST`
- `STA_ADAPTER_INST`
- `DESIGN_INST`

when they own lifecycle initialization, committed design mutation, or external adapter setup.

### Module Should Receive Explicit Data

Module code should receive:

- `dbu_per_um`
- `routing_layer`
- `wire_width_um`
- `buffer master catalog`
- `pin/load caps`
- `slew/cap limits`
- `clock route RC query`
- algorithm options

as typed inputs.

Migration should be incremental:

1. Add explicit options next to existing singleton reads.
2. Update flow callers to pass options.
3. Remove implicit singleton/config recovery from module implementation.
4. Add focused tests that do not require global runtime initialization.

## Compatibility Strategy

- Keep public `CTSAPI` stable.
- Keep root flow stage order stable.
- Preserve existing output files unless a report-structure task separately approves changes.
- Do not change algorithm defaults during structure-only phases.
- Rename/move with CMake target updates in the same patch.
- Keep behavior-preserving split commits separate from boundary-enforcing commits.

## Validation Strategy

During implementation phases:

```bash
ninja -C build <affected-targets>
ctest --test-dir build -N -R icts
```

Representative fast tests:

```bash
ninja -C build icts_test_flow icts_test_database_adapter_fast_sta icts_test_flow_synthesis icts_test_flow_synthesis_htree icts_test_module_characterization
./bin/icts_test_flow --gtest_color=no
./bin/icts_test_database_adapter_fast_sta --gtest_color=no
./bin/icts_test_flow_synthesis --gtest_color=no
./bin/icts_test_flow_synthesis_htree --gtest_color=no
./bin/icts_test_module_characterization --gtest_color=no
```

Final iCTS quality gate:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Restore Strategy

- Keep each folder/target split behavior-preserving.
- Rename commits should be isolated from behavior commits.
- Remove/deprecate public internal APIs only after all callers are migrated.
- FastSTA internal split should preserve facade behavior first, then narrow facade APIs in a later patch.
- If a target split breaks build visibility, restore only the CMake/include split without reverting unrelated semantic renames.
