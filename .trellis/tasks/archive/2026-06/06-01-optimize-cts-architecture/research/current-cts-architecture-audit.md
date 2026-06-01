# Current CTS Architecture Audit

## Scope

This audit covers CTS architecture quality at the module boundary level. It is
not an algorithm correctness review.

Primary code areas inspected:

- `src/operation/iCTS/api`
- `src/operation/iCTS/source`
- `src/operation/iCTS/test`
- `src/platform/tool_manager/tool_api/icts_io`
- `src/interface/tcl/tcl_icts`
- `src/interface/python/py_icts`
- iCTS CMake files under `src/operation/iCTS` and `cmake/operation`

## Existing Structure

The current CTS tree broadly matches the intended iCTS package structure:

- `api`: external API facade.
- `source/database`: config, design model, wrappers, adapters.
- `source/flow`: orchestration pipeline.
- `source/module`: reusable CTS algorithm modules.
- `source/utils`: shared CTS utilities.
- `test`: module, flow, synthesis, HTree, and database tests.

Approximate size observed during inspection:

- iCTS total: about 466 `.hh` / `.cc` files and about 83k lines.
- `source`: about 62k lines.
- `source/flow`: about 25.9k lines.
- `source/module`: about 17.1k lines.
- `source/database`: about 16.7k lines.
- `source/utils`: about 2.5k lines.

`ctest --test-dir build -N` listed 14 iCTS tests, including database, flow,
synthesis, HTree, characterization, routing, and topology module tests.

## Positive Architecture Signals

- `CTSAPI` is the intended single external CTS singleton boundary.
- `CTSAPI::init` resets runtime state and runs setup before `runCTS`.
- `Flow::runCTS` follows the recognizable CTS pipeline.
- `CTSRuntime` keeps runtime-owned services together at the flow boundary.
- `Design` owns clocks, instances, pins, and nets with maps for lookup.
- `Wrapper` isolates iDB/Liberty/RC/clock IO concerns.
- `FastSTA` has explicit environment and clock-build input structs.
- `Topology` and `HTree` have explicit input/output/facade contracts.
- Existing archived tasks already established naming direction for setup,
  synthesis, optimization, instantiation, evaluation, report, topology, and
  htree.

## Main Findings

### 1. API and Tool Entry Status Is Too Weak

Evidence:

- `src/operation/iCTS/api/CTSAPI.hh`: `init()` and `runCTS()` are `void`.
- `src/operation/iCTS/api/CTSAPI.cc`: `init()` and `runCTS()` execute flow
  behavior without returning a status.
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`: `CtsIO::runCTS()`
  calls CTS init/run and then returns success.
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`: Tcl auto-run checks a boolean from
  tool-manager, but the lower CTS path can currently hard-code success.

Impact:

- CTS failure can be hidden from command callers.
- Tcl/Python integration cannot reliably distinguish config/setup/run/report
  failures.
- Follow-up architecture work lacks a consistent error channel.

Recommended fix:

- Add typed status/result contracts at the API boundary and propagate them
  through `CtsIO`, tool-manager, Tcl, and Python.

### 2. Entry Paths Are Not Fully Unified

Evidence:

- Tcl/Python report commands call `CTS_API_INST.report(...)` directly in some
  paths.
- Run paths go through tool-manager/CtsIO in other paths.

Impact:

- Status handling and lifecycle assumptions can diverge.
- Future API changes require duplicated integration updates.

Recommended fix:

- Route CTS commands through one boundary where practical. If a direct API path
  remains, it must use the same status contract.

### 3. Config Contract Drift

Evidence:

- Tcl config exposes `use_netlist` and `net_list`.
- Sample CTS JSON configs contain `use_netlist` and `net_list`.
- `Config::parse` does not parse these options as active runtime fields.
- `ClockDataRead` now reads clock data from SDC-derived flow.
- Numeric config conversion can silently default invalid present values.

Impact:

- Users can set options that appear supported but are not active.
- Invalid values can produce confusing downstream behavior.

Recommended fix:

- Treat `use_netlist` / `net_list` as legacy unused items.
- Align Tcl config, JSON samples, and runtime parser around that deprecation
  decision.
- Warn for unsupported config keys: unknown keys are invalid config keys; known
  deprecated keys are no longer used.
- Treat invalid present numeric values as config errors.

### 4. CMake Include Boundaries Are Still Broad

Evidence:

- `src/operation/iCTS/source/CMakeLists.txt` exposes `${ICTS_SOURCE}` through
  the aggregate target.
- `src/operation/iCTS/source/database/CMakeLists.txt` exposes several database
  include roots as `INTERFACE`.
- `src/operation/iCTS/source/flow/CMakeLists.txt` exposes broad flow/source
  roots.
- `cmake/operation/icts.cmake` still references legacy include directories such
  as old solver/config/io/util style paths.

Impact:

- Hidden transitive include dependencies are easier to introduce.
- Stale include metadata misleads future maintainers.
- Production and test dependency boundaries are harder to reason about.

Recommended fix:

- Remove or update stale legacy include lists.
- Tighten production target include roots incrementally.
- Keep no link groups or duplicate archive workarounds.

### 5. Flow Facades Are Growth Hotspots

Evidence:

- `Flow` owns runtime lifecycle, setup readiness, and full run orchestration.
- `Synthesis` combines per-clock orchestration, summaries, and status table
  behavior.
- `Topology` combines sink domain construction, commit/rollback, layout, and
  reporting behavior.

Impact:

- These files are still understandable, but future changes may increase
  coupling and review risk.

Recommended fix:

- Do not split mechanically.
- Extract named status/result structs and domain-specific helpers only when
  they reduce actual coupling.

## Recommended Priority

1. Status/entry contract.
2. Config contract.
3. CMake/include boundary cleanup.
4. Facade decomposition guided by the first three steps.

This order reduces silent failure first, then uses the new status path to make
config cleanup safer, then addresses build boundaries, and only then performs
optional code movement.
