# Implementation Plan

## Current Status

Planning, implementation, and runtime validation are complete for the first two
iSTA-focused optimization slices.

Implemented:

- Slice A: visitor dispatch fast path in `visitGroup()`, `visitSimpleAttri()`,
  and `visitComplexAttri()`.
- Slice B: scalar-pin fast path in `visitPin()`.

Final validation result:

- `read_data`: `32.656 s` baseline to `10.469 s` final.
- Liberty link total: `26.992 s` baseline to `4.682 s` final.
- CTS total: `43.883 s` baseline to `21.674 s` final.
- Tracked iCTS metrics match the baseline.

Two completed 06-03 tasks were archived and committed separately:

```text
11e881573 chore(task): archive completed 06-03 tasks
```

## Planned Scope

Implement performance-oriented, behavior-preserving local changes in iSTA's
Liberty reader/linker. Do not edit CTS source code for this task.

The first implementation slice should target the measured link-time overhead in
`src/database/manager/parser/liberty/LibParserCpp.cc`.

## Ordered Checklist

1. Finish planning artifacts:
   - Update `prd.md` with iSTA Liberty parser/linker scope.
   - Update `design.md` with ranked algorithmic bottlenecks.
   - Add a task-local research note for Liberty parser core logic.
2. Ask the user to review/approve starting implementation.
3. Before source edits, load project/package guidance with `trellis-before-dev`
   for the iSTA/database parser area.
4. Start task with `task.py start` only after review/approval.
5. Implement Slice A: visitor dispatch fast path.
   - Replace per-call `std::map<std::function>` construction in
     `visitGroup()`.
   - Replace per-call `std::map<std::function>` construction in
     `visitSimpleAttri()`.
   - Replace per-call `std::map<std::function>` construction in
     `visitComplexAttri()`.
   - Preserve all existing conversions, frees, logging, and side effects.
6. Build the touched Liberty parser targets. Do not run `ecc_dev_tools` for
   iSTA; iSTA is an external module for this task and the user explicitly
   requested not to use ecc dev checks on it.
   - `ninja -C build liberty`
   - `ninja -C build iEDA`
7. Re-run the user workload:
   - `cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl`
8. Compare runtime and outputs against the baseline artifacts:
   - `read_data` seconds and percent of CTS total.
   - Liberty load/link timestamp split.
   - `iCTS_metrics.json` key fields.
9. If Slice A does not sufficiently reduce link time, implement Slice B:
   - Add scalar-pin fast path in `visitPin()`.
   - Preserve range-pin behavior.
   - Re-run the same validation and runtime comparison.
10. Only consider Slice C after measuring A/B:
    - `visitStmtInGroup()` traversal changes.
    - `visitAxisOrValues()` allocation/parsing changes.
11. Update `runtime-distribution.md` or add a post-change report.
12. Run `git status --short` and report modified files.

## Validation Commands

Primary:

```bash
ninja -C build liberty
ninja -C build iEDA
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Do not run:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iSTA
```

Performance profiling when needed:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
perf record -F 49 -g --call-graph fp -o <task-artifacts>/perf.data -- ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
perf report --children --stdio -i <task-artifacts>/perf.data > <task-artifacts>/perf_report_children.txt
```

## Risk Areas

- Branch-for-branch behavior drift while replacing large local dispatch maps.
- Missing an attribute side effect in `visitSimpleAttri()`.
- Changing logging frequency or unknown-attribute behavior.
- Bus/range pin compatibility if Slice B is implemented.
- Runtime variance from perf overhead and system load.
- Accidental churn from running broad iSTA static checks; avoid `ecc_dev_tools`
  on iSTA for this task.

## Rollback Points

- Revert Slice A independently if any Liberty reader test or output comparison
  diverges.
- Revert Slice B independently if range-pin behavior diverges.
- Do not start Slice C unless A/B results justify touching traversal/table
  parsing.
