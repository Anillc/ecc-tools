# Implementation plan

## Phase 0: Clean Slate

- [x] Remove the previous active fixed-bin/value-state task from `.trellis/tasks`.
- [x] Remove the previous archived value-bound task from `.trellis/tasks/archive/2026-05`.
- [x] Remove previous experimental source/test directories before recreating new code.
- [x] Remove CMake references to the deleted experimental directories.
- [x] Search for previous prototype identifiers and stale experimental include references.
- [x] Build after cleanup to catch stale CMake or include references.

## Phase 1: Mathematical Formulation

- [x] Rewrite PRD/design/implementation plan around the new mathematical solver only.
- [x] Research mathematical optimization methods only:
  - affine MILP,
  - disjunctive programming,
  - indicator constraints,
  - tight big-M,
  - McCormick or perspective reformulations if needed,
  - convex MIQP/MIQCP,
  - MINLP/global nonlinear as last resort.
- [x] Validate local OR-Tools package MILP capability outside the main build:
  - `find_package(ortools)` works in a standalone CMake probe,
  - `MPSolver::CreateSolver("SCIP")` solves a small MIP,
  - `bin/scip` solves generated LP files.
- [x] Identify main-build dependency issue:
  - direct OR-Tools C++ package import conflicts with iEDA's existing Abseil targets,
  - current integration uses external SCIP LP execution to avoid dependency contamination.
- [x] Update `formulation-options.md` with accepted/deferred/rejected decisions and dependency notes.
- [x] Decide whether affine MILP is sufficient as the production solver class after fit validation.
  - Current validation keeps affine MILP as the production class.
  - `ics55_dev` reaches optimal status.
  - `ics55_huge_dev` reaches a complete feasible solution with finite 11.31% gap and passes final Evaluation STA.

## Phase 2: Characterization Export and Fit Validation

- [x] Use the existing characterization data path for fit validation and math-model inputs without keeping an experimental characterization module in the final integration.
- [x] Export real characterization samples for representative slot/pattern/metric cases.
- [x] Validate affine fits on real characterization samples.
- [ ] If Python is used, run under:

```bash
conda activate nu_htree
```

- [x] Report sample count, coefficients, R2, RMSE, max absolute residual, max relative residual, and domain coverage.
- [x] Decide whether affine residuals are acceptable for production MILP.
  - Huge report: 490 input samples, 5 input patterns, 20 metric models,
    20 accepted, 0 rejected fits, min R2 0.8455, max RMSE 0.0139,
    max absolute residual 0.0288, max relative residual 0.4182.
  - The validation path treats sampling grids as fit evidence only; MILP state is continuous.

## Phase 3: Solver Model Prototype

- [x] Define initial `MathHtreeProblem` and `MathHtreeSolution` data contracts.
- [x] Build an external-SCIP MILP skeleton for slot-choice problems.
- [x] Solve `min_delay`, `min_power`, and normalized tradeoff on a small continuous slot-choice test.
- [x] Export richer model diagnostics and solver status.
- [ ] Validate very small instances against exhaustive enumeration as an offline oracle only.
- [x] Measure model size, solve time, and memory on extracted larger instances.

## Phase 4: C++ Integration

- [x] Integrate the mathematical solver directly under `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver`.
- [x] Remove the old shortlist/beam/top-K analytical candidate path from the active solver build.
- [x] Add CMake entries for the new math model and external SCIP solver sources.
- [x] Add model-builder code from existing H-tree slots/patterns/fits.
- [x] Add external SCIP-backed affine MILP implementation for the clean mathematical model skeleton.
- [x] Add materialization back into existing H-tree structures.
- [x] Add diagnostics for selected choices and continuous boundary values.
- [x] Keep the accepted path free of fixed bins/lattices, value-state DP, beam/top-K, heuristic frontier truncation, and large sequence enumeration.
- [x] Keep analytical opt-in through `enable_analytical_htree` without adding compile-time macros.
- [x] Add native H-tree frontier/topology pattern memory compaction while preserving sink-load coverage legality.

Current build evidence:

```bash
cmake --build build --target \
  icts_source_flow_synthesis_htree_topology_pruning \
  icts_source_flow_synthesis_htree_analytical_solver \
  icts_source_flow_synthesis_htree_solution \
  icts_source_flow_synthesis_htree \
  icts_test_flow_synthesis_htree_analytical_solver -- -j 16

ctest --test-dir build -R '^icts_test_flow_synthesis_htree_analytical_solver$' --output-on-failure
```

The analytical solver test currently passes.

## Phase 5: Real Design Validation

- [x] Run `ics55_dev`:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Run `ics55_huge_dev`:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

- [x] Compare against native H-tree:
  - CTS runtime,
  - downstream H-tree runtime,
  - peak RSS,
  - H-tree buffer count,
  - final buffer count,
  - wirelength,
  - WNS/TNS or existing QoR metrics,
  - Evaluation STA/iSTA error.
- [x] Confirm huge run has significant runtime improvement and does not reproduce the previous high-memory/OOM behavior.

Validation evidence:

```text
ics55_dev native: scripts/design/ics55_dev/result_current_migrated_native_fix_20260526_1447
H-tree: 2.150 s, selected_depth=11
CTS: finished, 14.322 s internal, 0:35.34 /usr/bin/time wall
QoR: setup WNS 7.317 ns, hold WNS 0.044 ns
Legality: cap legal true, slew legal true, target_met true
RSS: 5,272,492 KB
```

```text
ics55_dev analytical: scripts/design/ics55_dev/result_current_migrated_math_fix_20260526_1447
H-tree: 9.324 s, solver optimal, gap 0, selected_depth=11
MILP: 1026 vars, 156 binaries, 870 continuous, 4169 constraints, solver wall 9.212 s
CTS: finished, 21.221 s internal, 0:42.16 /usr/bin/time wall
QoR: setup WNS 7.324 ns, hold WNS 0.045 ns
Legality: cap legal true, slew legal true, target_met true
RSS: 5,571,356 KB
```

```text
ics55_huge_dev native: scripts/design/ics55_huge_dev/result_current_migrated_native_fix_20260526_1343
H-tree: 232.789 s, selected_depth=14
CTS: finished, 534.871 s internal, 9:42.18 /usr/bin/time wall
QoR: setup WNS 3.492 ns, hold WNS 0.071 ns; setup/hold worst skew 0.101 ns / -0.065 ns
Legality: cap legal true, slew legal true, slew violation count 0
RSS: 73,905,232 KB
```

```text
ics55_huge_dev analytical: scripts/design/ics55_huge_dev/result_current_migrated_math_fix_20260526_1354
H-tree: 59.939 s, solver feasible_with_gap, gap 0.112700, selected_depth=14
MILP: 2184 vars, 324 binaries, 1860 continuous, 15032 constraints, solver wall 57.788 s
Solver totals: delay 0.489159 ns, power 0.040147 W; validated delay 0.610728 ns, power 0.040159 W
CTS: finished, 574.141 s internal, 10:19.80 /usr/bin/time wall
QoR: setup WNS 3.487 ns, hold WNS 0.076 ns; setup/hold worst skew 0.089 ns / -0.070 ns
Legality: cap legal true, slew legal true, slew violation count 0
RSS: 21,749,964 KB
```

The analytical path reduces huge H-tree build time from 232.789 s to 59.939 s
and peak RSS from 73.9 GB to 21.75 GB.  End-to-end huge runtime is still slower
because downstream CTS optimization takes 436.505 s for analytical versus
227.067 s for native; this is recorded as follow-up objective work rather than
a solver integration failure.

## Phase 6: Final Check

- [x] Run normal build/test checks for touched code.
- [x] Run the iCTS ECC dev check only after implementation is complete:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

The final full-module checker is run after implementation and real-design
validation. Any in-scope findings must be fixed before the work commit.

Final full-check result:

```text
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
Overall summary: In-scope findings: 0.
```

## Risky Files and Boundaries

- `src/operation/iCTS/source/flow/synthesis/htree/solution/analytical/AnalyticalSolution.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/analytical_solver`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`

## Rollback Points

- Native H-tree remains the default unless the mathematical analytical solver is explicitly enabled by config.
- Existing analytical H-tree can remain as a baseline, but cannot satisfy this task's acceptance criteria.
- If OR-Tools cannot solve the accepted model class, stop and report the missing solver dependency.
- If affine fit quality fails, do not hide residual error inside MILP; move to the documented quadratic/MINLP decision point.
