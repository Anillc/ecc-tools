# CTS convergence design

## Boundary Model

This task uses a source-structure contract before applying mechanical moves.

### Behavior Directory External Contract

A behavior directory is a directory whose main responsibility is an operation, adapter, solver, builder, router, or flow step. Examples:

- `database/adapter/fast_sta`
- `database/io`
- `module/characterization`
- `module/routing/bound_skew_tree`
- `module/topology/fast_clustering`
- `flow/synthesis/htree/analytical_solver`

For these directories:

- the directory root contains the intended external `.hh/.cc` contract files and `CMakeLists.txt`;
- implementation headers and source files live under CTS responsibility subfolders;
- production source outside the directory includes only the external root contract headers;
- subfolder include paths are `PRIVATE` to implementation targets unless a nested contract is explicitly designed;
- broad root helper headers are not used as convenience include surfaces;
- tests may include internal headers only through component-test targets, not by expanding production include visibility.

### Stable Domain-Data Directory Exception

A stable domain-data directory can expose multiple root headers when the headers represent CTS objects that other modules are expected to use
directly. Examples to audit as possible exceptions:

- `database/design`
- `database/characterization`
- `database/routing`
- `database/spatial`

Exception requirements:

- each exposed header names a CTS domain object, not a helper category;
- the header does not exist only to support one behavior module's implementation;
- the directory's `CMakeLists.txt` documents the public target boundary through target visibility;
- any exception is recorded in this task before marking the audit complete.

This exception policy is confirmed by the user. It is not permission to keep behavior-module helper headers at root; it only applies when the
headers are stable CTS data contracts.

## FastSTA Target Shape

### Current Problem

The root currently exposes:

- `FastSta.hh/.cc`;
- clock analysis, timing, power, parasitic, sizing-edit, id, and segment-characterization helper headers.

External production code includes those helper headers directly. That means FastSTA internals are still part of the broader iCTS source contract,
even though the implementation was split into subfolders.

### Target Shape

Root:

```text
database/adapter/fast_sta/
  CMakeLists.txt
  FastSta.hh
  FastSta.cc
```

Subfolders stay CTS-responsibility based:

```text
clock_state/
clock_tree/
clock_net_parasitic/
timing/
power/
clock_sizing/
segment_char/
liberty/
report/
```

Corrective rules:

- FastSTA ids, clock timing observations, clock power observations, parasitic values, sizing edits, and segment-characterization sample data must not
  be exposed as separate root headers.
- If a type is part of the external FastSTA contract, expose it through `FastSta.hh` or move it to a stable CTS domain-data location.
- If a type exists only for implementation collaboration between subfolders, keep it under a subfolder and expose it only through private target
  include paths.
- Source outside `database/adapter/fast_sta` should depend on `FastSta.hh` or a true database/domain header, not `FastStaClock*.hh` or
  `FastStaIds.hh`.
- Re-check `module/characterization` and `flow/optimization` after this change because they are the current direct users.

## `database/io` Target Shape

### Current Problem

The directory root currently includes:

- `ClockIdbWritebackData.hh`
- `ClockIdbPinMembership.cc`

The user rejected `Writeback` and `Membership`, and prefers the visible shape around the original `Wrapper` / `WrapperClockReader` /
`WrapperClockWriter` files.

### Target Shape

Preferred root:

```text
database/io/
  CMakeLists.txt
  Wrapper.hh
  Wrapper.cc
  WrapperClockReader.cc
  WrapperClockWriter.cc
```

Correction strategy:

- First try to fold writer-only declarations and helper routines into `WrapperClockWriter.cc` as local implementation details.
- If the writer code becomes too large for one file, split it under a writer-specific subfolder and keep the root visible shape unchanged.
- Avoid root-level helper filenames for writer preservation data and iDB pin attachment logic.
- Avoid replacing `Writeback` / `Membership` with other vague terms. Use direct CTS/iDB clock-tree wording only after user confirmation if a split
  remains necessary.

This correction direction is confirmed by the user.

## `module/characterization` Target Shape

### Current Problem

The directory root mixes:

- `CharBuilder` external construction contract;
- builder implementation slices;
- characterization circuit construction;
- wire/slew/load sampling;
- pattern enumeration and storage;
- feasibility checks;
- frontier/table/trait helpers used by H-tree pruning;
- buffer-cell electrical limits.

### Proposed Folder Split

```text
module/characterization/
  CMakeLists.txt
  Characterization.hh
  Characterization.cc
  builder/
  buffer_cell/
  circuit/
  sampling/
  pattern/
  table/
  pruning/
```

Responsibility intent:

- `builder/`: build flow, options normalization, progress state, top-level build orchestration.
- `circuit/`: temporary characterization circuit creation, parasitic setup, and cleanup.
- `sampling/`: slew/load sampling and STA sample collection.
- `pattern/`: buffering-pattern enumeration and pattern storage.
- `table/`: segment/H-tree characterization table storage helpers.
- `pruning/`: frontier, join engine, and trait helpers used by H-tree pruning.
- `buffer_cell/`: buffer-cell electrical and pin-role data if it remains a characterization-owned public contract.

Final correction:

- `Characterization.hh/.cc` is the only root contract for `module/characterization`.
- `CharBuilder`, `CharacterizationBufferCell`, pattern combiners, pruning keys/traits, and characterization tables live under responsibility
  subfolders.
- External source and tests include `characterization/Characterization.hh` when they need characterization contracts; they do not include
  `characterization/builder`, `buffer_cell`, `pattern`, `pruning`, or `table` paths directly.

## `module/routing/bound_skew_tree` Target Shape

### Current Problem

The directory root mixes:

- `BSTRouter` external routing entry;
- `BoundSkewTree` algorithm class;
- tree merge/join/embed algorithm slices;
- geometry calculation;
- routing components;
- routing configuration;
- route-tree conversion.

### Proposed Folder Split

```text
module/routing/bound_skew_tree/
  CMakeLists.txt
  BSTRouter.hh
  BSTRouter.cc
  tree/
  geometry/
  component/
  clock_tree_conversion/
  config/
```

Responsibility intent:

- `tree/`: `BoundSkewTree` construction, merge, joining, balance, embedding, topology flow.
- `geometry/`: geometric calculation and point/line/rect routines.
- `component/`: area, point, line, match, and other BST working components.
- `clock_tree_conversion/`: conversion to/from `ClockSteinerTree` structures.
- `config/`: BST routing parameters and enum contracts if they must be shared with the root router.

Recommended default:

- Keep `BSTRouter.hh/.cc` as the root external contract.
- Move `BSTRoutingConfig.hh`, `BoundSkewTree.hh/.cc`, `Components.hh/.cc`, `GeomCalc.hh/.cc`, and algorithm slices under subfolders.
- Do not expose geometry or components to external routing users unless a concrete caller proves they are part of the CTS routing API.
- External source includes only `bound_skew_tree/BSTRouter.hh`; `BSTRouter.hh` re-exports the routing config needed by callers.

## Secondary Convergence Targets

### H-tree Analytical Solver

Likely root external contract:

- `AnalyticalSolver.hh`

Candidate subfolders:

- `candidate/`
- `search/`
- `model/`
- `shortlist/`
- `validation/`

Need care because `AnalyticalCandidate.hh` is part of result data. If it remains public, either keep it as an explicit solver contract or move it to
a stable H-tree analytical data location.

### SDC Adapter

Final root external contract:

- `SdcClockReader.hh`

Implementation subfolders:

- `clock_parser/`
- `clock_trace/`

The SDC adapter follows the same strict behavior-directory rule as FastSTA. `SdcClockReader.hh` owns the public SDC clock declaration, case-analysis,
and trace-result contracts. Parser and trace internals are not external include sites.

### Fast Clustering

Likely root external contract:

- `FastClustering.hh/.cc`

Candidate subfolders:

- `boundary/`
- `partition/`
- `polish/`
- `geometry/`
- `finalize/`

`FastClusteringDraft.hh` should not be a root public header if it is only working data for the clustering algorithm.

### STA Adapter

Likely root external contract:

- `STAAdapter.hh/.cc`

`STAAdapterTimingQuery.hh` appears to be adapter-internal. Prefer moving it under a subfolder or making it local to implementation if possible.

### H-tree Solution

This directory should be reviewed after analytical-solver and H-tree folder convergence. Its names are still generic relative to CTS/H-tree
semantics, so it belongs partly to the final naming pass.

## Secondary Convergence Decisions

The implementation keeps the strict root-contract rule for large behavior directories and uses documented exceptions only where the root headers are
actual CTS data or subflow contracts.

- `flow/synthesis/htree/analytical_solver`: strict root contract. Root exposes only `AnalyticalSolver.hh/.cc`; candidate, model, and selection
  details live under subfolders.
- `module/topology/fast_clustering`: strict root contract. Root exposes only `FastClustering.hh/.cc`; boundary search, partition, polish, geometry,
  finalization, and clustering draft data live under responsibility subfolders.
- `database/adapter/sta`: strict root contract. Root exposes only `STAAdapter.hh/.cc`; iSTA cell-master lookup, clock lookup, net RC, RC-tree,
  root-driver, timing query, and timing update code live under responsibility subfolders.
- `flow/synthesis/htree/solution`: strict root contract. Root exposes only `Solution.hh/.cc`; analytical solution, selection, and reporting code
  live under subfolders.
- `database/adapter/sdc`: strict root contract. Root exposes only `SdcClockReader.hh/.cc`; SDC clock declarations, trace result contracts, and the
  parser/trace implementation are owned behind that facade.
- `module/characterization`: strict root contract. Root exposes only `Characterization.hh/.cc`; builder, buffer-cell, pattern, pruning, and table
  contracts are reached through the facade.
- `module/routing/bound_skew_tree`: strict root contract. Root exposes only `BSTRouter.hh/.cc`; routing config, tree, geometry, component, and
  clock-tree conversion details are under subfolders.
- `flow/synthesis/htree/segment_pruning`: documented H-tree shared-contract exception. The segment frontier and pattern-library headers are
  intentionally consumed by sibling H-tree subflows. Their names are kept for user naming review rather than hidden as private implementation
  details.
- `database/design`, `database/characterization`, `database/routing`, and `database/spatial`: documented data-model exceptions. These roots expose
  stable CTS objects and are not treated as behavior-module helper roots.

## New-File Naming Review

Naming review waits until folder convergence is done.

Process:

1. Run git status and rename detection.
2. Separate pure moves from newly introduced files.
3. Produce a user-facing list of true new source files and moved-and-edited files whose names are still uncertain.
4. Ask the user for naming direction before uncertain renames.
5. Apply approved names and update CMake/includes.

Terms to avoid in source structural names unless explicitly approved:

```text
snapshot
Internal
Support
Request
Response
Types
rollback
fallback
Input
Session
Writeback
Membership
```

`Network` is allowed only for established CTS/database semantics such as `ClockNetwork`; do not introduce it casually where it can be confused with
CTS clock-net or `Net` semantics.

## Validation Design

Validation runs in layers:

1. Focused build target after each moved directory.
2. Include and naming scans for the corrected area.
3. iCTS source quality check.
4. Focused iCTS tests affected by the moved source.
5. Final `ics55_dev` binary command if implementation touches runtime behavior paths.

Final quality command:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Runtime command when needed:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```
