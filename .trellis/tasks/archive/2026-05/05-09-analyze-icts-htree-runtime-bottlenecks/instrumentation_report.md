# iCTS H-tree Runtime Instrumentation Report

Date: 2026-05-09

## Scope

This report covers the instrumented `ics55_dev` iCTS run with:

- `max_buf_tran = 0.5000 ns`
- `max_sink_tran = 0.5000 ns`
- binary: `scripts/design/ics55_dev/iEDA`, built from the current workspace
- run script: `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`

The goal was to attribute H-tree runtime to major phases and identify pruning or algorithmic optimization directions that preserve, or only lightly affect, solution space.

## Changed Files

Temporary instrumentation remains in the workspace.

Source files changed:

- `src/operation/iCTS/source/flow/synthesis/htree/TemporaryInstrumentation.hh`
- `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc`
- `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc`
- `src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc`

This is not a production-ready optimization patch. The source edits above are temporary probes left in the
workspace for review and should be removed or gated before any production handoff.

Current `git status --short` at report check time:

```text
 M src/operation/iCTS/source/flow/synthesis/htree/HTree.cc
 M src/operation/iCTS/source/flow/synthesis/htree/compensation/RootDriverCompensation.cc
 M src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc
 M src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc
 M src/operation/iCTS/source/flow/synthesis/htree/segment_pruning/SegmentPruning.cc
 M src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc
 M src/operation/iCTS/source/flow/synthesis/topology/trunk/SourceTrunkSegment.cc
?? .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/
?? .trellis/tasks/05-09-compare-icts-max-slew-results/
?? src/operation/iCTS/source/flow/synthesis/htree/TemporaryInstrumentation.hh
```

Task artifacts added:

- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/run.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/htree_probe.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/cts.log`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/iCTS_metrics.json`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/reports/cts_stat.json`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/reports/cts_stat.rpt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/statistics/cell_stats.rpt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/statistics/lib_cell_dist.rpt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/statistics/wirelength.rpt`
- `.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/instrumentation_report.md`

## Commands Run

Build:

```bash
cmake --build build -j "$(nproc)" --target iEDA
```

Status: success. The executable was linked at `scripts/design/ics55_dev/iEDA`.

Instrumented flow run:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev && ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl > /home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/run.log 2>&1
```

Status: success. The log reports `iCTS run successfully.`

Probe extraction:

```bash
rg "HTREE_PROBE" .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/run.log > .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/htree_probe.log
```

Status: success.

Artifact copy:

```bash
cp -f scripts/design/ics55_dev/result/cts/cts.log .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/cts.log
cp -f scripts/design/ics55_dev/result/metric/iCTS_metrics.json .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/iCTS_metrics.json
cp -f scripts/design/ics55_dev/result/report/cts_stat.json scripts/design/ics55_dev/result/report/cts_stat.rpt .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/reports/
cp -f scripts/design/ics55_dev/result/cts/statistics/cell_stats.rpt scripts/design/ics55_dev/result/cts/statistics/lib_cell_dist.rpt scripts/design/ics55_dev/result/cts/statistics/wirelength.rpt .trellis/tasks/05-09-analyze-icts-htree-runtime-bottlenecks/artifacts/instrumented_0p5/statistics/
```

Status: success.

## Run Summary

From `cts.log`:

| Metric | Value |
| --- | ---: |
| CTS synthesis runtime | 106.582 s |
| Total CTS elapsed time | 123.004 s |
| Peak synthesis vmem delta | 4119.748 MB |
| Total run peak vmem delta | 7527.636 MB |
| Sink count | 8751 |
| Selected H-tree depth | 5 |
| Selected H-tree level count | 5 |
| Segment chars | 19515 |
| Selected final frontier | 243981 |
| Selected candidate frontier | 236991 |
| Selected feasible solutions | 130294 |
| Selected feasible frontier | 126566 |

The extracted probe file has 3,093,713 `HTREE_PROBE` lines and is about 609 MB. Most of that volume is per-entry sink-region cache-hit logging; those timings are useful for rough attribution but include material instrumentation overhead.

## Major H-tree Phase Timings

From `HTREE_PROBE event=htree_build_phase` and `event=htree_build_end`:

| Phase | Time (s) | Share of H-tree build | Key counters |
| --- | ---: | ---: | --- |
| Global selection | 46.764 | 50.5% | feasible input 362592, candidate input 679256, selected feasible |
| Depth search | 31.223 | 33.7% | 4 depths, global feasible pool 536458, global candidate pool 1004862 |
| Characterization | 7.487 | 8.1% | 19515 segment chars, 15 slew steps, 15 cap steps |
| Global sink-load coverage filter | 6.766 | 7.3% | 536458 -> 362592 feasible, 1004862 -> 679256 candidate |
| Segment entry synthesis | 0.355 | 0.4% | 4 required lengths, 73352 total segment frontier entries |
| Topology build | 0.002 | ~0.0% | 319 loads, 9 topology levels |
| Selected compensation | 0.000 | ~0.0% | selected depth 5 |
| Embedding | 0.000 | ~0.0% | 40 inserted insts, 40 inserted nets |
| H-tree build total | 92.598 | 100.0% | selected depth 5 |

Dominant result: one global selection pass consumes about half of the H-tree build and about 44% of the reported CTS synthesis runtime.

## Depth Candidate Breakdown

From `HTREE_PROBE event=topology_candidate_build`:

| Depth | Total (s) | Pattern search (s) | Candidate sink-region filter (s) | Feasible sink-region filter (s) | Local selection (s) | Final frontier | Feasible frontier |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 12.069 | 8.916 | 1.705 | 0.900 | 0.095 | 377232 | 201284 |
| 7 | 6.866 | 4.975 | 0.995 | 0.518 | 0.049 | 220756 | 117936 |
| 6 | 5.050 | 3.680 | 0.739 | 0.396 | 0.027 | 169883 | 90672 |
| 5 | 7.004 | 5.064 | 1.081 | 0.570 | 0.033 | 243981 | 126566 |

Depth 5 was selected, but all four candidate depths are built before global selection. The largest single depth build is depth 8, mostly due to pattern search plus root-driver compensation.

## Pattern Search and Compensation

From `HTREE_PROBE event=topology_pattern_search_end` and `event=root_driver_compensation_apply`:

| Levels | Pattern search total (s) | Compose (s) | Root compensation (s) | Final state frontier (s) | Pre-comp frontier | Final frontier | Topology patterns |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 8.916 | 2.731 | 6.115 | 0.065 | 380607 | 377232 | 21094640 |
| 7 | 4.975 | 1.481 | 3.452 | 0.036 | 220756 | 220756 | 12530486 |
| 6 | 3.680 | 1.162 | 2.485 | 0.028 | 169883 | 169883 | 8660110 |
| 5 | 5.064 | 1.461 | 3.550 | 0.042 | 243981 | 243981 | 11699271 |
| Total | 22.635 | 6.835 | 15.602 | 0.171 | 1015227 | 1011852 | - |

Root-driver compensation is not doing many unique direct cost lookups: across 1,015,227 compensated entries, only 68 unique lookup keys were created and 1,015,159 were direct cost-cache hits. However, load resolution still ran once per entry and used FLUTE route estimation every time:

| Entries | Unique lookup delta | Cost cache hit delta | Load resolutions | FLUTE route estimates | Load resolution time (s) | Apply elapsed (s) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 380607 | 58 | 380549 | 380607 | 380607 | 5.866 | 6.115 |
| 220756 | 0 | 220756 | 220756 | 220756 | 3.353 | 3.452 |
| 169883 | 0 | 169883 | 169883 | 169883 | 2.412 | 2.485 |
| 243981 | 10 | 243971 | 243981 | 243981 | 3.445 | 3.550 |
| 1015227 | 68 | 1015159 | 1015227 | 1015227 | 15.077 | 15.602 |

This suggests compensation cost is dominated by per-entry root-load resolution, not cell-delay/power lookup.

## Pareto Selection Hotspots

The key measured hotspot is `SelectBestGlobalEntry`:

| Selector | Input entries | Pareto output | Time |
| --- | ---: | ---: | ---: |
| Global H-tree feasible selector | 362592 | 120 | 46.764 s |
| Local depth 8 selector | 201284 | 224 | 0.095 s |
| Local depth 7 selector | 117936 | 264 | 0.049 s |
| Local depth 6 selector | 90672 | 216 | 0.027 s |
| Local depth 5 selector | 126566 | 248 | 0.033 s |
| Source-to-root segment selector | 24152 | 8 | 0.002 s |

The global selector and local selectors both use nested-loop delay/power Pareto extraction. The global case is the real bottleneck because its input is a large cross-depth pool and the dominance relation leaves enough work to make the O(N^2) pass expensive. The source-to-root strict-candidate pool had 24152 entries, but this run measured only 2.148 ms for its selector; it is not a bottleneck in this experiment.

## Sink-load Region Filtering

From `HTREE_PROBE event=sink_region_legality_resolve`:

| Resolve status | Count |
| --- | ---: |
| cache_hit | 3082611 |
| monotone_pruned | 10717 |
| evaluated | 31 |

The algorithm already has strong sink-region caching by legality signature. Only 31 real sink-load-region evaluations occurred; the millions of cache hits are cheap logically but were expensive to log in this temporary run. Treat sink-region timing as an upper bound because the probe writes one line per cache hit.

Global coverage filtering still rejects many entries by leaf-cap coverage:

| Pool | Input | Kept | Rejected by leaf cap | Time |
| --- | ---: | ---: | ---: | ---: |
| global feasible | 536458 | 362592 | 173866 | 2.354 s |
| global candidate | 1004862 | 679256 | 325606 | 4.412 s |

## Interpretation

The runtime jump at `0.5/0.5` is primarily search-space expansion, not topology generation or embedding. Relaxed slew allows many more segment chars and downstream topology states to survive. That produces four depth frontiers totaling about 1.0M global candidate references before final selection.

The dominant measured bottleneck is exact global delay/power Pareto extraction inside `SelectBestGlobalEntry`. The current nested-loop selector reduced 362592 feasible entries to 120 Pareto entries in 46.764 s. This is pure selection cost after global sink-load coverage filtering, and it directly matches the expected O(N^2) behavior.

The second major cost is depth search, especially root-driver compensation inside topology pattern search. Compensation applies to 1,015,227 entries and spends 15.077 s in load resolution. Cost lookup itself is already cached aggressively; load-resolution/cache strategy is the remaining compensation issue.

The third cost is sink-region filtering over large candidate pools. The run shows the legality cache is effective, but the filter is still scanning 1.5M global refs plus per-depth pools. Some of this timing is inflated by temporary per-entry cache-hit logging, but the scan volume is real.

## Optimization Ideas

### 1. Replace O(N^2) delay/power Pareto selection with an exact 2D scan

Classification: exact/no-solution-loss.

Current behavior checks every entry against every other entry. For two objectives, exact Pareto extraction can be done by sorting by delay ascending and scanning for strictly decreasing best power. Ties need deterministic handling with the existing tie-breakers and pattern ids. Complexity becomes O(N log N), preserving exactly the same non-dominated delay/power set if equality and tie handling are mirrored.

Expected impact: largest. The measured 46.764 s global selector should fall to sorting plus linear scan over 362592 entries. Local selectors also benefit, though they are small in this run.

QoR risk: low if tests compare selected pattern id or selected delay/power/power-median behavior against the current nested implementation over randomized and real captured candidate sets.

### 2. Reuse or cache root-load resolution by equivalence class

Classification: exact/no-solution-loss if the cache key includes all data that affects root load resolution.

Root compensation has only 68 unique direct cost keys, but load resolution ran 1,015,227 times and triggered 1,015,227 FLUTE route estimates. The existing root-load cache is keyed in a way that still grows per topology pattern in these composed frontiers. A better key should capture the direct root-side geometry/load equivalence, bottom/root segment pattern, terminal count, and any routing inputs that affect wire cap.

Expected impact: high. Compensation spent 15.602 s total, with 15.077 s in load resolution. If many topology patterns share equivalent root load, this should collapse to tens or hundreds of real route estimates rather than one per entry.

QoR risk: low only if the key is proven complete. An incomplete key would silently reuse wrong root load caps and alter delay/power.

### 3. Apply exact leaf-cap coverage filtering before expensive global selection

Classification: exact/no-solution-loss.

The global feasible pool shrank from 536458 to 362592 after sink-load leaf-cap coverage checks. This filter already happens before global selection in the current flow, which is good. The same principle can be applied earlier in per-depth processing where the required leaf-load cap is known or can be resolved by signature before local selection and before adding refs to the global pool. The filter condition is exact: entries with leaf-load cap index below the required cap cannot satisfy sink-load-region coverage.

Expected impact: moderate. It directly reduces global selector input and may reduce per-depth local selector and global pool sizes. In this run, global feasible filtering removed 173866 entries before selection.

QoR risk: low if applied only after exact legality/required-cap resolution and only using the existing coverage condition.

### 4. Bound global selection to the best exact frontier per depth after exact Pareto compression

Classification: exact/no-solution-loss if compression keeps the full delay/power Pareto frontier per depth, not only one selected entry.

Instead of passing every feasible entry from each depth to the final global selector, compress each depth feasible frontier to its exact delay/power Pareto set first. The selected policy only depends on delay/power Pareto followed by power-median ordering, so globally dominated points do not need to survive. Per-depth exact Pareto compression is safe because a point dominated within the same depth is also dominated in the global union.

Expected impact: high if implemented with the exact O(N log N) scan. The measured local depth Pareto sizes are small: 224, 264, 216, and 248. If exact per-depth Pareto sets are used for global refs, global selection input could drop from 362592 to about 952 entries for the feasible pool in this run.

QoR risk: low for delay/power selection, but validate that later reporting, fallback, and selected-depth metadata do not require dominated entries.

### 5. Heuristic frontier caps or epsilon dominance

Classification: approximate/risk.

Cap each frontier by power/delay bins, epsilon dominance, or a fixed top-K per state. This attacks search-space growth before compensation and filtering. It can materially reduce memory and runtime, but it intentionally removes legal solutions.

Expected impact: potentially high.

QoR risk: medium to high. It needs explicit QoR guardrails, comparison runs, and probably a config-controlled experimental mode rather than default behavior.

## Recommended Direction

Implement exact Pareto scan replacement first, then add exact per-depth Pareto compression before global selection.

Reason:

- It targets the measured top hotspot: 46.764 s in `SelectBestGlobalEntry`.
- It is exact for the selection objective, so it should preserve solution space relevant to delay/power Pareto selection.
- It is local to topology pruning and does not require changing CTS database, STA, embedding, or root compensation boundaries.
- It also creates a reusable implementation for local H-tree and source-trunk segment selectors.

Implementation outline:

1. Add a small exact 2D Pareto helper near the existing selector code or in the existing frontier utilities if reuse is appropriate.
2. Sort candidate refs by delay ascending, then power ascending, then existing deterministic tie-breaks.
3. Scan while tracking best power seen for lower/equal delay; keep only non-dominated entries.
4. Preserve current median-by-power ordering after Pareto extraction.
5. Apply the helper to `BuildDelayPowerParetoFront` for `HTreeTopologyChar`, `CandidateCharRef`, and optionally `SegmentChar`.
6. After the helper is validated, optionally compress each depth feasible/candidate global contribution to exact Pareto refs before appending to the global pool.

Validation plan:

1. Unit-test the new helper against the current nested-loop implementation on deterministic corner cases: equal delay, equal power, duplicate points, null candidate refs, and deterministic pattern-id tie cases.
2. Add randomized tests comparing the exact output set against the old nested implementation for thousands of small candidate sets.
3. Run the `0.05/0.05` and `0.5/0.5` `ics55_dev` flows and compare selected depth, selected pattern id, selected delay/power, inserted buffer count, metrics JSON, skew/transition/cap reports, and runtime.
4. Confirm the selected global Pareto size remains 120 for this captured `0.5/0.5` run before any per-depth compression is added.
5. If per-depth compression is added, verify that the final selected candidate remains identical or explain any difference by deterministic tie behavior.

## Remaining Risks and Follow-ups

- The temporary sink-region cache-hit probe is too verbose for repeated use. It produced a 627 MB `run.log` and 609 MB `htree_probe.log`. Keep it only for this one investigation or gate it behind a sampling/detail flag before any future run.
- The run is instrumented, so absolute runtimes include logging overhead. The global selector timing is reliable because it emits one line after the O(N^2) work; sink-region timings are inflated by per-entry cache-hit logs.
- Root compensation looks like the second algorithmic target, but its safe cache key needs careful proof. Do not optimize it by pointer or pattern id alone unless all load-resolution inputs are represented.
- No broad final quality check was run, per instruction. The main session should dispatch `trellis-check`.
