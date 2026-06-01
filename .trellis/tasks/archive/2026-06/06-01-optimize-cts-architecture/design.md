# CTS Architecture Optimization Design

## Current Architecture Map

iCTS is organized around a workable layered model:

- `api/CTSAPI` is the external singleton facade.
- `source/database` owns config, design data, database adapters, and IO
  wrappers.
- `source/flow` owns pipeline orchestration and the `CTSRuntime`.
- `source/flow/synthesis` owns synthesis-level behavior and delegates topology
  and HTree construction through named facades.
- `test` registers focused iCTS tests for database, flow, synthesis, HTree, and
  module behavior.

The intended dependency direction is:

```text
interface / tool_manager -> iCTS api -> iCTS source
test -> iCTS api/source
iCTS source -> no iCTS api dependency
```

The main pipeline currently follows the expected behavior sequence:

```text
setup -> synthesis -> optimization -> instantiation -> evaluation -> report
```

## Code Quality Assessment

### Strengths

- The repository already has a clear CTS module root under
  `src/operation/iCTS`.
- `CTS_API_INST` is the only intended external singleton boundary.
- `Flow` owns a concrete runtime object rather than scattering global state
  everywhere.
- `Design` uses owned objects and name maps for clocks, instances, pins, and
  nets.
- `FastSTA` has an explicit environment/input contract instead of reading all
  runtime state directly.
- Synthesis topology and HTree have readable high-level facades that match the
  archived architecture direction.
- iCTS has registered tests across database, flow, synthesis, HTree, and module
  layers.

### Architecture Risks

- `CTSAPI::init()` and `CTSAPI::runCTS()` are `void`; callers cannot reliably
  distinguish success from failure.
- `CtsIO::runCTS()` unconditionally returns success after calling CTS API
  methods, so Tcl can report success even when the underlying run failed.
- Tcl/Python/report paths sometimes call `CTS_API_INST` directly and sometimes
  route through tool-manager APIs.
- `use_netlist` and `net_list` appear in Tcl config and sample JSON but are not
  parsed as runtime config values.
- Numeric config parsing can silently fall back to defaults for invalid present
  values.
- Some CMake targets still expose broad include roots such as `${ICTS_SOURCE}`
  and stale include declarations from the pre-refactor directory layout.
- `Flow`, `Synthesis`, and `Topology` are facades with increasing orchestration
  and reporting responsibilities. They are still readable, but they are growth
  hotspots.

## Proposed Architecture

### 1. Status and Entry Boundary

Introduce a small status/result contract at the CTS public boundary.

Recommended shape:

```cpp
enum class CTSStatusCode {
  kOk,
  kNotInitialized,
  kConfigError,
  kDatabaseError,
  kFlowError,
  kReportError
};

struct CTSStatus {
  CTSStatusCode code = CTSStatusCode::kOk;
  std::string message;
  std::vector<std::string> diagnostics;

  bool ok() const;
};
```

Keep API-facing status types lightweight and independent from source-layer
implementation details. Internally, `Flow` may use a richer `FlowRunResult` or
`FlowRunSummary`, then `CTSAPI` maps it to public status.

Target entry behavior:

- `CTSAPI::init()` returns `CTSStatus`.
- `CTSAPI::runCTS()` returns `CTSStatus`.
- `CTSAPI::report()` returns `CTSStatus`.
- `CtsIO` returns or stores the last CTS status instead of returning hard-coded
  success.
- Tcl and Python entry points check status and surface the message through the
  existing logging/error path.
- Report commands use the same status-checked CTS boundary as run commands, or
  a documented direct API path that still checks the status.

This keeps `CTS_API_INST` as the single CTS boundary while making failures
observable.

### 2. Runtime and Flow Boundary

Keep `CTSRuntime` at the flow/API boundary:

```text
CTSRuntime
  Config
  Design
  Wrapper
  FastSTA
  SchemaWriter
```

Do not pass `CTSRuntime&` into lower-level algorithms as a service locator.
When lower layers need data, pass narrow inputs such as:

- routing layer facts
- buffer library facts
- sink/source clock facts
- topology build policies
- report/output options

Recommended internal flow result:

```cpp
struct FlowRunSummary {
  bool setup_ready = false;
  bool synthesis_done = false;
  bool optimization_done = false;
  bool instantiation_done = false;
  bool evaluation_done = false;
  std::string failed_step;
  std::string message;
};
```

The exact fields can be adjusted during implementation, but the design intent
is to avoid `void` orchestration once control crosses a public or flow-level
boundary.

### 3. Config Contract

Align all configuration surfaces:

- Tcl options in `tcl_ctsconfig`.
- Sample JSON config files under `scripts/design`.
- `Config::parse`.
- Setup/clock data read behavior.

Decision: `use_netlist` and `net_list` are deprecated/unused CTS config items.
Do not implement a net-list based CTS input mode as part of this task.

Implementation direction:

- Remove `use_netlist` and `net_list` from active sample JSON configs.
- Remove them from active Tcl config surfaces where feasible.
- If a user config still contains either key, warn that the item is no longer
  used and ignore the value.
- If a user config contains any other unsupported key, warn that it is an
  invalid config key and ignore the value.
- Keep warning text consistent from one parsing path to another so users do not
  see different semantics for JSON, Tcl-generated JSON, or future config input
  paths.

For numeric values, preserve defaults only for missing optional fields. Invalid
present values should produce a config diagnostic and a failed init status.

Recommended parser shape:

```text
for each key in config json:
  if key is supported:
    parse and validate
  else if key is deprecated:
    warn: this config item is no longer used
  else:
    warn: invalid config key
```

Warnings for unknown/deprecated keys should not fail CTS by themselves. Invalid
typed values for supported keys should fail configuration because the user
explicitly supplied a broken value.

### 4. CMake and Include Boundary

Clean build contracts in small steps:

- Remove or update `cmake/operation/icts.cmake` so it no longer advertises stale
  legacy include folders.
- Audit `PUBLIC` and `INTERFACE` include roots on iCTS targets.
- Keep broad roots only where public headers actually require them.
- Prefer target dependencies over include path tunneling.
- Keep test convenience separate from production dependency hygiene. Tests may
  use a broad fixture target if needed, but production targets should model real
  dependencies.
- Do not reintroduce static archive duplication, link groups, or link-order
  workarounds.

Validation should include the prior duplicate-archive sensitivity check if the
implementation changes iCTS target wiring materially.

### 5. Facade Decomposition

Do not split files only because they are large. Split only when the extracted
piece has a stable name and reduces coupling.

Candidate extractions:

- Flow run status/result structs and status mapping.
- Synthesis per-clock execution summary and status table formatting.
- Topology sink-domain build/commit/rollback helper, if it can be named around
  the real domain behavior rather than generic "stage" naming.

Keep the public facade files readable:

```text
flow/Flow.hh
flow/synthesis/Synthesis.hh
flow/synthesis/topology/Topology.hh
flow/synthesis/htree/HTree.hh
```

Implementation helpers should sit under the existing behavior folder and avoid
generic architecture names that make code harder to navigate.

## Migration Strategy

Use four implementation checkpoints:

1. Status/entry contract first, because it reduces silent failure risk.
2. Config contract cleanup second, because status propagation gives config
   errors somewhere useful to go.
3. CMake/include cleanup third, because dependency changes are easier to verify
   after entry behavior is stable.
4. Facade decomposition last, because it should be guided by actual coupling
   observed during the first three steps.

Each checkpoint should build and test independently.

## Risks

- Tightening include roots can reveal hidden transitive dependencies. Keep this
  incremental and prefer one target group at a time.
- Changing API return types can affect Tcl/Python/tool-manager wrappers. Update
  all call sites in the same checkpoint.
- Config cleanup can surprise legacy flows that still contain `use_netlist` /
  `net_list`. The mitigation is a warning-and-ignore deprecation path rather
  than silent removal.
- Facade extraction can become churn. Only extract named, cohesive units with
  tests or compile-time coverage.

## Validation Plan

- Build: `cmake --build build --target ecc_bin -j 32`.
- Focused tests: run iCTS `ctest` targets affected by API/config/build changes.
- Existing smoke: run representative iCTS tests such as database, flow, and
  synthesis tests.
- Runtime: run `ics55_dev` CTS flow if the local PDK/design environment is
  available.
- Static checks: `python3 ./.trellis/ecc_dev_tools/check.py check --path
  src/operation/iCTS`.
- Whitespace: `git diff --check`.
