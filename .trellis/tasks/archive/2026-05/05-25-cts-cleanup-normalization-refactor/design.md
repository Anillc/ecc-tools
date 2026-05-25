# CTS Cleanup And Normalization Refactor Design

## Objective

Normalize iCTS module boundaries so public headers describe real business contracts, while implementation helpers and test seams stay inside their
owning module or test tree. This task is a cleanup/refinement pass after the explicit runtime-boundary refactor, not a QoR or algorithm rewrite.

## Boundary Principles

1. Root facade rule:
   - A behavior directory exposes one main `Name.hh` / `Name.cc` contract whenever practical.
   - Callers outside that behavior directory include the facade, not internal helper headers.
   - Stable data-model directories are exempt when each root header is a domain object, such as `Clock`, `Net`, `Point`, or characterization data.

2. Public surface rule:
   - Keep only APIs with production callers or intentionally supported external semantics.
   - Remove unused APIs.
   - Move test-only APIs to tests or test helpers.
   - Move mutable internal accessors private.

3. Input/config/output rule:
   - Use `{Name}Input` and `{Name}Config` only at stable module/stage boundaries.
   - Omit empty `Input`, `Config`, `Output`, or `Summary` shapes.
   - Do not wrap a single `Summary` inside an otherwise empty `Output`.
   - `Output` contains design payload or data consumed by a caller; `Summary` contains status, diagnostics, metrics, and report data.

4. Runtime ownership rule:
   - `CTSRuntime` is an API/flow boundary owner, not an algorithm dependency.
   - Algorithms receive exact dependencies through explicit input/config parameters or bound private implementation state.
   - Do not introduce a replacement singleton, global context, or service locator.

5. CMake visibility rule:
   - Default to `PRIVATE`.
   - Use `PUBLIC` only when a target's public header names that dependency.
   - Tests may link internal targets deliberately, but production facades must not grow test-only methods.

## Desired Shape

### API Layer

`CTSAPI` remains the only singleton entry. It owns runtime and flow objects through private implementation state. `CTSAPI` external methods preserve
current behavior.

### Flow Layer

`source/flow/` should expose `Flow.hh/.cc` as the root lifecycle contract. `CTSRuntime` should live in `Flow.hh` or otherwise be reachable through
the flow facade, not as a peer root header.

Flow public methods should be reviewed against the external contract:

- keep API-facing lifecycle methods;
- make partial stage methods private unless they are intentionally supported;
- if tests need partial stage access, provide a test-local driver instead of keeping production public surface only for tests.

### Synthesis / Topology / HTree

`Synthesis.hh` remains the synthesis stage facade.

`Topology.hh` remains the topology stage facade. Internal sink/trunk/buffer/trace helpers stay below topology or trace subdirectories and should not
be included outside their owning stage unless there is a narrow reason.

`HTree.hh` becomes the single public HTree contract. `HTreeContracts.hh` is removed. Prefer names under the `HTree` facade, such as:

- `HTree::Input`
- `HTree::Config`
- `HTree::Output`
- `HTree::Summary`
- `HTree::Build`
- `HTree::DiagnosticBuild`

The ordinary `HTree::Build` path should carry design payload and minimal status. Heavy diagnostics should stay on `HTree::DiagnosticBuild` or be
emitted through a reporter/observer path so production output is not a transport for report-only data.

### FastSTA

`FastSta.hh` should be a CTS-facing timing/power service facade. It should expose:

- environment binding;
- clock/characterization context lifecycle;
- batch buffer-master changes;
- timing/power refresh;
- route-tree injection with needed counts;
- aggregate timing/power/query results consumed by optimization/evaluation.

It should not expose:

- raw context registration;
- raw context lookup/mutation;
- unused scalar queries;
- redundant overloads;
- structs with no callers.

### Optimization

`Optimization.hh` remains the external stage facade. Policy, state, preparation, candidate, solver, and report helpers are internal to
`source/flow/optimization`. They may remain as separate headers when shared by multiple optimization translation units, but CMake and include
visibility should make that internal status clear.

### Test Strategy

Tests should verify behavior through the nearest meaningful production boundary. When a test must cover a private algorithm helper:

- place helper factories and adapters under the mirrored `test/` directory;
- link the internal target deliberately;
- avoid adding production facade methods only for test setup or inspection.

## Compatibility

Required compatibility:

- `CTSAPI` external behavior is preserved.
- Existing report files and runtime summaries remain behaviorally equivalent unless a removed test-only diagnostic path is explicitly documented.
- Real-tech tests keep their scenario coverage, but their helpers may move from production headers to test headers.

Permitted internal changes:

- source include paths may change;
- internal helper headers may move or disappear;
- tests may include new test-only helper headers;
- CMake visibility may become stricter.

## Rollback Strategy

Implement in small phases. After each phase:

- rebuild the nearest affected target;
- run the focused iCTS tests for that target;
- do not proceed with broad header deletion until all direct include users are migrated.

If a phase creates excessive churn or breaks a hidden dependency chain, roll back only that phase and keep the previous phase's narrowed public API
where it is already stable.

