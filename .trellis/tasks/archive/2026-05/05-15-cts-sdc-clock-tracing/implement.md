# CTS SDC Clock Tracing Implementation Plan

## Implementation Checklist

- [x] Move the CTS clock-only SDC reader boundary from `database/adapter/sta` to `database/adapter/sdc`.
- [x] Add CTS-owned SDC record types for object references, primary/generated clocks, and case analysis.
- [x] Implement a side-effect-free SDC subset parser/evaluator for:
  - [x] comments, command separators, quotes/braces/brackets
  - [x] `set`
  - [x] `$var` substitution
  - [x] bracket command substitution
  - [x] `expr` arithmetic needed by clock periods
  - [x] `set_units -time`
  - [x] `get_ports`, `get_pins`, `get_nets`, `get_clocks`, `all_clocks`
  - [x] `create_clock`, `create_generated_clock`, `set_case_analysis`
- [x] Keep unsupported non-clock commands as ignored diagnostics, not fatal parse failures.
- [x] Implement clock trace resolver over iDB + Liberty without full-design STA.
- [x] Distinguish trace-through nets from synthesis target nets.
- [x] Support multiple accepted CTS target nets per SDC clock.
- [x] Preserve compatibility with configured `use_netlist` mappings as fallback/manual input.
- [x] Add `Clock Trace Overview` report for accepted/rejected/ambiguous/trace-through records.
- [x] Add `SDC Clock Ownership Overview` report with clock kind, master clock, owned nets, and CTS target nets.
- [x] Add report-only `Unowned Clock-like Nets` diagnostics for CK-sink-driving nets without SDC ownership.
- [x] Wire tracing into `DesignConversion::readClockData()` before `Wrapper::readClocks()`.
- [x] Ensure characterization still uses char-only STA and no full-design STA refresh is called by clock discovery.

## Validation

Do not run ecc dev checks until the design-script validation passes.

Primary validation command:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Validation setup:

- Disable manual `use_netlist` for the run.
- Confirm the SDC parser reports `[get_ports clock]` as a port seed.
- Confirm clock tracing discovers accepted downstream CTS target nets without manual `clock -> n194404` mapping.
- Confirm the flow completes and emits a correct `Clock Trace Overview`.

After the primary validation passes:

- Run the repository's ecc dev checks required by the backend quality workflow.
- Fix any failures introduced by this task.

## Review Gates

- Gate 1: SDC adapter compiles and parses both `scripts/design/ics55_dev/default.sdc` and `scripts/design/ics55_huge_dev/constraints.sdc` without external SDC edits.
- Gate 2: CTS read-data accepts multiple target nets for one SDC clock.
- Gate 3: `ics55_huge_dev` script passes with manual `use_netlist` disabled.
- Gate 4: ecc dev checks pass after Gate 3.

## Rollback Points

- If structural tracing is incomplete, keep the configured `use_netlist` fallback path operational.
- If the SDC parser rejects a previously accepted SDC, allow read-data to emit diagnostics and fall back to manual mappings when configured.
