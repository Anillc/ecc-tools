# iCTS H-tree Rollback to Opt3 Report

Date: 2026-05-09

## Decision

Production iCTS H-tree source has been rolled back to the opt3 backup state:

```text
refs/backups/icts-runtime-pre-next-optimizations-20260509-131717
```

That backup object is:

```text
558fb66cc7a06fd5b8aa731eea96ecbea6a8dfd5
```

The rollback keeps the earlier opt1/opt2/opt3 production changes that were already present in the backup:

- opt1 exact delay/power Pareto sort/scan;
- opt2 per-depth Pareto compression with lazy fallback compression;
- opt3 root-load signature cache for root-driver compensation.

The rollback removes the later post-opt3 production/prototype/analyzer changes from the restored H-tree paths:

- P1 lazy fallback pipeline code;
- P2 strict pre-compensation gate debug/prototype code;
- P3 sink coverage frontier-state analyzer code;
- P4 cross-depth reuse opportunity analyzer code;
- P6 root-compensation full-signature grouping code;
- related test and checker-cleanup changes that were introduced after the opt3 backup.

P5, P7, and P8 were report-only investigations and remain investigation-only. No `.trellis/spec/` file was changed.

## Restored Paths

The restored code/test paths are all files currently modified under:

- `src/operation/iCTS/source/flow/synthesis/htree/`
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc`

Verification command:

```bash
git diff --name-only refs/backups/icts-runtime-pre-next-optimizations-20260509-131717 -- \
  src/operation/iCTS/source/flow/synthesis/htree \
  src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc
```

Result: no output. The restored H-tree source/test paths match the opt3 backup ref exactly.

These files still appear as modified relative to branch `HEAD` because `HEAD` does not contain the opt1/opt2/opt3 backup-state changes:

- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.hh`
- `src/operation/iCTS/test/flow/synthesis/htree/HTreeTest.cc`

## Post-Opt3 Attempt Outcome

The post-opt3 reports and artifacts are retained for evidence, but they are no longer the current production state.

| Item | Current production state after rollback | Fixture outcome | Reason not kept as default production code |
| --- | --- | --- | --- |
| P1 lazy fallback pipeline | Removed by rollback | Negligible runtime impact in the `ics55_dev 0.5/0.5` fixture. `opt4` reported CTS total `48.151 s` vs opt3 `48.294 s`, and wall `70.22 s` vs opt3 `70.15 s`. | The strict-feasible path succeeded, but the measured gain was too small to justify keeping additional pipeline complexity as default code without broader evidence. |
| P2 strict pre-comp gate | Removed by rollback; investigation-only | Prototype/debug evidence was fixture-local. | Moving coverage before root-driver compensation can change compensation-aware state-frontier dominance. It needs broader equivalence proof and fallback coverage before promotion. |
| P3 sink coverage frontier state | Removed by rollback; investigation-only | Analyzer found potential would-prune volume, but only as debug evidence. | Compose-time pruning still lacks proof for absolute topology level offsets, compensation-aware dominance, pattern id stability, and final Pareto/median preservation. |
| P4 cross-depth frontier reuse | Removed by rollback; investigation-only | Analyzer did not find a useful exact leaf-side suffix reuse opportunity in this fixture. | Candidate-local topology pattern ids, tie-break behavior, materialization, and root compensation make direct reuse high risk without a remap/equivalence proof. |
| P5 monotone lattice dominance | Report-only | Not implemented. | Cross-state dominance is not proven under exact hash-join keys and global Pareto power-median selection. |
| P6 root-compensation grouping | Removed by rollback | Negligible runtime impact in the `ics55_dev 0.5/0.5` fixture. `opt5` reported CTS synthesis `31.637 s` vs opt3 `31.766 s`, and wall `68.94 s` vs opt3 `70.15 s`. | The single-fixture gain was small relative to opt3 and not enough to justify new grouped compensation code without a broader design/config matrix. |
| P7 demand-driven characterization | Report-only | Not implemented. | Potentially exact, but needs a reachability analyzer before any lazy characterization refactor. |
| P8 depth screening / epsilon caps | Report-only | Not implemented. | Approximate unless backed by lower-bound and median-invariance proof; not suitable as default production behavior. |

## Historical Reports Marked Superseded

The following reports describe post-opt3 experiments that are now historical:

- `final_optimization_convergence_report.md`
- `p2_strict_pre_comp_gate_report.md`
- `p3_sink_coverage_frontier_state_report.md`
- `p4_cross_depth_frontier_reuse_report.md`
- `p5_p7_p8_remaining_opportunities_report.md`
- `p6_root_compensation_grouping_report.md`
- `runtime_complexity_and_next_optimizations.md`

Use `runtime_optimization_report.md` plus this rollback report as the current opt3 production-state summary. Use the post-opt3 reports only as historical investigation notes and artifact indexes.

## Next Transformational Directions

Further runtime work should focus on transformational search-space reductions rather than small default-on cleanups.

1. Exact compose-time pruning with a proof harness.
   Start from the P3/P5 problem area, but first build an offline or debug-only equivalence harness that compares current exact frontiers, global Pareto membership, median selection, selected topology ids, and fallback behavior. Do not prune during composition until the proof covers absolute topology levels, compensation ordering, and deterministic tie behavior.

2. Characterization reachability analysis before lazy characterization.
   Add a default-off analyzer that records which characterized `(length, pattern, input slew, load cap)` tuples are actually consumed by segment synthesis, topology frontiers, and the selected topology. Only consider P7 lazy characterization if the unused ratio is material and eager-vs-lazy equivalence can be checked.

3. Pattern-id independent frontier reuse.
   If revisiting P4, avoid directly reusing candidate-local topology ids. Investigate reusable immutable segment/frontier signatures first, or define a complete candidate-local pattern-id remap with deterministic tie-break preservation.

4. Exact depth screening only with Pareto/median safety.
   Depth screening and epsilon caps should stay offline or explicitly approximate unless a lower bound proves omitted depths cannot contribute to the covered global Pareto set or shift the power-median selected entry.

5. Re-measure opt3 residuals before new production code.
   The current production baseline is opt3. Any next default-on change should first show a material residual hotspot in an opt3-based run and should include a multi-design/config validation matrix, including strict and relaxed slew settings and at least one fallback/no-strict-feasible case.

## Verification

Code-identity and lightweight checks performed during rollback:

- restored H-tree code/test paths from the backup ref;
- verified the restored paths have no diff against `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`;
- ran `git diff --check`;
- built `icts_test_flow_synthesis_htree`;
- ran `./bin/icts_test_flow_synthesis_htree`, which passed `6/6` tests;
- updated task reports only under `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/reports/`.

## Rollback Opt3 Validation Benchmark

The parent process later rebuilt the iEDA target successfully and ran a full rollback validation benchmark against the restored opt3 state.

Artifact directory:

```text
.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/rollback_opt3_validation/
```

Command:

```bash
cd scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Result:

- exit code: `0`
- `cts.log` synthesis runtime: `31.756 s`
- `cts.log` total runtime: `47.649 s`
- selected depth: `5`
- selected topology pattern id: `10297477`
- selected level segment ids: `522717,468375,28,24,6`
- candidate frontier entries before feasible filtering: `236991`
- feasible solutions: `130294`

Selected H-tree metrics:

| Metric | Value |
| --- | ---: |
| selected delay / power | `0.4959 ns / 217.271 uW` |
| raw H-tree char | `0.2897 ns / 192.458 uW` |
| root-driver compensation | `0.2063 ns / 24.813 uW` |
| selected physical root load | `0.1428 pF` |
| selected root driver cell master | `BUFX20H7L` |

Final CTS metrics:

| Metric | Value |
| --- | ---: |
| final clock buffer count | `360` |
| total clock network wirelength | `43151.203 um` |
| setup WNS | `7.302292 ns` |
| hold WNS | `0.008315 ns` |

Interpretation:

- The restored opt3 state is benchmark-valid on the `ics55_dev` flow after a successful iEDA rebuild.
- Runtime matches the expected opt3 regime: synthesis remains about `31.8 s`, with total CTS flow about `47.6 s`.
- The selected topology, root-driver choice, delay/power, buffer count, wirelength, and STA WNS values match the known opt3-quality solution family, so the rollback did not leave the production tree in a merely compile-clean but unvalidated state.
- The post-opt3 P1/P6 code remains rolled back despite its historical artifacts; the validated production state is still opt1/opt2/opt3.
