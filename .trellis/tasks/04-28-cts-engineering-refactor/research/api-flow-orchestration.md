# Research: API and Flow Orchestration Architecture

- Query: Research the current iCTS public API and flow orchestration architecture for the CTS engineering refactor. Inspect `src/operation/iCTS/api`, `src/operation/iCTS/source/flow`, report/evaluation code, and relevant CMake. Identify stable external interfaces, conflated responsibilities in `FlowManager` / `CTSAPI`, semantic refactor boundaries, compatibility risks, and suggested class/file decomposition.
- Scope: internal
- Date: 2026-04-28

## Findings

### Files Found

| Path | Description |
|---|---|
| `src/operation/iCTS/api/CTSAPI.hh` | Singleton public iCTS API facade and `CTS_API_INST` macro. |
| `src/operation/iCTS/api/CTSAPI.cc` | API implementation for init/reset/run/report/feature summary bridging. |
| `src/operation/iCTS/source/flow/FlowManager.hh` | Internal singleton flow orchestrator facade, run summary, and evaluation readiness state. |
| `src/operation/iCTS/source/flow/FlowManager.cc` | Main CTS stage orchestration plus per-clock synthesis, sink-group preparation, logging, evaluation, and report coordination. |
| `src/operation/iCTS/source/flow/netlist/ClockNetManager.hh/.cc` | Final clock-netlist mutation helper, clock-data read, sink partitioning, root-buffer/downstream-net creation, reconnect, restore, and commit helpers. |
| `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh/.cc` | Per-net synthesis facade for optional clustering, H-tree build, source-to-root build, temporary inserted objects, and rollback of local algorithm side effects. |
| `src/operation/iCTS/source/flow/htree/*` | H-tree builder implementation and sub-steps already split across multiple files. |
| `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh/.cc` | Evaluation summary types and evaluation stage implementation. Performs iDB writeback, STA refresh, RC-tree/timing installation, metrics collection, summary aliases, and statistics emission. |
| `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh/.cc` | Statistics data structs plus writer for `statistics/wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt`. |
| `src/operation/iCTS/source/flow/report/CTSVisualizationReport.hh/.cc` | Report-stage SVG artifact generation for CTS design and flyline views. |
| `src/operation/iCTS/CMakeLists.txt` | iCTS root CMake options and subdirectory inclusion. |
| `src/operation/iCTS/api/CMakeLists.txt` | Defines `icts_api`, public API include directory, and private links to source/external deps. |
| `src/operation/iCTS/source/CMakeLists.txt` | Defines `icts_source` interface aggregator. |
| `src/operation/iCTS/source/flow/CMakeLists.txt` | Defines flow subtargets and `icts_source_flow`. |
| `src/operation/iCTS/source/flow/evaluation/CMakeLists.txt` | Defines `icts_source_flow_evaluation`. |
| `src/operation/iCTS/source/flow/report/CMakeLists.txt` | Defines `icts_source_flow_report`. |
| `src/platform/tool_manager/tool_api/icts_io/icts_io.h/.cpp` | Platform-facing iCTS IO wrapper used by ToolManager, Tcl, Python, file-manager, and GUI paths. |
| `src/platform/tool_manager/tool_manager.h/.cpp` | Higher-level external platform facade for `autoRunCTS` and `reportCTS`. |
| `src/interface/tcl/tcl_icts/*` | Tcl command registration and command adapters for `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config`. |
| `src/interface/python/py_icts/*` | Python bindings for `run_cts` and `cts_report`. |
| `src/feature/database/feature_icts.h` and `src/feature/builder/feature_builder_tool.cpp` | Feature summary ABI consumed through `CTSAPI::outputSummary()`. |
| `src/operation/iCTS/test/flow/FlowManagerTest.cc` | Current log/report/reset/sink-group behavior contract tests. |

Requested path note: `src/operation/iCTS/report/evaluation` does not exist in this checkout. The matching report/evaluation code is under `src/operation/iCTS/source/flow/report` and `src/operation/iCTS/source/flow/evaluation`.

### Related Specs

- `.trellis/spec/backend/directory-structure.md`: iCTS layers are API, Source, Test; API depends on Source; Source must not depend on API; `source/flow` owns orchestration; `api/CTSAPI` should stay focused on external entry points; internal flow lifecycle belongs under `source/flow` (`.trellis/spec/backend/directory-structure.md:13`, `.trellis/spec/backend/directory-structure.md:21`, `.trellis/spec/backend/directory-structure.md:28`, `.trellis/spec/backend/directory-structure.md:51`).
- `.trellis/spec/backend/database-guidelines.md`: singleton roles are fixed around `CTS_API_INST`, `DESIGN_INST`, `CONFIG_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST`; external callers enter through `CTS_API_INST`; module code should receive options instead of reaching into config; final design ownership belongs to `Design`; temporary algorithm objects should be committed only after success (`.trellis/spec/backend/database-guidelines.md:14`, `.trellis/spec/backend/database-guidelines.md:25`, `.trellis/spec/backend/database-guidelines.md:31`, `.trellis/spec/backend/database-guidelines.md:58`).
- `.trellis/spec/backend/logging-guidelines.md`: runtime logging uses `LOG_*`; structured output belongs in schema/report helpers; API/flow entry layers coordinate stage boundaries and output timing but should not own low-level field assembly; stage/runtime state belongs in `SCHEMA_WRITER_INST` (`.trellis/spec/backend/logging-guidelines.md:13`, `.trellis/spec/backend/logging-guidelines.md:40`, `.trellis/spec/backend/logging-guidelines.md:42`, `.trellis/spec/backend/logging-guidelines.md:43`).
- `.trellis/spec/backend/quality-guidelines.md`: CMake dependencies should be expressed through target links, default `PRIVATE`, and use `PUBLIC` only when a dependency appears in public headers (`.trellis/spec/backend/quality-guidelines.md:57`).

### Stable External Interfaces

The most stable public C++ interface is `CTSAPI`. Its header defines `CTS_API_INST`, a Meyers singleton accessor, and five public static methods: `runCTS()`, `report(save_dir)`, `resetAPI()`, `init(config_file, work_dir)`, and `outputSummary()` (`src/operation/iCTS/api/CTSAPI.hh:33`, `src/operation/iCTS/api/CTSAPI.hh:38`, `src/operation/iCTS/api/CTSAPI.hh:44`, `src/operation/iCTS/api/CTSAPI.hh:48`, `src/operation/iCTS/api/CTSAPI.hh:52`). `CTSAPI.hh` exposes only the API include directory through the `icts_api` target (`src/operation/iCTS/api/CMakeLists.txt:1`, `src/operation/iCTS/api/CMakeLists.txt:3`) while `icts_source` and `icts_api_external_libs` are private implementation links (`src/operation/iCTS/api/CMakeLists.txt:5`).

`CTSAPI::outputSummary()` returns `ieda_feature::CTSSummary`, whose public field names are compatibility-sensitive: `buffer_num`, `buffer_area`, `clock_path_min_buffer`, `clock_path_max_buffer`, `max_level_of_clock_tree`, `max_clock_wirelength`, `total_clock_wirelength`, and `clocks_timing` (`src/feature/database/feature_icts.h:25`). The feature builder directly calls `CTS_API_INST.outputSummary()` (`src/feature/builder/feature_builder_tool.cpp:57`), so feature consumers depend on that return type and field mapping.

`CtsIO` is a platform-level public facade beyond the newer `CTSAPI` surface. It exposes `runCTS(config, work_dir)` and `reportCTS(path)` (`src/platform/tool_manager/tool_api/icts_io/icts_io.h:49`) and also legacy/GUI/file-manager data accessors for routing data and tree data (`src/platform/tool_manager/tool_api/icts_io/icts_io.h:57`, `src/platform/tool_manager/tool_api/icts_io/icts_io.h:78`). `ToolManager` exposes `autoRunCTS(config, work_dir)` and `reportCTS(path)` (`src/platform/tool_manager/tool_manager.h:102`) and delegates to `ctsInst` (`src/platform/tool_manager/tool_manager.cpp:270`, `src/platform/tool_manager/tool_manager.cpp:275`).

Tcl command names are public script API. `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config` are registered in `tcl_register_cts.h` (`src/interface/tcl/tcl_icts/tcl_register_cts.h:34`). `run_cts` accepts `-config` and `-work_dir`, then delegates through `ToolManager::autoRunCTS` (`src/interface/tcl/tcl_icts/tcl_cts.cpp:24`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:47`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:52`). `cts_report` accepts `-name` and `-path`; `-name` goes through `ToolManager::reportCTS`, while `-path` directly calls `CTS_API_INST.report()` (`src/interface/tcl/tcl_icts/tcl_cts.cpp:67`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:92`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:100`).

Python binding names are also stable: `run_cts(cts_config, cts_work_dir)` and `cts_report(path)` (`src/interface/python/py_icts/py_register_icts.h:25`). Python `run_cts` delegates through `ToolManager`, while Python `cts_report` calls `CTS_API_INST.report()` directly (`src/interface/python/py_icts/py_icts.cpp:23`, `src/interface/python/py_icts/py_icts.cpp:29`).

The log/report artifacts are de facto external contracts. Tests assert section names and absence/presence of fields in `cts.log`, including `## Input Summary`, `## Synthesis Summary`, `## Evaluation Summary`, `## Runtime Summary`, `## Run Results`, `CTS Runtime Summary`, `CTS Key Results`, and `CTS Evaluation Summary` (`src/operation/iCTS/test/flow/FlowManagerTest.cc:231`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:241`). Statistics reports are named `wirelength.rpt`, `cell_stats.rpt`, and `lib_cell_dist.rpt` (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:146`). Visualization artifacts are `cts_design.svg` and `cts_flyline.svg` under the report `output` directory (`src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:55`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:622`).

### Current Flow Orchestration

The top-level external run path is:

```text
Tcl/Python/ToolManager -> CtsIO::runCTS -> CTSAPI::init -> CTSAPI::runCTS -> FlowManager::runCTS
```

`CtsIO::runCTS` defaults an empty config path from `flowConfigInst->get_icts_path()`, sets the global flow stage label, starts platform stats, calls `CTS_API_INST.init(config, work_dir)`, calls `CTS_API_INST.runCTS()`, then records runtime/memory and returns `true` unconditionally (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:33`).

`CTSAPI::init` currently owns session reset, config parsing, work-dir creation, output path derivation, schema writer opening, iDB builder lookup, wrapper initialization, STA adapter initialization, and runtime setup emission (`src/operation/iCTS/api/CTSAPI.cc:94`). `CTSAPI::resetAPI` resets `CONFIG_INST`, `DESIGN_INST`, `WRAPPER_INST`, `FLOW_MANAGER_INST`, and `SCHEMA_WRITER_INST` (`src/operation/iCTS/api/CTSAPI.cc:85`). `CTSAPI::runCTS`, `CTSAPI::report`, and `CTSAPI::outputSummary` only delegate to `FLOW_MANAGER_INST` plus feature-summary mapping (`src/operation/iCTS/api/CTSAPI.cc:75`, `src/operation/iCTS/api/CTSAPI.cc:80`, `src/operation/iCTS/api/CTSAPI.cc:136`).

`FlowManager::runCTS` owns stage sequencing and total runtime accounting. It resets runtime metrics, starts a total runtime metric and a `CTS` stage, then calls `readData()`, `run()`, and `evaluate()` before emitting runtime summary and key results (`src/operation/iCTS/source/flow/FlowManager.cc:420`). `FlowManager::readData` resets run/evaluation state, resets the evaluator summary, and calls `ClockNetManager::readClockData()` (`src/operation/iCTS/source/flow/FlowManager.cc:444`). `FlowManager::run` loops `DESIGN_INST.get_clocks()`, calls the anonymous `runClock(...)` helper, records clock/sink-group counts, emits the flow summary, and marks stage/runtime success or failure (`src/operation/iCTS/source/flow/FlowManager.cc:458`).

The anonymous `runClock(...)` helper is the dense synthesis orchestration core. It restores the clock-source net, clears CTS membership, validates source and source net, partitions sinks into hard macro and regular groups, prepares one root-buffer/downstream-net context per non-empty group, computes top-level source-to-root lengths, creates a local `CharacterizationLibrary`, synthesizes each sink group, then synthesizes source-to-root (`src/operation/iCTS/source/flow/FlowManager.cc:336`). Failure paths restore the source net and clear membership after preparation or synthesis failures (`src/operation/iCTS/source/flow/FlowManager.cc:377`, `src/operation/iCTS/source/flow/FlowManager.cc:399`, `src/operation/iCTS/source/flow/FlowManager.cc:410`).

`FlowManager::evaluate` wraps the evaluator in runtime/stage scopes, emits evaluation headings, calls `ClockTreeEvaluator::evaluate()`, and sets `_evaluation_ready` from `ClockTreeEvaluator::hasEvaluationResult()` (`src/operation/iCTS/source/flow/FlowManager.cc:518`). `FlowManager::report` requires an initialized work dir, chooses a report root dir, reuses evaluation state when `_evaluation_ready` and `ClockTreeEvaluator::hasEvaluationResult()` are both true, otherwise calls `evaluate()`, then writes statistics and visualization artifacts (`src/operation/iCTS/source/flow/FlowManager.cc:535`).

`ClockNetManager` already separates a useful netlist-mutation facade from `FlowManager`. It reads clock data either from `CONFIG_INST.get_net_list()` or `WRAPPER_INST.collectClockNetPairs()` and writes it through `WRAPPER_INST.readClocks()` (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:284`). It partitions macro/regular sinks (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:311`), creates root buffers and downstream nets (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:338`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:395`), reconnects/restores source nets (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:363`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:401`), and validates/commits temporary algorithm objects into `Design` (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:428`).

`ClockSynthesis` is the per-root-net algorithm facade. It takes `BuildOptions`, temporary inserted-object vectors, optional cluster results, H-tree build result, and source-to-root result (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:43`). It still reads `CONFIG_INST`, `STA_ADAPTER_INST`, `WRAPPER_INST`, `DESIGN_INST`, and `SCHEMA_WRITER_INST` internally for clustering config, buffer port/drive resolution, DBU conversion, and distance reports (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:57`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:357`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:372`). Its temporary-object ownership and restore lambdas are valuable refactor anchors because failures restore root-net/pin side effects before returning (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:504`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:536`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:662`).

`ClockTreeEvaluator` combines multiple post-synthesis stages. Its public surface owns summary retrieval, readiness checks, report writing, and reset (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:83`). `evaluate()` clears static latest summary/statistics, writes clocks to iDB, refreshes STA timing context, propagates clocks, gathers buffer/area/wirelength data, installs RC trees, updates timing, queries timing/skew metrics, syncs compatibility aliases, emits evaluation summary, and writes statistics to the work dir (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:451`).

`CTSStatisticsWriter` is a clean report writer boundary. It owns statistics data structs and writes exactly three reports under `<root>/statistics` (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh:47`, `src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:131`). `CTSVisualizationReport` is similarly isolated as a report-stage artifact writer, although its `.cc` still owns collection, routing/fallback segment creation, SVG layout/style, and file emission in one translation unit (`src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:157`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:269`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:547`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:622`).

### Responsibilities Currently Conflated

`CTSAPI` is both external facade and runtime/session initializer. It should stay an API entry point, but it currently performs config initialization, directory creation, output path policy, schema writer opening, iDB builder lookup, wrapper init, STA init, and runtime setup emission (`src/operation/iCTS/api/CTSAPI.cc:96`, `src/operation/iCTS/api/CTSAPI.cc:107`, `src/operation/iCTS/api/CTSAPI.cc:117`, `src/operation/iCTS/api/CTSAPI.cc:124`, `src/operation/iCTS/api/CTSAPI.cc:129`, `src/operation/iCTS/api/CTSAPI.cc:132`). That conflicts with the spec direction that `api/CTSAPI` stay focused on external entry points and lifecycle internals live under `source/flow` (`.trellis/spec/backend/directory-structure.md:51`).

`FlowManager` is a stage controller, per-clock synthesis coordinator, netlist transaction coordinator, schema field assembler, summary owner, and report coordinator. `FlowManager.hh` exposes stage methods and summary methods while storing `_run_summary`, `_runtime_setup_emitted`, and `_evaluation_ready` (`src/operation/iCTS/source/flow/FlowManager.hh:60`, `src/operation/iCTS/source/flow/FlowManager.hh:81`). `FlowManager.cc` has many anonymous helpers that are not separately testable: sink-group context, report-root resolution, run-summary recorders, H-tree log context creation, sink-group synthesis, flow table row formatting, membership cleanup, unit formatting, sink-group preparation, source-to-root length collection, source-to-root synthesis, summary emission, and `runClock` (`src/operation/iCTS/source/flow/FlowManager.cc:67`, `src/operation/iCTS/source/flow/FlowManager.cc:78`, `src/operation/iCTS/source/flow/FlowManager.cc:86`, `src/operation/iCTS/source/flow/FlowManager.cc:106`, `src/operation/iCTS/source/flow/FlowManager.cc:118`, `src/operation/iCTS/source/flow/FlowManager.cc:143`, `src/operation/iCTS/source/flow/FlowManager.cc:157`, `src/operation/iCTS/source/flow/FlowManager.cc:181`, `src/operation/iCTS/source/flow/FlowManager.cc:248`, `src/operation/iCTS/source/flow/FlowManager.cc:270`, `src/operation/iCTS/source/flow/FlowManager.cc:315`, `src/operation/iCTS/source/flow/FlowManager.cc:336`).

`FlowManager::report` couples report-session policy, evaluation reuse policy, statistics writing, visualization writing, runtime metrics, and stage status (`src/operation/iCTS/source/flow/FlowManager.cc:535`). A report coordinator could own this while `FlowManager` only sequences the report stage.

`ClockTreeEvaluator` conflates iDB writeback, STA refresh, clock propagation, RC tree installation, timing updates, metric collection, report table emission, compatibility aliasing, and global latest-state storage (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:59`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:79`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:114`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:300`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:343`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:433`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:451`). It is a high-value split target because `_evaluation_ready` in `FlowManager` and static latest state in `ClockTreeEvaluator` jointly control report correctness.

`ClockSynthesis` is semantically closer to a flow algorithm coordinator than to pure synthesis. It prepares config-derived options, resolves STA buffer ports/drive, creates temporary design objects, emits schema distance tables, and calls H-tree/segment builders (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:57`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:167`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:201`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:273`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:590`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:688`). This is acceptable under `source/flow`, but extraction should not push those config/adapter reads into `source/module`.

### Semantic Refactor Boundaries

Keep `CTSAPI.hh`, `CTS_API_INST`, `CTSAPI` method names/signatures, and the `icts_api` target stable. Move implementation details behind new flow/session classes while leaving `CTSAPI.cc` as a delegating facade. This preserves API callers and CMake include behavior (`src/operation/iCTS/api/CTSAPI.hh:33`, `src/operation/iCTS/api/CMakeLists.txt:1`).

Extract session initialization from `CTSAPI::init` into a flow-layer runtime/session object. The semantic boundary is "prepare one CTS session": reset state, load config, resolve work/output/log paths, open schema writer, initialize Wrapper/STA, and emit runtime setup. It belongs under `source/flow` because it coordinates API, config, adapters, and reporting, but it must keep direct external-tool access inside `Wrapper` / `STAAdapter` per database spec (`.trellis/spec/backend/database-guidelines.md:58`).

Keep `FlowManager` as the public internal orchestrator facade, but reduce it to stage sequencing and state ownership. Introduce stage/coordinator classes that return structured results instead of mutating `_run_summary` and schema rows through hidden helpers. The natural stage boundary is already visible in `runCTS()`: read data, synthesize, evaluate, report/runtime summary (`src/operation/iCTS/source/flow/FlowManager.cc:426`).

Extract a per-clock synthesis coordinator from the anonymous `runClock`. Its boundary should own `ClockRunContext`, sink partitioning decisions, sink-group preparation, downstream synthesis calls, source-to-root synthesis, rollback, and `CTSFlowRunSummary` updates. This would make the macro/regular sink split and top-level root synthesis testable without routing all tests through `FlowManager`.

Extract a netlist edit/transaction helper from `FlowManager` plus `ClockNetManager`. `ClockNetManager::commitInsertedObjects` already validates duplicate names and commits temporary objects only after success (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:428`). A `ClockNetEditSession` or `ClockRunTransaction` can centralize restore/clear behavior currently repeated in `runClock` failure paths (`src/operation/iCTS/source/flow/FlowManager.cc:377`, `src/operation/iCTS/source/flow/FlowManager.cc:399`, `src/operation/iCTS/source/flow/FlowManager.cc:410`).

Split `ClockTreeEvaluator` into post-synthesis services while preserving `ClockTreeEvaluator` as a facade during migration. Suggested semantic pieces:

- `CTSDesignWriteback`: `WRAPPER_INST.writeClocks(clocks)`, design-ready checks, and DBU/unit capture.
- `CTSTimingRefresh`: STA timing context refresh, propagated clocks, timing update/report, timing/skew queries.
- `CTSClockMetricsCollector`: buffer counts, area, clock-net classification, wirelength/HPWL, RC-tree measurement.
- `CTSFeatureSummaryAdapter`: mapping `ClockTreeSummary` to `ieda_feature::CTSSummary` and compatibility aliases.
- `CTSEvaluationState`: latest summary/statistics ownership and reset/readiness checks.

Keep `CTSStatisticsWriter` as a writer boundary. It already has a small public surface and a clear output contract (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.hh:64`). If report files expand, add new writer/helper classes beside it rather than returning low-level table rows to `FlowManager`.

Keep `CTSVisualizationReport` under `source/flow/report`, but consider splitting data collection and SVG writing if it grows further. Current internal boundaries are already visible: collect nets/insts, collect design/flyline segments, write SVG, emit artifact status (`src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:157`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:181`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:269`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:294`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:547`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:599`).

### Suggested Class/File Decomposition

Conservative decomposition that preserves the current public surface:

| New or retained file | Role |
|---|---|
| `api/CTSAPI.hh` and `api/CTSAPI.cc` | Retain as stable external facade. `CTSAPI.cc` delegates to flow/session classes. |
| `source/flow/FlowManager.hh` and `source/flow/FlowManager.cc` | Retain singleton and public internal facade. Reduce to sequencing and state handoff. |
| `source/flow/session/CTSRuntimeSession.hh/.cc` | Own current `CTSAPI::init` setup work: reset, config/work dir/output paths, schema open, Wrapper/STA init, runtime setup. |
| `source/flow/session/CTSRuntimePaths.hh` | Small value object for `work_dir`, `cts.log`, `visualization_dir`, and `statistics_dir`; useful for testing path policy without touching adapters. |
| `source/flow/stage/CTSReadDataStage.hh/.cc` | Wrap `ClockNetManager::readClockData`, summary reset, and read-data stage metrics. |
| `source/flow/stage/CTSSynthesisStage.hh/.cc` | Loop clocks, call per-clock coordinator, emit flow summary, return `CTSFlowRunSummary`. |
| `source/flow/synthesis/ClockRunCoordinator.hh/.cc` | Extract `runClock`: prepare clock, partition sinks, build sink groups, synthesize sink groups, synthesize source-to-root, rollback on failure. |
| `source/flow/synthesis/SinkGroupPlanner.hh/.cc` | Build `SinkGroupContext` equivalents, naming prefixes, macro/regular grouping, root input collection. |
| `source/flow/netlist/ClockNetEditSession.hh/.cc` | Centralize restore source net, clear membership, rollback, and commit of inserted objects around a clock run. |
| `source/flow/evaluation/ClockTreeEvaluator.hh/.cc` | Retain facade for current callers; delegate to evaluation services below. |
| `source/flow/evaluation/CTSEvaluationState.hh/.cc` | Own latest `ClockTreeSummary` and `CTSStatistics`, reset/readiness APIs. |
| `source/flow/evaluation/CTSDesignWriteback.hh/.cc` | Own iDB writeback and design-unit capture. |
| `source/flow/evaluation/CTSTimingEvaluator.hh/.cc` | Own STA refresh/propagate/update/report and timing/skew metric query. |
| `source/flow/evaluation/CTSMetricCollector.hh/.cc` | Own clock-net measurement, buffer stats, wirelength/HPWL, and statistics accumulation. |
| `source/flow/evaluation/CTSFeatureSummaryAdapter.hh/.cc` | Own compatibility aliases and conversion to `ieda_feature::CTSSummary`; eventually lets `CTSAPI.cc` avoid low-level mapping. |
| `source/flow/evaluation/CTSStatisticsWriter.hh/.cc` | Retain writer facade. |
| `source/flow/report/CTSReportCoordinator.hh/.cc` | Own current `FlowManager::report`: report root resolution, evaluation reuse/rebuild policy, statistics writer call, visualization call, report stage status. |
| `source/flow/report/CTSVisualizationReport.hh/.cc` | Retain SVG facade. Optionally split collector/writer later if report code keeps growing. |

CMake should follow existing hierarchy. Add subdirectories or subtargets only where the split creates real `.cc` files; link through the nearest flow aggregator and expose headers `PUBLIC` only when public headers depend on them. This follows the target rules in the backend quality spec and the existing `flow` subtarget pattern (`.trellis/spec/backend/quality-guidelines.md:57`, `src/operation/iCTS/source/flow/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/CMakeLists.txt:13`).

### Compatibility Risks

Do not change `CTSAPI.hh` signatures, macro name, or file path without a compatibility shim. C++ callers, Tcl/Python adapters, feature builder, and tests depend on this surface (`src/operation/iCTS/api/CTSAPI.hh:33`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:19`, `src/interface/python/py_icts/py_icts.cpp:21`, `src/feature/builder/feature_builder_tool.cpp:57`).

Do not change Tcl/Python command names, option names, or positional argument behavior during this refactor. `run_cts`, `cts_report`, `cts_save_tree`, `cts_config`, Python `run_cts`, and Python `cts_report` are registered as external interfaces (`src/interface/tcl/tcl_icts/tcl_register_cts.h:34`, `src/interface/python/py_icts/py_register_icts.h:25`).

`CtsIO::runCTS` and `CtsIO::reportCTS` currently return `true` unconditionally after API calls (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:33`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:53`). Changing failures to propagate as `false` would be behaviorally better but is a compatibility change for ToolManager/Tcl call sites. Preserve behavior first; introduce better status reporting separately.

`CtsIO::reportCTS` defaults empty `path` to `flowConfigInst->get_icts_path()` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:53`). That value appears to be a config path, while `FlowManager::report` treats its argument as a report root directory (`src/operation/iCTS/source/flow/FlowManager.cc:535`). This ambiguity is a compatibility risk: scripts may depend on either legacy behavior or accidental output location.

`CTSAPI::resetAPI` clears config, design, wrapper, flow state, and schema writer (`src/operation/iCTS/api/CTSAPI.cc:85`). Refactoring session state must preserve this broad reset. Tests expect `resetAPI()` to clear evaluation summary aliases and timing data (`src/operation/iCTS/test/flow/FlowManagerTest.cc:406`).

`FlowManager::report` depends on both `_evaluation_ready` and `ClockTreeEvaluator::hasEvaluationResult()` to decide whether to reuse state or rebuild evaluation (`src/operation/iCTS/source/flow/FlowManager.cc:542`). If state ownership moves, preserve this two-condition guard or replace it with an equivalent single owner that cannot become stale after reset or partial failure.

`ClockTreeSummary` has compatibility aliases that intentionally do not map one-to-one to current log names (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.hh:70`). `syncCompatibilityAliases` maps final metrics back into legacy feature fields (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:114`). Preserve these aliases until feature consumers migrate.

`cts.log` format is test-covered and likely user-visible. Flow tests assert section names, table names, and absence of old/verbose field names (`src/operation/iCTS/test/flow/FlowManagerTest.cc:241`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:250`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:256`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:265`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:267`). Move logging code carefully and keep schema strings stable unless PRD explicitly calls for report format changes.

Clock-net mutation relies on borrowed pointer ownership rules. `Design` owns final `Clock`, `Inst`, `Pin`, `Net` objects, while `Clock` stores borrowed membership views and algorithm results temporarily own objects until commit (`.trellis/spec/backend/database-guidelines.md:31`). Splitting `ClockNetManager`, synthesis, and transaction logic must not cache borrowed pointers across resets or commit temporary objects before all validation passes.

CMake target visibility is a real risk. `FlowManager.hh` includes `evaluation/ClockTreeEvaluator.hh`, so `icts_source_flow` links `icts_source_flow_evaluation` as `PUBLIC` (`src/operation/iCTS/source/flow/FlowManager.hh:29`, `src/operation/iCTS/source/flow/CMakeLists.txt:15`). If `ClockTreeSummary` moves to a smaller header, the public dependency can potentially shrink; if not, keep it public. `tool_api_icts` directly includes/calls `CTSAPI` through `icts_io.cpp` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:22`) but its local CMake does not link `icts_api` (`src/platform/tool_manager/tool_api/icts_io/CMakeLists.txt:10`); higher-level targets currently compensate by linking `icts_api` in Tcl/Python or `icts_source` through ToolManager (`src/interface/tcl/tcl_icts/CMakeLists.txt:7`, `src/platform/tool_manager/CMakeLists.txt:20`). Avoid making this implicit dependency more fragile.

`CTSVisualizationReport` and `CTSStatisticsWriter` output paths are public enough to preserve: report writes `statistics/*.rpt` and `output/cts_*.svg` (`src/operation/iCTS/source/flow/evaluation/CTSStatisticsWriter.cc:138`, `src/operation/iCTS/source/flow/report/CTSVisualizationReport.cc:622`). A report coordinator should delegate to the existing writer/facade first.

### Migration Sequence Recommendation

1. Add new flow-layer facades without changing public APIs: `CTSRuntimeSession`, `CTSSynthesisStage`, `ClockRunCoordinator`, `CTSReportCoordinator`, and `CTSEvaluationState`. Keep `CTSAPI` and `FlowManager` method signatures as shims.
2. Move pure helpers first: formatting, report root resolution, run-summary field updates, sink-group context construction, and flow-row assembly. This reduces `FlowManager.cc` size without changing behavior.
3. Move netlist rollback/restore into `ClockNetEditSession` while preserving existing `ClockNetManager` public helper names used by tests.
4. Split `ClockTreeEvaluator` behind its facade. First move state ownership and feature alias conversion, then writeback/timing/metrics. Keep `ClockTreeEvaluator::evaluate()`, `outputSummary()`, `hasEvaluationResult()`, `writeStatistics()`, and `resetSummary()` intact.
5. Only after behavior is stable, adjust CMake visibility to remove unnecessary `PUBLIC` links. Rebuild after each target split because include-path and target-link errors are likely in this area.
6. Keep the log/report contract tests running after every split. Existing tests are better behavioral guards than visual inspection for this refactor.

## Caveats / Not Found

- `src/operation/iCTS/report/evaluation` was not found. The inspected equivalents are `src/operation/iCTS/source/flow/evaluation` and `src/operation/iCTS/source/flow/report`.
- No external web/docs research was performed. This artifact is based on local source, local CMake, local tests, and Trellis specs only.
- No production code was modified and no build/test commands were run for this research pass.
- Public API stability is inferred from local consumers (`ToolManager`, Tcl, Python, feature builder, tests). There may be out-of-tree consumers of `CTSAPI`, `CtsIO`, or Tcl/Python command names that are not visible in this checkout.
