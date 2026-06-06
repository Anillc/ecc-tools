# iSTA Liberty Parser/Linker Core Analysis

## Scope

This note researches the Liberty parsing/linking logic that dominates the
current `ics55_dev` CTS `read_data` runtime.

The task scope is iSTA-side optimization. CTS source code is not an
implementation target for this task; CTS is only the workload entry that exposes
the iSTA Liberty parser/linker cost.

Development principle:

- Performance is the priority.
- Existing Liberty reader functionality and semantics must not change.
- Minimal invasiveness means local, reviewable boundaries, not avoiding a local
  algorithm replacement when the existing local algorithm is clearly expensive.

## Runtime Evidence

Task-local baseline:

- CTS total: `43.883 s`.
- `read_data`: `32.656 s`, `74.42%` of CTS total.
- Two Liberty file load/link sequences inside `read_data`: about `32.516 s`.
- Raw Liberty load total: about `5.524 s`.
- Liberty link total: about `26.992 s`.
- Post-Liberty clock trace/materialize/report: about `0.138 s`.

perf children-mode evidence:

- `icts::Wrapper::loadLibertyIfNeeded`: `60.32%`.
- `ista::LibertyReader::linkLib`: `50.97%`.
- `ista::LibertyReader::visitPin`: `38.67%`.
- `ista::LibertyReader::visitInternalPower`: `21.26%`.
- `ista::LibertyReader::visitSimpleAttri`: repeated sub-hotspot below pin,
  internal power, table, and group traversal frames.

Interpretation:

- The heavy part is not CTS synthesis.
- The heavy part is not the small clock-trace work after Liberty is available.
- The dominant cost is iSTA Liberty link/object construction.

## iSTA Liberty Entry Points

`src/database/manager/parser/liberty/Lib.cc:2127`

```text
Lib::loadLibertyWithCppParser(file)
  -> LibertyReader liberty_reader(file)
  -> liberty_reader.readLib()
  -> return LibertyReader
```

`src/database/manager/parser/liberty/LibParserCpp.cc:1396`

```text
LibertyReader::readLib()
  -> liberty_parse_lib(_file_name.c_str())
```

This creates the raw parsed Liberty representation.

`src/database/manager/parser/liberty/LibParserCpp.cc:1415`

```text
LibertyReader::linkLib()
  -> liberty_convert_raw_group_stmt(_lib_file)
  -> visitGroup(lib_group)
  -> liberty_free_lib_group(_lib_file)
```

This converts raw parsed statements into iSTA's `LibLibrary`, `LibCell`,
`LibPort`, timing arc, power arc, table, and attribute objects. The baseline
shows this link phase is much more expensive than raw file parse.

## Normal iSTA Owner Flow

`src/operation/iSTA/source/module/sta/Sta.cc:549`

```text
Sta::readLiberty(file)
  -> Lib::loadLibertyWithCppParser(file)
  -> addLibReaders(reader)
```

`src/operation/iSTA/source/module/sta/Sta.cc:571`

`Sta::readLiberty(vector)` loads files through a `ThreadPool`.

`src/operation/iSTA/source/module/sta/Sta.cc:602`

```text
Sta::linkLibertys()
  -> if _libs already linked, return
  -> reader.set_build_cells(get_link_cells())
  -> reader.linkLib()
  -> takeLib()
  -> addLib()
```

This link step also uses a `ThreadPool`.

`src/operation/iSTA/api/TimingIDBAdapter.cc:981`

`TimingIDBAdapter::configStaLinkCells()` collects iDB instance master names and
adds them to iSTA's link-cell set. This allows `LibertyReader::visitCell()` to
skip unneeded cells when `_build_cells` is nonempty.

`src/database/manager/parser/liberty/LibParserCpp.hh:399`

```text
set_build_cells(...)
isNeedBuild(cell_name)
```

`src/database/manager/parser/liberty/LibParserCpp.cc:959`

`visitCell()` calls `isNeedBuild(cell_name)` and returns immediately for cells
that do not need object construction.

Observation:

- iSTA already has a useful filtered-link mechanism.
- The current task is still focused on the linker implementation because perf
  points at per-node visitor/link work. Improving the visitor/linker benefits
  both full-link and filtered-link cases.

## Visitor Core Logic

Top-level link dispatch:

`src/database/manager/parser/liberty/LibParserCpp.cc:1339`

```text
visitGroup(group)
  -> inspect group->group_name
  -> dispatch library / cell / pin / timing / internal_power / table / ...
```

Group statement traversal:

`src/database/manager/parser/liberty/LibParserCpp.cc:804`

```text
visitStmtInGroup(group)
  -> first pass: simple attributes
  -> second pass: complex attributes and child groups
```

This preserves the existing simple-before-children behavior, which matters
because many child visitors depend on builder state set by simple attributes.

Attribute visitors:

`src/database/manager/parser/liberty/LibParserCpp.cc:56`

```text
visitSimpleAttri(attri)
  -> inspect attri->attri_name
  -> update library/cell/port/timing/power/table builder state
```

`src/database/manager/parser/liberty/LibParserCpp.cc:679`

```text
visitComplexAttri(attri)
  -> handle selected complex attributes
  -> delegate index*/values to visitAxisOrValues()
```

Pin and power visitors:

`src/database/manager/parser/liberty/LibParserCpp.cc:1037`

`visitPin()` creates one or more `LibPort` objects, then visits pin statements.
It currently runs a regex match for every pin name before the common scalar-pin
case.

`src/database/manager/parser/liberty/LibParserCpp.cc:1114`

`visitInternalPower()` creates `LibPowerArc` and `LibInternalPowerInfo`, visits
simple attributes first, then visits power data groups. This path amplifies
attribute visitor overhead because internal power groups are numerous in
table-heavy Liberty files.

Table value visitor:

`src/database/manager/parser/liberty/LibParserCpp.cc:606`

`visitAxisOrValues()` converts string or float Liberty values into vectors of
`LibAttrValue` objects and assigns them to table axes or values.

## Algorithmic Bottleneck Hypotheses

### 1. Dynamic dispatch table construction per group/attribute

Evidence:

- `visitGroup()` constructs a local
  `std::map<std::string, std::function<unsigned(...)>>` with `std::bind` values
  every time a group is visited.
- `visitSimpleAttri()` constructs a large local
  `std::map<std::string, std::function<void()>>` with many capturing lambdas
  every time a simple attribute is visited.
- `visitComplexAttri()` does the same for complex attributes.
- Each path uses `contains(...)` and then `operator[]`, causing double lookup.

Why this is expensive:

- Local map construction repeats for every Liberty node.
- `std::function` type erasure and capture storage add overhead.
- `std::bind` adds another layer for group dispatch.
- Ordered `std::map` lookup performs string comparisons through tree traversal.
- `operator[]` copies/creates access to the function wrapper after `contains`.

Expected impact of fix: high.

Preferred fix:

- Use direct, allocation-free dispatch with `std::string_view` or existing
  string comparison helpers.
- Call the existing visitor/member logic directly.
- Preserve every current branch body and side effect.

Risk: medium because branch coverage is broad, especially in
`visitSimpleAttri()`.

### 2. Regex on every pin name

Evidence:

- perf shows `visitPin()` as the largest child under link.
- `visitPin()` builds a regex pattern string and calls `Str::matchPattern()` on
  every pin.
- Most Liberty pins are scalar names where a range regex cannot match.

Expected impact of fix: medium-high.

Preferred fix:

- Fast path scalar pins with no `'['`.
- Keep range behavior for names like `A[3:0]`.
- If needed, replace regex with a small manual parser for the current accepted
  range shape.

Risk: low-medium. Must preserve current bus/range behavior.

### 3. Two-pass group statement traversal

Evidence:

- `visitStmtInGroup()` scans group statements twice.
- `visitInternalPower()` has a similar two-pass shape.

Expected impact of fix: uncertain after dispatch overhead is removed.

Preferred handling:

- Do not start here. The two-pass behavior encodes ordering semantics.
- Re-measure after dispatch and pin fast path.

Risk: medium-high due to ordering dependencies.

### 4. Table value parsing allocations

Evidence:

- `visitAxisOrValues()` splits comma-separated strings with `istringstream` and
  allocates one `LibFloatValue` object per parsed value.
- Table-heavy timing/power data can multiply this overhead.

Expected impact of fix: uncertain in the current perf profile because dispatch
and pin/internal-power visitor overhead dominate first.

Preferred handling:

- Defer until after dispatch/pin improvements.
- Preserve table value representation unless a broader data-model change is
  approved.

Risk: medium because timing/power table semantics are critical.

## Ranked Candidate Plan

1. Replace dynamic visitor dispatch in `visitGroup()`,
   `visitSimpleAttri()`, and `visitComplexAttri()`.
2. Re-measure.
3. Add scalar-pin/range-pin fast path in `visitPin()` if `visitPin()` remains
   high.
4. Re-measure.
5. Only then consider traversal or table parsing changes.

## Existing Test Surface

Known local test files:

- `src/operation/iSTA/test/LibertyTest.cc`
- `src/operation/iSTA/test/LibertyAlignmentTest.cc`

Examples:

- `LibertyTest.cc:87`: C++ Liberty reader smoke test.
- `LibertyTest.cc:143`: parse/link/print library JSON path.
- `LibertyTest.cc:157`: FF pin capacitance range conversion.
- `LibertyTest.cc:198`: same-sense arc behavior.
- `LibertyAlignmentTest.cc`: generated/reference Liberty alignment coverage and
  many writer/reader behavior expectations.

The exact executable/check target should be discovered before implementation.
For this task, do not use `ecc_dev_tools` on iSTA; validation should use
targeted build plus the user workload.

```bash
ninja -C build liberty
ninja -C build iEDA
```

## Validation Requirements

After implementation:

- `ninja -C build liberty` passes.
- `ninja -C build iEDA` passes.
- The user workload completes:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Compare `cts.log` runtime distribution against baseline.
- Compare Liberty load/link timestamps against baseline.
- Compare key `iCTS_metrics.json` fields against baseline.
- Optional but recommended: collect perf again and verify reduced samples under
  `std::map`, `std::function`, `visitSimpleAttri`, and `visitPin`.

## Decision Needed Before Implementation

Recommended first implementation slice:

```text
Optimize iSTA LibertyReader visitor dispatch first:
visitGroup + visitSimpleAttri + visitComplexAttri.
```

Reason:

- It targets the largest link-time algorithmic overhead visible from source and
  perf.
- It stays local to `LibParserCpp.cc`.
- It preserves parser syntax and data-model semantics.
- It does not require CTS source changes.

Trade-off:

- This is more code churn than adding instrumentation, but it is aligned with
  the user's performance-first constraint and directly attacks the measured
  bottleneck.
