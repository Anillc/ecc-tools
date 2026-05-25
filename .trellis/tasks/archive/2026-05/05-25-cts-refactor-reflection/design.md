# CTS refactor reflection design

## Context

This task reflects on the uncommitted iCTS desingleton and contract-polish work before it is treated as architecturally complete.

The current refactor has real value:

- `CTSAPI` is now the only intended singleton boundary.
- `Config`, `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, `SchemaWriter`, and `Flow` are runtime-owned objects rather than hidden global singletons.
- Most source/test `_INST` usage has been removed.
- Many broad `Options` / `Result` contracts have been converted into `Input`, `Config`, `Output`, `Summary`, or direct domain payloads.
- Empty contracts and summary-only output wrappers were removed.
- The backend specs now document explicit runtime ownership, minimal config, and output/summary separation.

The remaining issue is not whether the direction is right. The direction is right. The issue is that the implementation is still partly
mechanical: some code replaced hidden globals with long explicit parameter lists, and some result objects were renamed without fully revisiting why
their data crosses a module boundary.

## Current Strengths

### Singleton Removal

The strongest part of the refactor is the ownership change:

```text
CTSAPI -> CTSRuntime -> Flow -> stages/modules/adapters
```

This is a meaningful improvement over global singleton access because object lifetime and reset order are now visible. It also creates a real path
toward independent test runtimes and future parallel flow instances.

### Dependency Visibility

The source layer now generally exposes when a stage needs `Design`, `Wrapper`, `STAAdapter`, `FastSTA`, `SchemaWriter`, or a shared
characterization library. That makes review easier than reading through global macros.

### Contract Vocabulary

The introduced vocabulary is useful:

- `Input`: execution context, domain data, runtime facts, references, paths, report sinks, libraries, and caches.
- `Config`: behavior-changing policy and algorithm knobs.
- `Output`: payload consumed by downstream design flow.
- `Summary`: status, counters, diagnostics, metrics, and report rows.

This vocabulary is the right foundation, but it needs stricter boundary discipline.

## Current Weaknesses

### Long Parameter Lists Still Expose Runtime Plumbing

Example:

```cpp
Synthesis::run(const Config& config,
               Design& design,
               Wrapper& wrapper,
               STAAdapter& sta_adapter,
               FastSTA& fast_sta,
               schema::SchemaWriter& reporter,
               ClockLayout& clock_layout,
               CharacterizationLibrary& char_library)
```

This signature is explicit, but it is not readable as a stable CTS boundary. The reader must mentally reconstruct which parameters are runtime
context, which are mutable stage outputs, and which are algorithm decisions. It also differs from already-cleaned stages such as `Optimization`,
`Instantiation`, `Evaluation`, and `Report`, which use input structs.

This is the main lesson: removing singletons is necessary but insufficient. Explicit dependencies still need domain grouping.

### `schema::SchemaWriter` Leaks Infrastructure Vocabulary

`SchemaWriter` is a runtime-owned report sink from the perspective of CTS business logic. The repeated `schema::SchemaWriter` spelling in flow and
module signatures adds visual noise and exposes logger implementation namespace details at the wrong level.

The schema namespace should remain for report DSL types and helpers such as `schema::StageReportOptions`, `schema::KeyValueFields`, and
`schema::EmitKeyValueTable`. The runtime dependency itself should read as CTS-level `SchemaWriter`.

### `HTreeSummary` Is Too Wide

`HTreeSummary` currently holds:

- control-flow status: success, failure reason;
- selected design facts: selected depth, selected root driver cell;
- characterization metrics;
- depth-search metrics;
- sink-load cap distribution;
- root-driver compensation details;
- boundary-relaxation diagnostics;
- analytical solver diagnostics;
- log context and object name prefix.

Then `Topology::Summary` and `SourceTrunkSummary` embed or forward this full summary. This means HTree internal report/test data is transported
through topology and synthesis even when the caller only needs a few control-flow fields.

The problem is not the word `Summary`; it is the lifetime and audience. A good summary should describe what the caller needs after the stage
returns. HTree's detailed diagnostics mostly need to be reported inside HTree while the selected candidates and local build context still exist.

### Tests Are Pulling Internal Diagnostics Through Production Contracts

Several tests inspect internal HTree summary fields such as characterization grid details, frontier counts, analytical counts, and load cap
distribution. Those are useful regression observations, but putting them in production `Summary` makes test needs shape the public module boundary.

Better test strategies:

- assert final design payload and committed topology where possible;
- assert report/artifact contents for report-only data;
- use a test-only observation hook or helper for internal algorithm diagnostics if the diagnostic cannot be derived from output.

### FastSTA Still Exposes Runtime Plumbing In Its Clock-Context API

The post-structural-refactor audit found another example of explicit but noisy dependency passing:

```cpp
auto FastSTA::buildClockContext(const Config& config,
                                STAAdapter& sta_adapter,
                                const Wrapper& wrapper,
                                const Clock& clock) -> FastStaClockId;

auto FastSTA::buildClockContext(const Config& config,
                                STAAdapter& sta_adapter,
                                const Wrapper& wrapper,
                                const Clock& clock,
                                const FastStaClockRouteGeometry& route_geometry) -> FastStaClockId;
```

`FastStaBuilder` mirrors the same two overloads. Current production code only calls the route-geometry overload from `Optimization.cc`. The no-route
overload remains public but has no production caller in the current tree; tests construct synthetic contexts directly with `registerClockContext`.

This is a symptom of the same deeper issue as the earlier stage-facade cleanup:

- `Config`, `STAAdapter`, and `Wrapper` are runtime environment facts, not per-clock algorithm choices.
- `Wrapper` is only used to query DBU-per-micron; FastSTA should not depend on a full wrapper when it only needs DBU.
- `Config` is used as a broad policy source for routing layer, wire width, root input slew, max cap, and timing-limit behavior; FastSTA should
  receive a typed FastSTA policy derived near the flow boundary.
- Exposing two public build paths makes the timing model ambiguous. The currently used production semantics are route-geometry build plus route-tree
  injection and timing/power refresh.

The cleaner architecture is to bind stable FastSTA environment once and leave per-clock build calls to express only per-clock data.

## First Principles

CTS is a domain pipeline:

```text
constraints + tech + existing design
  -> clock intent and timing model
  -> synthesized clock topology
  -> committed CTS design objects
  -> iDB/timing projection
  -> evaluation and reports
```

Every boundary should answer five questions:

1. Who owns this data?
2. Who may mutate it?
3. Who consumes it after this function returns?
4. Is it a behavior decision, an environment fact, a payload, or a diagnostic?
5. Is this type named in CTS business language?

If a field cannot answer these questions clearly, it should not cross the boundary.

## Recommended Architecture

### Layer Model

Use four conceptual layers inside iCTS:

```text
API boundary
  CTSAPI

Runtime and flow orchestration
  CTSRuntime
  Flow
  Setup / ClockDataRead / Synthesis / Optimization / Instantiation / Evaluation / Report

Domain services and adapters
  Design / ClockLayout / Wrapper / STAAdapter / FastSTA / SchemaWriter / CharacterizationLibrary

Algorithms and modules
  HTree / TopologyGen / clustering / routing / characterization / optimization solvers
```

Dependency direction:

- API owns runtime.
- Flow orchestrates stages.
- Stages receive runtime-owned dependencies through named stage input contracts.
- Adapters isolate iDB/iSTA/FastSTA details.
- Algorithms receive CTS domain objects and narrow service references, not `CTSRuntime`.

### Runtime Owner

`CTSRuntime` should remain an owner, not a dependency object passed deep into algorithms:

```cpp
struct CTSRuntime
{
  Config config;
  Design design;
  Wrapper wrapper;
  STAAdapter sta_adapter;
  FastSTA fast_sta;
  SchemaWriter reporter;
};
```

`CTSRuntime&` should appear only at API/Flow setup boundaries. It should not be used as a service locator.

### Runtime-Bound Domain Services

Some runtime-owned objects are not pure algorithms. They are stateful domain services with a stable environment:

- `Wrapper` bridges iDB and design units.
- `STAAdapter` bridges iSTA/liberty/timing query semantics.
- `FastSTA` owns fast timing contexts and incremental timing/power mutation.
- `SchemaWriter` owns report output state.

For these services, the right rule is not "always pass every dependency into every method." The better rule is:

- bind stable environment once at the owning boundary;
- pass per-operation CTS domain data explicitly;
- do not hide data that varies per operation;
- do not pass `CTSRuntime&` as a substitute for a typed environment.

For FastSTA, `Config`, `STAAdapter`, and DBU are stable for one runtime. `Clock`, route geometry, route-tree source, and update behavior are
per-clock inputs.

Target direction:

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

This makes the API say what changes per clock, while preserving explicit runtime ownership.

### Stage Contracts

Public flow-stage facades should use named input contracts when they need multiple runtime/domain dependencies.

Example target shape:

```cpp
struct SynthesisInput
{
  const Config* config = nullptr;
  Design* design = nullptr;
  Wrapper* wrapper = nullptr;
  STAAdapter* sta_adapter = nullptr;
  FastSTA* fast_sta = nullptr;
  SchemaWriter* reporter = nullptr;
  ClockLayout* clock_layout = nullptr;
  CharacterizationLibrary* characterization_library = nullptr;
};

class Synthesis
{
 public:
  static auto run(const SynthesisInput& input) -> SynthesisTraceSummary;
};
```

Do not add `SynthesisConfig` unless there is a real synthesis-level policy object distinct from global runtime `Config`.

The same principle applies to:

- `SetupInput`
- `RuntimeSetupInput` or `SetupReportInput`
- `ClockDataReadInput`
- `ClockTopologyInput` or `TopologyFormationInput`
- `SourceTrunkSegment::Input`, including source/sink/root net data now passed separately

### Input vs Config

`Input` contains data and services for this run:

- `Design`, `Clock`, `Net`, `Pin`, `ClockLayout`;
- `Wrapper`, `STAAdapter`, `FastSTA`, `SchemaWriter`;
- DBU, clock period, paths, object prefixes, log context;
- characterization library/cache;
- semantic role, such as sink-domain vs source-to-root.

`Config` contains behavior policy:

- enable/disable decisions;
- fanout, cap, slew, skew, target depth;
- topology tolerance;
- search window;
- boundary relaxation;
- analytical solver enablement;
- root-driver sizing policy.

Do not put reporter, DBU, work dirs, object prefixes, log context, adapter pointers, or caches into `Config`.

### Bound Environment vs Call Input

Use a third category for stateful runtime services: `Environment` or `RuntimePolicy`.

`Environment` contains stable dependencies/facts that are bound to a service lifetime:

- adapter references such as `STAAdapter`;
- unit facts such as DBU-per-micron;
- derived runtime policy such as routing layer, wire width, root slew, and max-cap override;
- caches owned by the service.

`Input` contains the per-call domain object:

- the `Clock` being analyzed;
- route geometry or route trees for that clock;
- selected candidate edits;
- requested report artifacts.

This is the missing distinction behind the `FastSTA::buildClockContext` issue. Passing `Config`, `STAAdapter`, and `Wrapper` on every build is
explicit, but it wrongly suggests these dependencies vary per clock.

### Output vs Summary vs Diagnostics

`Output` is downstream payload:

- temporary inserted inst/pin/net ownership;
- topology/tree/route payload;
- reusable evaluation state;
- generated artifact paths if downstream code consumes them.

`Summary` is caller-relevant status and aggregation:

- success/failure reason;
- selected depth if the caller aggregates it;
- inserted object counts if the caller reports flow-level totals;
- evaluation/report success per artifact.

Detailed diagnostics should remain stage-local:

```cpp
struct HTreeDiagnostics
{
  CharacterizationSummary characterization;
  DepthSearchSummary depth_search;
  RootDriverCompensationReport root_driver_compensation;
  AnalyticalAttemptSummary analytical;
  SinkLoadRegionLegalitySummary selected_sink_load_region;
};
```

Diagnostics should be emitted by the owning stage while it has full context. They should not be embedded into upstream summaries unless the upstream
caller actually needs them for control flow or aggregation.

### HTree Target Shape

The current `HTree::build(input, config) -> HTreeBuild` can remain, but the build internals should change.

Recommended target:

```cpp
struct HTreeOutput
{
  Tree topology;
  std::vector<HTreeLevelPlan> levels;
  std::optional<HTreeTopologyChar> best_char;
  std::optional<HTreeTopologyPattern> best_pattern;
  std::vector<std::unique_ptr<Inst>> inserted_insts;
  std::vector<std::unique_ptr<Pin>> inserted_pins;
  std::vector<std::unique_ptr<Net>> inserted_nets;
  std::vector<HTreeInsertedInstLevel> inserted_inst_levels;
  std::vector<HTreeInsertedNetLevel> inserted_net_levels;
  Inst* root_inst = nullptr;
  Pin* root_input_pin = nullptr;
  Pin* root_output_pin = nullptr;
  Net* root_net = nullptr;
};

struct HTreeSummary
{
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> selected_depth = std::nullopt;
  bool used_boundary_relaxation = false;
};
```

Detailed HTree metrics should move to an internal `HTreeDiagnostics` / `HTreeReportData` object consumed by HTree report emission.

`Topology::Summary` should not contain full `HTree::Summary`. It should contain only topology-level status and flow aggregation fields:

```cpp
struct TopologySummary
{
  bool success = false;
  std::string failure_reason;
  bool sink_clustering_enabled = false;
  std::optional<ClusterLeafDistanceSummary> cluster_leaf_distance_summary = std::nullopt;
  std::size_t selected_htree_level_count = 0U;
  std::optional<unsigned> selected_htree_depth = std::nullopt;
  std::size_t htree_inserted_buffer_count = 0U;
  std::size_t htree_inserted_net_count = 0U;
};
```

`SourceTrunkSummary` should similarly avoid full HTree summary nesting.

### HTree Lifetime

A short-lived HTree build object is a good design direction:

```cpp
class HTreeBuilder
{
 public:
  HTreeBuilder(const HTreeInput& input, const HTreeConfig& config);
  auto build() -> HTreeBuild;

 private:
  HTreeDiagnostics _diagnostics;
};
```

This improves locality because diagnostics, selected candidates, stage scopes, and report data live with the build process. It avoids bloating the
returned `Summary`.

However, HTree should not become a long-lived stateful service owned by topology or flow. That would create borrowed-pointer lifetime hazards and
could become another hidden state container. Prefer a per-build object with bounded lifetime.

### Reporter Boundary

Add a CTS-level alias or forward header:

```cpp
namespace icts {
namespace schema {
class SchemaWriter;
}
using SchemaWriter = schema::SchemaWriter;
}
```

Business contracts use `SchemaWriter&` or `SchemaWriter*`. Schema/report DSL types remain under `schema::`.

This keeps signatures readable without flattening the entire logger namespace.

## Short-Term Work For Current Contract-Polish Task

These should remain in the current `cts-contract-polish-convergence` task because they are direct gaps in its acceptance intent:

- Convert long public stage facade parameters into named input contracts:
  - `SynthesisInput`
  - `SetupInput`
  - `ClockDataReadInput`
  - `TopologyFormationInput`
  - `SourceTrunkSegment::Input` absorbing source/sink/root data
- Introduce top-level `SchemaWriter` alias and remove `schema::SchemaWriter` spelling from business signatures.
- Slim `HTreeSummary`, `Topology::Summary`, and `SourceTrunkSummary` so they do not transport report-only diagnostics.
- Move HTree detailed report data into internal diagnostics consumed by HTree report emission.
- Update tests that currently depend on production summary internals.

## Longer-Term Follow-Up Work

These are larger architectural changes and should be separate tasks after the current task is stable:

- Replace static HTree facade internals with a per-build `HTreeBuilder` object.
- Split synthesis into a clearer per-clock pipeline:
  - `ClockSynthesisRun`
  - `SinkDomainSynthesis`
  - `SourceTrunkSynthesis`
  - `ClockTopologyCommitPlan`
- Make build and commit boundaries explicit: algorithms build temporary payloads; synthesis/instantiation commit to `Design`.
- Introduce report/artifact based regression checks for internal HTree diagnostics rather than exposing them through production summaries.
- Consider typed stage policies derived once from global `Config`, so lower stages no longer repeatedly query global runtime config.
- Bind FastSTA's stable environment once per runtime and replace the duplicated `buildClockContext` overloads with one per-clock build input.
- Remove the no-route FastSTA clock-context public overload unless a concrete flow with defined timing semantics needs it.

## Risks

- Slimming summaries will break tests that read internal diagnostics directly.
- Moving diagnostics into HTree-local state must not remove existing report fields needed by real flow users.
- Adding input structs can become a new dumping ground if fields are not reviewed against ownership and consumer rules.
- A long-lived HTree object would hurt maintainability if it stores borrowed design pointers beyond the build scope.
- Introducing broad context objects such as `CTSRuntime&` or `ClockContext&` deep in algorithms would recreate service-locator behavior.

## Recommendation

Do not revert the desingleton work. It solved the most important hidden-state problem.

Do revise the current contract cleanup before committing it. The best next step is to treat readability as a boundary-design problem, not a naming
problem:

- group stage runtime dependencies into `{Name}Input`;
- keep `{Name}Config` for policy only;
- return only caller-needed payload/status;
- report detailed diagnostics at the owning stage;
- keep tests from forcing production summaries to expose internal algorithm state.

The best long-term CTS architecture is an explicit runtime-owned flow with short-lived domain build objects and narrow contracts at every mutation
boundary. That preserves the benefits of desingletonization while making the code read like CTS business logic rather than dependency plumbing.
