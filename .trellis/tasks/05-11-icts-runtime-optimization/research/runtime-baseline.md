# Runtime Baseline Research

## Scope

Target command:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

This note records current runtime distribution and source-level bottleneck attribution for the iCTS runtime optimization task.

## Existing Log Snapshot Before Fresh Run

Existing `scripts/design/ics55_dev/result/cts/cts.log` was generated at `2026-05-11 11:33:11`.

CTS stage runtime:

| Stage | Elapsed Time (s) | Peak VMem Delta (MB) |
| --- | ---: | ---: |
| read_data | 8.022 | 0.000 |
| synthesis | 31.896 | 5615.868 |
| instantiation | 0.011 | 0.000 |
| evaluation | 7.598 | 1780.688 |
| total | 47.530 | 7396.556 |
| report | 1.333 | 0.000 |

Runtime share within CTS total:

| Stage | Share |
| --- | ---: |
| synthesis | 67.1% |
| read_data | 16.9% |
| evaluation | 16.0% |
| instantiation | ~0.0% |

Key H-tree/search metrics in the existing log:

| Metric | Value |
| --- | ---: |
| clock sinks | 8751 |
| sink-cluster / topology loads | 319 |
| characterization segment chars | 19515 |
| executed STA samples | 19515 |
| generated segment patterns | 87 |
| depth candidates | 4 |
| selected depth | 5 |
| selected final frontier count | 243981 |
| candidate frontier entries | 236991 |
| feasible solutions | 130294 |
| feasible frontier entries | 126566 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| final clock buffer count | 360 |
| total clock network wirelength | 43151.203 um |
| setup / hold WNS | 7.302 ns / 0.008 ns |

## Prior Work That Constrains This Task

Archived opt3 baseline facts:

- opt1 exact global delay/power Pareto scan removed the former O(N^2) selector bottleneck.
- opt2 per-depth Pareto compression reduced global selection input without changing the exact result.
- opt3 root-load signature cache removed repeated physical root-load FLUTE estimation.
- Post-opt3 P1/P6 production experiments had negligible fixture benefit and were rolled back.
- P2/P3/P4/P5 exact pruning families remain unproven or low-value in prior attempts.
- P7 characterization reachability / lazy characterization is a recommended future direction because characterization remained a large fraction of HTree build after opt3.

## Fresh Baseline

Captured at `2026-05-11 13:08:37` using the current `scripts/design/ics55_dev/iEDA` binary.

Artifacts are stored under:

```text
.trellis/tasks/05-11-icts-runtime-optimization/artifacts/fresh_baseline/
```

Captured files:

- `run.log`
- `time.txt`
- `stage_markers.log`
- `cts.log`
- `iCTS_metrics.json`
- `cts_stat.json`

### Process Runtime

From `time.txt`:

| Metric | Value |
| --- | ---: |
| wall time | 69.99 s |
| user time | 92.85 s |
| system time | 15.96 s |
| CPU | 155% |
| max RSS | 6174168 KB |
| exit status | 0 |

### CTS Runtime Distribution

From fresh `cts.log`:

| Stage | Elapsed Time (s) | Share of CTS Total | Peak VMem Delta (MB) |
| --- | ---: | ---: | ---: |
| read_data | 9.426 | 19.6% | 0.000 |
| synthesis | 31.167 | 64.8% | 3760.772 |
| instantiation | 0.011 | ~0.0% | 0.000 |
| evaluation | 7.480 | 15.6% | 3439.164 |
| total | 48.088 | 100.0% | 7199.936 |
| report | 1.433 | outside CTS total | 0.000 |

### Synthesis / HTree Marker Distribution

From `stage_markers.log`:

| Component | Runtime | Share of Synthesis | Share of CTS Total | Basis |
| --- | ---: | ---: | ---: | --- |
| `CTSFlow` synthesis | 31.167 s | 100.0% | 64.8% | schema stage |
| pre-HTree synthesis work | ~1.478 s | 4.7% | 3.1% | `CTSFlow` start to `HTree` start |
| `HTree::build` total | 17.079 s | 54.8% | 35.5% | schema stage |
| `CharBuilder::build` | 7.472 s | 24.0% | 15.5% | schema stage |
| HTree non-CharBuilder residual | ~9.607 s | 30.8% | 20.0% | `HTree::build - CharBuilder::build` |
| post-HTree synthesis work | ~12.610 s | 40.5% | 26.2% | `CTSFlow` finish minus `HTree` finish |

Important limitation: `post-HTree synthesis work` is not currently broken down by schema stage. Source inspection shows this region includes downstream topology commit/layout/reporting and source-to-root trunk synthesis, but the current logs do not prove the exact split.

### Fresh HTree / QoR Metrics

The fresh run matches the known opt3 solution family:

| Metric | Value |
| --- | ---: |
| clock sinks | 8751 |
| topology loads after sink clustering | 319 |
| characterization segment chars | 19515 |
| executed STA samples | 19515 |
| skipped STA samples | 60 |
| generated segment patterns | 87 |
| depth candidates | 4 |
| selected depth | 5 |
| selected topology pattern id | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` |
| selected final frontier count | 243981 |
| candidate frontier entries | 236991 |
| feasible solutions | 130294 |
| feasible frontier entries | 126566 |
| selected delay / power | 0.4959 ns / 217.271 uW |
| raw H-tree char metric | 0.2897 ns / 192.458 uW |
| root-driver compensation | 0.2063 ns / 24.813 uW |
| final clock buffer count | 360 |
| total clock network wirelength | 43151.203 um |
| setup / hold WNS | 7.302292 ns / 0.008315 ns |

## Source-Level Attribution

The current high-level bottleneck is still `synthesis`, not DB read, report, or instantiation. Based on archived opt3 evidence and the current log shape, the likely remaining algorithmic bottleneck is no longer the old final Pareto selector or root-load estimation loop. The next useful work should first prove where opt3 spends time today, with special attention to:

- eager characterization and STA sample generation;
- topology composition/frontier generation before final filtering;
- sink-load coverage or legality checks only if they prune before expensive upstream work;
- depth-candidate reuse only if pattern-id and tie-break equivalence can be proven.

Current source map:

| Runtime Region | Code Boundary | Observation |
| --- | --- | --- |
| top-level synthesis | `Synthesis::run` in `src/operation/iCTS/source/flow/synthesis/Synthesis.cc` | Only one coarse `synthesis` runtime metric exists. |
| sink-domain topology formation | `Topology::formClock`, `BuildSinkTree`, `PrepareSinkTreeLoads` | Sink clustering and object preparation have no dedicated schema timing. |
| downstream HTree | `HTree::build` | Has a schema stage and is 17.079 s in the fresh run. |
| characterization | `RunCharacterizationFlow`, `CharBuilder::build`, `CharBuilder::sampleFeasibleTopology` | `CharBuilder::build` is 7.472 s and executes 19515 STA samples. |
| HTree topology search | `SearchTopologyDepthCandidates`, `EvaluateCandidateBuild`, `BuildPatternSearch` | Current cts.log reports final counts but not per-depth or per-substage runtime. |
| root-driver compensation | `RootDriverCompensationPass::apply/evaluate` | opt3 cache is present; fresh logs do not expose current residual compensation time. |
| sink-load legality / coverage | `FilterSinkLoadRegionLegalEntries`, `FilterGlobalEntriesBySinkLoadRegionCoverage` | Counts are reported only after filtering; runtime is not reported. |
| source-to-root trunk | `BuildSourceTrunkTree`, `SourceTrunkSegment::build` | Log reports strict candidate count 24152 but no runtime stage. |

## Recommended Next Step

Use a two-step implementation slice rather than jumping straight into pruning:

1. Add low-noise, production-quality substage runtime metrics for the unmeasured synthesis regions.
   Measure at least sink clustering/preparation, downstream HTree non-characterization, HTree topology search, segment frontier synthesis, root-driver compensation, sink-load filtering/coverage, embedding, source-trunk segment build, and commit/layout construction.

2. Add a default-off characterization reachability analyzer if the substage metrics confirm characterization remains a large actionable component.
   The analyzer should report which characterized `(length_idx, pattern_id, input_slew_idx, load_cap_idx)` tuples are consumed by segment synthesis, topology frontiers, and the selected topology.

This direction is materially different from the prior failed/low-value late-pruning loops because it first proves whether runtime can be removed before expensive characterization or topology materialization occurs.

## Substage Instrumentation Run

Captured at `2026-05-11 13:26` using the rebuilt `scripts/design/ics55_dev/iEDA` with schema-backed substage timing.

Artifacts are stored under:

```text
.trellis/tasks/05-11-icts-runtime-optimization/artifacts/substage_metrics/
```

### Process Runtime

| Metric | Value |
| --- | ---: |
| wall time | 69.83 s |
| user time | 90.61 s |
| system time | 16.16 s |
| CPU | 152% |
| max RSS | 6231280 KB |
| exit status | 0 |

### CTS Runtime Distribution

| Stage | Elapsed Time (s) | Share of CTS Total |
| --- | ---: | ---: |
| read_data | 8.220 | 17.0% |
| synthesis | 32.411 | 66.9% |
| instantiation | 0.011 | ~0.0% |
| evaluation | 7.837 | 16.2% |
| total | 48.482 | 100.0% |
| report | 1.332 | outside CTS total |

### Synthesis Substage Distribution

| Region | Runtime | Share of Synthesis | Notes |
| --- | ---: | ---: | --- |
| pre-topology synthesis residual | ~0.045 s | 0.1% | `CTSFlow` start to sink-load preparation start |
| sink load preparation/clustering | 1.401 s | 4.3% | 8751 sinks clustered to 319 H-tree sinks |
| downstream HTree wrapper | 18.667 s | 57.6% | includes `HTree::build` plus result move/assignment overhead |
| `HTree::build` | 18.279 s | 56.4% | downstream H-tree algorithm region |
| `CharBuilder::build` | 8.321 s | 25.7% | 19515 STA samples |
| HTree segment frontier synthesis | 0.327 s | 1.0% | 73352 segment frontier entries across 4 length sets |
| HTree topology depth search | 9.429 s | 29.1% | 4 depths, 1015227 compensated candidates |
| HTree global sink-load coverage | 0.158 s | 0.5% | 362592 covered feasible refs, 679256 covered candidate refs |
| HTree global topology selection | 0.040 s | 0.1% | 824 feasible Pareto refs; strict-feasible selected |
| sink-domain commit/layout | 2.113 s | 6.5% | object commit plus layout merge |
| source-to-root trunk build | 10.181 s | 31.4% | newly identified actionable post-HTree bottleneck |
| source trunk segment frontier synthesis | 7.442 s | 23.0% | generated only one consumed all-frontier set but also built unused branch/leaf sets |
| source trunk segment selection/object build | ~0.002 s | ~0.0% | selected unbuffered segment, no inserted objects |
| source trunk residual/destruction/report | ~2.737 s | 8.4% | dominated by cleanup of large local frontier/pattern data after segment synthesis |

The key new finding is that the largest non-HTree region is not commit/layout. It is source-to-root top-segment frontier synthesis and cleanup. `SourceTrunkSegment::build` only consumes `all_frontier_entries`, but the shared `SynthesizeSegmentEntrySets` API also builds `branch_buffered_entries` and `leaf_unbuffered_entries`, which are required by downstream HTree topology search but unused by the top-segment path.

## Implemented Optimization: Source Trunk All-Frontier Synthesis

Implemented a source-trunk-specific segment synthesis entry point:

- `SynthesizeSegmentEntrySets(...)` keeps the existing full all/branch/leaf behavior for HTree.
- `SynthesizeSegmentAllFrontierEntrySets(...)` builds only `all_frontier_entries` and is used by `SourceTrunkSegment::build`.

This preserves the exact downstream HTree path and narrows the source-to-root top-segment work to the data that the caller actually reads.

### Optimized Benchmark

Captured at `2026-05-11 13:31`.

Artifacts are stored under:

```text
.trellis/tasks/05-11-icts-runtime-optimization/artifacts/source_trunk_all_frontier/
```

Process runtime:

| Metric | Fresh baseline | Instrumented pre-opt | Optimized | Delta vs fresh | Delta vs pre-opt |
| --- | ---: | ---: | ---: | ---: | ---: |
| wall time | 69.99 s | 69.83 s | 65.32 s | -4.67 s | -4.51 s |
| user time | 92.85 s | 90.61 s | 87.27 s | -5.58 s | -3.34 s |
| system time | 15.96 s | 16.16 s | 15.58 s | -0.38 s | -0.58 s |
| max RSS | 6174168 KB | 6231280 KB | 6149684 KB | -24484 KB | -81596 KB |

CTS runtime:

| Stage | Fresh baseline | Instrumented pre-opt | Optimized | Delta vs fresh | Delta vs pre-opt |
| --- | ---: | ---: | ---: | ---: | ---: |
| read_data | 9.426 s | 8.220 s | 8.115 s | -1.311 s | -0.105 s |
| synthesis | 31.167 s | 32.411 s | 27.603 s | -3.564 s | -4.808 s |
| instantiation | 0.011 s | 0.011 s | ~0.011 s | ~0.000 s | ~0.000 s |
| evaluation | 7.480 s | 7.837 s | 7.779 s | +0.299 s | -0.058 s |
| total | 48.088 s | 48.482 s | 43.511 s | -4.577 s | -4.971 s |
| report | 1.433 s | 1.332 s | 1.428 s | -0.005 s | +0.096 s |

Source-trunk hot path:

| Region | Instrumented pre-opt | Optimized | Delta |
| --- | ---: | ---: | ---: |
| `Topology::Build source trunk` | 10.181 s | 5.231 s | -4.950 s |
| `SourceTrunk::Dispatch source trunk synthesis` | 10.147 s | 5.171 s | -4.976 s |
| `SourceTrunkSegment::Synthesize segment frontier` | 7.442 s | 3.769 s | -3.673 s |
| source-trunk post-object residual/destruction | ~2.72 s | ~1.45 s | ~-1.27 s |

### QoR / Correctness Comparison

The optimized run preserved the default CTS result:

| Metric | Fresh baseline | Optimized |
| --- | ---: | ---: |
| selected HTree depth | 5 | 5 |
| selected topology pattern id | 10297477 | 10297477 |
| selected level segment pattern ids | `522717,468375,28,24,6` | `522717,468375,28,24,6` |
| selected delay / power | 0.4959 ns / 217.271 uW | 0.4959 ns / 217.271 uW |
| raw H-tree metric | 0.2897 ns / 192.458 uW | 0.2897 ns / 192.458 uW |
| selected physical root load | 0.1428 pF | 0.1428 pF |
| selected root driver cell | BUFX20H7L | BUFX20H7L |
| final clock buffer count | 360 | 360 |
| total clock network wirelength | 43151.203 um | 43151.203 um |
| setup WNS | 7.302292 ns | 7.302292 ns |
| hold WNS | 0.008315 ns | 0.008315 ns |

The source-trunk internal selected segment pattern id changed from `9095920` to `4729619` because the all-frontier-only mode allocates fewer unused branch/leaf pattern ids. The selected top segment still inserted zero objects and the committed CTS/QoR metrics are unchanged.

## Updated Bottleneck Ranking

After the source-trunk optimization, `synthesis` is still the dominant CTS stage, but its post-HTree source-trunk bottleneck is materially reduced. The remaining algorithmic hotspots are now:

1. HTree topology depth search: `9.609 s` in the optimized run.
2. CharBuilder characterization: `8.311 s`.
3. Source-trunk all-frontier synthesis and cleanup: `5.231 s` total source-trunk build, with `3.769 s` in frontier synthesis.

The next runtime-reducing loop should target HTree topology composition or characterization reachability/lazy characterization. Source trunk still has some residual potential, but the largest remaining synthesis costs are now back inside downstream HTree search and characterization.
