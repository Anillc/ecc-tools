# Sub-Module: `timing/`

- **Query**: timing 子模块结构、职责
- **Scope**: internal
- **Date**: 2026-05-19

## Directory Layout

Two files only:

```
timing/
├── CMakeLists.txt          (single target `icts_source_module_timing`)
├── TimingEngine.hh         (61 lines)
└── TimingEngine.cc         (240 lines)
```

## Role in CTS Flow

`TimingEngine` is a pure RC-tree propagation engine: takes a populated
`RCTree` and walks it bottom-up / top-down to compute downstream
capacitance, arrival, slew, and arc delay. It has **no** STA dependency
— it relies only on the Elmore-style closed-form propagation defined inside.

## Public Surface

`timing/TimingEngine.hh:30-58`:

```cpp
class TimingEngine
{
public:
  struct Metrics {
    double skew = 0.0, min_delay, max_delay, max_slew, total_cap;
  };

  TimingEngine() = delete;

  static auto update(RCTree& rc_tree) -> Metrics;
  static auto updateDownstreamCap(RCTree& rc_tree) -> void;
  static auto updateIncreaseDelay(RCTree& rc_tree) -> void;
  static auto updateArrival(RCTree& rc_tree) -> void;
  static auto updateSlew(RCTree& rc_tree) -> void;
  static auto updateDownstreamDelay(RCTree& rc_tree) -> void;

  static auto evaluate(const RCTree& rc_tree) -> Metrics;
  static auto calcSkew(const RCTree& rc_tree) -> double;
  static auto calcArcDelay(double downstream_cap, double resistance, double capacitance) -> double;
  static auto calcIdealSlew(double arc_delay) -> double;
};
```

10 static methods, 1 nested `struct Metrics`, 1 forward-declared `class RCTree`.

## Dependencies

- PUBLIC: `icts_source_database_timing` (gives `RCTree`).
- PRIVATE: `icts_source_utils`.

CMake at `timing/CMakeLists.txt:1-26` (clean, no chained dependencies).

## External Consumers

```
flow/evaluation/qor/QorEvaluationMetrics.cc:48           #include "timing/TimingEngine.hh"
module/topology/cluster_constraints/
  ClusterConstraintEvaluator.cc:42                       #include "TimingEngine.hh"
```

## Naming

- "Engine" is a generic noun ("engine" / "processor" / "manager" patterns
  often seen in service-oriented code). In CTS context "TimingEngine" reads
  as "engine that propagates timing through an RC tree", which is at least
  domain-relevant. Alternative names from STA literature would be
  `RCDelayPropagator`, `RCTreeTimingPropagator`, or `ElmoreTimingPropagator`.
- All method names use CTS/STA-domain verbs: `update*`, `evaluate`,
  `calcSkew`, `calcArcDelay`, `calcIdealSlew` — these read well.

## Cohesion / Coupling

- `timing/` is the **leanest** sub-module (single class, single file pair).
- Has no inter-sub-module dependencies inside `module/` other than the
  database layer.
- Acts as a downstream "utility" called by `routing/router/` (via
  `QorEvaluationMetrics`) and `topology/cluster_constraints/`.

## Caveats / Not Found

- `TimingEngine.hh` is so small that the directory itself feels
  over-structured (one file per sub-folder). Could either remain (clear
  separation) or be merged into the database layer next to `RCTree`.
- No tests for `TimingEngine` are referenced from inside the directory.
