# CTS runtime-bound service boundary cleanup design

## Context

The previous desingleton work made runtime ownership explicit. The follow-up structural task improved HTree and synthesis readability. The remaining
problem is subtler: some APIs now pass explicit dependencies, but still expose runtime plumbing rather than CTS-domain intent.

The clearest example is `FastSTA::buildClockContext(config, sta_adapter, wrapper, clock, route_geometry)`. `Config`, `STAAdapter`, and `Wrapper` are
stable runtime facts for one `FastSTA` service, while `Clock` and route geometry are per-clock data. Passing all of them on every call is explicit,
but it is not semantically clean.

## Design Principles

### Runtime Owner Is Not A Service Locator

`CTSRuntime` owns long-lived state at API/Flow level only. It must not be passed deep into synthesis, optimization, FastSTA, HTree, or topology
algorithms.

### Runtime-Bound Services Can Bind Environment

Some objects are stateful services rather than pure algorithms:

- `STAAdapter`;
- `FastSTA`;
- `Wrapper`;
- `SchemaWriter`;
- `CharacterizationLibrary`.

For those objects, repeatedly passing stable dependencies to every method is not a readability win. The service should bind stable environment once,
then per-call methods should receive only operation-specific domain input.

### Environment Is Not Input

Use these categories:

- `Environment`: stable runtime facts/dependencies bound to a service lifetime.
- `Input`: per-operation domain data.
- `Config` / `Policy`: behavior-changing parameters, narrowed to the current stage or service.
- `Output`: design payload consumed downstream.
- `Summary`: caller-relevant status/metrics.
- `Diagnostics`: local report/test observation data.

### Public APIs Must Express Real Supported Semantics

If two overloads exist, both must represent supported, tested production semantics. Otherwise one should be removed, made private, or renamed as a
test/diagnostic helper.

## Target Architecture

### FastSTA

Introduce a typed runtime environment:

```cpp
struct FastStaEnvironment
{
  STAAdapter* sta_adapter = nullptr;
  int32_t dbu_per_um = 0;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  double root_input_slew_ns = 0.0;
  std::optional<double> max_cap_pf = std::nullopt;
};
```

Introduce a per-clock build input:

```cpp
struct FastStaClockBuildInput
{
  const Clock* clock = nullptr;
  const FastStaClockRouteGeometry* route_geometry = nullptr;
};
```

Target facade:

```cpp
class FastSTA
{
 public:
  auto bindEnvironment(const FastStaEnvironment& environment) -> void;
  auto buildClockContext(const FastStaClockBuildInput& input) -> FastStaClockId;
};
```

Expected effects:

- no public `buildClockContext(config, sta_adapter, wrapper, clock)` overload;
- no public `buildClockContext(config, sta_adapter, wrapper, clock, route_geometry)` overload;
- `Wrapper` is replaced by `dbu_per_um`;
- broad `Config` is replaced by `FastStaEnvironment` fields or a nested policy;
- `STAAdapter` lifetime is explicitly bound before context construction.

### FastStaBuilder

`FastStaBuilder` should not mirror broad public overloads. It can be either:

- a private implementation helper taking `FastStaEnvironment` and `FastStaClockBuildInput`; or
- a short-lived builder object bound to `FastStaEnvironment`.

The builder may keep separate internal graph-construction branches if needed, but those branches should not leak as ambiguous public API.

### Optimization

Optimization should derive FastSTA environment near the stage boundary:

```text
OptimizationInput
  -> FastStaEnvironment
  -> per-clock FastStaClockBuildInput
  -> FastSTA context id
  -> route tree injection
  -> solver
  -> accepted edit application
```

The per-clock context id should have a clear lifetime. Preferred direction is a scoped local owner/guard or a `ClockOptimizationRun` that erases the
context in one place.

### Topology Sink / Source Helpers

Private helpers in `SinkBranch.cc` and `SourceTrunk.cc` should not carry long runtime parameter lists. Replace helper signatures such as:

```cpp
BuildSinkHtreeInput(config, design, wrapper, sta_adapter, fast_sta, reporter, root_net, ...)
```

with one of:

- `BuildSinkHtreeInput(input, local_policy, root_net, ...)`;
- a small local `TopologyRuntimeBinding` built from `Topology::Input`;
- direct assembly in a short-lived builder if that reads better.

The per-call domain facts should remain visible: root net, clock source, root input, sink/source role, object prefix, and log context.

### STAAdapter Config Uses

Some `STAAdapter` APIs currently accept `Config` because they are configured adapter operations. This task should not blindly remove every such
call. It should:

- remove broad `Config` from the touched FastSTA context construction path;
- prefer typed policies for RC, cap, and slew limits where the calling code already derives policy;
- leave explicitly named configured adapter operations only when their meaning remains clear;
- record any remaining broad Config path as a follow-up if it is too large for this task.

## Migration Plan

1. Add `FastStaEnvironment` and `FastStaClockBuildInput`.
2. Add environment binding to `FastSTA` and convert production optimization to use it.
3. Collapse public `buildClockContext` overloads.
4. Refactor `FastStaBuilder` to use environment/input.
5. Clarify route geometry and route tree injection responsibilities.
6. Refactor topology helper long signatures.
7. Audit remaining same-class patterns and either clean or record explicit follow-up classification.

## Compatibility

- External CTS API should not change.
- Reports and metrics should remain behavior-compatible unless a change is explicitly justified.
- Real `ics55_dev` flow is the final behavior gate.

## Risks

- FastSTA stores `STAAdapter*`; environment binding must not introduce stale pointers across runtime reset.
- Removing the no-route overload may break hidden tests if they call the public API directly.
- Centralizing optimization context lifetime must preserve all current skip/failure behavior.
- Over-generalizing `Environment` could recreate a service locator. Keep it service-specific and typed.
