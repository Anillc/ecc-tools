# Optimize CTS read_data performance

## Goal

Analyze and optimize the iCTS `read_data` stage runtime on the `ics55_huge_dev` case, using `design_clock.def` as the temporary dev-script input so the large clock sink set exercises the CTS clock materialization path.

## Background / Known Context

- The user observed that running `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl` spends a long time in CTS `read_data`.
- `run_iCTS_dev.tcl` currently defaults `INPUT_DEF` to `$WORKSPACE/design.def`.
- For this task only, the dev script should temporarily default to `$WORKSPACE/design_clock.def`; it must be restored to `$WORKSPACE/design.def` before task completion.
- `design_clock.def` is the large-clock experimental DEF used in earlier huge-case CTS validation.
- The target stage is iCTS clock data reading and materialization, not the later final evaluation STA stall.

## Requirements

- Temporarily switch the huge dev script input to `design_clock.def` for reproducible local investigation.
- Run or instrument the huge dev command enough to isolate where CTS `read_data` time is spent.
- Identify whether the delay is from Tcl setup / iDB input loading / SDC clock resolution / CTS wrapper conversion / CTS design indexing / reporting.
- If there is a local iCTS code optimization with acceptable risk, implement it in the CTS read-data path.
- Preserve existing behavior and output semantics for clock source, loads, nets, pin indexing, and clock distribution summary.
- Restore `run_iCTS_dev.tcl` to `design.def` before finishing this task.

## Acceptance Criteria

- [ ] The analysis names the concrete `read_data` hotspot with supporting timing or profiling evidence.
- [ ] Any implemented optimization is narrowly scoped to CTS read-data materialization and does not change expected CTS topology semantics.
- [ ] The huge dev script is restored to `design.def` before final handoff.
- [ ] Relevant iCTS build/tests pass, or any inability to run them is reported with a concrete reason.
- [ ] The final report includes baseline/after timing when available, code-level cause, implemented change, and remaining risk.

## Definition of Done

- Tests added or updated when behavior risk warrants it.
- Build and focused tests pass for the touched CTS code.
- Trellis quality checks are run before completion if code changes are made.
- No temporary script change remains unless the user explicitly requests it.

## Out of Scope

- Optimizing final evaluation STA slew propagation.
- Changing analytical H-tree behavior or QoR policy.
- Changing the huge design input files themselves.
- Adding broad iDB, iSTA, or Tcl infrastructure changes beyond what is needed to isolate CTS `read_data`.

## Research References

- To be filled during investigation.
