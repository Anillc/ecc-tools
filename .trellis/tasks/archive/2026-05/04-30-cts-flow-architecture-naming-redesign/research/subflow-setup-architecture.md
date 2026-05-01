# Research: setup subflow architecture

- Query: Research the proposed `setup` subflow for task `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`; inspect current `run_setup`, `CTSAPI::init/reset`, config/wrapper/STA adapter initialization, and relevant tests/specs; determine setup responsibilities, exclusions, and whether `Setup.hh/.cc` alone is enough.
- Scope: internal
- Date: 2026-04-30

## Findings

### Files Found

- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md` - Current task requirements and accepted direction around `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md` - Existing architecture proposal; defines `setup` as runtime environment and requires one primary entry file per flow folder.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/current-cts-flow-code-map.md` - Prior local code map; identifies `run_setup` as config/workdir/logging/wrapper/STA readiness.
- `.trellis/spec/backend/directory-structure.md` - Authoritative iCTS placement rules; explicitly places run setup under `source/flow/run_setup/`.
- `.trellis/spec/backend/database-guidelines.md` - Singleton ownership, adapter boundaries, output directory rules, and readonly evaluation/report rules.
- `.trellis/spec/backend/quality-guidelines.md` - Naming, dependency visibility, and semantic-boundary rules relevant to a future rename to `setup/Setup.*`.
- `.trellis/spec/guides/cross-layer-thinking-guide.md` - Relevant because setup crosses API, config, wrapper, STA adapter, logger, and iDB boundaries.
- `.trellis/spec/guides/code-reuse-thinking-guide.md` - Relevant for deciding whether setup helper extraction or subfolders are justified.
- `src/operation/iCTS/api/CTSAPI.hh` - Public API surface exposing `runCTS`, `report`, `resetAPI`, `init`, and `outputSummary`.
- `src/operation/iCTS/api/CTSAPI.cc` - Implements API reset/init and delegates initialization to `CTSRunSetup`.
- `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.hh` - Current setup facade with `initialize()` and `emitRuntimeSetup()`.
- `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc` - Current setup implementation for config, runtime paths, schema/logging, wrapper, and STA adapter readiness.
- `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt` - Current setup target, `icts_source_flow_run_setup`, linked privately to `idm`, database, and utils.
- `src/operation/iCTS/source/flow/FlowManager.hh` - Flow state owner; includes runtime setup emission guard.
- `src/operation/iCTS/source/flow/FlowManager.cc` - Top-level lifecycle sequence and runtime setup emission delegation.
- `src/operation/iCTS/source/flow/CMakeLists.txt` - Existing flow subdirectory registration and `run_setup` linkage.
- `src/operation/iCTS/source/database/config/Config.hh` - Config singleton defaults and runtime path fields.
- `src/operation/iCTS/source/database/config/Config.cc` - Config `init()` and JSON parsing.
- `src/operation/iCTS/source/database/io/Wrapper.hh` - iDB adapter singleton interface and reset boundary.
- `src/operation/iCTS/source/database/io/Wrapper.cc` - iDB adapter initialization and design/layout readiness.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh` - STA adapter singleton interface and full-design/characterization entry points.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` - STA adapter base initialization.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.hh` - Internal STA adapter helper declarations.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc` - STA workspace, iDB adapter, Liberty, and readiness helpers.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc` - Full-design timing refresh/update entry points that depend on setup but do not belong to setup.
- `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.*` - Clock-data import stage after setup.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.*` - Clock-data import and netlist mutation helper; shows what setup should not absorb.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc` - Synthesis stage; uses wrapper DBU query after setup.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc` - iDB writeback/materialization boundary after synthesis.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc` - Evaluation boundary after writeback.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc` - Report boundary; resolves report output directories and may rebuild evaluation state.
- `src/operation/iCTS/test/flow/FlowManagerTest.cc` - Flow log-contract and reset summary tests.
- `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc` - Real-tech fixture setup pattern for config, iDB, wrapper, and STA adapter.
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc` - Test support that snapshots/restores config and reinitializes STA.
- `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc` - Benchmark fixture with config/STA/wrapper initialization.

### Current Setup Call Path

Current production initialization is:

```text
CTSAPI::init(config_file, work_dir)
  -> CTSAPI::resetAPI()
  -> CTSRunSetup::initialize(config_file, work_dir)
  -> FlowManager::outputRuntimeSetup()
       -> CTSRunSetup::emitRuntimeSetup()
```

The API surface makes `init()` and `resetAPI()` external lifecycle calls (`src/operation/iCTS/api/CTSAPI.hh:44`, `src/operation/iCTS/api/CTSAPI.hh:49`, `src/operation/iCTS/api/CTSAPI.hh:50`). `CTSAPI::init()` first resets all current singleton run state, then calls setup initialization, then emits runtime setup through `FlowManager` (`src/operation/iCTS/api/CTSAPI.cc:86`, `src/operation/iCTS/api/CTSAPI.cc:88`, `src/operation/iCTS/api/CTSAPI.cc:89`, `src/operation/iCTS/api/CTSAPI.cc:90`).

`CTSAPI::resetAPI()` currently resets `CONFIG_INST`, `DESIGN_INST`, `WRAPPER_INST`, `FLOW_MANAGER_INST`, and `SCHEMA_WRITER_INST` (`src/operation/iCTS/api/CTSAPI.cc:77`, `src/operation/iCTS/api/CTSAPI.cc:79`, `src/operation/iCTS/api/CTSAPI.cc:80`, `src/operation/iCTS/api/CTSAPI.cc:81`, `src/operation/iCTS/api/CTSAPI.cc:82`, `src/operation/iCTS/api/CTSAPI.cc:83`). This is broader than setup: it clears design and flow/evaluation state, not only runtime environment. In a future architecture, this reset can keep the public API name and delegate internals, but it should not be treated as a setup subflow responsibility unless the top-level flow design explicitly moves all lifecycle reset behind setup.

`FlowManager::outputRuntimeSetup()` owns an emission guard and delegates to setup report emission once (`src/operation/iCTS/source/flow/FlowManager.hh:72`, `src/operation/iCTS/source/flow/FlowManager.cc:127`, `src/operation/iCTS/source/flow/FlowManager.cc:129`, `src/operation/iCTS/source/flow/FlowManager.cc:132`, `src/operation/iCTS/source/flow/FlowManager.cc:134`). This is a reasonable split: flow owns "emit once during lifecycle"; setup owns the runtime setup content.

### Current Setup Responsibilities

The current setup facade is intentionally narrow: `CTSRunSetup.hh` exposes only `initialize()` and `emitRuntimeSetup()` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.hh:30`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.hh:35`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.hh:36`).

`CTSRunSetup::initialize()` performs the following production responsibilities:

- Load runtime CTS config from a config file via `CONFIG_INST.init(config_file)` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:53`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:57`).
- Resolve the run work directory from the explicit API argument or `CONFIG_INST.get_work_dir()` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:58`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:59`).
- Create the work directory and persist the resolved path back into config (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:44`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:47`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:48`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:60`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:61`).
- Derive `cts.log`, `visualization`, and `statistics` paths, create the output subdirectories, and persist those paths into config (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:63`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:64`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:65`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:66`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:67`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:68`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:69`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:70`).
- Open the schema/log writer for the run and record top-level run metadata (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:72`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:73`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:74`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:75`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:76`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:77`).
- Validate that `dmInst->get_idb_builder()` is available, then initialize `WRAPPER_INST` with that iDB builder (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:79`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:80`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:81`).
- Initialize the STA adapter base context (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:83`).

`CTSRunSetup::emitRuntimeSetup()` emits runtime setup diagnostics:

- Starts a `## Runtime Setup` section (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:86`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:88`).
- Emits runtime paths from config (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:89`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:90`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:91`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:92`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:93`).
- Emits config and configured wire-RC setup reports through `CONFIG_INST` and `STA_ADAPTER_INST` (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:95`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:96`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:97`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:98`).

This matches the backend spec, which says `source/flow/run_setup/` owns "config, work directory, logging, and adapter initialization" (`.trellis/spec/backend/directory-structure.md:45`, `.trellis/spec/backend/directory-structure.md:47`), while API remains stable external entry points (`.trellis/spec/backend/directory-structure.md:46`) and internal lifecycle steps belong under `source/flow` (`.trellis/spec/backend/directory-structure.md:61`).

### Config, Wrapper, and STA Boundaries

Setup coordinates config readiness but does not own config parsing rules. `Config::init()` resets the config and parses the JSON file (`src/operation/iCTS/source/database/config/Config.cc:227`, `src/operation/iCTS/source/database/config/Config.cc:229`, `src/operation/iCTS/source/database/config/Config.cc:230`). Config owns default runtime paths such as `work_dir`, `log_file`, `visualization_dir`, and `statistics_dir` (`src/operation/iCTS/source/database/config/Config.hh:80`, `src/operation/iCTS/source/database/config/Config.hh:81`, `src/operation/iCTS/source/database/config/Config.hh:82`, `src/operation/iCTS/source/database/config/Config.hh:83`) and exposes setters used by setup (`src/operation/iCTS/source/database/config/Config.hh:147`, `src/operation/iCTS/source/database/config/Config.hh:148`, `src/operation/iCTS/source/database/config/Config.hh:149`, `src/operation/iCTS/source/database/config/Config.hh:150`).

Setup coordinates iDB adapter readiness but does not own iDB read/write semantics. `Wrapper::init()` only stores the iDB builder and resolves design/layout pointers (`src/operation/iCTS/source/database/io/Wrapper.hh:78`, `src/operation/iCTS/source/database/io/Wrapper.hh:79`, `src/operation/iCTS/source/database/io/Wrapper.cc:154`, `src/operation/iCTS/source/database/io/Wrapper.cc:156`, `src/operation/iCTS/source/database/io/Wrapper.cc:157`, `src/operation/iCTS/source/database/io/Wrapper.cc:158`). The wrapper owns `read()`, `readClocks()`, `writeClock()`, and `writeClocks()` (`src/operation/iCTS/source/database/io/Wrapper.hh:105`, `src/operation/iCTS/source/database/io/Wrapper.hh:106`, `src/operation/iCTS/source/database/io/Wrapper.hh:107`, `src/operation/iCTS/source/database/io/Wrapper.hh:108`, `src/operation/iCTS/source/database/io/Wrapper.hh:109`, `src/operation/iCTS/source/database/io/Wrapper.hh:110`), and setup should not pull those data import/writeback operations into the setup subflow.

Setup coordinates STA base readiness but does not own STA adapter internals or timing refresh. `STAAdapter::init()` resets char state, creates or reuses a timing engine, installs the timing iDB adapter and Liberty base context if needed, configures the STA workspace, and resets transient STA state (`src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:44`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:47`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:48`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:49`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:50`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:51`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:55`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:56`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:60`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:61`). The workspace helper derives STA subdirectories from `CONFIG_INST.get_work_dir()` (`src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:156`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:158`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:162`), which is why setup must establish the work directory before calling `STA_ADAPTER_INST.init()`.

Full-design timing preparation is not setup. `STAAdapter::refreshFullDesignTimingContext()` requires `STAAdapter::init()` first, then converts iDB to timing netlist, reads SDC if available, builds the graph, initializes RC tree, and configures worst-path reporting (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:224`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:228`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:230`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:234`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:235`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:236`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:237`, `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:238`). That belongs near evaluation/timing refresh or adapter internals, not setup.

These boundaries match database spec rules: API/flow/database/adapter boundaries may read config when they own initialization or external-tool setup (`.trellis/spec/backend/database-guidelines.md:25`, `.trellis/spec/backend/database-guidelines.md:27`); iDB access stays inside `Wrapper` and iSTA access stays inside `STAAdapter` (`.trellis/spec/backend/database-guidelines.md:58`, `.trellis/spec/backend/database-guidelines.md:62`, `.trellis/spec/backend/database-guidelines.md:63`); output roots are derived from runtime work directories and rooted in `visualization_dir`/`statistics_dir` (`.trellis/spec/backend/database-guidelines.md:70`, `.trellis/spec/backend/database-guidelines.md:72`, `.trellis/spec/backend/database-guidelines.md:73`).

### What Belongs in `setup`

The proposed `setup/Setup.hh` and `setup/Setup.cc` should own exactly the runtime environment and readiness boundary:

- A primary setup class/facade such as `CTSSetup` or `Setup` with `initialize(config_file, work_dir)` and `emitRuntimeSetup()` matching the folder name rule.
- Config load orchestration through `CONFIG_INST.init()`, without moving JSON parsing out of `source/database/config`.
- Work directory resolution, creation, and propagation back into `CONFIG_INST`.
- `cts.log`/schema writer opening and run metadata.
- Derivation and creation of standard runtime output roots: `visualization_dir` and `statistics_dir`.
- Runtime setup diagnostics: runtime paths, config report, configured wire-RC report.
- Boundary validation for required global inputs such as `dmInst->get_idb_builder()`.
- Adapter readiness orchestration by calling `WRAPPER_INST.init(idb_builder)` and `STA_ADAPTER_INST.init()` in the correct order.

This also means `setup` is allowed to depend on `idm`, `icts_source_database`, and `icts_source_utils`, as the current setup target already does privately (`src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:3`, `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:6`, `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:7`, `src/operation/iCTS/source/flow/run_setup/CMakeLists.txt:8`). A future rename should preserve private target dependencies and avoid duplicating include-path wiring, matching quality rules (`.trellis/spec/backend/quality-guidelines.md:67`, `.trellis/spec/backend/quality-guidelines.md:69`, `.trellis/spec/backend/quality-guidelines.md:70`, `.trellis/spec/backend/quality-guidelines.md:73`).

### What Does Not Belong in `setup`

Setup should not absorb the clock-data import stage. `FlowManager::runCTS()` currently runs `readData()`, `run()`, `writeback()`, and `evaluate()` after setup (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`, `src/operation/iCTS/source/flow/FlowManager.cc:69`, `src/operation/iCTS/source/flow/FlowManager.cc:70`, `src/operation/iCTS/source/flow/FlowManager.cc:71`). `readData()` clears per-run flow state and then calls `CTSClockDataLoadStep::run()` (`src/operation/iCTS/source/flow/FlowManager.cc:87`, `src/operation/iCTS/source/flow/FlowManager.cc:89`, `src/operation/iCTS/source/flow/FlowManager.cc:90`, `src/operation/iCTS/source/flow/FlowManager.cc:91`, `src/operation/iCTS/source/flow/FlowManager.cc:92`, `src/operation/iCTS/source/flow/FlowManager.cc:93`, `src/operation/iCTS/source/flow/FlowManager.cc:94`). `CTSClockDataLoadStep::run()` emits input/clock data sections and calls `ClockNetEditor::readClockData()` (`src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:31`, `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:33`, `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:35`, `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:36`, `src/operation/iCTS/source/flow/stage/CTSClockDataLoadStep.cc:37`). That is input import, not runtime setup.

Setup should not decide clock nets or read clocks into `Design`. `ClockNetEditor::readClockData()` chooses between configured net-list pairs and `WRAPPER_INST.collectClockNetPairs()`, calls `WRAPPER_INST.readClocks()`, emits design clock distribution, and writes a read-data summary (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:300`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:305`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:311`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:317`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:318`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:320`). This belongs to an input/import stage or a future data-load subflow, not `setup`.

Setup should not synthesize trees. `CTSClockTreeSynthesisStep::run()` resets the clock-tree view, sets DBU from wrapper, iterates `DESIGN_INST.get_clocks()`, delegates each clock to `ClockTreeSynthesisDriver`, aggregates sink-domain counters, and marks synthesis complete (`src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:63`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:65`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:66`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:69`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:70`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:72`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:82`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:90`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:101`, `src/operation/iCTS/source/flow/stage/CTSClockTreeSynthesisStep.cc:109`). This belongs to `synthesis`.

Setup should not materialize/write final CTS data back to iDB. `CTSClockTreeWritebackStep::run()` starts a writeback stage, checks wrapper design readiness, calls `WRAPPER_INST.writeClocks()`, emits writeback summary, and marks runtime status (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:36`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:37`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:41`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:43`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:48`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:49`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:53`). This should map to the proposed `instantiation` or materialization layer, not setup.

Setup should not evaluate or report final CTS results. Evaluation is a separate stage over committed results (`src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:31`, `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:33`, `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:37`, `src/operation/iCTS/source/flow/stage/CTSClockTreeEvaluationStep.cc:38`), and report is a separate stage that resolves report-specific roots, may rebuild evaluation state, writes statistics, and emits SVG/GDS visualizations (`src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:75`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:79`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:83`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:84`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:85`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:98`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:100`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:103`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:105`, `src/operation/iCTS/source/flow/stage/CTSClockTreeReportStep.cc:106`). This follows the database spec's readonly evaluation/report boundary (`.trellis/spec/backend/database-guidelines.md:65`, `.trellis/spec/backend/database-guidelines.md:66`).

Setup should not become a database/config/wrapper/STA implementation folder. Database-layer placement remains `source/database/config`, `source/database/io`, and `source/database/adapter/sta` (`.trellis/spec/backend/database-guidelines.md:45`, `.trellis/spec/backend/database-guidelines.md:48`, `.trellis/spec/backend/database-guidelines.md:50`, `.trellis/spec/backend/database-guidelines.md:51`). Subfolders such as `setup/config`, `setup/wrapper`, or `setup/sta` would duplicate database ownership names and invite adapter logic to leak into flow.

### Tests and Fixture Signals

`FlowManagerTest.EmptyAPIRunEmitsConciseMainLogContract` verifies that `CTSAPI::runCTS()` emits input, synthesis, evaluation, runtime summary, run results, and stage-status blocks, and avoids deprecated wording (`src/operation/iCTS/test/flow/FlowManagerTest.cc:275`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:281`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:285`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:286`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:288`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:289`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:290`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:315`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:316`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:317`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:318`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:319`). This is a log contract for the main flow, not setup specifically, and reinforces keeping setup separate from read/synthesis/evaluation/report stage logs.

`FlowManagerTest.ResetAPIClearsEvaluationSummary` verifies that `CTSAPI::resetAPI()` clears the evaluation summary exposed by API output (`src/operation/iCTS/test/flow/FlowManagerTest.cc:574`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:582`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:583`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:584`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:586`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:587`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:588`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:589`, `src/operation/iCTS/test/flow/FlowManagerTest.cc:590`). This supports keeping reset as a lifecycle/API concern, because it resets evaluation/flow state that setup does not own.

Real-tech and benchmark fixtures repeat parts of setup, but for test-data loading rather than production flow architecture. `LoadRealTechAssets()` initializes config, reads LEF/DEF/Verilog through `dmInst`, configures data-manager paths, sets work dir, initializes wrapper, calls `WRAPPER_INST.read()`, and initializes STA (`src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:428`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:434`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:436`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:452`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:463`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:497`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:500`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:506`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:507`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:508`, `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:509`). A future test helper could reuse the setup entry for common runtime initialization, but fixture-specific technology/design loading and `WRAPPER_INST.read()` should not be moved into production `setup`.

`RealTechCharSession::restore()` restores a saved config state and reinitializes STA (`src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:201`, `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:207`, `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:208`). This confirms that STA adapter readiness depends on config/workdir state and may need repeated initialization in tests, but it does not justify moving STA internals into setup.

`FastClusteringRealTechBenchmarkSetup::LoadBenchmarkCase()` similarly initializes config/workdir, loads technology/design inputs, initializes STA, resets design, and initializes/reads wrapper (`src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:164`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:167`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:168`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:169`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:177`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:181`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:193`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:195`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:216`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:217`, `src/operation/iCTS/test/module/topology/fast_clustering/realtech/FastClusteringRealTechBenchmarkSetup.cc:218`). These fixture responsibilities are broader than production setup and should remain test-support code.

### Single Entry File vs Secondary Folders

Current `run_setup` has only three files: `CTSRunSetup.hh`, `CTSRunSetup.cc`, and `CMakeLists.txt`. The code volume is small: 39 lines in the header, 101 lines in the source, and 24 lines in CMake. The source has one private helper, `ensureDirectory()`, and two public operations (`src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:44`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:53`, `src/operation/iCTS/source/flow/run_setup/CTSRunSetup.cc:86`).

Therefore a single `setup/Setup.hh` and `setup/Setup.cc` entry pair is enough for the current responsibility set. `CMakeLists.txt` is the expected build-file exception. This exactly matches the task proposal's primary-entry rule: each folder should expose one readable primary entry, the folder root should contain only the primary entry pair plus CMake, small local helpers stay private in the main `.cc`, and subfolders are justified only when helpers grow into separately understandable responsibilities (`.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:74`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:91`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:94`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:103`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:105`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:109`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:110`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:111`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:112`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:114`).

No secondary setup folders are justified today. Splitting now would create artificial boundaries such as `setup/config`, `setup/logging`, `setup/adapter`, or `setup/paths`, each wrapping only a few lines while duplicating names already owned by `database/config`, `database/io`, `database/adapter/sta`, and `utils/logger`.

Secondary folders would become justified only if setup grows into multiple independent, multi-file responsibilities, for example:

- `environment/` or `runtime/`: if path resolution, directory policy, schema-log opening, workspace cleanup, and cross-platform filesystem behavior become large enough to need tests and helpers.
- `adapters/`: only if setup-level orchestration across Wrapper/STA/iDB readiness grows beyond simple calls, while concrete Wrapper and STA implementation remains in `source/database/io` and `source/database/adapter/sta`.
- `diagnostics/`: only if runtime setup reporting grows into multiple files or schemas beyond the current path/config/wire-RC tables.

Even then, secondary names should describe setup-level responsibilities, not database implementation areas. Avoid `setup/config`, `setup/wrapper`, and `setup/sta` because they imply ownership over implementations that the database layer already owns.

### Recommended Target Shape

For the proposed architecture, use:

```text
src/operation/iCTS/source/flow/setup/
  CMakeLists.txt
  Setup.hh
  Setup.cc
```

Recommended class name: `CTSSetup` if the project wants CTS-explicit class names, or `Setup` only if the surrounding architecture uses folder-local short names. The existing codebase uses CTS-specific facades (`CTSAPI`, `CTSRunSetup`, `CTSClockTreeSynthesisStep`, `CTSClockTreeReportStep`), so `CTSSetup` is the safer class name while `Setup.hh/.cc` satisfies the primary-entry file rule.

Recommended public operations:

```cpp
class CTSSetup
{
 public:
  CTSSetup() = delete;

  static auto initialize(const std::string& config_file, const std::string& work_dir) -> void;
  static auto emitRuntimeSetup() -> void;
};
```

Do not add setup-owned state unless the lifecycle design requires it. The current `FlowManager` emission guard is already enough for "emit once" semantics (`src/operation/iCTS/source/flow/FlowManager.hh:72`, `src/operation/iCTS/source/flow/FlowManager.cc:127`, `src/operation/iCTS/source/flow/FlowManager.cc:129`, `src/operation/iCTS/source/flow/FlowManager.cc:132`).

### Architecture Answer

Setup is a run-level readiness subflow, not a clock-data, synthesis, instantiation, evaluation, or report subflow. Its cohesive responsibility is:

```text
config file + work/log/report roots + schema/log open + wrapper base readiness + STA base readiness + setup diagnostics
```

It should stop before:

```text
clock-net discovery/read -> per-clock synthesis -> committed object materialization/iDB writeback -> timing refresh/evaluation -> statistics/visualization report
```

A single `Setup.hh/.cc` entry is enough for the current code. No secondary setup folders are justified by current code volume or responsibility boundaries.

## External References

- No new external references were needed for this setup-specific research. The question is governed by local code and Trellis specs.
- Existing task research files already cover broader industry CTS terminology: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/open-source-cts-comparison.md`.

## Related Specs

- `.trellis/spec/backend/directory-structure.md:37` - Defines the CTS flow framework.
- `.trellis/spec/backend/directory-structure.md:45` - Defines current flow placement boundaries.
- `.trellis/spec/backend/directory-structure.md:47` - Assigns run setup to config/workdir/logging/adapter initialization.
- `.trellis/spec/backend/directory-structure.md:61` - Keeps internal CTS lifecycle under `source/flow`, not `api/CTSAPI`.
- `.trellis/spec/backend/directory-structure.md:62` - Allows runtime config access at API/flow/database/test boundaries.
- `.trellis/spec/backend/database-guidelines.md:14` - Defines existing singleton roles.
- `.trellis/spec/backend/database-guidelines.md:27` - Allows config reads at initialization/external-tool setup boundaries.
- `.trellis/spec/backend/database-guidelines.md:60` - Requires critical singleton state validation at initialization boundaries.
- `.trellis/spec/backend/database-guidelines.md:62` - Keeps iDB access inside `Wrapper`.
- `.trellis/spec/backend/database-guidelines.md:63` - Keeps iSTA access inside `STAAdapter`.
- `.trellis/spec/backend/database-guidelines.md:70` - Defines output directory ownership.
- `.trellis/spec/backend/quality-guidelines.md:31` - Requires CTS semantic names and boundaries.
- `.trellis/spec/backend/quality-guidelines.md:67` - Requires dependency visibility through targets.

## Caveats / Not Found

- No dedicated `run_setup` or `Setup` test directory exists today. Setup behavior is covered indirectly through API/flow tests and real-tech fixture setup patterns.
- `CTSAPI::resetAPI()` does not reset `STA_ADAPTER_INST`; no STA adapter reset API was found in the inspected files. This may be intentional because `STAAdapter::init()` resets adapter transient state, but it is a lifecycle caveat if future setup reset semantics are formalized.
- Test fixtures sometimes call `WRAPPER_INST.read()` as part of real-tech setup. That is fixture-specific design loading and should not be used as evidence that production `setup` should import clock/design data.
- Current directory name is `run_setup/` and class name is `CTSRunSetup`; the proposed architecture can rename the folder/file to `setup/Setup.*`, but behavior should remain the same unless a separate migration task changes lifecycle semantics.
