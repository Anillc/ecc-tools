# Research: database-netlist-build

- Query: Research the current iCTS database/netlist/adapter/build/test surface for the CTS engineering refactor. Inspect `src/operation/iCTS/source/database`, `source/flow/netlist`, STA/Wrapper adapters, CMakeLists, README/tests, and the final iEDA validation command path. Identify external adapter contracts, singleton/reset ordering, CMake risks, test/build commands, and compatibility risks.
- Scope: internal
- Date: 2026-04-28

## Findings

### Files Found

- `.trellis/tasks/04-28-cts-engineering-refactor/prd.md`: CTS engineering refactor requirements, public API compatibility constraints, validation commands, and out-of-scope adapter replacement notes.
- `.trellis/spec/backend/index.md`: backend spec entry point for `src/operation/iCTS/`.
- `.trellis/spec/backend/directory-structure.md`: API/source/test layer boundaries, source categories, and hierarchical CMake rules.
- `.trellis/spec/backend/database-guidelines.md`: singleton roles, ownership rules, and external adapter boundaries.
- `.trellis/spec/backend/quality-guidelines.md`: naming, include, dependency visibility, and final validation guidance.
- `.trellis/spec/project-constraints.md`: hard iCTS file, logging, CMake, exception, and validation constraints.
- `.trellis/spec/guides/cross-layer-thinking-guide.md`: cross-layer questions for Wrapper/STAAdapter/data ownership changes.
- `.trellis/spec/guides/code-reuse-thinking-guide.md`: reuse and CMake target-link guidance for helper extraction.
- `src/operation/iCTS/api/CTSAPI.hh`: public CTS singleton facade and external call shapes.
- `src/operation/iCTS/api/CTSAPI.cc`: API lifecycle implementation and delegation to `FlowManager`, `Wrapper`, and `STAAdapter`.
- `src/operation/iCTS/source/flow/FlowManager.cc`: read-data, synthesis, evaluation, report, runtime, and summary orchestration.
- `src/operation/iCTS/source/flow/netlist/ClockNetManager.hh`: static netlist mutation API used by flow code.
- `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc`: clock-net discovery, root-buffer insertion helpers, net reconnection, and inserted-object commit logic.
- `src/operation/iCTS/source/database/design/Design.hh`: `DESIGN_INST` and final ownership containers for clocks, insts, pins, and nets.
- `src/operation/iCTS/source/database/design/Design.cc`: design reset, commit, remove, and clock-membership cleanup behavior.
- `src/operation/iCTS/source/database/design/Clock.hh`: per-clock borrowed pointers for source/load/member views.
- `src/operation/iCTS/source/database/design/Pin.hh`: pin borrowed `Inst*` and `Net*` topology edges.
- `src/operation/iCTS/source/database/design/Net.hh`: net borrowed driver/load topology edges.
- `src/operation/iCTS/source/database/io/Wrapper.hh`: `WRAPPER_INST`, iDB pointer storage, iDB/CTS cross-reference maps, and read/writeback API.
- `src/operation/iCTS/source/database/io/Wrapper.cc`: iDB initialization, clock collection, iDB-to-CTS read, CTS-to-iDB writeback.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh`: `STA_ADAPTER_INST` and STA/characterization/timing/RC API.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc`: STA adapter initialization and char-only state reset behavior.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc`: timing-engine creation, iDB adapter installation, Liberty and SDC loading helpers.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc`: full-design STA refresh, timing update/report, transient-state reset.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc`: exact CTS route-tree installation into STA RC trees.
- `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc`: iDB writeback, STA refresh, RC install, timing update, and statistics/report production.
- `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp`: tool-manager CTS IO bridge that calls `CTS_API_INST.init()` and `runCTS()`.
- `src/platform/tool_manager/tool_manager.cpp`: platform tool-manager `autoRunCTS()` and `reportCTS()` delegation.
- `src/interface/tcl/tcl_icts/tcl_cts.cpp`: Tcl `run_cts` and `cts_report` command behavior.
- `src/interface/tcl/tcl_icts/tcl_register_cts.h`: Tcl command registration for CTS commands.
- `src/interface/python/py_icts/py_icts.cpp`: Python CTS wrappers.
- `src/feature/builder/feature_builder_tool.cpp`: feature summary caller for `CTS_API_INST.outputSummary()`.
- `src/operation/iCTS/CMakeLists.txt`: top-level iCTS CMake entry point.
- `src/operation/iCTS/source/CMakeLists.txt`: `icts_source` interface aggregator.
- `src/operation/iCTS/source/database/CMakeLists.txt`: database submodule and interface aggregator wiring.
- `src/operation/iCTS/source/database/adapter/sta/CMakeLists.txt`: STA adapter source list and dependency wiring.
- `src/operation/iCTS/source/database/io/CMakeLists.txt`: Wrapper target and iDB/STA/design/utils links.
- `src/operation/iCTS/source/flow/CMakeLists.txt`: flow submodule targets and `icts_source_flow` links.
- `src/operation/iCTS/source/flow/netlist/CMakeLists.txt`: ClockNetManager target wiring.
- `src/operation/iCTS/api/CMakeLists.txt`: `icts_api` target wiring.
- `src/operation/iCTS/test/CMakeLists.txt`: base test targets and `icts_add_test_executable()` registration helper.
- `src/operation/iCTS/test/README.md`: test layout, real-tech fallback behavior, and artifact directories.
- `src/operation/iCTS/README.md`: empty in this workspace.
- `build.sh`: normal configure/build entry point and optional hello smoke test.
- `README.md`: repository-level source build example.
- `scripts/design/ics55_dev`: requested final validation working directory. It currently contains only the `iEDA` binary.
- `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl`: available ICS55 CTS script with current `run_cts`/save/report shape.
- `scripts/design/ics55_gcd/run_iEDA.sh`: available ICS55 full-flow script that includes `iCTS_script/run_iCTS.tcl`.

### Related Specs

- Backend spec applies to `src/operation/iCTS/`; the index names Project Constraints, Directory Structure, Database Guidelines, Logging, Error Handling, Quality, and the checker workflow as authority docs (`.trellis/spec/backend/index.md:1`, `.trellis/spec/backend/index.md:12`).
- Directory rules define three layers: `api/` external entry points, `source/` internal implementation, and `test/` mirrored tests; API may depend on Source, Test may depend on API/Source, and Source must not depend on API (`.trellis/spec/backend/directory-structure.md:13`, `.trellis/spec/backend/directory-structure.md:21`).
- Source categories are `database/`, `utils/`, `module/`, and `flow/`; runtime lifecycle belongs in `source/flow`, while `api/CTSAPI` stays focused on external entry points (`.trellis/spec/backend/directory-structure.md:28`, `.trellis/spec/backend/directory-structure.md:51`).
- CMake rules require new `.cc` files to be added to the module CMakeLists, headers exposed by the module target, parent `add_subdirectory()` wiring for new modules, and nearest-target linking (`.trellis/spec/backend/directory-structure.md:69`, `.trellis/spec/backend/directory-structure.md:77`).
- Database singleton roles are explicitly documented: `CTS_API_INST`, `DESIGN_INST`, `CONFIG_INST`, `WRAPPER_INST`, `STA_ADAPTER_INST`, and `LOG_INST` (`.trellis/spec/backend/database-guidelines.md:14`).
- Database ownership rules state that `Design` owns final `Clock`, `Inst`, `Pin`, and `Net` objects through `std::unique_ptr`; `Clock`, `Inst`, `Net`, `Pin`, and Wrapper maps hold borrowed views and must not cache borrowed pointers across owner reset boundaries (`.trellis/spec/backend/database-guidelines.md:31`, `.trellis/spec/backend/database-guidelines.md:42`).
- Adapter boundaries are explicit: keep iDB access inside `Wrapper`, keep iSTA access inside `STAAdapter`, and keep module code operating on CTS types rather than external-tool types (`.trellis/spec/backend/database-guidelines.md:58`).
- Dependency rules require `target_link_libraries`, default `PRIVATE`, `PUBLIC` only for public-header dependencies, `INTERFACE` for header-only libraries, and existing-target reuse instead of duplicated include paths (`.trellis/spec/backend/quality-guidelines.md:57`).
- Validation rules say not to run `ecc_dev_tools` during the normal edit/build/test loop and reserve it for the final finish-work pass (`.trellis/spec/backend/quality-guidelines.md:73`, `.trellis/spec/backend/quality-guidelines.md:83`; `.trellis/spec/project-constraints.md:87`).
- Project constraints require `.hh`/`.cc`, PascalCase filenames, `#pragma once`, no exceptions, `LOG_*`, schema/report helpers, and CMake updates before implementing new files or modules (`.trellis/spec/project-constraints.md:18`, `.trellis/spec/project-constraints.md:60`).
- Cross-layer guide is directly relevant because this refactor touches Wrapper/STAAdapter, ownership, and initialization order (`.trellis/spec/guides/cross-layer-thinking-guide.md:12`, `.trellis/spec/guides/cross-layer-thinking-guide.md:44`).
- Code-reuse guide is relevant before extracting helpers or changing CMake because it asks implementers to search existing utility/module patterns and existing targets first (`.trellis/spec/guides/code-reuse-thinking-guide.md:10`, `.trellis/spec/guides/code-reuse-thinking-guide.md:27`).

### PRD Constraints

- Goal is an engineering refactor without changing public CTS interface or behavior; `CTSAPI` remains the external singleton facade and Tcl/Python/tool-manager/feature-summary callers must keep compiling and behaving the same (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:3`).
- User-confirmed constraints include unchanged external interfaces, singleton `CTSAPI`, no `ecc_dev_tools` during the refactor loop, unified quality checks only after convergence, and the final binary validation command (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:11`).
- Required public symbols/call shapes include `CTS_API_INST`, `CTSAPI::getInst()`, `CTSAPI::init`, `CTSAPI::runCTS`, `CTSAPI::report`, `CTSAPI::resetAPI`, and `CTSAPI::outputSummary` (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:24`).
- Required flow behavior includes initialization reset of `Config`, `Design`, `Wrapper`, `FlowManager`, and `SchemaWriter`; path resolution; iDB wrapper and STA adapter setup; read-data/synthesis/evaluation/runtime/report sequencing; report reuse/rebuild behavior; and default summary behavior before evaluation (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:37`).
- Required final netlist semantics include config-or-wrapper clock discovery, hard macro and regular sink grouping, root buffer/downstream net per non-empty group, source-to-root connection, commit-after-success, and rollback/cleanup on failed per-clock work (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:44`).
- Out of scope includes changing public CTS API names/signatures/singleton macro, rewriting algorithms, touching unrelated modules, iterative `ecc_dev_tools` repair, replacing iDB/iSTA adapter contracts, and changing default runtime configuration semantics (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:132`).
- PRD already notes that `scripts/design/ics55_dev` currently contains the `iEDA` binary and the requested `./script/iCTS_script/run_iCTS_dev.tcl` path must be checked during final binary validation (`.trellis/tasks/04-28-cts-engineering-refactor/prd.md:141`, `.trellis/tasks/04-28-cts-engineering-refactor/prd.md:149`).

### External API and Caller Contracts

- `CTSAPI.hh` defines `CTS_API_INST` as `(icts::CTSAPI::getInst())` (`src/operation/iCTS/api/CTSAPI.hh:33`).
- Public API call shapes are static `runCTS()`, `report(const std::string&)`, `resetAPI()`, `init(const std::string&, const std::string& = "")`, and `outputSummary()` (`src/operation/iCTS/api/CTSAPI.hh:44`).
- `CTSAPI::runCTS()` delegates directly to `FLOW_MANAGER_INST.runCTS()` (`src/operation/iCTS/api/CTSAPI.cc:75`).
- `CTSAPI::report()` delegates directly to `FLOW_MANAGER_INST.report(save_dir)` (`src/operation/iCTS/api/CTSAPI.cc:80`).
- `CTSAPI::outputSummary()` returns a feature summary built from `FLOW_MANAGER_INST.outputSummary()` (`src/operation/iCTS/api/CTSAPI.cc:136`).
- Tool-manager CTS IO includes `iCTS/api/CTSAPI.hh`, resolves an empty config path through `flowConfigInst->get_icts_path()`, sets the status stage, calls `CTS_API_INST.init(config, work_dir)`, then calls `CTS_API_INST.runCTS()` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:22`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:33`, `src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:44`).
- Tool-manager `reportCTS()` defaults an empty path through `flowConfigInst->get_icts_path()` and calls `CTS_API_INST.report(path)` (`src/platform/tool_manager/tool_api/icts_io/icts_io.cpp:53`).
- Platform `ToolManager::autoRunCTS()` and `ToolManager::reportCTS()` delegate to `ctsInst->runCTS(config, work_dir)` and `ctsInst->reportCTS(path)` (`src/platform/tool_manager/tool_manager.cpp:270`).
- Tcl `run_cts` command accepts `-config` and `-work_dir`, then calls `iplf::tmInst->autoRunCTS(config_path)` or `autoRunCTS(config_path, dir_path)` depending on whether work dir is present (`src/interface/tcl/tcl_icts/tcl_cts.cpp:24`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:41`).
- Tcl `cts_report` can call the tool manager by name or call `CTS_API_INST.report(str_path)` directly by path (`src/interface/tcl/tcl_icts/tcl_cts.cpp:86`, `src/interface/tcl/tcl_icts/tcl_cts.cpp:100`).
- Tcl registration preserves command names `run_cts`, `cts_report`, `cts_save_tree`, and `cts_config` (`src/interface/tcl/tcl_icts/tcl_register_cts.h:34`).
- Python `CtsAutoRun()` delegates to `iplf::tmInst->autoRunCTS(cts_config, cts_work_dir)`, while `CtsReport()` calls `CTS_API_INST.report(path)` (`src/interface/python/py_icts/py_icts.cpp:23`, `src/interface/python/py_icts/py_icts.cpp:29`).
- Feature summary `FeatureBuilder::buildCTSSummary()` calls `CTS_API_INST.outputSummary()` (`src/feature/builder/feature_builder_tool.cpp:57`).

### Singleton and Reset Ordering

- Current API reset order is `CONFIG_INST.reset()`, `DESIGN_INST.reset()`, `WRAPPER_INST.reset()`, `FLOW_MANAGER_INST.reset()`, then `SCHEMA_WRITER_INST.reset()` (`src/operation/iCTS/api/CTSAPI.cc:85`).
- `CTSAPI::resetAPI()` does not explicitly destroy or reset the STA timing engine. STA state is handled by `STAAdapter::init()` and STAAdapter-specific reset helpers, so adding a hard STA reset to `resetAPI()` would be a behavior change unless intentionally designed and tested (`src/operation/iCTS/api/CTSAPI.cc:85`; `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:44`).
- `CTSAPI::init()` calls `resetAPI()`, initializes config, resolves/creates work dir, creates `cts.log`, `cts.gds`, and `output/`, opens `SCHEMA_WRITER_INST`, gets `dmInst->get_idb_builder()`, initializes `WRAPPER_INST`, initializes `STA_ADAPTER_INST`, and emits runtime setup through `FLOW_MANAGER_INST.outputRuntimeSetup()` (`src/operation/iCTS/api/CTSAPI.cc:94`, `src/operation/iCTS/api/CTSAPI.cc:117`, `src/operation/iCTS/api/CTSAPI.cc:124`, `src/operation/iCTS/api/CTSAPI.cc:129`, `src/operation/iCTS/api/CTSAPI.cc:132`).
- `FlowManager::runCTS()` resets runtime metrics, starts a CTS stage, then runs `readData()`, `run()`, and `evaluate()` in that order before runtime summary and key-result emission (`src/operation/iCTS/source/flow/FlowManager.cc:420`).
- `FlowManager::readData()` resets `_run_summary`, clears `_evaluation_ready`, resets evaluator summary, and calls `ClockNetManager::readClockData()` (`src/operation/iCTS/source/flow/FlowManager.cc:444`).
- `FlowManager::run()` resets evaluator summary and run summary again, clears `_evaluation_ready`, obtains `DESIGN_INST.get_clocks()`, loops per clock, and records successful/skipped/failed clock and sink-group counts (`src/operation/iCTS/source/flow/FlowManager.cc:458`, `src/operation/iCTS/source/flow/FlowManager.cc:477`, `src/operation/iCTS/source/flow/FlowManager.cc:495`).
- `FlowManager::evaluate()` calls `ClockTreeEvaluator::evaluate()` and sets `_evaluation_ready` based on `ClockTreeEvaluator::hasEvaluationResult()` (`src/operation/iCTS/source/flow/FlowManager.cc:518`).
- `FlowManager::report()` requires an initialized work dir, reuses evaluation state when `_evaluation_ready` and evaluator result are present, otherwise runs `evaluate()` before writing statistics and visualization artifacts (`src/operation/iCTS/source/flow/FlowManager.cc:535`, `src/operation/iCTS/source/flow/FlowManager.cc:542`, `src/operation/iCTS/source/flow/FlowManager.cc:552`).

### Database Ownership and Borrowed Pointer Surface

- `DESIGN_INST` is the design singleton macro (`src/operation/iCTS/source/database/design/Design.hh:39`).
- `Design` owns final CTS objects through `std::vector<std::unique_ptr<Clock>>`, `std::vector<std::unique_ptr<Inst>>`, `std::vector<std::unique_ptr<Pin>>`, and `std::vector<std::unique_ptr<Net>>` (`src/operation/iCTS/source/database/design/Design.hh:93`).
- `Design::reset()` clears clocks and topology objects (`src/operation/iCTS/source/database/design/Design.cc:101`).
- `Clock` stores borrowed source/load/member views: `Pin*` clock source, `Net*` source net, `std::vector<Pin*>` loads, `std::vector<Inst*>` insts, and `std::vector<Net*>` nets (`src/operation/iCTS/source/database/design/Clock.hh:45`, `src/operation/iCTS/source/database/design/Clock.hh:79`).
- `Pin` stores borrowed `Inst*` and `Net*` references; setters mutate those topology edges (`src/operation/iCTS/source/database/design/Pin.hh:58`, `src/operation/iCTS/source/database/design/Pin.hh:64`, `src/operation/iCTS/source/database/design/Pin.hh:76`).
- `Net` stores borrowed driver/load pin references (`src/operation/iCTS/source/database/design/Net.hh:42`, `src/operation/iCTS/source/database/design/Net.hh:47`, `src/operation/iCTS/source/database/design/Net.hh:61`).
- `Design::removeClockMembershipObjects()` removes clock member nets except the clock source net, then removes member insts (`src/operation/iCTS/source/database/design/Design.cc:438`).
- Main risk: no component should cache CTS raw pointers across `Design::reset()`, `Design::clearClocks()`, `Design::clearTopologyObjects()`, `Wrapper::readClocks()`, or `ClockNetManager` rollback/commit boundaries.

### Wrapper Adapter Contract

- `WRAPPER_INST` is the iDB adapter singleton macro (`src/operation/iCTS/source/database/io/Wrapper.hh:46`).
- `Wrapper::reset()` nulls `_idb`, `_idb_design`, `_idb_layout` and clears all CTS/iDB cross-reference maps (`src/operation/iCTS/source/database/io/Wrapper.hh:71`).
- Wrapper public API includes `init(idb::IdbBuilder*)`, readiness checks, `queryDbUnit()`, `isClockNet()`, `collectClockNetPairs()`, `read()`, `readClocks()`, `writeClock()`, `writeClocks()`, and `withinCore()` (`src/operation/iCTS/source/database/io/Wrapper.hh:68`, `src/operation/iCTS/source/database/io/Wrapper.hh:85`, `src/operation/iCTS/source/database/io/Wrapper.hh:95`).
- Wrapper owns no iDB or CTS topology. It stores borrowed `_idb`, `_idb_design`, `_idb_layout`, and borrowed cross-reference maps between CTS pointers and iDB pointers (`src/operation/iCTS/source/database/io/Wrapper.hh:139`, `src/operation/iCTS/source/database/io/Wrapper.hh:143`).
- `Wrapper::init()` stores the passed `IdbBuilder*`, DEF design, and LEF layout (`src/operation/iCTS/source/database/io/Wrapper.cc:127`).
- `Wrapper::collectClockNetPairs()` checks iDB/idm net-list readiness and iterates `dmInst->getClockNetList()` to produce `(net_name, net_name)` clock/net pairs (`src/operation/iCTS/source/database/io/Wrapper.cc:173`, `src/operation/iCTS/source/database/io/Wrapper.cc:181`, `src/operation/iCTS/source/database/io/Wrapper.cc:186`).
- `Wrapper::readClocks()` validates iDB builder, DEF service, iDB design, and net list, then clears all cross-reference maps and clears `Design` clocks/topology before converting clock nets into CTS objects (`src/operation/iCTS/source/database/io/Wrapper.cc:214`, `src/operation/iCTS/source/database/io/Wrapper.cc:237`, `src/operation/iCTS/source/database/io/Wrapper.cc:243`).
- `Wrapper::writeClock()` requires a ready iDB design, creates/reuses iDB insts, ensures iDB nets, rewrites net pins, and writes source/member nets for a clock (`src/operation/iCTS/source/database/io/Wrapper.cc:606`, `src/operation/iCTS/source/database/io/Wrapper.cc:623`, `src/operation/iCTS/source/database/io/Wrapper.cc:634`).
- `Wrapper::writeClocks()` folds `writeClock()` over all clocks and returns aggregate success (`src/operation/iCTS/source/database/io/Wrapper.cc:645`).
- Contract risk: moving iDB logic outside Wrapper violates specs and can leak external-tool types into flow/module code.

### STAAdapter Contract

- `STA_ADAPTER_INST` is the STA adapter singleton macro (`src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:61`).
- STAAdapter public static surface includes full-design timing update/report/refresh, propagated clock setup, exact RC-tree installation, wire RC queries, cell/pin queries, and char-only timing/power helpers (`src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:124`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:132`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:143`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:145`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh:149`).
- `STAAdapter::init()` resets char timing/power state and char-only mode. It destroys/recreates Power and TimingEngine, installs the iDB adapter, and loads Liberty only when base STA context is absent; otherwise it keeps the existing base context and resets transient state (`src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:44`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:51`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:59`).
- `STAAdapter::initCharOnly()` finishes previous char-only mode if already active, initializes base context if absent, sets char-only mode, switches workspace to `sta_char`, and resets transient state (`src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:65`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:68`, `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:78`).
- `sta_adapter_internal::GetStaEngine()` uses `ista::TimingEngine::getOrCreateTimingEngine()` (`src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:104`).
- `InstallTimingIDBAdapter()` constructs `ista::TimingIDBAdapter`, sets its iDB builder from `dmInst->get_idb_builder()`, and installs it on the timing engine (`src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:177`).
- `LoadConfiguredLiberty()` reads configured Liberty paths and links Liberty cells once for lightweight cell queries (`src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:184`).
- `LoadConfiguredSdcIfPresent()` warns and skips on empty/missing SDC, otherwise reads SDC (`src/operation/iCTS/source/database/adapter/sta/STAAdapterInternal.cc:193`).
- `STAAdapter::refreshFullDesignTimingContext()` requires `STAAdapter::init()` first, exits char-only mode, resets transient state, converts iDB to timing netlist, loads SDC, builds the graph, and initializes RC trees (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:224`).
- `STAAdapter::updateTiming()` fatals if char-only mode is active or if full-design context is not ready; the message explicitly requires `refreshFullDesignTimingContext()` after iDB writeback (`src/operation/iCTS/source/database/adapter/sta/STAAdapterTimingUpdate.cc:199`).
- `STAAdapter::installClockNetRcTree()` installs exact RC trees into STA from CTS nets and route trees (`src/operation/iCTS/source/database/adapter/sta/STAAdapterRcTree.cc:159`).
- Contract risk: full-design STA refresh must occur after Wrapper has written CTS results into iDB. Refreshing before writeback produces stale timing netlists.

### Netlist Mutation Surface

- `ClockNetManager` is a static helper boundary for clock data read, sink partitioning, root-buffer insertion, net reconnection, source-net routing, and commit of inserted algorithm objects (`src/operation/iCTS/source/flow/netlist/ClockNetManager.hh:38`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.hh:43`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.hh:51`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.hh:57`).
- `ClockNetManager::readClockData()` reads clock/net pairs from `CONFIG_INST.get_net_list()` when `CONFIG_INST.is_use_netlist()` is true; otherwise it uses `WRAPPER_INST.collectClockNetPairs()`, then calls `WRAPPER_INST.readClocks(clock_net_pairs)` (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:284`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:289`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:295`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:301`).
- `addRootBufferForSinkGroup()` resolves a minimum-drive root buffer and creates an inserted root-buffer inst/pins at the sink-group location (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:338`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:354`).
- `reconnectNet()` clears the old driver back-pointer when it differs from the new driver and rewrites topology edges for driver/loads (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:363`).
- `commitInsertedObjects()` validates duplicate and preexisting inst/pin/net names before committing temporary algorithm objects into `DESIGN_INST` and adding committed insts/nets to the clock membership view (`src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:428`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:441`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:462`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:479`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:490`, `src/operation/iCTS/source/flow/netlist/ClockNetManager.cc:514`).
- Compatibility risk: `commitInsertedObjects()` validates first but then commits insts, pins, and nets in sequence. A rare failure after partial commits could leave existing behavior with partial final objects. A refactor should not silently change this failure mode unless it adds explicit rollback tests.

### Evaluation and Writeback Contract

- `ClockTreeEvaluator::evaluate()` clears latest summary/statistics, gets clocks from `DESIGN_INST`, records DBU from `WRAPPER_INST.queryDbUnit()`, and writes CTS clocks back to iDB through `WRAPPER_INST.writeClocks(clocks)` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:451`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:458`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:459`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:460`).
- STA refresh is conditional on wrapper readiness and successful iDB writeback; then evaluator calls `STA_ADAPTER_INST.refreshFullDesignTimingContext()` and `setPropagatedClocks()` (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:461`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:463`).
- Evaluator walks final CTS insts/nets, accumulates cell and wire stats, installs/measures RC trees when STA was refreshed, updates timing, reports timing, appends latency/skew/timing data, marks statistics/result valid, emits evaluation summary, and writes reports under the work dir (`src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:468`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:492`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:508`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:518`, `src/operation/iCTS/source/flow/evaluation/ClockTreeEvaluator.cc:521`).
- Ordering risk: the writeback and STA refresh sequence is a behavioral contract: final CTS objects live in `Design`, then `Wrapper` writes iDB, then `STAAdapter` refreshes from iDB, then timing/RC/statistics are produced.

### CMake Surface and Risks

- Top-level iCTS CMake adds `external_libs`, `source`, `api`, and `test` subdirectories (`src/operation/iCTS/CMakeLists.txt:44`).
- `icts_source` is an `INTERFACE` aggregator over database, utils, flow, and module targets, with `${ICTS_SOURCE}` as an interface include dir (`src/operation/iCTS/source/CMakeLists.txt:11`, `src/operation/iCTS/source/CMakeLists.txt:13`, `src/operation/iCTS/source/CMakeLists.txt:22`).
- Database CMake adds adapter, config, design, io, spatial, routing, timing, and characterization subdirs, then defines `icts_source_database` as an `INTERFACE` aggregator over those targets and include dirs (`src/operation/iCTS/source/database/CMakeLists.txt:10`, `src/operation/iCTS/source/database/CMakeLists.txt:20`, `src/operation/iCTS/source/database/CMakeLists.txt:22`, `src/operation/iCTS/source/database/CMakeLists.txt:35`).
- STA adapter CMake lists every split STAAdapter source in one real library target and links logger publicly, while design/config/utils/power/external libs are private (`src/operation/iCTS/source/database/adapter/sta/CMakeLists.txt:1`, `src/operation/iCTS/source/database/adapter/sta/CMakeLists.txt:27`).
- Wrapper CMake builds `icts_source_database_io` from `Wrapper.cc`, exposes database/io includes publicly, and privately links `idm`, `icts_source_database_adapter_sta`, design, and utils (`src/operation/iCTS/source/database/io/CMakeLists.txt:1`, `src/operation/iCTS/source/database/io/CMakeLists.txt:3`, `src/operation/iCTS/source/database/io/CMakeLists.txt:15`).
- Flow CMake adds evaluation, htree, netlist, report, and synthesis subdirs; `icts_source_flow` builds `FlowManager.cc`, links evaluation publicly, and links database/module/htree/netlist/report/synthesis/utils privately (`src/operation/iCTS/source/flow/CMakeLists.txt:7`, `src/operation/iCTS/source/flow/CMakeLists.txt:13`, `src/operation/iCTS/source/flow/CMakeLists.txt:15`).
- Netlist CMake builds `icts_source_flow_netlist` from `ClockNetManager.cc`, privately links `icts_source_database`, design, and utils, and publicly exposes `${ICTS_FLOW}` (`src/operation/iCTS/source/flow/netlist/CMakeLists.txt:1`, `src/operation/iCTS/source/flow/netlist/CMakeLists.txt:3`, `src/operation/iCTS/source/flow/netlist/CMakeLists.txt:11`).
- API CMake builds `icts_api` from `CTSAPI.cc`, exposes `${ICTS_API}`, and privately links `icts_source` and external libs (`src/operation/iCTS/api/CMakeLists.txt:1`, `src/operation/iCTS/api/CMakeLists.txt:5`).
- Test CMake defines `icts_test_base` as an `INTERFACE` target linking log, source, api, and test external libs; `icts_test_realtech_base` links `idm`; `icts_add_test_executable()` creates an executable and registers it with CTest (`src/operation/iCTS/test/CMakeLists.txt:3`, `src/operation/iCTS/test/CMakeLists.txt:20`, `src/operation/iCTS/test/CMakeLists.txt:34`, `src/operation/iCTS/test/CMakeLists.txt:47`).
- `ICTS_BUILD_SLOW_REALTECH_TESTS` defaults OFF, so slow real-tech regression targets are excluded unless opted in (`src/operation/iCTS/test/CMakeLists.txt:32`; `src/operation/iCTS/test/module/characterization/CMakeLists.txt:29`; `src/operation/iCTS/test/flow/htree/CMakeLists.txt:35`).
- Focused test targets found include `icts_test_flow_manager`, `icts_test_flow_synthesis`, `icts_test_flow_synthesis_realtech`, `icts_test_module_characterization`, `icts_test_module_characterization_realtech`, and H-tree targets (`src/operation/iCTS/test/flow/CMakeLists.txt:1`; `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt:1`; `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt:7`; `src/operation/iCTS/test/module/characterization/CMakeLists.txt:3`; `src/operation/iCTS/test/module/characterization/CMakeLists.txt:12`; `src/operation/iCTS/test/flow/htree/CMakeLists.txt:1`).
- CMake risk: `database/io` privately links the STA adapter target, while STA adapter also reaches into `dmInst`/iDB through `TimingIDBAdapter` setup. Moving code between Wrapper and STAAdapter can create target cycles or push `idm`, `power`, or iSTA/iPA dependencies into broader public surfaces.
- CMake risk: broad aggregators such as `icts_source_database` can hide missing nearest-target links during a refactor. For new files, prefer the nearest existing submodule target and keep dependency visibility `PRIVATE` unless headers require `PUBLIC`.
- CMake risk: moving flow-stage code into new subdirectories requires both parent `add_subdirectory()` and linking from the nearest aggregator target; adding only include dirs will violate local dependency rules.

### README and Test Surface

- Repository README shows the normal source build path as `bash build.sh` and hello smoke test `./bin/iEDA -script scripts/hello.tcl` (`README.md:110`).
- `build.sh` configures with `cmake -S "$IEDA_WORKSPACE" -B "$BUILD_DIR"` and builds with `cmake --build "$BUILD_DIR" -j "$BUILD_THREADS" --target "$BINARY_TARGET"`; default binary target is `iEDA` (`build.sh:23`, `build.sh:86`, `build.sh:95`).
- `build.sh -r` hello path invokes `"${BINARY_DIR}"/iEDA -script "${IEDA_WORKSPACE}"/scripts/hello.tcl` (`build.sh:263`).
- `src/operation/iCTS/test/README.md` says test layout mirrors `source/`, reusable helpers live under `test/common/`, and feature-specific tests link only needed pieces (`src/operation/iCTS/test/README.md:1`).
- Test README lists real-tech support helpers for repo-local ICS55 workspace probing, real-design load extraction, synthetic fallback generation, cached setup state, and load-selection helpers (`src/operation/iCTS/test/README.md:17`).
- Real-tech helpers probe repo-local ICS55 workspaces under `scripts/design/`, resolve PDK root from `ICTS_REALTECH_PDK_DIR`, `PDK_DIR`, or checked-in run scripts, and fall back to synthetic loads when required LEF/DEF/LIB/SDC files are not available (`src/operation/iCTS/test/README.md:44`).
- `ICTS_TEST_OUTPUT_DIR` controls SVG/log output, defaulting to `icts_test_output` near the executable; each gtest case gets a dedicated root containing `cts.log` and `test.log` (`src/operation/iCTS/test/README.md:51`, `src/operation/iCTS/test/README.md:57`).
- `src/operation/iCTS/README.md` is empty in this workspace.

### Build and Test Commands

- Normal full build path:

```bash
bash build.sh
```

- Equivalent direct build after configuration exists:

```bash
cmake --build build -j 8 --target iEDA
```

- Focused CTS refactor build targets:

```bash
cmake --build build --target iEDA icts_test_flow_manager icts_test_flow_synthesis icts_test_module_characterization icts_test_flow_htree -j 8
```

- Focused CTest run:

```bash
ctest --test-dir build -R 'icts_test_(flow_manager|flow_synthesis|module_characterization|flow_htree)$' --output-on-failure
```

- Real-tech smoke tests exist but may fall back to synthetic assets or depend on environment/PDK availability:

```bash
cmake --build build --target icts_test_flow_synthesis_realtech icts_test_module_characterization_realtech icts_test_flow_htree_realtech -j 8
ctest --test-dir build -R 'icts_test_.*realtech' --output-on-failure
```

- Slow real-tech regressions require configuring with `-DICTS_BUILD_SLOW_REALTECH_TESTS=ON`; default is OFF.

- Final iCTS checker command from PRD, only after refactor convergence:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

### Script Validation Surface

- PRD final binary validation command is:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- In this workspace, `scripts/design/ics55_dev` exists and contains only executable `iEDA`; no `script/` directory and no `script/iCTS_script/run_iCTS_dev.tcl` were found.
- The exact PRD validation command is therefore currently blocked by a missing script path, not by a missing `iEDA` binary.
- Available analogous ICS55 flow is under `scripts/design/ics55_gcd`: `run_iEDA.sh` sets ICS55 PDK-dependent environment and includes `iCTS_script/run_iCTS.tcl` in the step list (`scripts/design/ics55_gcd/run_iEDA.sh:17`, `scripts/design/ics55_gcd/run_iEDA.sh:36`, `scripts/design/ics55_gcd/run_iEDA.sh:54`).
- Available ICS55 CTS Tcl initializes flow/db/lib/sdc/lef/def, runs `run_cts -config $IEDA_CONFIG_DIR/cts_default_config.json -work_dir $TOOL_REPORT_DIR`, then saves DEF, netlist, db reports, feature metrics, and `cts_report` (`scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:29`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:34`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:64`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:69`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:79`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS.tcl:87`).
- Available ICS55 post-CTS STA Tcl reloads the CTS DEF and runs `run_sta -output $::env(RESULT_DIR)/cts/sta/` (`scripts/design/ics55_gcd/script/iCTS_script/run_iCTS_STA.tcl:34`, `scripts/design/ics55_gcd/script/iCTS_script/run_iCTS_STA.tcl:40`).

### Compatibility Risks to Preserve or Call Out

- Public `CTSAPI.hh` symbols and static method signatures are compile-time contracts for Tcl, Python, tool-manager, and feature-summary callers. Moving implementation is fine; changing names, macro, signatures, include path, or return types is not.
- `CTSAPI::resetAPI()` has observable ordering and does not reset/destroy STA engine directly. A refactor that centralizes session lifecycle must preserve or explicitly test any change to STA lifetime.
- `CTSAPI::init()` owns config/work/log/gds/output path resolution and opens schema logging before Wrapper/STA setup and runtime reporting. Moving setup into flow-level components must preserve side effects and failure timing.
- Wrapper read clears Design and cross-reference maps. Any new stage object must not retain CTS pointers or Wrapper map entries across `ClockNetManager::readClockData()` or `Wrapper::readClocks()`.
- Design/Clock/Pin/Net relationships are raw borrowed topology edges. New helper objects should own only algorithm-local temporary objects until success, then commit into `Design`.
- iDB access belongs in Wrapper and iSTA/iPA/timing access belongs in STAAdapter. Module code and flow business objects should continue to operate on CTS types or explicit option/result structs.
- Evaluation order matters: `Design` final objects -> Wrapper writeback to iDB -> STA full-design refresh -> RC install/timing update/statistics/report. Inverting the writeback/refresh sequence can create stale STA timing.
- CMake target boundaries are fragile around `database/io`, `database/adapter/sta`, and flow/netlist. Moving files between these areas can introduce cycles or unintentionally widen public external dependencies.
- Real-tech tests may silently use synthetic fallback when PDK/design assets are unavailable. Passing those tests does not prove real ICS55 assets were used unless the logs/setup state confirm real asset loading.
- The requested final `ics55_dev` validation command currently cannot run as written because `./script/iCTS_script/run_iCTS_dev.tcl` is absent under `scripts/design/ics55_dev`.

### External References

- None. This research used repository-local source, tests, CMake, scripts, PRD, and Trellis specs only. No external web documentation or package-version lookup was needed.

## Caveats / Not Found

- No production code was modified.
- No build, unit test, CTest, `ecc_dev_tools`, or final iEDA validation command was run during this research pass.
- The exact final validation script `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl` was not found. `scripts/design/ics55_dev` contains only the `iEDA` binary in this workspace.
- `src/operation/iCTS/README.md` has zero lines, so iCTS-specific runnable documentation comes from `src/operation/iCTS/test/README.md`, CMake, `build.sh`, repository README, and scripts under `scripts/design/`.
- Real-tech test commands may pass through synthetic fallback unless the environment provides required ICS55 LEF/DEF/LIB/SDC assets.
- STAAdapter reset/lifetime behavior is inferred from current source: `CTSAPI::resetAPI()` does not reset STA directly, while `STAAdapter::init()` resets char/transient state and conditionally recreates base STA context only when absent. Any implementation change here needs focused validation.
