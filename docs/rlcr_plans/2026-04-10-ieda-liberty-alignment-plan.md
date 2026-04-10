# iEDA Liberty Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring `iEDA`'s exported timing-model Liberty for `NV_NVDLA_partition_m` up to the acceptance criteria defined in [reports/liberty-alignment-acceptance-criteria.md](/home/zhaoxueyan/code/write-lib_back/reports/liberty-alignment-acceptance-criteria.md), starting with structural/semantic parity and then closing quantitative gaps against OpenROAD.

**Architecture:** Keep `LibLibrary` / `LibCell` / `LibPort` / `LibArc` as the internal IR, but stop treating `printLibertyLibrary()` as a best-effort dump. The implementation should evolve in three layers: populate richer IR in `StaCharacterTiming`, expose a more explicit export API in `TimingEngine`, and refactor the Liberty writer into a deterministic serializer with regression coverage. Structural/semantic parity should be solved before LUT/table parity; do not hard-code ASAP7-specific templates or index values.

**Tech Stack:** C++20, iSTA/iEDA Liberty IR (`Lib.hh` / `Lib.cc`), `StaCharacterTiming`, `TimingEngine`, GoogleTest, NV_NVDLA benchmark, OpenROAD golden output

---

## Scope and assumptions

- This plan targets the `iEDA` repository under `/home/zhaoxueyan/code/write-lib_back/iEDA`.
- The acceptance target is the staged standard already written in [reports/liberty-alignment-acceptance-criteria.md](/home/zhaoxueyan/code/write-lib_back/reports/liberty-alignment-acceptance-criteria.md).
- `NV_NVDLA_partition_m` remains the primary golden case for regression and comparison.
- `OpenROAD` remains the golden exporter for comparison, but the success condition is semantic interchangeability, not text identity.
- Power/ground pin parity likely requires design database context beyond plain Verilog. The implementation should therefore support an optional DEF/IDB-assisted export path instead of assuming all information is present in Verilog.
- Do not write PDK-specific LUT index values, thresholds, or pin naming conventions into the main flow.

## File map

### Primary implementation files

- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.cc`

### Regression / comparison files

- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/CharacterTimingTest.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyTest.cc`
- Create: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`
- Create: `/home/zhaoxueyan/code/write-lib_back/scripts/ieda/compare_openroad_ieda_liberty.py`

### Supporting docs to update after implementation

- Update: `/home/zhaoxueyan/code/write-lib_back/reports/NV_NVDLA_partition_m-openroad-vs-ieda.md`
- Update: `/home/zhaoxueyan/code/write-lib_back/reports/liberty-alignment-acceptance-criteria.md`

## Milestones

- Milestone A: Structural/semantic parity
  - No duplicate pins
  - `timing_type` complete
  - setup/hold both present
  - bus/type and library-level metadata present
- Milestone B: Interface parity
  - power/ground pins present when IDB/DEF context is available
  - normalized pin and arc coverage reaches 100%
- Milestone C: Quantitative parity
  - LUT/template/index generation no longer degenerates to scalar-only output where OpenROAD exports tables
  - normalized arc error mostly within `max(5ps, 5%)`
- Milestone D: Downstream replaceability
  - WNS/endpoint behavior stays within the agreed tolerance envelope

### Task 1: Freeze the baseline and add machine-checkable alignment tests

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/CharacterTimingTest.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyTest.cc`
- Create: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`

- [ ] **Step 1: Add a focused Liberty-structure regression test file**

Create `LibertyAlignmentTest.cc` with file-level checks for:
- duplicated pin names
- missing `timing_type`
- presence of `setup_*` and `hold_*`
- presence of library header fields such as `time_unit`, `capacitive_load_unit`, and `delay_model`
- presence of `bus(...)` and `type(...)`

- [ ] **Step 2: Extend `CharacterTimingTest.example1` into a reusable benchmark harness**

Refactor the current one-shot test in `CharacterTimingTest.cc` so it can:
- emit deterministic output paths
- optionally generate both max-view and min-view timing models
- preserve today’s benchmark inputs:
  - Verilog: `NV_NVDLA_partition_m.v`
  - SDC: `workspace/output/dreamplace/compat/NV_NVDLA_partition_m_optimizer_compat.sdc`

- [ ] **Step 3: Run the new tests in failing mode to capture today’s gaps**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='CharacterTimingTest.example1:LibertyAlignmentTest.*'
```

Expected at this stage:
- `CharacterTimingTest.example1` still passes
- the new alignment tests fail on duplicate pins, missing `timing_type`, missing hold arcs, and missing library metadata

- [ ] **Step 4: Record the failing baseline in the plan execution log**

Capture the current output files and failure symptoms under:
- `/home/zhaoxueyan/code/write-lib_back/artifacts/ieda/character_timing/NV_NVDLA_partition_m`

### Task 2: Refactor Liberty serialization so one logical pin maps to one emitted block

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.hh`
- Test: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`

- [ ] **Step 1: Write a failing test that asserts no duplicate pin blocks**

Add a regression asserting:
- duplicated pin names = `0`
- `nvdla_core_clk` appears exactly once as a pin declaration

- [ ] **Step 2: Replace the current three-pass pin emission logic with a normalized pin aggregation pass**

In `Lib.cc`, stop serializing pins in:
1. delay-source sweep
2. remaining cell port sweep
3. sink-port timing sweep

Instead:
- build one normalized `PinWriteRecord` per logical port
- merge direction, capacitance, function, and timing arcs into that record
- emit each pin exactly once

- [ ] **Step 3: Preserve deterministic ordering**

Keep emission order stable by:
- using library/interface order where possible
- preserving bus member order
- explicitly sorting fallback maps by pin name when no stronger order exists

- [ ] **Step 4: Re-run the structure-only tests**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='LibertyAlignmentTest.no_duplicate_*:LibertyAlignmentTest.pin_*'
```

Expected:
- duplicate-pin failures disappear
- remaining failures move to metadata and timing semantics

### Task 3: Make the writer emit complete library-level metadata, bus definitions, and type definitions

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc`
- Test: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`

- [ ] **Step 1: Add failing tests for missing header metadata and bus/type coverage**

Add assertions for:
- `time_unit`
- `capacitive_load_unit`
- `delay_model`
- at least one `bus(...)`
- at least one `type(...)`

- [ ] **Step 2: Promote existing library fields from passive storage to serializer output**

`LibLibrary` already stores multiple relevant fields in `Lib.hh`.
Update `printLibertyLibrary()` to emit:
- units
- thresholds
- nominal voltage
- `delay_model`
- LUT templates already present in `_lut_templates`
- `type(...)` definitions already present in `_types`

- [ ] **Step 3: Populate bus/type IR during timing-model generation**

Extend `StaCharacterTiming` so interface collection does not flatten everything immediately.
Use:
- `LibType`
- `LibPortBus`
- bus width / range information from netlist or IDB bus metadata when available

The fallback rule should be:
- if only scalar ports are known, export scalars correctly
- if bus structure is known, preserve bus structure instead of flattening

- [ ] **Step 4: Plan and implement the optional power-pin source path**

Because current characterization only reads `Verilog + SDC`, define an optional path that can query IDB/DEF context when available for:
- top-level IO pin list
- pin use / type
- power/ground pins such as `VDD` and `VSS`

This work may require adding an export-context object or API hook rather than assuming Verilog contains supply pins.

- [ ] **Step 5: Re-run bus/header regressions**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='LibertyAlignmentTest.library_header_*:LibertyAlignmentTest.bus_*:LibertyAlignmentTest.type_*'
```

### Task 4: Make timing semantics complete, especially delay `timing_type` and hold arcs

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/CharacterTimingTest.cc`
- Test: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`

- [ ] **Step 1: Add a failing regression for missing delay `timing_type`**

Assert:
- no `timing()` block is missing `timing_type`
- `rising_edge` or `falling_edge` delay arcs are present for clocked outputs

- [ ] **Step 2: Emit `timing_type` for delay arcs**

In `Lib.cc`, update the delay-arc serializer to emit the arc type already computed in `StaCharacterTiming`:
- `rising_edge`
- `falling_edge`
- `combinational`

- [ ] **Step 3: Introduce a dual-view export path instead of max-only export**

`TimingEngine::extractTimingModel()` currently builds one `StaCharacterTiming` using a single `AnalysisMode`.
Refactor this so export can produce:
- max-view check arcs for setup
- min-view check arcs for hold
- a merged `LibLibrary` output containing both

Recommended implementation direction:
- add a dedicated export options struct in `TimingEngine`
- keep the old API as a compatibility wrapper if needed
- let the merged export be the default path for harden timing-model generation

- [ ] **Step 4: Add tests for setup/hold coexistence**

Update `CharacterTimingTest` and `LibertyAlignmentTest` to assert:
- `setup_*` present
- `hold_*` present
- delay arcs still present after the merge

- [ ] **Step 5: Re-run semantic regressions**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='CharacterTimingTest.example1:LibertyAlignmentTest.timing_*'
```

Expected:
- missing-`timing_type` failures disappear
- hold-arc failures disappear

### Task 5: Separate export configuration from PDK specifics and add characterization/table configuration

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/api/TimingEngine.cc`
- Possibly Create: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaTimingModelExportConfig.hh`
- Possibly Create: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaTimingModelExportConfig.cc`

- [ ] **Step 1: Define an explicit export/config object**

Introduce a config object that can carry:
- whether to merge max/min
- whether IDB/DEF context is available
- characterization sample policy
- output library naming
- optional template strategy

This avoids baking ASAP7 assumptions directly into `StaCharacterTiming`.

- [ ] **Step 2: Encode non-hardcoded defaults**

Defaults should come from:
- source library units and thresholds
- design interface structure
- runtime characterization settings

Not from:
- benchmark-specific pin names
- ASAP7-specific templates
- fixed index vectors

- [ ] **Step 3: Add a regression around config-driven export**

Create a small unit or file-based test that verifies:
- export still works with only Verilog+SDC
- richer metadata appears when optional context is supplied

### Task 6: Add LUT/template/index generation as a second-phase characterization improvement

**Files:**
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/database/manager/parser/liberty/Lib.cc`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.hh`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/source/module/sta/StaCharacterTiming.cc`
- Test: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/LibertyAlignmentTest.cc`

- [ ] **Step 1: Add failing regressions for template and index presence**

Assert for the golden case:
- `lu_table_template(...)` exists
- `index_1(...)` exists for non-scalar tables

- [ ] **Step 2: Implement template construction without hardcoding**

Choose one of these sources, in this order:
1. derive from explicit characterization sweep configuration
2. reuse compatible template shapes inferred from source library arcs
3. fall back to scalar only when the model truly has only a single sample

- [ ] **Step 3: Add multi-sample characterization for interface arcs**

This is the first genuinely larger algorithmic step.
For interface arcs, generate multiple samples over:
- output load
- optional input slew

Then populate:
- table axes
- template objects
- table values

The implementation must make the sampling policy configurable and PDK-agnostic.

- [ ] **Step 4: Keep scalar fallback explicit**

If an arc genuinely cannot support multi-point characterization yet:
- keep emitting scalar
- but do not pretend it is a LUT
- make the gap visible in the comparison script

- [ ] **Step 5: Re-run LUT regressions**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='LibertyAlignmentTest.template_*:LibertyAlignmentTest.index_*'
```

### Task 7: Automate OpenROAD-vs-iEDA comparison and gate the rollout with acceptance metrics

**Files:**
- Create: `/home/zhaoxueyan/code/write-lib_back/scripts/ieda/compare_openroad_ieda_liberty.py`
- Modify: `/home/zhaoxueyan/code/write-lib_back/iEDA/src/operation/iSTA/test/CharacterTimingTest.cc`
- Update: `/home/zhaoxueyan/code/write-lib_back/reports/NV_NVDLA_partition_m-openroad-vs-ieda.md`

- [ ] **Step 1: Write a comparison script that normalizes before comparing**

The script should compare:
- library header fields
- normalized pin sets
- normalized arc sets
- timing-type coverage
- bus/type coverage
- table/template coverage

It should not rely on textual line diff.

- [ ] **Step 2: Add quantitative comparisons**

Once tables exist, compare:
- normalized scalar delays/constraints
- interpolated table values when templates differ
- max absolute / relative errors

- [ ] **Step 3: Add a machine-readable summary output**

Emit JSON or Markdown summary containing:
- pass/fail for each acceptance layer
- counts and coverage
- worst mismatches

- [ ] **Step 4: Re-run the full golden-case flow**

Run:

```bash
cd /home/zhaoxueyan/code/write-lib_back/iEDA
./bin/iSTATest --gtest_filter='CharacterTimingTest.example1'

python3 /home/zhaoxueyan/code/write-lib_back/scripts/ieda/compare_openroad_ieda_liberty.py \
  --openroad-lib /home/zhaoxueyan/code/write-lib_back/artifacts/NV_NVDLA_partition_m/openroad/NV_NVDLA_partition_m.lib \
  --ieda-lib /home/zhaoxueyan/code/write-lib_back/artifacts/ieda/character_timing/NV_NVDLA_partition_m/NV_NVDLA_partition_m.characterized.max.lib
```

- [ ] **Step 5: Update the report with new measured parity**

Refresh:
- structural parity numbers
- semantic parity numbers
- table parity numbers
- unresolved gaps

## Recommended execution order

Implement in this order:

1. Task 1
2. Task 2
3. Task 3
4. Task 4
5. Task 7 structural/semantic parts
6. Task 5
7. Task 6
8. Task 7 quantitative parts

This ordering ensures:
- the writer stops emitting malformed output first
- semantic coverage closes before more expensive characterization work begins
- LUT generation is only attempted after the output format can represent it correctly

## Verification checklist

Before calling the work “aligned”, verify all of the following against the golden case:

- [ ] duplicated pin names = `0`
- [ ] pin unique-name coverage = `100%`
- [ ] bus/type coverage = `100%` where design context is available
- [ ] missing `timing_type` count = `0`
- [ ] `setup_*` and `hold_*` coexist in the merged export
- [ ] delay arc `timing_type` values are present and normalized
- [ ] library units and `delay_model` are emitted
- [ ] templates/indexes are generated from characterization logic, not hardcoded
- [ ] comparison script reports structural/semantic parity as PASS
- [ ] quantitative comparisons satisfy the thresholds in the acceptance doc

## Risks to watch

- Power/ground pins may require IDB/DEF integration, not only Verilog parsing.
- LUT parity is a larger characterization problem than serializer parity; do not block early semantic fixes on it.
- Existing Liberty IR classes already store some metadata, so avoid inventing a second parallel model unless necessary.
- The current test only exports `kMax`; forgetting to add a merged min/max path will leave hold parity permanently broken.
- Bus restoration must preserve generality across PDKs and naming styles.

## Completion definition

This plan is complete when:

- `CharacterTimingTest.example1` exports a richer Liberty without structural errors
- `LibertyAlignmentTest.*` passes
- the comparison script reports acceptance-layer progress using the agreed criteria
- the updated report shows which acceptance layers are now satisfied and which remain open
