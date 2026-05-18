# Results: CTS optimization runtime bottleneck

## Summary

The first huge-case stall was caused by route-tree injection rebuilding every net load after each injected net. The retained fix changes route-tree parasitic injection to update only the net being injected.

After that fix, `ics55_huge_dev` moves past route-tree injection quickly. The remaining dominant bottleneck is candidate trial evaluation: every batch trial applies a candidate and restores it through `FastStaAdapter::changeBufferMasters(...)`, and that path still runs full fast STA timing and power updates. On the huge case this is about 29 seconds per candidate trial, so the current solver cannot complete practical huge runs with hundreds of candidates per iteration.

## Evidence

### Previous huge stall

Run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

80ps config:

```text
.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_huge_80ps.json
```

Saved log:

```text
scripts/design/ics55_huge_dev/.trellis_runtime_probe_huge_80ps_net200.log
```

Measured route-tree injection progress before the fix:

| Metric | Value |
|---|---:|
| DAG nets | 44737 |
| Progress at 7300 injected nets | 153.251 s |
| Progress at 7700 injected nets | 161.719 s |
| Approximate rate | about 2.1 s / 100 nets |
| Projected injection runtime | about 15-16 min |

Code cause:

```text
FastStaParasitics::buildNetParasiticFromRouteTree(...)
  -> updateNetLoads(context)
```

Because `injectRouteTrees(...)` calls `buildNetParasiticFromRouteTree(...)` once per net, this made route-tree injection effectively recalculate all net loads for each injected net.

### Retained fix

Changed `FastStaParasitics.cc` so the net-load calculation is shared through a local helper and route-tree injection updates only the injected net:

```text
FastStaParasitics::buildNetParasiticFromRouteTree(...)
  -> updateNetLoad(context, fast_net)
```

Full-context `FastStaParasitics::updateNetLoads(context)` is still used at normal full timing-update boundaries.

### Huge after route-tree fix

Saved log:

```text
scripts/design/ics55_huge_dev/.trellis_runtime_probe_huge_80ps_final_routefix_solverprobe.log
```

Measured setup and injection:

| Stage | Runtime |
|---|---:|
| CTS synthesis | 136.183 s |
| route-tree cache | 0.2271 s |
| build layout fast STA context | 0.6170 s |
| initial timing update | 6.8403 s |
| initial power update | 7.9762 s |
| route-tree injection | 0.2329 s |
| post-injection timing update | 7.0943 s |
| post-injection power update | 7.7310 s |

The route-tree injection stage is no longer the blocker.

### Remaining huge bottleneck

The same run reached solver iteration 1:

| Solver Metric | Value |
|---|---:|
| current skew | 0.115448 ns |
| current area | 125699 um^2 |
| candidates in iteration 1 | 589 |
| trial 2 runtime | 29.521 s |
| trial 3 runtime | 28.855 s |

At roughly 29 seconds per trial, evaluating all 589 candidates in only the first iteration would take about 4.7 hours. The run was intentionally stopped after the bottleneck was proven.

The responsible path is:

```text
findBestBatchTrial(...)
  -> tryBatch(...)
    -> changeFastStaMasters(... candidate ...)
      -> FastStaAdapter::changeBufferMasters(...)
        -> FastStaTiming::update(...)
        -> FastStaPower::update(...)
    -> captureState(...)
    -> changeFastStaMasters(... restore ...)
      -> FastStaTiming::update(...)
      -> FastStaPower::update(...)
```

So each candidate trial performs at least two full timing/power recomputations on the huge fast STA graph.

### Reverted incremental-batch experiment

An experiment changed `FastStaAdapter::changeBufferMasters(...)` to apply each batch action through existing single-buffer incremental region updates. It was reverted because it did not improve the huge case:

| Trial | Runtime |
|---|---:|
| candidate 1 apply, 1 action | 21.416 s |
| candidate 1 restore, 1 action | 20.795 s |
| candidate 2 apply, 2 actions | 22.603 s |

This is worse than the retained full-update batch path for high-level dirty regions. The likely reason is that upper-tree buffer changes invalidate a very large downstream region, and region update still carries full-graph costs such as output/input pairing maps and full power summation.

## Small Regression Matrix

Final retained source: route-tree net-load fix plus profiling logs; no incremental-batch behavior change.

Logs:

```text
scripts/design/ics55_dev/.trellis_runtime_probe_80ps_final_routefix.log
scripts/design/ics55_dev/.trellis_runtime_probe_40ps_final_routefix.log
scripts/design/ics55_dev/.trellis_runtime_probe_0ps_final_routefix.log
```

| Target | Initial Skew | Optimized Skew | Mutations | Trials | Optimization Runtime | solve_clock | batch_trial_eval | Full Run |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 80ps | 0.0883 ns | 0.0763 ns | 7 | 328 | 4.199 s | 4.0984 s | 4.0395 s | 33.14 s |
| 40ps | 0.0883 ns | 0.0480 ns | 20 | 681 | 8.931 s | 8.8254 s | 8.7044 s | 38.11 s |
| 0ps | 0.0883 ns | 0.0480 ns | 20 | 681 | 8.907 s | 8.8025 s | 8.6822 s | 38.16 s |

Small-case QoR matches the expected baseline behavior, while route-tree injection is now about 0.011 s on `ics55_dev`.

## Current Classification

Confirmed bottlenecks:

1. Fixed: route-tree injection / parasitic build due per-net full-context load update.
2. Open: batch trial evaluation due two full timing/power recomputations per candidate trial.

The next performance task should not focus on route-tree injection anymore. It should redesign candidate evaluation so huge cases do not call full timing/power update hundreds or thousands of times per iteration. Viable directions include candidate prefiltering with cheap timing proxies, evaluating only frontier-limited candidate subsets, or a real multi-source dirty-region update that avoids full graph remapping and power summation for every candidate.

## Memory Notes

Huge probes stayed in the same memory range as the original report. Observed RSS samples were roughly 43-49 GB while `ics55_huge_dev` was in synthesis/optimization. The retained route-tree load-update fix improves CPU runtime for injection but does not materially change peak resident memory ownership.

## Final Check Status

Final full iCTS check passed after source changes converged:

```bash
python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS
```

Result:

```text
In-scope findings: 0
Total runtime: 492.933s
```

## Scalable Batch Solver Probe

The follow-up implementation adds a large-case solver path instead of evaluating every generated candidate exactly.

Code changes:

- `FastStaAdapter::changeBufferMastersTimingOnly(...)` applies a batch master change and runs only `FastStaTiming::update(...)`; `power_valid` is left false until an explicit final power update.
- `Optimization.cc` keeps the existing exact full timing/power solver for small contexts.
- Large contexts use `scalable_timing_only_batch` when the fast STA graph is at least `50000` nodes or has at least `5000` optimizable buffers.
- The scalable path:
  - collects late/early frontier coverage,
  - builds scored batch candidates from frontier paths,
  - exact-verifies a bounded set of candidates per iteration with timing-only apply/capture/restore,
  - tracks area by sizing action deltas during trials,
  - preserves cap and slew legality checks against the baseline,
  - runs one final `FastStaAdapter::updatePower(...)` before summary emission.

Build:

```bash
ninja -C build icts_source_database_adapter_fast_sta icts_source_flow_optimization iEDA
```

### `ics55_dev` sanity

Run:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_dev
CTS_CONFIG_PATH=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_80ps.json \
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Saved log:

```text
scripts/design/ics55_dev/.trellis_scalable_sanity_80ps.log
```

Result:

| Case | Solver | Initial Skew | Optimized Skew | Mutations | Optimization Runtime | Full Run |
|---|---|---:|---:|---:|---:|---:|
| `ics55_dev` 80ps | `exact_full_power_batch` | 0.0883 ns | 0.0763 ns | 7 | 4.253 s | 33.63 s |

The small case still uses the exact baseline solver, so QoR matches the retained route-fix baseline.

### `ics55_huge_dev` scalable 80ps probes

Final binary was copied from:

```text
scripts/design/ics55_dev/iEDA
```

to:

```text
scripts/design/ics55_huge_dev/iEDA
```

Run shape:

```bash
cd /home/liweiguo/project/ecc-tools/scripts/design/ics55_huge_dev
CTS_CONFIG=/home/liweiguo/project/ecc-tools/.trellis/tasks/05-17-cts-optimization-critical-frontier-batch-sizing/run_configs/cts_opt_huge_80ps.json \
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Measured probes:

| Probe | Exact Trials / Iteration | Initial Skew | Optimized Skew | Improvement | Mutations | Optimization Runtime | Full Run | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `trellis_scalable_huge_80ps` | 4 | 0.1154 ns | 0.1154 ns | 0.0000 ns | 0 | 63.48 s | 331.49 s | Top total-score batches did not improve skew. |
| `trellis_scalable_huge_80ps_v2` | 8 | 0.1154 ns | 0.1105 ns | 0.0049 ns | 5 | 255.53 s | 495.14 s | Normalized scoring found one legal improving downsize batch. |
| `trellis_scalable_huge_80ps_v3` | 16 | 0.1154 ns | 0.1060 ns | 0.0094 ns | 4 | 502.00 s | 741.13 s | Higher exact budget found a better first batch; second iteration had no improving candidate in the verified set. |

Final retained behavior currently corresponds to the v3 source.

Final v3 graph/runtime profile:

| Metric | Value |
|---|---:|
| fast STA nodes | 211483 |
| fast STA nets | 44737 |
| sinks | 122010 |
| optimizable buffers | 44736 |
| generated candidates | 1269 |
| trial count | 32 |
| accepted batch count | 1 |
| cap rejected | 0 |
| slew rejected | 0 |
| batch trial eval | 456.9133 s |
| solve clock total | 471.7703 s |

Final v3 accepted transitions:

| Transition | Count |
|---|---:|
| `BUFX12H7L -> BUFX8H7L` | 2 |
| `BUFX20H7L -> BUFX16H7L` | 2 |

QoR interpretation:

- The scalable path solves the original huge-case runtime blocker: the solver reaches a summary instead of attempting hundreds of full timing/power trials.
- Timing-only trial runtime is still about 14 seconds per exact candidate on `ics55_huge_dev`, so exact trial budget is the dominant scalability knob.
- The current cheap scoring is still weak for QoR: many high-score early-downsize batches are legal but worsen skew substantially, often to 170-325ps. This suggests the next quality improvement should penalize high-level early-branch downsizing while target skew is unmet, or use a stronger cheap predictor before exact verification.
- The final 80ps target is not met on huge: optimized skew is 106.0ps. The task proves scalable execution and exposes candidate quality as the next bottleneck.

Final check passed before commit: `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS` reported zero in-scope findings.
