# Problem Inventory · CTS Desingleton Refactor

## Scope Checked

- Source: `src/operation/iCTS/source`
- API: `src/operation/iCTS/api`
- Tests: `src/operation/iCTS/test`
- Specs: `.trellis/spec/backend/*`, `.trellis/spec/guides/*`
- Related history: archived `05-20-icts-incremental-refactor-from-origin`

## Singleton Inventory

Macro definitions currently present:

| Macro | Definition File | Role Today | Keep? |
|---|---|---|---|
| `CTS_API_INST` | `api/CTSAPI.hh` | External CTS API entry | Yes, external only |
| `FLOW_INST` | `source/flow/Flow.hh` | Flow lifecycle owner | No |
| `CONFIG_INST` | `source/database/config/Config.hh` | Runtime config singleton | No |
| `DESIGN_INST` | `source/database/design/Design.hh` | CTS design database singleton | No |
| `WRAPPER_INST` | `source/database/io/Wrapper.hh` | iDB adapter singleton | No |
| `STA_ADAPTER_INST` | `source/database/adapter/sta/STAAdapter.hh` | iSTA adapter singleton facade | No |
| `FAST_STA_INST` | `source/database/adapter/fast_sta/FastSta.hh` | Fast STA context store singleton | No |
| `SCHEMA_WRITER_INST` | `source/utils/logger/Schema.hh` | Structured reporter singleton | No |

Usage counts:

| Scope | `CONFIG` | `DESIGN` | `WRAPPER` | `STA` | `FAST_STA` | `FLOW` | `SCHEMA` | `CTS_API` | Total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Source + API | 99 | 70 | 22 | 72 | 2 | 7 | 98 | 1 | 371 |
| Test | 146 | 130 | 49 | 37 | 0 | 13 | 29 | 0 | 404 |
| Total | 245 | 200 | 71 | 109 | 2 | 20 | 127 | 1 | 775 |

High-density source/API files:

| File | Uses | Issue |
|---|---:|---|
| `source/flow/setup/Setup.cc` | 25 | Initializes global config/wrapper/STA/reporter and writes paths into global config |
| `source/flow/synthesis/htree/characterization/library/CharacterizationLibrary.cc` | 24 | Builds char options from global config and STA queries |
| `source/flow/Flow.cc` | 19 | Flow singleton owns runtime state and reads global design/reporter |
| `source/flow/instantiation/design_conversion/DesignConversion.cc` | 18 | Commits design through global `Design` |
| `source/flow/synthesis/htree/HTree.cc` | 13 | HTree reads global DBU/config/reporter |
| `source/flow/evaluation/qor/QorEvaluation.cc` | 11 | Evaluation reads global design/wrapper/STA |
| `source/database/io/WrapperClockReader.cc` | 11 | Wrapper materializes into global design |
| `source/database/adapter/fast_sta/clock_state/FastStaBuilder.cc` | 11 | Fast STA context reads global config/wrapper/STA |
| `api/CTSAPI.cc` | 11 | Reset and API calls hard-code singleton list |
| `source/flow/synthesis/topology/sink/SinkLoadClustering.cc` | 10 | Clustering config and buffer choices from globals |
| `source/flow/synthesis/htree/region/SinkLoadRegion.cc` | 10 | Sink-load legality reads global config/STA |
| `source/database/adapter/fast_sta/liberty/FastStaLiberty.cc` | 10 | Fast STA liberty model queries global STA adapter |

## Problem Categories

### 1. Lifecycle Ownership Is Hidden

`CTSAPI::resetAPI()` manually resets `Config`, `Design`, `Wrapper`, `FastSTA`, `Flow`, and `SchemaWriter`. This makes reset order visible but still global. Tests and flow code can mutate shared state between cases unless every fixture remembers all resets. The previous `SingletonRegistry` attempt was withdrawn because it made reset order less visible, so the right direction is explicit runtime ownership, not a registry.

### 2. Flow Singleton Blocks Multiple Runs

`Flow` owns `_run_summary`, `_clock_layout`, `_evaluation_state`, `_instantiation_result`, and `_char_library`, but is accessed via `FLOW_INST`. These are run-local state and should belong to a normal `Flow` object owned by `CTSAPI` or by a caller-created runtime.

### 3. Config Reads Are Scattered Across Algorithm Code

Examples:

- `HTree.cc` reads `CONFIG_INST.get_htree_topology_tolerance()` and `CONFIG_INST.get_max_fanout()`.
- `Plan.cc` reads `CONFIG_INST.get_htree_depth_explore_window()`.
- `Constraint.cc` reads `CONFIG_INST.is_force_branch_buffer()`.
- `SinkBranch.cc` / `SourceTrunk.cc` read root input slew and analytical HTree enablement.
- `CharacterizationLibrary.cc` reads max slew/cap, wirelength lattice, buffer types, routing layers, wire width, char redundancy, and STA route RC.
- `Optimization.cc` reads `CONFIG_INST.get_skew_bound()`.

This makes it hard to know which knobs actually affect an algorithm. It also mixes true algorithm decisions with environment facts such as directories and DBU.

### 4. HTree Options And Result Mix Different Concepts

`HTreeSynthesisOptions` currently contains:

- real knobs: force branch buffer, target depth, depth window, topology tolerance, root-driver sizing, boundary relaxation, analytical solver.
- runtime inputs: clock period, fixed root location, additional characterization lengths.
- dependencies: `CharacterizationLibrary*`.
- reporting/naming: `log_context`, `object_name_prefix`.
- semantic mode: `topology_loads_are_local_buffers`.

`HTreeSynthesisResult` currently contains:

- design payload: topology, inserted insts/pins/nets, root pins/nets.
- summary/status: success, failure reason, selected depth, counts, char grid, compensation, analytical stats.
- input echoes: log context, object prefix, root net/pins.
- report-only metrics and diagnostics.

This is the main example of the broader pattern the user called out: options/result names hide true input/output boundaries.

### 5. DBU And Environment Facts Are Treated Like Algorithm Configuration

`HTree.cc`, `Topology.cc`, `SourceTrunkSegment.cc`, `TopologyGen.cc`, `RootDriverCompensationLoad.cc`, and reporting/visualization paths query or pass DBU in different places. DBU is a design/layout invariant for the run, not a tunable HTree config. It should be captured once in runtime/design-unit input and passed as an environment fact.

### 6. Reporter Is Global And Leaks Into Modules

`SCHEMA_WRITER_INST` appears in flow code, HTree, topology, module topology, and characterization builder. Even module code such as `TopologyGen` and `CharBuildOrchestrator` writes to the global reporter. This prevents isolated algorithm tests and prevents multiple reporter destinations in future parallel flows.

### 7. Wrapper And Design Are Entangled By Globals

`Wrapper` owns iDB references and cross-reference maps, while `Design` owns CTS objects. `WrapperClockReader.cc` and writeback code currently reach global `DESIGN_INST`. The adapter should accept a `Design&` for materialization/writeback so the target design is visible and testable.

### 8. STAAdapter And FastSTA Are Hidden Dependencies

Many HTree/topology/optimization/evaluation paths query global `STA_ADAPTER_INST`. `FastSTA` has real mutable context store but exposes mostly static methods and singleton `getInst()`. The explicit design should make `STAAdapter&` and `FastSTA&` dependencies visible at the flow or module boundary.

### 9. Tests Encode Global Fixtures

Tests use globals heavily:

- `FlowDesignFixture.hh` resets config/design/wrapper and creates design objects through `DESIGN_INST`.
- Real-tech fixtures initialize `CONFIG_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST`.
- HTree and topology tests often rely on global config defaults.

This is expected with the current design, but after refactor these fixtures should construct `CTSRuntime` or narrower test objects directly.

### 10. Specs Currently Conflict With The New Direction

`.trellis/spec/backend/database-guidelines.md` explicitly lists singleton roles as the established rule. It also says module code should receive runtime configuration through options, which is aligned with this task, but the singleton table must be replaced with new internal ownership rules.

`quality-guidelines.md` currently discourages standalone names such as `Input`; the new convention should clarify that module-qualified contracts such as `HTreeInput`, `HTreeConfig`, `HTreeOutput`, and `HTreeSummary` are allowed and preferred, while standalone generic `Input` / `Output` remains forbidden.

## Initial Refactor Targets

### Must Change Early

- `api/CTSAPI.hh/.cc`: own runtime and flow state; keep external entry stable.
- `source/flow/Flow.hh/.cc`: remove singleton; accept runtime/services explicitly.
- `source/flow/setup/Setup.*`: produce initialized runtime state instead of mutating globals.
- `source/utils/logger/Schema.*`: remove singleton access; pass reporter explicitly.
- `source/database/config/Config.*`: normal object parse/reset.
- `source/database/design/Design.*`: normal object owner.
- `source/database/io/Wrapper.*`: adapter instance that receives/uses explicit `Design&`.
- `source/database/adapter/sta/STAAdapter.*`: normal adapter facade instance.
- `source/database/adapter/fast_sta/FastSta.*`: normal context-store instance.

### Contract Cleanup Hotspots

- `source/flow/synthesis/htree/HTreeSynthesisOptions.hh`
- `source/flow/synthesis/htree/HTreeSynthesisResult.hh`
- `source/flow/synthesis/htree/HTree.cc`
- `source/flow/synthesis/topology/Topology.hh/.cc`
- `source/flow/synthesis/topology/sink/*`
- `source/flow/synthesis/topology/trunk/*`
- `source/flow/synthesis/htree/characterization/library/*`
- `source/module/characterization/builder/*`
- `source/module/topology/TopologyGen.*`
- `source/flow/optimization/*`
- `source/flow/evaluation/*`
- `source/flow/report/*`

## Non-Goals Learned From History

- Do not use `SingletonRegistry`; prior task proved that hidden registration order is worse than explicit ownership.
- Do not make a broad "context" global and access it from anywhere; passing a context object through every call without narrowing it would preserve the same coupling.
- Do not simply rename `Options` to `Config`; the fields must be reclassified.
- Do not move report metrics into design objects unless the design truly owns that semantic data.
