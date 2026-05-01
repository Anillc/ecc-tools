# Research: subflow instantiation architecture

- Query: Research the proposed `instantiation` subflow for task `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign`; inspect current `src/operation/iCTS/source/flow/netlist`, `stage/CTSClockTreeWritebackStep.*`, `ClockTreeSynthesisTransaction` commit behavior, `Design` commit methods, and `Wrapper::writeClocks` usage; determine the responsibility boundary for materializing synthesis output into `Design` and iDB without using `writeback` as architecture naming; propose readable second-level subfolders if needed.
- Scope: internal
- Date: 2026-04-30

## Findings

### Files Found

- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md`: task requirements; names `instantiation` as the desired high-level layer for materializing CTS algorithm results into design/iDB objects and explicitly avoids `writeback` architecture naming.
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md`: current design proposal; defines `instantiation` as the layer that converts synthesis result or clock-tree model into committed `Design` and iDB objects.
- `.trellis/spec/backend/directory-structure.md`: current authoritative flow placement spec; still describes the legacy framework as `read data -> synthesis/writeback -> evaluation -> report`.
- `.trellis/spec/backend/database-guidelines.md`: ownership and access-boundary spec; says `Design` owns final CTS objects, algorithm results may own temporary objects, `Wrapper` owns iDB adaptation, and evaluation/report/visualization are readonly.
- `.trellis/spec/backend/quality-guidelines.md`: naming spec; prefers CTS/physical-design terms such as `committed design object`, `clock tree`, `source-to-root`, `downstream tree`, and `root buffer`.
- `src/operation/iCTS/source/flow/CMakeLists.txt`: current flow CMake topology; exposes `netlist`, `stage`, `synthesis`, `htree`, `evaluation`, `visualization`, `clock_tree_view`, and `run_setup` as separate modules.
- `src/operation/iCTS/source/flow/netlist/CMakeLists.txt`: builds the current `icts_source_flow_netlist` target from only `ClockNetEditor.cc`.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.hh`: current static facade for clock-data import, clock-net mutation, source-net restoration, and inserted-object commit.
- `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc`: current implementation of read/import helpers, root-buffer and downstream-net creation, net reconnection, rollback helpers, and final `Design` commit.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.hh`: current stage result and entry for iDB writeback.
- `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc`: current iDB projection step; gets committed clocks from `Design` and calls `WRAPPER_INST.writeClocks`.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.hh`: current per-clock transaction boundary for rollback, sink-domain commit, and source-to-root synthesis.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc`: current code that commits successful synthesis results into `Design` through `ClockNetEditor`, merges the clock-tree view, records metrics, and rolls back on failure.
- `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc`: current per-clock coordinator that prepares sink domains, runs synthesis, and invokes transaction methods.
- `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc`: current sink-domain preparation; materializes root buffers and downstream nets before sink-tree synthesis.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh`: current synthesis result model; owns temporary inserted `Inst`, `Pin`, and `Net` objects before commit.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.*`: current synthesis-local temporary-object and side-effect-guard helpers.
- `src/operation/iCTS/source/database/design/Design.hh`: `Design` API exposing `make*`, `commit*`, `find*`, indexing, clear, and removal methods.
- `src/operation/iCTS/source/database/design/Design.cc`: current final-object ownership, collision checks, index maintenance, topology cleanup, and clock-membership removal.
- `src/operation/iCTS/source/database/design/Clock.hh`: current clock identity and borrowed membership view over final clock insts/nets.
- `src/operation/iCTS/source/database/io/Wrapper.hh`: iDB adapter API; exposes `writeClock` and `writeClocks`.
- `src/operation/iCTS/source/database/io/Wrapper.cc`: current CTS-to-iDB object mapping, iDB instance creation/reuse, iDB net creation/rewrite, and clock batch writing.
- `src/operation/iCTS/source/flow/FlowManager.*`: current top-level lifecycle; calls `writeback()` between synthesis and evaluation.
- `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.*`: current readonly view status carries both synthesis completion and writeback completion flags.
- `src/operation/iCTS/test/flow/FlowManagerTest.cc`: tests around rollback, commit failure, and pending view merge behavior.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc`: tests around `Design` commit collision rejection and final ownership.

### Code Patterns

#### Existing top-level lifecycle

- `FlowManager::runCTS()` currently sequences `readData()`, `run()`, private `writeback()`, and `evaluate()` before recording success (`src/operation/iCTS/source/flow/FlowManager.cc:62`, `src/operation/iCTS/source/flow/FlowManager.cc:68`, `src/operation/iCTS/source/flow/FlowManager.cc:70`, `src/operation/iCTS/source/flow/FlowManager.cc:73`).
- `FlowManager::writeback()` runs only after synthesis success, calls `CTSClockTreeWritebackStep::run()`, marks `ClockTreeView` writeback state, and folds writeback failure into `_run_summary.success` (`src/operation/iCTS/source/flow/FlowManager.cc:105`, `src/operation/iCTS/source/flow/FlowManager.cc:108`, `src/operation/iCTS/source/flow/FlowManager.cc:109`, `src/operation/iCTS/source/flow/FlowManager.cc:110`, `src/operation/iCTS/source/flow/FlowManager.cc:111`).
- `ClockTreeView` currently models `writeback_done` as a readiness/provenance bit in the readonly view (`src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:142`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:144`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.hh:161`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:32`, `src/operation/iCTS/source/flow/clock_tree_view/ClockTreeView.cc:37`).

#### Current synthesis result ownership

- `ClockSynthesis::BuildResult` and `ClockSynthesis::SourceToRootBuildResult` own temporary `inserted_insts`, `inserted_pins`, and `inserted_nets` as `std::unique_ptr` vectors (`src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:89`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:104`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:111`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:121`).
- `ClockSynthesisNetEditor::CreateBufferInstance` constructs temporary buffer insts and pins into a synthesis result, not directly into `Design` (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:124`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:128`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:133`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:137`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:142`).
- `ClockSynthesisNetEditor::CreateNet` constructs a temporary net into the synthesis result and returns a borrowed pointer to that temporary object (`src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:150`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:152`, `src/operation/iCTS/source/flow/synthesis/ClockSynthesisNetEditor.cc:155`).
- Sink-tree and source-to-root synthesis use side-effect guards and restore on failure, reinforcing that failed synthesis should not leave partial final topology behind (`src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:54`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:59`, `src/operation/iCTS/source/flow/synthesis/ClockSinkTreeSynthesizer.cc:73`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:60`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:69`, `src/operation/iCTS/source/flow/synthesis/ClockSourceRootSynthesizer.cc:81`).

#### Current Design materialization path

- `ClockTreeSynthesisDriver::run()` prepares sink domains, runs downstream synthesis per domain, then runs source-to-root synthesis; it merges the per-clock view only after both phases succeed (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:87`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:110`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:116`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisDriver.cc:119`).
- `ClockSinkDomainBuilder::prepare()` currently creates a root buffer and downstream net through `ClockNetEditor` before the sink-domain H-tree synthesis result exists (`src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:55`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:69`, `src/operation/iCTS/source/flow/stage/ClockSinkDomainBuilder.cc:76`).
- `ClockNetEditor::addRootBufferForSinkDomain()` creates root-buffer inst/pins using `DESIGN_INST.makeInst` and `DESIGN_INST.makePin`, indexes pins, and immediately adds the root buffer to the clock membership (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:209`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:230`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:241`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:253`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:257`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:269`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:273`).
- `ClockNetEditor::connectSinkDomainDownstreamNet()` creates a downstream net with `DESIGN_INST.makeNet`, reconnects its driver and loads, and immediately adds the net to clock membership (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:277`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:288`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:293`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:294`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:411`).
- `ClockTreeSynthesisTransaction::commitSinkDomain()` builds a pending clock-tree view, commits temporary inserted objects through `ClockNetEditor::commitInsertedObjects`, reconnects and rolls back on failure, then merges the pending view and records synthesis metrics on success (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:160`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:163`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:165`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:167`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:169`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:173`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:174`).
- `ClockTreeSynthesisTransaction::synthesizeSourceToRoot()` builds a pending source-to-root view, commits the source-to-root temporary objects through `ClockNetEditor::commitInsertedObjects`, then merges the view and records metrics (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:254`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:257`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:263`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:267`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:268`).
- `ClockNetEditor::commitInsertedObjects()` first validates duplicate/collision names for insts, pins, and nets, then moves temporary ownership into `DESIGN_INST.commitInst`, `DESIGN_INST.commitPin`, and `DESIGN_INST.commitNet`, adding committed inst/net pointers to the `Clock` membership view (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:444`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:448`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:464`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:486`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:506`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:511`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:519`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:530`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:535`).
- `ClockTreeSynthesisTransaction::rollbackClock()` restores the source net to original clock loads and removes current CTS membership objects from `Design` (`src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:119`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:121`, `src/operation/iCTS/source/flow/stage/ClockTreeSynthesisTransaction.cc:122`).

#### Design commit behavior

- `Design` exposes paired `make*` and `commit*` APIs for insts, pins, and nets; `commit*` transfers ownership of temporary objects while `make*` creates immediately owned final objects (`src/operation/iCTS/source/database/design/Design.hh:68`, `src/operation/iCTS/source/database/design/Design.hh:69`, `src/operation/iCTS/source/database/design/Design.hh:71`, `src/operation/iCTS/source/database/design/Design.hh:72`, `src/operation/iCTS/source/database/design/Design.hh:76`, `src/operation/iCTS/source/database/design/Design.hh:77`).
- `Design::commitInst()` rejects name collisions, stores the pointer in `_inst_by_name`, and moves ownership into `_insts` (`src/operation/iCTS/source/database/design/Design.cc:184`, `src/operation/iCTS/source/database/design/Design.cc:190`, `src/operation/iCTS/source/database/design/Design.cc:195`, `src/operation/iCTS/source/database/design/Design.cc:196`).
- `Design::commitPin()` rejects full-name collisions, attaches the pin to its inst, indexes the full name, and moves ownership into `_pins` (`src/operation/iCTS/source/database/design/Design.cc:259`, `src/operation/iCTS/source/database/design/Design.cc:265`, `src/operation/iCTS/source/database/design/Design.cc:273`, `src/operation/iCTS/source/database/design/Design.cc:276`, `src/operation/iCTS/source/database/design/Design.cc:283`).
- `Design::commitNet()` rejects name collisions, updates driver/load back-pointers, indexes the net name, and moves ownership into `_nets` (`src/operation/iCTS/source/database/design/Design.cc:343`, `src/operation/iCTS/source/database/design/Design.cc:349`, `src/operation/iCTS/source/database/design/Design.cc:354`, `src/operation/iCTS/source/database/design/Design.cc:357`, `src/operation/iCTS/source/database/design/Design.cc:362`, `src/operation/iCTS/source/database/design/Design.cc:363`).
- `Design::removeClockMembershipObjects()` removes all clock-member nets except the clock source net, then removes clock-member insts and their pins (`src/operation/iCTS/source/database/design/Design.cc:438`, `src/operation/iCTS/source/database/design/Design.cc:440`, `src/operation/iCTS/source/database/design/Design.cc:445`, `src/operation/iCTS/source/database/design/Design.cc:448`, `src/operation/iCTS/source/database/design/Design.cc:449`).
- `Clock` stores only borrowed membership pointers to clock insts and nets; it does not own the final objects (`src/operation/iCTS/source/database/design/Clock.hh:45`, `src/operation/iCTS/source/database/design/Clock.hh:48`, `src/operation/iCTS/source/database/design/Clock.hh:49`, `src/operation/iCTS/source/database/design/Clock.hh:60`, `src/operation/iCTS/source/database/design/Clock.hh:61`, `src/operation/iCTS/source/database/design/Clock.hh:62`).
- Tests assert this ownership split: `DesignCommitRejectsFinalNameCollisions` checks collision rejection by `commitInst`, `commitPin`, and `commitNet`, while `DesignOwnsFinalObjectsAndClockKeepsMembershipOnly` checks that removing clock membership deletes objects from `Design` and clears borrowed clock membership (`src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:202`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:218`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:232`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:263`, `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc:267`).

#### Current iDB materialization path

- `CTSClockTreeWritebackStep::run()` starts a `writeback` metric/stage, gets clocks from `DESIGN_INST`, checks `WRAPPER_INST.is_design_ready()`, and calls `WRAPPER_INST.writeClocks(clocks)` if iDB is ready (`src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:34`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:36`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:40`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:43`, `src/operation/iCTS/source/flow/stage/CTSClockTreeWritebackStep.cc:49`).
- `Wrapper::writeClocks()` simply iterates non-null clocks and combines `writeClock` success (`src/operation/iCTS/source/database/io/Wrapper.cc:672`, `src/operation/iCTS/source/database/io/Wrapper.cc:675`, `src/operation/iCTS/source/database/io/Wrapper.cc:679`).
- `Wrapper::writeClock()` materializes every clock-member inst into iDB, ensures/reuses iDB nets for the clock source net and inserted clock nets, and rewrites iDB net pins from committed CTS net connectivity (`src/operation/iCTS/source/database/io/Wrapper.cc:633`, `src/operation/iCTS/source/database/io/Wrapper.cc:640`, `src/operation/iCTS/source/database/io/Wrapper.cc:641`, `src/operation/iCTS/source/database/io/Wrapper.cc:645`, `src/operation/iCTS/source/database/io/Wrapper.cc:650`, `src/operation/iCTS/source/database/io/Wrapper.cc:654`, `src/operation/iCTS/source/database/io/Wrapper.cc:661`, `src/operation/iCTS/source/database/io/Wrapper.cc:662`, `src/operation/iCTS/source/database/io/Wrapper.cc:666`).
- `Wrapper::ctsToIdb(Inst*)` creates or reuses an iDB instance, sets cell master/orient/location/status, and cross-references CTS/iDB inst and pins (`src/operation/iCTS/source/database/io/Wrapper.cc:486`, `src/operation/iCTS/source/database/io/Wrapper.cc:495`, `src/operation/iCTS/source/database/io/Wrapper.cc:512`, `src/operation/iCTS/source/database/io/Wrapper.cc:514`, `src/operation/iCTS/source/database/io/Wrapper.cc:519`, `src/operation/iCTS/source/database/io/Wrapper.cc:522`, `src/operation/iCTS/source/database/io/Wrapper.cc:524`, `src/operation/iCTS/source/database/io/Wrapper.cc:535`).
- `Wrapper::ensureIdbNet()` reuses an existing iDB net or creates a clock net with `IdbConnectType::kClock`; `rewriteIdbNetPins()` clears and repopulates iDB pins from the committed CTS net driver/load pointers (`src/operation/iCTS/source/database/io/Wrapper.cc:566`, `src/operation/iCTS/source/database/io/Wrapper.cc:572`, `src/operation/iCTS/source/database/io/Wrapper.cc:584`, `src/operation/iCTS/source/database/io/Wrapper.cc:591`, `src/operation/iCTS/source/database/io/Wrapper.cc:597`, `src/operation/iCTS/source/database/io/Wrapper.cc:625`, `src/operation/iCTS/source/database/io/Wrapper.cc:626`, `src/operation/iCTS/source/database/io/Wrapper.cc:627`).

### Responsibility Boundary for `instantiation`

The proposed `instantiation` layer should be the flow-level materialization boundary, not an algorithm layer and not a raw database adapter. It should own the transition from a successful CTS synthesis result or stable `ClockTree` model into committed, externally visible topology:

1. `Design` materialization: validate and commit CTS-created insts, pins, nets, source-to-root/downstream connectivity, and clock membership into `Design`.
2. iDB materialization: ask `Wrapper` to project the committed `Design`/`Clock` topology into iDB, keeping all raw iDB operations inside `Wrapper`.
3. Transaction status: report whether materialization was attempted, whether the iDB design was ready, how many clocks were processed, and whether the instantiated result is usable by evaluation/report.
4. Rollback cleanup: coordinate rollback of partially materialized clock-tree objects when a per-clock/domain commit fails, using `Design` removal and clock membership clearing rather than letting callers manipulate object ownership directly.

The `instantiation` layer should not own:

- H-tree topology search, characterization, candidate selection, sink clustering policy, or source-to-root algorithm dispatch. Those stay in `synthesis` and `synthesis/topology/htree`.
- Clock-data import. `ClockNetEditor::readClockData()` currently lives in `netlist`, but its role is input import/setup, not instantiation (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:300`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:317`).
- Sink-domain classification policy. `ClockNetEditor::partitionClockSinks()` currently classifies macro vs regular sinks, but this belongs with the synthesis/sink-domain manager, not final materialization (`src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:327`, `src/operation/iCTS/source/flow/netlist/ClockNetEditor.cc:340`).
- Raw iDB object access. `database-guidelines.md` requires raw iDB access to stay inside `Wrapper` (`.trellis/spec/backend/database-guidelines.md:62`, `.trellis/spec/backend/database-guidelines.md:68`).
- Evaluation, report, visualization, or broad report-only snapshots. Evaluation/report/visualization remain readonly consumers of committed CTS results (`.trellis/spec/backend/database-guidelines.md:65`, `.trellis/spec/backend/database-guidelines.md:66`).

The most important semantic distinction is that current code has two different "materialization" moments:

- Commit-to-`Design`: `ClockNetEditor::commitInsertedObjects()` moves successful temporary algorithm-owned objects into `Design` and updates `Clock` membership. This is the first and most important instantiation boundary.
- Project-to-iDB: `CTSClockTreeWritebackStep::run()` calls `Wrapper::writeClocks()` after synthesis success. This is still instantiation work, but it is an external database projection over already committed `Design` objects.

Therefore, replacing architecture-level `writeback` with `instantiation` is accurate if the new layer explicitly covers both `Design` materialization and iDB projection. Using only `iDB write` or `writeback` as the folder name would hide the `Design` commit boundary, which is where ownership actually changes.

### Mapping Current Code to Target `instantiation`

| Current item | Current responsibility | Target placement | Notes |
|---|---|---|---|
| `CTSClockTreeWritebackStep.*` | Flow stage that writes committed clocks to iDB via `Wrapper::writeClocks` | `flow/instantiation/` or `flow/instantiation/idb/` | Rename architecture to instantiation; internal logs can still say iDB write if needed, but folder/class/result should not be named writeback. |
| `ClockNetEditor::commitInsertedObjects` | Validates and commits temporary synthesis objects into `Design`, updates clock membership | `flow/instantiation/design/` | This is the core `Design` materialization operation. |
| `ClockTreeSynthesisTransaction::commitSinkDomain` | Coordinates pending view, commit call, rollback, metrics | Split between `synthesis` transaction/coordinator and `instantiation/design` commit service | Synthesis should decide when a result is successful; instantiation should perform materialization. |
| `ClockTreeSynthesisTransaction::synthesizeSourceToRoot` commit block | Commits source-to-root temporary objects into `Design` | `flow/instantiation/design/` | Same boundary as sink-domain commit. |
| `ClockSinkDomainBuilder::prepare` root-buffer/downstream-net creation | Pre-synthesis final object creation using `Design::make*` | Transitional: keep near synthesis until a planned-object result exists; target: `instantiation/design` | A pure instantiation layer needs synthesis to express planned root-domain objects instead of pre-committing them. |
| `ClockNetEditor::readClockData` | Clock/net input import from config or `Wrapper` | `setup` or clock input/import subflow | Not instantiation; it reads existing design data. |
| `ClockNetEditor::partitionClockSinks` | Macro/regular sink classification | `synthesis` sink-domain manager | Not instantiation; this is synthesis policy. |
| `ClockNetEditor::restoreClockSourceNetToClockLoads` and `reuseClockSourceNetAsSourceToRootBuffers` | Net reconnection helpers for rollback/preparation | `instantiation/design` if final-topology mutation; `synthesis` if temporary pre-result mutation | Boundary should be based on whether the mutation is final committed topology or temporary algorithm setup. |
| `Design::commitInst/commitPin/commitNet` | Database ownership transfer and indexing | Remain in `database/design` | `instantiation` calls these; it should not duplicate ownership/index rules. |
| `Wrapper::writeClock/writeClocks` | External iDB projection and iDB object/net mutation | Remain in `database/io`; called by `instantiation/idb` | Raw iDB access remains inside `Wrapper`. |

### Recommended Target Shape

Use `instantiation` as the high-level flow layer name. The root should expose only the primary entry pair unless the implementation is still small:

```text
src/operation/iCTS/source/flow/
  instantiation/
    Instantiation.hh
    Instantiation.cc
```

If the implementation would otherwise mix the current `ClockNetEditor`-sized design mutation logic with the iDB projection wrapper, split by materialization target:

```text
src/operation/iCTS/source/flow/
  instantiation/
    Instantiation.hh
    Instantiation.cc
    design/
      DesignInstantiation.hh
      DesignInstantiation.cc
    idb/
      IdbInstantiation.hh
      IdbInstantiation.cc
```

Recommended meanings:

- `Instantiation`: primary flow entry. It coordinates full materialization after synthesis success, returns a coarse `CTSInstantiationResult`, updates run/view readiness, and delegates to the subfolders.
- `design/DesignInstantiation`: commits CTS synthesis output into `Design` and `Clock` membership, validates collisions, reconnects final CTS nets, and performs rollback cleanup. It should call `Design::commit*` instead of reproducing database ownership/indexing rules.
- `idb/IdbInstantiation`: projects committed `Design`/`Clock` topology into iDB by calling `Wrapper::writeClocks`. It should not expose raw iDB pointers or duplicate `Wrapper` conversion logic.

Avoid these second-level names:

- `writeback/`: conflicts with the task's naming constraint and hides the `Design` commit boundary.
- `commit/`: too generic and describes a mechanism rather than the materialized target.
- `netlist/` as the only instantiation subfolder: it is readable for `Design` connectivity mutation, but it does not cover iDB projection and it already contains unrelated input import/sink classification in the current code.
- `sink_domain/` or `per_clock/` under `instantiation`: those are synthesis coordination concepts unless the object being materialized is explicitly a domain-scoped result model.

If a narrower EDA term is preferred for `design/`, `netlist/` can be used under `instantiation` only after `readClockData` and sink partitioning move out:

```text
instantiation/
  Instantiation.hh
  Instantiation.cc
  netlist/
    NetlistInstantiation.hh
    NetlistInstantiation.cc
  idb/
    IdbInstantiation.hh
    IdbInstantiation.cc
```

However, `design/` is the cleaner second-level name because the ownership transition is specifically into the iCTS `Design` database, not merely textual netlist generation.

### Suggested Naming Changes

- `CTSClockTreeWritebackStep` -> `CTSInstantiation` or `ClockTreeInstantiation` depending on whether the new entry is full CTS-layer or clock-tree-specific.
- `CTSClockTreeWritebackResult` -> `CTSInstantiationResult`.
- `writeback_done` -> `instantiation_done`, `materialization_done`, or split into `design_committed` and `idb_committed`.
- `_writeback_result` -> `_instantiation_result`.
- `FlowManager::writeback()` -> `instantiate()` or `instantiateClockTrees()`.
- `ClockTreeView::markWritebackDone()` -> `markInstantiationDone()` if the view tracks full materialization readiness, or split the status into `markDesignCommitted()` and `markIdbCommitted()` if reports need that distinction.
- Schema/runtime stage `"CTSWriteback"` -> `"CTSInstantiation"`; table `"CTS Writeback Summary"` -> `"CTS Instantiation Summary"`. A field can still say `idb_status` or `idb_materialized`.

The name `instantiation` is preferable to `writeback` because the layer creates/materializes real design objects and external DB state. `writeback` describes only an output direction and misses the earlier `Design` ownership transition.

### Migration Implications

1. First isolate `CTSClockTreeWritebackStep` behavior behind an instantiation-named entry that still calls `Wrapper::writeClocks`. This is the lowest-risk rename because it is currently small and already only runs after synthesis success.
2. Then move `ClockNetEditor::commitInsertedObjects`, rollback helpers, and final clock-net reconnection into a `design` materialization helper under `instantiation`, while leaving `Design::commit*` in `database/design`.
3. Move `ClockNetEditor::readClockData` out of the materialization helper before removing or renaming `flow/netlist`; it belongs to setup/input import, not instantiation.
4. Move sink partitioning/root-domain policy into synthesis-owned context/manager code. Root buffer and downstream net creation need a design decision: keep transitional pre-synthesis creation in synthesis until a richer synthesis result exists, or make synthesis produce planned root-domain objects so `instantiation/design` can materialize them uniformly.
5. Keep `Wrapper::writeClock/writeClocks` in `database/io`. The instantiation layer should depend on the adapter facade, not on iDB types.

### Related Specs

- `.trellis/spec/backend/directory-structure.md`: current flow framework and placement rules; flow code belongs under `source/flow`, shared data/adapters under `source/database`, and flow changes must preserve the CTS stage order (`.trellis/spec/backend/directory-structure.md:37`, `.trellis/spec/backend/directory-structure.md:41`, `.trellis/spec/backend/directory-structure.md:45`, `.trellis/spec/backend/directory-structure.md:54`, `.trellis/spec/backend/directory-structure.md:105`).
- `.trellis/spec/backend/database-guidelines.md`: final object ownership and external adapter rules; `Design` owns final CTS objects, algorithm results may own temporary objects, raw iDB stays inside `Wrapper`, and only synthesis/writeback may commit CTS topology or write final CTS objects to iDB (`.trellis/spec/backend/database-guidelines.md:35`, `.trellis/spec/backend/database-guidelines.md:36`, `.trellis/spec/backend/database-guidelines.md:38`, `.trellis/spec/backend/database-guidelines.md:62`, `.trellis/spec/backend/database-guidelines.md:65`, `.trellis/spec/backend/database-guidelines.md:68`).
- `.trellis/spec/backend/quality-guidelines.md`: naming should reflect CTS/physical-design concepts and avoid generic service terms; `committed design object` is already an endorsed term (`.trellis/spec/backend/quality-guidelines.md:31`, `.trellis/spec/backend/quality-guidelines.md:35`, `.trellis/spec/backend/quality-guidelines.md:36`).
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md`: user principles require `setup`, `synthesis`, `instantiation`, `evaluation`, and `report`; root folders should expose matching primary entry files; subfolders should be proportional and readable (`.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:20`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:24`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:26`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:27`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:28`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/prd.md:29`).
- `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md`: design proposal already states that `instantiation` materializes CTS algorithm results into committed design/iDB objects and should not own H-tree search, sink-domain algorithm policy, or evaluation/report logic (`.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:12`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:54`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:76`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:83`, `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/info.md:98`).

### External References

- No fresh external browsing was needed for this focused internal boundary pass.
- Related persisted research: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/open-source-cts-comparison.md` records OpenROAD/TritonCTS terminology and notes that OpenROAD has a `writeDataToDb` phase after `buildClockTrees` as of its `master` branch inspected on 2026-04-30.
- Related persisted research: `.trellis/tasks/04-30-cts-flow-architecture-naming-redesign/research/industry-cts-flow-terminology.md` records public Innovus/CCOpt and mflowgen CTS terminology as of 2026-04-30. It supports keeping `CTS`/`clock tree synthesis` for algorithmic build and avoiding vendor-specific names.

## Caveats / Not Found

- This artifact intentionally did not modify source code.
- Current code does not have a pure post-synthesis instantiation model yet. Root buffers and downstream nets are currently created as final `Design` objects during sink-domain preparation before sink-tree synthesis. A clean target architecture needs either a richer synthesis result/`ClockTree` model for planned root-domain objects or a transitional rule that keeps those preparations near synthesis until the result model exists.
- Current specs still use the legacy phrase `synthesis/writeback`; this research follows the task PRD and current `info.md` direction to use `instantiation` for architecture naming while preserving the same behavior.
- `Wrapper::writeClocks` remains the correct adapter boundary for iDB projection. The proposed instantiation layer should not move raw iDB logic out of `Wrapper`.
