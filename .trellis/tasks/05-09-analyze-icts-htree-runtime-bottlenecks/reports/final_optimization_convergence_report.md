# iCTS H-tree Optimization Convergence Report

Date: 2026-05-09

> Superseded on 2026-05-09 by `rollback_to_opt3_report.md`. This file is historical evidence for post-opt3 P1/P2/P3/P4/P6 experiments. It no longer describes the current production code. Production H-tree code has been restored to `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`, which keeps opt1/opt2/opt3 only. P1 and P6 are not default-enabled after the rollback, and P2/P3/P4 analyzer/prototype code has been removed from production source.

## Scope

This report summarizes the implementation and convergence status for the iCTS H-tree runtime optimization sequence requested after the pre-optimization backup.

Backup recorded before this sequence:

- git backup ref: `refs/backups/icts-runtime-pre-next-optimizations-20260509-131717`
- backup commit object: `558fb66cc7a06fd5b8aa731eea96ecbea6a8dfd5`
- task-local backup metadata: `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/backups/pre-next-optimizations.meta`
- task-local source diff: `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/backups/pre-next-optimizations.diff`

The historical post-opt3 default production state before rollback was:

- default enabled: opt1 exact Pareto, opt2 per-depth Pareto, opt3 root-load signature cache, P1 lazy fallback, P6 root-compensation grouping;
- default disabled: P2 strict pre-comp gate prototype, P3 sink coverage frontier-state analyzer, P4 depth reuse opportunity analyzer;
- report-only: P5 monotone lattice dominance, P7 demand-driven characterization, P8 depth screening / epsilon caps.

## Historical Post-Opt3 Default Runtime

Historical latest default-enabled benchmark artifact:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/opt5_p6_root_compensation_grouping/`

Run command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Runtime progression:

| Run | Default state | CTS synthesis s | CTS total s | External wall s | Max RSS KB |
| --- | --- | ---: | ---: | ---: | ---: |
| opt0 | clean post-instrumentation baseline | 91.532 | 107.114 | 128.67 | 6497612 |
| opt1 | exact O(N log N) Pareto | 46.942 | 63.147 | 85.19 | 6491448 |
| opt2 | per-depth Pareto lazy fallback compression | 46.855 | 62.738 | 84.56 | 6497688 |
| opt3 | root-load signature cache | 31.766 | 48.294 | 70.15 | 6498200 |
| opt4 | P1 lazy fallback pipeline | n/a | 48.151 | 70.22 | 6168276 |
| opt5 | P6 root-compensation grouping | 31.637 | 47.599 | 68.94 | 6177132 |

Notes:

- opt4/P1 did not materially reduce wall time in this fixture, but it removed unused fallback materialization and reduced RSS in the strict-feasible success path.
- opt5/P6 gave a small default runtime win while preserving all selected topology and QoR metrics.
- P2/P3/P4 analyzer/prototype runs are intentionally not used as production runtime baselines.

## QoR Consistency

The historical latest default-enabled run preserved the key selected H-tree and final CTS metrics relative to opt3/opt4:

| Metric | Value |
| --- | --- |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| fallback candidate frontier | `not_materialized` |
| used boundary fallback | false |
| selected delay | 0.4959 ns |
| selected power | 217.271 uW |
| raw H-tree char metric | 0.2897 ns / 192.458 uW |
| root-driver compensation | 0.2063 ns / 24.813 uW |
| selected physical root load | 0.1428 pF |
| selected root driver cell master | BUFX20H7L |
| final CTS buffer count | 360 |
| total clock network wirelength | 43151.203 um |
| setup WNS / hold WNS | 7.302 ns / 0.008 ns |

## Item Status

| Item | Status | Default enabled | Result |
| --- | --- | :---: | --- |
| opt1 exact Pareto | implemented | yes | Removed the O(N^2) global selector bottleneck. |
| opt2 per-depth Pareto | implemented | yes | Exact structural compression; small incremental win after opt1. |
| opt3 root-load signature cache | implemented | yes | Removed repeated per-entry FLUTE root-load estimates. |
| P1 lazy fallback pipeline | implemented | yes | Skips fallback candidate materialization/global coverage on strict-feasible success. |
| P2 strict pre-comp gate | debug/prototype | no | Current fixture equivalent, but not globally proven because compensation-aware pruning order can change results. |
| P3 sink coverage frontier state | analyzer | no | Incremental signature matched post-hoc on 1,015,227 entries; would-prune opportunity is large, but compose-time pruning proof is missing. |
| P4 cross-depth frontier reuse | analyzer | no | Current depth candidates share root-side prefixes, not full leaf-side suffixes reusable by current leaf-to-root composition; no safe reuse prototype. |
| P5 monotone lattice dominance | report-only | no | Cross-state dominance proof is missing; median selection can change if Pareto points are removed. |
| P6 root compensation grouping | implemented | yes | Exact grouping by full root-compensation signature; small runtime win and no QoR drift. |
| P7 demand-driven characterization | report-only | no | Potential exact direction, but reachability analyzer should precede any lazy characterization refactor. |
| P8 depth screening / epsilon caps | report-only | no | Approximate unless lower-bound and median-invariance proof exists; not enabled. |

## Default-Off Diagnostic Controls

These controls are intentionally disabled unless set to exact string `1`:

| Env var | Purpose | Default behavior |
| --- | --- | --- |
| `ICTS_HTREE_DEBUG_STRICT_PRE_COMP_GATE=1` | Same-run non-invasive equivalence check for P2. | Current post-compensation path still selects. |
| `ICTS_HTREE_ENABLE_STRICT_PRE_COMP_GATE=1` | Env-gated P2 prototype optimized path. | Disabled by default; not promoted. |
| `ICTS_HTREE_DEBUG_SINK_COVERAGE_FRONTIER_STATE=1` | P3 analyzer for tracked incremental sink-load signature and would-prune counts. | Disabled by default; no production pruning. |
| `ICTS_HTREE_DEBUG_DEPTH_REUSE_OPPORTUNITY=1` | P4 analyzer for adjacent-depth prefix/suffix reuse opportunity. | Disabled by default; no reuse. |

## Final Validation

Final validation was rerun after the convergence report and checker cleanup:

```bash
git diff --check
cmake --build build --target icts_test_flow_synthesis_htree -j $(nproc)
./bin/icts_test_flow_synthesis_htree
cmake --build build --target iEDA -j $(nproc)
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Results:

- `git diff --check` passed.
- `icts_test_flow_synthesis_htree` built successfully and passed `13/13` tests.
- `iEDA` built successfully at `scripts/design/ics55_dev/iEDA`.
- The full `src/operation/iCTS` checker passed with `0` in-scope findings.
- The checker still reported `4930` out-of-scope diagnostics from external database/liberty headers and related external modules triggered by iCTS translation units; these are outside this H-tree change scope.
- `.trellis/spec/` has no diff. No code-spec update was made because this task produced task-local algorithmic decisions and diagnostic env controls, not a reusable project-wide coding convention or cross-layer contract.

## Supporting Reports

- `runtime_complexity_and_next_optimizations.md`
- `runtime_optimization_report.md`
- `p2_strict_pre_comp_gate_report.md`
- `p3_sink_coverage_frontier_state_report.md`
- `p4_cross_depth_frontier_reuse_report.md`
- `p5_p7_p8_remaining_opportunities_report.md`
- `p6_root_compensation_grouping_report.md`

## Historical Recommended Next Work

These recommendations were written before the rollback and are superseded by `rollback_to_opt3_report.md`.

1. Historical recommendation before rollback: keep P1 and P6 default-enabled.
2. Do not promote P2/P3/P4 without a broader design/config/fallback matrix.
3. For the next exact optimization candidate, start with P7 reachability analysis because characterization is now a significant HTree component and an exact lazy path may be possible if unused ratio is high.
4. Treat P5 and P8 as research/approximation directions until a formal compatibility or median-invariance proof exists.
