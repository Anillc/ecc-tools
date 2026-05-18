# Implementation Plan: CTS huge design optimization performance

## Phase 1: Research And Planning

- [x] Create Trellis task.
- [x] Review the current fast STA optimization logs and implementation.
- [x] Research external timing-control and CTS optimization techniques.
- [x] Write `research/timing-control-strategy-report.md`.
- [x] Draft `design.md` with the recommended algorithm.

## Phase 2: Data Structures

- [x] Add a topology interval view for the fast STA clock tree:
  - parent/children adjacency from `TopologyIndex`;
  - sink DFS/postorder traversal;
  - subtree sink stats for each buffer output;
  - subtree arrival min/max, late/early counts, and window violation sums.
- [x] Keep all new structures local to `flow/optimization` unless reused by another CTS flow.
- [x] Add compact schema/log counters for:
  - pure late branches;
  - pure early branches;
  - mixed branches rejected by risk filter;
  - scored action and generated batch counts after prefiltering.

## Phase 3: Candidate Scoring

- [x] Replace current raw coverage scoring with target-window violation scoring.
- [x] Penalize or reject high-level mixed branches.
- [x] Make area a secondary objective until target skew is met.
- [x] Keep exact fast STA cap/slew legality as the acceptance gate.

## Phase 4: Adaptive Batch Verification

- [x] Build a primary non-overlapping batch from top scored actions.
- [x] Add limited adaptive verification:
  - split late and early sides;
  - use non-overlapping topology selection;
  - verify several batch sizes under a fixed exact-trial budget.
- [x] Use existing timing-only fast STA trial path.
- [x] Preserve exact acceptance gate for skew/cap/slew non-regression.

## Phase 5: Validation

- [x] Build:

```bash
ninja -C build icts_source_flow_optimization iEDA
```

- [ ] Run `ics55_dev`:
  - 80ps;
  - 40ps;
  - 0ps.
- [x] Run `ics55_huge_dev`:
  - 80ps first;
  - 40ps and 0ps only if 80ps shows meaningful QoR/runtime improvement.
- [x] Compare against current v3 baseline:
  - `0.1154 ns -> 0.1060 ns`;
  - `32` exact trials;
  - `456.9133 s` batch trial eval;
  - `502.000 s` optimization runtime.
- [x] Run final full check before commit:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

## Current Run Notes

- Build passed:

```bash
ninja -C build icts_source_flow_optimization
ninja -C build iEDA
```

- Huge 80ps run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev
CTS_CONFIG=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_huge_80ps.json \
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- Log: `/home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev/.trellis_window_huge_80ps_v1.log`
- Result dir: `/home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev/result_trellis_window_huge_80ps_v1`
- Final ecc dev check passed with 0 in-scope findings:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

- `ics55_dev` 80ps/40ps/0ps was not rerun in this task closure; current user request prioritized huge-dev QoR/runtime and final ecc dev validation.
