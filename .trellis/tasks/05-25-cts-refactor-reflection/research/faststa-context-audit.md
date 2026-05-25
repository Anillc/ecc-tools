# FastSTA clock-context boundary audit

Date: 2026-05-25

## Scope

Audited the current post-refactor FastSTA clock-context construction path:

- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/FastSta.cc`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_state/FastStaBuilder.cc`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_tree/FastStaClockTree.hh`
- `src/operation/iCTS/source/database/adapter/fast_sta/clock_tree/FastStaClockTree.cc`
- `src/operation/iCTS/source/flow/optimization/Optimization.cc`
- `src/operation/iCTS/source/flow/optimization/preparation/OptimizationPreparation.cc`

## Confirmed facts

- `FastSTA` exposes two public clock-context build overloads:
  - `buildClockContext(config, sta_adapter, wrapper, clock)`;
  - `buildClockContext(config, sta_adapter, wrapper, clock, route_geometry)`.
- `FastStaBuilder` mirrors the same two overloads internally.
- Production code has only one caller:
  - `Optimization.cc` calls the route-geometry overload after `BuildClockRouteGeometry(clock_layout, clock_index)`.
- The no-route overload has no production caller in the current tree.
- Tests currently register synthetic `FastStaClockContext` objects directly with `registerClockContext`; they do not justify the no-route public
  overload.
- `Optimization` builds a route-geometry FastSTA context, then immediately injects route trees from `Design`/ClockDAG and reruns timing/power.

## Dependency observations

The current public `FastSTA::buildClockContext` signature exposes three broad runtime dependencies:

- `Config`: used to resolve routing layer, wire width, root input slew, max cap policy, and STA-backed slew/cap limit policy.
- `STAAdapter`: stored into `FastStaClockContext` and used later by RC extraction, Liberty extraction, timing limits, and power/timing helpers.
- `Wrapper`: only used during context construction to query DBU-per-micron.

These dependencies are frequent and stable within one CTS runtime. Passing them repeatedly at each context build makes the public API explicit but
noisy, and allows accidental mixed-runtime calls such as a `FastSTA` store built with one `STAAdapter` and later populated with another.

## Interface issues

### Two public build paths overstate the supported semantics

The code exposes both committed-clock and route-geometry clock-context builds, but only the route-geometry path is actually used by optimization.
The no-route path builds graph topology from the committed `Clock`, collects caps/timing data, and then timing reduction falls back to load-only
parasitics where net RC is absent. That is a different timing model and should not be presented as an equally normal production entry unless a
specific flow needs it.

### Repeated broad dependencies are not meaningful algorithm input

From the caller's perspective, optimization is not choosing a different `Config`, `Wrapper`, or `STAAdapter` per clock. Those are runtime environment
facts. The actual per-clock input is:

- which `Clock` is being analyzed;
- what route geometry / route tree source should be used;
- whether timing and power should be initialized eagerly.

### `Wrapper` should not be a deep FastSTA dependency

FastSTA needs DBU-per-micron, not a full `Wrapper`. The flow boundary can derive `dbu_per_um` once and pass/bind it as a stable environment fact.

### `Config` should be narrowed before it reaches FastSTA internals

FastSTA context construction needs a typed policy, not the full global `Config`. A minimal target policy would include:

- `routing_layer`;
- `wire_width_um`;
- `root_input_slew_ns`;
- `max_cap_pf` / cap-limit policy;
- slew-limit/source-limit query policy currently mediated by `STAAdapter`.

## Recommended direction

Split FastSTA concepts into:

- `FastStaEnvironment`: runtime-bound stable dependencies and facts, such as `STAAdapter`, DBU-per-micron, routing layer, wire width, root input
  slew, and limit policy;
- `FastStaClockBuildInput`: per-clock data, such as `Clock`, optional route geometry, optional route-tree source, and eager timing/power behavior;
- `FastSTA`: the context store and incremental timing/power facade after the environment is bound.

Target public shape:

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

struct FastStaClockBuildInput
{
  const Clock* clock = nullptr;
  const FastStaClockRouteGeometry* route_geometry = nullptr;
};

class FastSTA
{
 public:
  auto bindEnvironment(const FastStaEnvironment& environment) -> void;
  auto buildClockContext(const FastStaClockBuildInput& input) -> FastStaClockId;
};
```

The first implementation step can still store a pointer to `Config` privately if needed for compatibility, but the target architecture should
derive a typed FastSTA policy at the flow boundary and remove direct `Config` / `Wrapper` parameters from the clock-context build API.
