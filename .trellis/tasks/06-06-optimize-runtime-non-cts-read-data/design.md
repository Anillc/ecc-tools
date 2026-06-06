# Design

## Problem

The current `ics55_dev` CTS runtime is dominated by `CTSReadData`, but the
measured cost is iSTA Liberty parser/linker work:

- CTS total: `43.883 s`
- `read_data`: `32.656 s` (`74.42%`)
- Liberty load/link inside `read_data`: about `32.516 s`
- Liberty link alone: about `26.992 s`
- Post-Liberty clock trace/materialization/report: about `0.138 s`

The optimization target for this task is iSTA's Liberty reader/linker
implementation. CTS is the workload entry that exposes the issue, not the code
area to change.

## Development Principles

- Do not edit CTS source code in this task.
- Prioritize performance as long as existing Liberty reader functionality and
  data semantics are preserved.
- Keep changes local and reviewable inside iSTA/Liberty parser code, but do not
  interpret "minimal invasive" as "only tiny edits"; a local algorithm
  replacement is acceptable when it removes measured overhead and preserves
  behavior.
- Avoid changing Liberty syntax support, data-model ownership, timing/power
  semantics, or configured library coverage.
- Prefer improvements that benefit both full-library link and iSTA's existing
  `_build_cells` filtered link path.

## Current Data Flow

```text
Lib::loadLibertyWithCppParser
  -> LibertyReader::readLib
     -> liberty_parse_lib
  -> caller stores LibertyReader

LibertyReader::linkLib
  -> liberty_convert_raw_group_stmt
  -> LibertyReader::visitGroup
     -> LibertyReader::visitLibrary / visitCell / visitPin / ...
        -> LibertyReader::visitStmtInGroup
           -> LibertyReader::visitSimpleAttri
           -> LibertyReader::visitComplexAttri
           -> LibertyReader::visitGroup
  -> liberty_free_lib_group
```

iSTA's normal owner path is:

```text
Sta::readLiberty(vector)
  -> parallel Sta::readLiberty(file)
  -> LibertyReader::readLib

Sta::linkLibertys
  -> parallel LibertyReader::linkLib
  -> set_build_cells(get_link_cells())
  -> add linked LibLibrary
```

`TimingIDBAdapter::configStaLinkCells()` can pre-populate `_build_cells` to skip
unneeded cells during normal iSTA DB conversion. This is useful but is not the
primary optimization target here because the current measured hotspot is inside
the LibertyReader visitor/linker implementation itself.

## Bottleneck Model

The measured split says raw Liberty parse is not the largest cost:

- Two raw file loads: about `5.524 s`.
- Two links/builds: about `26.992 s`.

The source shape matches the perf stack:

- `LibertyReader::visitGroup()` constructs a local
  `std::map<std::string, std::function<...>>` and `std::bind` dispatch table on
  every group visit, then does `contains` plus `operator[]`.
- `LibertyReader::visitSimpleAttri()` constructs a large local
  `std::map<std::string, std::function<void()>>` with many capturing lambdas on
  every simple attribute visit, then does `contains` plus `operator[]`.
- `LibertyReader::visitComplexAttri()` uses the same dynamic-map pattern for
  complex attributes.
- `LibertyReader::visitPin()` runs a regex match for every pin name before the
  common scalar-pin case.
- `visitStmtInGroup()` scans each group's statement vector twice to preserve
  simple-attribute-before-child-group semantics.
- `visitAxisOrValues()` allocates per value while splitting table strings.

The first three items are the highest-confidence algorithmic overhead because
they create dynamic containers and type-erased call wrappers per Liberty node.
They also line up with perf samples under `visitPin`, `visitInternalPower`, and
`visitSimpleAttri`.

## Proposed Implementation Slices

### Slice A: Visitor Dispatch Fast Path

Replace per-call local `std::map<std::function>` dispatch in
`visitGroup()`, `visitSimpleAttri()`, and `visitComplexAttri()` with local,
allocation-free dispatch:

- Use `std::string_view` or existing `Str::equal` comparisons.
- Call member handlers directly instead of through `std::function`.
- Avoid `contains` plus `operator[]` double lookup.
- Preserve every existing branch's conversion/free behavior and side effects.

Expected impact: high, because this removes repeated map construction,
heap-allocation-prone function wrappers, string tree lookup, and copied
`std::function` calls from the hottest link path.

Risk: medium. The code is broad because `visitSimpleAttri()` handles many
attributes. The mitigation is to keep each branch behavior-equivalent and rely
on Liberty reader tests plus baseline output comparison.

### Slice B: Pin Name Fast Path

Optimize `visitPin()` by avoiding regex work for the common scalar-pin case:

- If the pin name has no `'['`, create the scalar port directly.
- If it has brackets but no bus range form, preserve current behavior.
- For range forms like `A[3:0]`, use a small manual parser or a static compiled
  pattern rather than rebuilding/matching a dynamic regex string each time.

Expected impact: medium-high because perf shows `visitPin()` as the largest
child symbol under link.

Risk: low-medium if limited to preserving current accepted range syntax.

### Slice C: Statement Traversal and Table Parsing

Only consider after re-measuring Slices A/B:

- Single-pass classified traversal for `visitStmtInGroup()` while preserving
  simple-first semantics.
- Lower-allocation table value parsing in `visitAxisOrValues()`.

Expected impact: uncertain until dispatch and pin-name overhead are removed.

Risk: higher than Slices A/B because traversal order and table parsing are more
semantics-sensitive.

## Compatibility

- Existing Liberty reader public APIs stay intact.
- Existing parse/link result ownership stays intact.
- All currently supported attributes and groups must keep their existing side
  effects.
- Unknown attribute/group logging behavior should remain equivalent.
- Existing `_build_cells` filtering semantics must remain unchanged.

## Validation

Minimum validation after implementation:

```bash
ninja -C build liberty
ninja -C build iEDA
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Do not run `ecc_dev_tools` for `src/operation/iSTA` in this task. The iSTA
Liberty parser is the touched external module, and validation should stay on
targeted build plus functional/runtime/QoR comparison.

Runtime comparison:

- CTS total runtime.
- `read_data` runtime.
- Liberty load/link timestamps from `run.stderr`.
- `perf` children report for `LibertyReader::linkLib`, `visitPin`,
  `visitInternalPower`, and `visitSimpleAttri`.

Functional comparison:

- Liberty tests pass.
- iCTS command completes successfully.
- Key `iCTS_metrics.json` values remain stable:
  - `buffer_num`
  - `clock_path_max_buffer`
  - `clock_path_min_buffer`
  - `max_clock_wirelength`
  - `max_level_of_clock_tree`
  - `total_clock_wirelength`

## Rollback

Each slice should be independently revertible:

- Slice A only changes dispatch mechanics; revert to original local maps if any
  branch behavior diverges.
- Slice B only changes pin-name classification; revert to original regex path
  if bus/range compatibility fails.
- Slice C should not start unless Slices A/B have been measured and accepted.
