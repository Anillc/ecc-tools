# h-tree性能优化

## Goal

Analyze the current CTS H-tree implementation, especially the H-tree construction / characterization frontier algorithm, and establish a reproducible performance/QoR baseline for later optimization work.

The immediate experiment is an iteration/step sweep using the `ics55_dev` CTS flow as the source of truth for design input, technology setup, constraints, and CTS defaults:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The sweep varies `wirelength_iterations` from 1 through 5 and varies both `slew_steps` and `cap_steps` over 5, 10, and 15.

## Background / Known Context

- The existing H-tree implementation lives under `src/operation/iCTS/source/flow/synthesis/htree/`.
- H-tree construction is driven by CTS config fields parsed in `src/operation/iCTS/source/database/config/Config.cc`: `wirelength_unit_um`, `wirelength_iterations`, `slew_steps`, `cap_steps`, `htree_depth_explore_window`, `htree_topology_tolerance`, and related knobs.
- Existing matrix-style tests exist in `src/operation/iCTS/test/flow/synthesis/htree/HTreeRealTechMatrixSupport.cc`, but their compiled matrix is `{2,3,4,5} x {10,15}` and their fixture path is test-specific rather than explicitly the `scripts/design/ics55_dev` flow.
- The required experiment must follow `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`, including its DB setup, SDC/lib/LEF setup, input DEF, and `iEDA_config/cts_default_config.json` baseline.
- The baseline CTS config currently includes `max_cap = 0.15`, routing layers `[5, 6]`, buffers `BUFX8H7L`, `BUFX12H7L`, `BUFX16H7L`, `BUFX20H7L`, and net list `{clock_name: core_clock, net_name: clk}`.

## Assumptions

- For each matrix point, only `wirelength_iterations`, `slew_steps`, and `cap_steps` should be changed; all other flow inputs and constraints remain identical to `run_iCTS_dev.tcl`.
- `steps` means setting both CTS characterization knobs `slew_steps` and `cap_steps` to the same value.
- Test outputs should be isolated from the checked-in/example `scripts/design/ics55_dev/result` tree where possible, to avoid overwriting existing reports.

## Requirements

- Run the H-tree iteration/step sweep for:
  - `wirelength_iterations`: `1, 2, 3, 4, 5`
  - `steps`: `5, 10, 15`
- Use the `ics55_dev` flow configuration and constraints as the reference setup.
- Collect at least per-case runtime, pass/fail status, CTS QoR metrics, and H-tree selected-solution metrics available from logs/reports.
- Preserve raw logs/configs for each case so later optimization comparisons are reproducible.
- Summarize algorithm findings for the current H-tree construction path before making optimization changes.
- Track the small-fanout legality risk discovered during default-config tuning:
  - determine whether the H-tree leaf fanout-relative algorithm becomes overly tight when upstream sink clustering turns original sinks into local-buffer H-tree loads.
  - determine whether intermediate H-tree construction levels account for fanout, or whether fanout is only enforced later by sink-load-region legality filtering.
- Treat `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json` as the default-config path for the dev flow; do not use the similarly named config under `/home/liweiguo/project/ecc-tools` for this task unless explicitly comparing checkouts.

## Acceptance Criteria

- [x] Task artifacts describe the required sweep and the `ics55_dev` flow dependency.
- [x] All 15 matrix cases are attempted with isolated output directories or an explicit explanation if the binary/script prevents isolation.
- [x] A summary table reports each case's status, runtime, and available QoR/evaluation metrics.
- [x] Raw per-case configs/log paths are recorded in task research output.
- [x] H-tree construction algorithm notes identify likely performance-sensitive stages.
- [x] The default dev config is set to `wirelength_iterations = 3`, `slew_steps = 10`, and `cap_steps = 10`.
- [x] The small-fanout follow-up explicitly answers whether leaf fanout-relative legality is made too tight by clustering.
- [x] The small-fanout follow-up explicitly answers whether H-tree intermediate levels consider fanout during construction.
- [x] Final small-fanout validation uses the dev flow command under `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev`.

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
