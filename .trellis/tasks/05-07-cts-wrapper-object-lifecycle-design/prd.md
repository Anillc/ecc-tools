# brainstorm: CTS clock data read/write framework

## Goal

Systematically refactor the iCTS clock data read/write path so Inst/Pin/Net ownership, iDB lookup, CTS object creation, iDB writeback, and rollback have clear CTS-specific responsibilities. The current implementation is functionally aligned with the SDC-first clock source policy, but the Inst/Pin/Net conversion helpers mix read-time construction, write-time materialization, lookup, and rollback-sensitive mutation.

## What I Already Know

* SDC is the only authoritative source for CTS clock declarations and clock source expressions.
* iDB is used to materialize SDC-declared clock nets; missing iDB nets should fail directly.
* The current `Wrapper::ctsToIdb(Pin*)` was changed so net rewrite can resolve existing iDB pins without creating or mutating an instance as a hidden side effect.
* The current naming is still misleading because `ctsToIdb(...)` can mean query, reuse, create, update, or cross-reference depending on object type and call site.
* `Wrapper::readClocks()` clears Wrapper maps and CTS topology, resolves each SDC clock net by `find_net`, and imports iDB Inst/Pin/Net objects into CTS.
* `Wrapper::writeClocksDetailed()` rebuilds `ClockDAG`, snapshots touched iDB nets and instances, calls `writeClock()`, and attempts rollback on failure.
* `Clock::get_insts()` and `ClockDAG::reachableNets()` define the current writeback scope.

## Task Constraints

* Do not treat this as a staged minimal subset. The target is one systematic refactor with a clear final framework.
* Do not introduce vague generic core names for write context or object projection in the new design.
* Do not add persistent origin/state fields to CTS `Inst`, `Pin`, `Net`, or `Clock` unless a concrete invariant cannot be enforced locally.
* Keep SDC as the only source of CTS clock declaration and clock source expression.
* Do not add iDB fallback clock discovery.
* Missing required iDB objects in CTS clock read/write paths should fail directly. Do not hide invalid state behind skip/continue behavior.
* Logging should be compact and decisive. Prefer one fatal/error at the failing boundary instead of repeated defensive warnings in lower helpers.
* Keep public API churn limited to what is required to make read/write semantics explicit.
* Do not broaden this task into global CTS database redesign, STA redesign, or unrelated flow cleanup.

## CTS Responsibility Boundaries

* STA parses SDC clock declarations, source expressions, and periods. STA must not decide CTS clock topology or iDB writeback behavior.
* `DesignConversion::readClockData()` accepts only SDC clocks, applies optional config net mapping only when enabled, and asks Wrapper to build CTS clock data from the resolved net names.
* Wrapper owns iDB access and the CTS-to-iDB cross-reference maps.
* Wrapper read code builds CTS clock data from SDC-declared iDB clock nets.
* Wrapper write code writes committed CTS clock tree data back to iDB.
* CTS synthesis owns inserted clock tree instances, pins, and nets before writeback.
* `ClockDAG` owns clock tree reachability and path queries. Writeback and evaluation should rely on `ClockDAG` for clock tree traversal instead of ad hoc net walks.

## Current Flow Notes

### Read Data

* `DesignConversion::readClockData()` reads SDC clock declarations through `STAAdapter`.
* It builds `(clock_name, clock_source_expression_or_configured_net)` pairs.
* `Wrapper::readClocks()` directly resolves each net from iDB by name and fails if unresolved.
* `Wrapper::readClock()` creates CTS `Clock`, `Net`, `Inst`, and `Pin` data for the iDB clock net, then stores cross-reference maps.
* Imported source and sink objects represent existing iDB objects inside the CTS model.

### Write Clock

* `Wrapper::writeClocksDetailed()` rebuilds `Design::_clock_dag` before writeback.
* It snapshots touched iDB net pin membership and pre-existing iDB instance names.
* `Wrapper::writeClock()` materializes `clock.get_insts()` with `ctsToIdb(Inst*)`.
* For each reachable DAG net, `ensureIdbNet()` resolves or creates the iDB net, then `rewriteIdbNetPins()` rewrites pin membership.
* `rewriteIdbNetPins()` calls `ctsToIdb(Pin*)`, which currently performs existing pin resolution and limited fallback instance lookup.
* A writeback failure triggers rollback for touched nets and inserted CTS instances.

## Design Problems

* Conversion method names do not communicate side effects.
* Read and write phases share the same cross-reference maps but use them with different semantics.
* Instance ownership/origin is inferred from clock membership, type, and source-pin checks instead of explicit operation policy.
* Rollback scope and mutation scope are computed in separate helper paths and can drift.
* `ctsToIdb(Pin*)` hides the policy that pin resolution must not create non-CTS instances.

## Refactor Framework

Use two CTS-specific components behind Wrapper:

* `CtsClockReader`: reads SDC-selected iDB clock nets into the CTS design database.
* `CtsClockIdbWriter`: writes committed CTS clock tree data back to iDB.

Keep both components local to the iCTS database/io layer unless tests or reuse require separate files.

### CtsClockReader

Responsibilities:

* Clear clock read state once at the start of a full clock read.
* Resolve each SDC-declared clock net by exact iDB net name.
* Build CTS `Clock`, source `Net`, source `Pin`, sink `Pin`, and attached `Inst` data from that iDB net.
* Register CTS-to-iDB cross references while building objects.
* Fatal/error immediately if a required iDB object is missing or a CTS object cannot be indexed.

Suggested helper names:

* `clearClockReadData()`
* `findSdcClockNetOrFatal(clock_name, net_name)`
* `buildClockFromIdbNet(clock_name, net_name, idb_net)`
* `buildInstFromIdbInst(idb_inst)`
* `buildPinFromIdbPin(idb_pin)`
* `buildNetFromIdbNet(idb_net)`
* `bindIdbInst/Pin/Net(...)`

### CtsClockIdbWriter

Responsibilities:

* Rebuild and validate `ClockDAG` before touching iDB.
* Collect write scope from `ClockDAG::reachableNets(clock)` and `clock.get_insts()`.
* Backup touched iDB nets and existing iDB clock tree instance names before mutation.
* Create or update only CTS-owned clock tree instances from `clock.get_insts()`.
* Find or create only reachable clock tree nets.
* Rewrite iDB net pin membership from CTS net driver/load pins.
* Resolve source and sink pins from existing iDB only. Pin resolution must never create a non-CTS iDB instance.
* Roll back touched iDB nets and newly created clock tree instances on write failure.

Suggested helper names:

* `collectClockIdbWriteScope(clocks)`
* `backupClockIdbWriteScope(scope)`
* `writeClockTreeInstsToIdb(clock)`
* `createOrUpdateClockTreeInst(inst)`
* `findExistingIdbPinForClockNet(pin)`
* `findOrCreateClockTreeIdbNet(net, default_net_name)`
* `rewriteClockTreeIdbNetPins(idb_net, cts_net)`
* `rollbackClockIdbWrite(scope, backup)`

Only small local structs are allowed for rollback correctness:

* `ClockIdbWriteScope`: touched net names and clock tree instance names.
* `ClockIdbWriteBackup`: pre-existing net pin membership and pre-existing instance names.

These structs must not become a general CTS object origin system.

## Naming Rules

* Avoid `ctsToIdb(...)` for object-level operations because the name hides side effects.
* Keep `ctsToIdb(Point)` only if needed as a scalar coordinate conversion.
* Use `build*FromIdb*` for CTS object construction during read.
* Use `createOrUpdate*` only for operations that may mutate iDB.
* Use `find*` only for operations that must not create.
* Use `rewrite*` only for net pin membership replacement.
* Use `backup*` and `rollback*` only around iDB mutation scope.

## Alternatives

* Minimal rename-only refactor: lowest risk, but does not centralize rollback/mutation policy.
* Full Reader/Planner/Committer split: cleanest architecture, but wider file movement and review surface.
* Persistent object-origin tagging: makes policy explicit in the model, but conflicts with the current preference to avoid new data structures/state unless required.

Rejected for this task:

* A generic writeback-context abstraction.
* Generic object-projection terminology.
* A persistent object-origin enum on `Inst/Pin/Net`.
* Silent skip-based recovery for required clock data.

## Requirements

* Preserve SDC-only clock discovery.
* Missing SDC-declared iDB clock nets must fail directly.
* Writeback failure must rollback touched iDB nets and newly created CTS instances.
* Pin rewrite must never create a non-CTS iDB instance as a side effect.
* Public CTS APIs should stay stable unless an internal-only refactor cannot express the required invariants.
* Required read/write invariants should fail at the boundary where they are detected, with compact diagnostics.
* No repeated low-level warning logs for the same root cause.

## Acceptance Criteria

* [ ] The Wrapper read path is organized around `CtsClockReader` or equivalently clear CTS clock read helpers.
* [ ] The Wrapper write path is organized around `CtsClockIdbWriter` or equivalently clear CTS clock iDB write helpers.
* [ ] Helper names clearly distinguish build, find, create/update, rewrite, backup, and rollback behavior.
* [ ] `Wrapper::ctsToIdb(Pin*)` no longer exists as a misleading writeback helper, or is reduced to an obviously pure private lookup.
* [ ] Writeback scope is collected once from `ClockDAG` and `Clock::get_insts()`, then reused by backup, mutation, and rollback.
* [ ] Tests cover existing source/sink pin resolution, CTS buffer materialization, writeback failure rollback, and no implicit instance creation during pin resolution.
* [ ] Required clock read/write failures produce one decisive diagnostic path rather than cascaded skip warnings.

## Out of Scope

* Changing SDC clock discovery policy.
* Adding iDB fallback for clock discovery.
* Broadly refactoring the CTS design database ownership model.
* Adding global development guidelines unless future implementation reveals a reusable project-wide convention.
* Refactoring unrelated CTS synthesis algorithms.
* Changing STA SDC parsing beyond the minimal clock declaration/period/source API already required by CTS.

## Technical Notes

* Main files inspected:
  * `src/operation/iCTS/source/database/io/Wrapper.cc`
  * `src/operation/iCTS/source/database/io/Wrapper.hh`
  * `src/operation/iCTS/source/flow/instantiation/design_conversion/DesignConversion.cc`
  * `src/operation/iCTS/source/database/design/Clock.hh`
  * `src/operation/iCTS/source/database/design/Design.hh`
  * `src/operation/iCTS/source/database/design/Design.cc`
  * `src/operation/iCTS/source/database/design/Inst.hh`
  * `src/operation/iCTS/source/database/design/Pin.hh`
  * `src/operation/iCTS/source/database/design/Net.hh`
