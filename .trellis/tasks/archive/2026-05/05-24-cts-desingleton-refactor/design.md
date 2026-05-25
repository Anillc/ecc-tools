# Design · CTS 去单例化重构

## Direction

保留 `CTSAPI` 作为外部稳定入口。`CTSAPI` 内部持有普通 runtime objects，并把这些对象通过 flow 调用链显式传递。iCTS source 层不再通过宏或 static singleton 获得 mutable state。

目标数据流：

```text
CTSAPI
  -> CTSRuntime
       -> Flow
            -> Setup
            -> ClockDataRead
            -> Synthesis
            -> Optimization
            -> Instantiation
            -> Evaluation
            -> Report
                 -> Modules / adapters through explicit input references
```

## Execution Split

本设计由父任务统一约束，按子任务落地：

- `05-24-cts-runtime-flow-desingleton`：建立 runtime owner，先切断 `FLOW_INST`。
- `05-24-cts-reporter-config-explicit`：显式 reporter/config，建立 config 最小化规则。
- `05-24-cts-design-wrapper-explicit`：显式 design/wrapper ownership 与 writeback target。
- `05-24-cts-sta-faststa-explicit`：显式 timing dependencies。
- `05-24-cts-synthesis-contract-cleanup`：清理 HTree/Topology/Characterization contract。
- `05-24-cts-flow-contract-tests-spec`：清理剩余 flow/test/spec，并执行最终验收。

跨子任务不允许用 `CTSRuntime&` 泛化传递来规避接口设计；runtime 是 owner，不是下层模块的依赖接口。

## Ownership Model

建议引入一个由 `CTSAPI` 私有持有的 runtime owner：

```cpp
struct CTSRuntime
{
  Config config;
  Design design;
  Wrapper wrapper;
  STAAdapter sta_adapter;
  FastSTA fast_sta;
  schema::SchemaWriter reporter;
};
```

原则：

- `CTSRuntime` 是 owner，不是全局 service locator。它只在 API/flow 边界出现；下层模块接收更窄的 input/reference。
- `Flow` 是普通对象，持有 run-local state：run summary、clock layout、evaluation state、instantiation result、characterization library。
- `CTSAPI::resetAPI()` 清空自身持有的 runtime/flow 对象，不调用其它全局 reset。
- `Wrapper` 不拥有 `Design`，但所有 read/write/materialize 方法必须显式接收目标 `Design&` 或在构造时绑定一个明确 design lifetime。
- `SchemaWriter` 是普通 reporter。需要 report 的函数通过 input 或参数拿到 `schema::SchemaWriter&`。

## Contract Convention

每个 algorithm / flow 的入口只使用 `{Name}Input` + `{Name}Config`。返回内容拆为 `{Name}Output` + `{Name}Summary`。如果 C++ 需要同时返回两者，优先用 `std::pair<NameOutput, NameSummary>` 或一个非常薄的 transport wrapper；禁止让 wrapper 继续膨胀成当前 `Result` 混合体。

### `{Name}Input`

`Input` 是本次运行必需但不是算法 knob 的东西：

- 业务对象引用：`Clock&`、`Net&`、`Pin*` 列表、`ClockLayout&`、`Design&`。
- 服务引用：`Wrapper&`、`STAAdapter&`、`FastSTA&`、`schema::SchemaWriter&`、`CharacterizationLibrary&`。
- 运行环境事实：DBU、clock period、route RC、routing layer、wire width、object naming prefix、report scope。
- 语义模式：loads 是 sink 还是 local buffer、是否是 source-to-root stage。

这些字段可以影响计算，但它们不是用户调参意义上的 "config"。

### `{Name}Config`

`Config` 只包含会改变算法选择的 knobs：

- enable / disable 类决策：enable sink clustering、enable analytical HTree、enable root driver sizing。
- 约束和目标：max fanout、max cap、max slew、skew bound、target depth、depth search window、boundary relaxation、topology tolerance。
- 离散化策略：char slew/cap steps、wirelength lattice override、char redundancy。

不放：

- DBU、work dir、statistics dir、log path。
- reporter/log context。
- object name prefix。
- pointer/cache/library/reference。
- 从设计或 tech adapter 可查询的事实。
- 只为兼容 JSON 存在但不参与算法的 placeholder，例如当前 `max_length`。

### `{Name}Output`

`Output` 只放后续设计流程需要消费的 payload：

- inserted inst/pin/net objects。
- route/tree/topology objects。
- generated artifacts paths。
- evaluation state if it is the direct reusable payload.

不放 log rows、diagnostics、elapsed time、failure reason、selected candidate counts。

### `{Name}Summary`

`Summary` 放状态、metrics 和 report data：

- success / failure reason。
- selected depth、candidate counts、pruning stats、QoR metrics。
- char lattice summary、root-driver compensation summary、analytical summary。
- warnings/degraded reason。

Summary 不拥有 design objects。

## HTree Target Shape

Current:

```cpp
auto HTree::build(Net& root_net, const HTreeSynthesisOptions& options) -> HTreeSynthesisResult;
```

Target:

```cpp
struct HTreeInput
{
  Net& root_net;
  std::vector<Pin*> loads;
  int32_t dbu_per_um = 0;
  CharacterizationLibrary& char_library;
  STAAdapter& sta_adapter;
  schema::SchemaWriter& reporter;
  std::vector<double> additional_characterization_lengths_um;
  std::optional<Point<int>> fixed_topology_root_location;
  double clock_period_ns = 0.0;
  std::string clock_period_source;
  std::string object_name_prefix;
  HTreeLoadRole load_role = HTreeLoadRole::kSink;
  HTreeStageRole stage_role = HTreeStageRole::kDownstream;
};

struct HTreeConfig
{
  bool force_branch_buffer = false;
  bool enable_root_driver_sizing = true;
  bool allow_boundary_relaxation = false;
  bool enable_analytical_solver = false;
  std::optional<unsigned> target_depth = std::nullopt;
  unsigned depth_explore_window = 4;
  double topology_tolerance = 0.1;
  std::size_t max_fanout = 32;
};

struct HTreeOutput
{
  Tree topology;
  std::vector<HTreeLevelPlan> levels;
  std::optional<HTreeTopologyPattern> best_pattern;
  std::vector<std::unique_ptr<Inst>> inserted_insts;
  std::vector<std::unique_ptr<Pin>> inserted_pins;
  std::vector<std::unique_ptr<Net>> inserted_nets;
  std::vector<HTreeInsertedInstLevel> inserted_inst_levels;
  std::vector<HTreeInsertedNetLevel> inserted_net_levels;
  Pin* root_input_pin = nullptr;
  Pin* root_output_pin = nullptr;
};

struct HTreeSummary
{
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> selected_depth;
  std::size_t depth_candidate_count = 0U;
  HTreeRootDriverCompensationReport root_driver_compensation;
  HTreeCharacterizationSummary characterization;
  HTreeAnalyticalSummary analytical;
  HTreeLoadSummary loads;
};
```

This moves:

- `dbu_per_um`, `clock_period`, object name, reporter and char library into `HTreeInput`.
- true knobs into `HTreeConfig`.
- design payload into `HTreeOutput`.
- diagnostics and metrics into `HTreeSummary`.

The exact type names can change during implementation if local naming requires it, but this split is the contract.

## Flow-Level Refactor

### Setup

Current setup mutates `CONFIG_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST`, and `SCHEMA_WRITER_INST`.

Target:

- `SetupInput`: config file path, work dir override, `idb::IdbBuilder*`.
- `SetupConfig`: currently likely empty or narrow; do not invent knobs.
- `SetupOutput`: runtime paths and initialized adapter readiness.
- `SetupSummary`: setup ready, failure reason, config parse status.
- `Setup::initializeRuntime(CTSRuntime&, const SetupInput&, const SetupConfig&)`.

### ClockDataRead

Current clock read pulls SDC, uses wrapper, writes global design.

Target:

- input includes `Design&`, `Wrapper&`, `schema::SchemaWriter&`.
- output is materialized clock count / accepted clock targets if needed.
- summary holds failure reason and read-data report fields.

### Synthesis / Topology

Current synthesis reads global design/wrapper/reporter/config and delegates to `Topology`.

Target:

- `SynthesisInput`: `Design&`, `ClockLayout&`, `CharacterizationLibrary&`, `Wrapper&`, `STAAdapter&`, `schema::SchemaWriter&`, design units.
- `SynthesisConfig`: sink clustering config, HTree config, source trunk config.
- `SynthesisOutput`: updated `ClockLayout` if not passed by non-const reference.
- `SynthesisSummary`: current `SynthesisTraceSummary` renamed or slimmed.

Topology should receive built `TopologyConfig` / `HTreeConfig` from Synthesis rather than reading global `CONFIG_INST`.

### Optimization

Current optimization reads global design/config/STA and uses fast STA.

Target:

- `OptimizationInput`: `Design&`, `ClockLayout&`, `CharacterizationLibrary&`, `FastSTA&`, `STAAdapter&`, reporter.
- `OptimizationConfig`: skew bound, allowed sizing cells, cap/slew constraints.
- `OptimizationOutput`: accepted edits or changed design payload.
- `OptimizationSummary`: current optimization result and clock sizing metrics.

### Instantiation

Current instantiation writes iDB through global wrapper and reads global design.

Target:

- `InstantiationInput`: `Design&`, `Wrapper&`, reporter.
- `InstantiationConfig`: likely empty unless writeback mode becomes configurable.
- `InstantiationOutput`: writeback result / artifact payload.
- `InstantiationSummary`: counts and readiness status.

### Evaluation

Current evaluation reads global design/wrapper/STA.

Target:

- `EvaluationInput`: `Design&`, `ClockLayout*`, `Wrapper&`, `STAAdapter&`, reporter.
- `EvaluationConfig`: refresh STA timing, report timing toggle.
- `EvaluationOutput`: `EvaluationState`.
- `EvaluationSummary`: `QorSummary`.

### Report

Current report reads global config for paths and global reporter.

Target:

- `ReportInput`: `ClockLayout&`, `EvaluationState&`, report paths, reporter, design/wrapper if visualization needs them.
- `ReportConfig`: requested formats and refresh evaluation flag.
- `ReportOutput`: generated artifact paths.
- `ReportSummary`: success / failure per artifact.

## Adapter Boundaries

### Wrapper

- Keep iDB access inside `Wrapper`.
- Remove all direct `DESIGN_INST` usage from wrapper implementation.
- `Wrapper::read(...)`, `readClocks(...)`, `readTraceClockTargets(...)` should materialize into an explicit `Design&`.
- `Wrapper::writeClocksDetailed(...)` should receive `Design` or clocks explicitly and keep cross-ref maps tied to that design lifetime.

### STAAdapter

- Convert static facade methods to instance methods where practical.
- If some calls must remain static because iSTA API is global, wrap that behind an explicit `STAAdapter&` facade so callers still expose the dependency.
- No `STA_ADAPTER_INST`.

### FastSTA

- Convert static methods that touch `_contexts` to member methods on a `FastSTA&`.
- Characterization and optimization receive `FastSTA&` in input.
- No `FAST_STA_INST`.

## Reporter Boundary

`schema::EmitTable`, `EmitKeyValueTable`, `EmitDiagnostic`, `EmitArtifact` currently hide the global writer. Replace with one of:

- member calls on a passed `SchemaWriter&`;
- narrow helper functions that take `SchemaWriter&` as first argument;
- a small `ReportSink` interface if tests need a fake.

Do not create a global current reporter.

## Config Cleanup

Start from current `Config` parser for compatibility, but produce narrower flow/module configs:

- `CTSFlowConfig`
- `SynthesisConfig`
- `HTreeConfig`
- `CharacterizationConfig`
- `TopologyConfig`
- `OptimizationConfig`
- `EvaluationConfig`
- `ReportConfig`

Remove or deprecate fake config fields:

- `max_length`: current comment says placeholder; keep JSON parse compatibility only if existing configs depend on it, but do not expose it as algorithm config.
- Work/report paths: runtime setup output, not algorithm config.
- DBU: design units input.
- object name prefix and log context: input/report scope.

## Test Strategy

Create runtime fixtures instead of global reset fixtures:

```cpp
struct CtsRuntimeFixture
{
  CTSRuntime runtime;
  Flow flow;
};
```

Add tests that would fail under hidden globals:

- two runtime fixtures built in one test, with different config/design data, no state sharing;
- two consecutive `CTSAPI::init/reset/run` cycles prove reset is local to API-owned runtime;
- HTree unit can run with a fake reporter and explicit config without touching global state;
- Wrapper read/write uses a provided `Design&`.

## Spec Update Design

Update specs after implementation choices settle:

- `database-guidelines.md`: replace singleton table with explicit runtime ownership / adapter boundary rules.
- `directory-structure.md`: clarify API owns external lifecycle; source flow receives runtime references.
- `quality-guidelines.md`: allow module-qualified `NameInput/NameConfig/NameOutput/NameSummary`; keep standalone `Input`/`Output` forbidden.
- Possibly `logging-guidelines.md`: reporter must be explicit; no `SCHEMA_WRITER_INST`.

Keep spec changes concise and normative. Do not document every migration step.

## Compatibility

- Public `CTSAPI` methods should keep signatures unless user approves API changes.
- Existing JSON configs should continue parsing, but only real knobs should flow into module configs.
- Existing reports should remain semantically equivalent; field layout can change only if required by output/summary split.
- QoR should be unchanged unless fake config removal exposes an already-unused field.

## Risks

- Broad signature churn across HTree/Topology/Char/Optimization can cause large compile breakage. Work in vertical slices and build frequently.
- Reporter removal touches many files and tests; start with an adapter/helper taking `SchemaWriter&` to reduce mechanical churn.
- STAAdapter wraps external global engines; explicit object dependency improves code clarity but does not automatically make iSTA thread-safe.
- Tests currently depend on globals. Keep transitional fixture helpers narrow and delete them when `_INST` count reaches zero.
