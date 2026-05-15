# Initial FastClustering Diagnosis

## Huge-Case Evidence

Source run:

- `scripts/design/ics55_huge_dev/result_clock_native_sdc_full`
- input DEF: `design_clock.def`
- clock net: `clock`
- valid regular sinks: `129494`
- CTS `Prepare sink loads`: `232.807s`
- `Fast clustering done: loads=129494, clusters=44962, strategy=recursive_spatial_bisect`

The generated DEF cluster sink nets show this fanout distribution:

| Fanout | Cluster Count | Percent |
| ---: | ---: | ---: |
| 2 | 13412 | 29.83% |
| 3 | 23530 | 52.33% |
| 4 | 8020 | 17.84% |

Average fanout:

```text
129494 / 44962 = 2.88008
```

There are no fanout-1 clusters in this output, so the issue is not singleton cleanup failure. The algorithm systematically leaves legal but underfilled clusters.

## Current Algorithm Path

`FastClustering::run()`:

1. `CollectEntries(loads)`
2. `BuildSpatialRecursiveClusters(entries, config)`
3. `PolishSmallClusters(drafts, entries, config)`
4. `FinalizeClusters(drafts, entries, config)`
5. completeness check

`PolishSmallClusters()` internally calls:

1. merge polish for two fixed rounds
2. `PolishBoundaryLoads()`
3. remove inactive drafts

## Quality Root-Cause Hypothesis

The recursive partition stage accepts any leaf draft with `entry_ids.size() <= max_fanout`. For `max_fanout=4`, recursive subproblems naturally produce leaf sizes 2, 3, and 4. Examples:

- 5 loads become `3 + 2`
- 6 loads become `3 + 3`
- 7 loads become `4 + 3`
- 8 loads become `4 + 4`

Because 2+3, 3+3, and 3+4 exceed fanout 4 in many neighboring combinations, the later merge polish cannot recover high utilization after the partition has already committed to this shape.

The merge polish also only forces singleton merges. Fanout-2 and fanout-3 clusters are considered legal and only merge when the objective improves, so utilization is not an explicit objective.

Given `max_diameter=INT_MAX`, current evidence does not support diameter as the cause. Cap may still matter in final evaluation, but the clean 2/3/4-only distribution suggests the main cause appears before final electrical repair.

## Runtime Root-Cause Hypothesis

The current runtime bottleneck is likely not the H-tree solver but FastClustering polish:

- Existing logs show `Prepare sink loads = 232.807s`.
- `Fast clustering done` occurs only `0.046s` before `Prepare sink loads` finishes.
- Therefore almost the full `232.8s` is inside `FastClustering::run()`.

The strongest code-level hotspot is `SelectNearestActiveNeighbors()`:

```text
for every query:
  scan all clusters
  push active candidates
  sort all candidates
  keep only 8 or 12 nearest neighbors
```

With roughly 45k clusters, this is an avoidable all-pairs style operation. Both merge polish and boundary polish call it repeatedly. The algorithm document already notes this limitation and suggests replacing it with a spatial bucket / k-nearest graph.

## Immediate Next Measurement

Add timers inside `FastClustering::run()` for:

- collect entries
- recursive partition
- merge polish
- boundary polish
- finalize/electrical evaluation

Also emit:

- draft cluster count after partition
- draft cluster count after polish
- final cluster count
- fanout histogram
- repair split count

This will convert the current code-level hypothesis into measured evidence before changing algorithm behavior.

## Diagnostic Run With Internal Timers

Command shape:

```bash
cd scripts/design/ics55_huge_dev
env INPUT_DEF=./design_clock.def \
    RESULT_DIR=./result_fast_clustering_diagnosis \
    TOOL_REPORT_DIR=./result_fast_clustering_diagnosis/cts \
    OUTPUT_DEF=./result_fast_clustering_diagnosis/iCTS_result.def \
    OUTPUT_VERILOG=./result_fast_clustering_diagnosis/iCTS_result.v \
    DESIGN_STAT_TEXT=./result_fast_clustering_diagnosis/report/cts_stat.rpt \
    DESIGN_STAT_JSON=./result_fast_clustering_diagnosis/report/cts_stat.json \
    TOOL_METRICS_JSON=./result_fast_clustering_diagnosis/metric/iCTS_metrics.json \
    CTS_CONFIG=/home/liweiguo/project/ecc-tools-dev/.trellis/tasks/05-15-optimize-fast-clustering-quality-runtime/research/huge_design_clock_sdc_config.json \
    /usr/bin/time -p timeout 20m ./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

Run completed successfully.

Overall stage timing:

| Stage | Runtime |
| --- | ---: |
| read_data | 0.363s |
| synthesis | 313.213s |
| instantiation | 1.578s |
| evaluation | 55.361s |
| report | 5.305s |
| CTS total | 370.697s |
| script wall | 418.45s |

FastClustering internal timing:

| FastClustering Phase | Runtime | Output |
| --- | ---: | --- |
| collect entries | 0.002s | 129494 valid entries |
| recursive partition | 0.411s | 45316 drafts, avg fanout 2.858, histogram `2=17378,3=17014,4=10924` |
| merge polish round 0 | 76.333s | active 45316 -> 44980, 336 merges |
| merge polish round 1 | 74.567s | active 44980 -> 44962, 18 merges |
| boundary polish round 0 | 42.936s | 44962 sources considered, 11850 moved loads |
| boundary polish round 1 | 38.825s | 44962 sources considered, 3710 moved loads |
| polish total | 232.662s | active clusters 44962 |
| finalize | 1.722s | 44962 final clusters, 129494 assigned loads |
| FastClustering total | 234.798s | avg fanout 2.880 |
| Prepare sink loads total | 234.845s | htree_sinks 44962, cluster_buffers 44962 |

Final fanout distribution:

| Fanout | Cluster Count |
| ---: | ---: |
| 2 | 13412 |
| 3 | 23530 |
| 4 | 8020 |

## Diagnosis From Measurement

### Quality

The low-utilization shape is already present immediately after recursive partition:

```text
drafts=45316
avg_fanout=2.858
fanout_histogram=2=17378,3=17014,4=10924
```

Polish improves average fanout only from `2.858` to `2.880`; cluster count drops by only `354`, from `45316` to `44962`.

This confirms the quality problem is primarily created by the recursive partition policy. It stops as soon as each draft is legal under `size <= max_fanout`, but legality is weaker than high utilization. With `max_fanout=4`, recursive bisection naturally leaves many 2/3 clusters.

The issue is not caused by final electrical repair:

- finalize keeps the same `44962` clusters and the same fanout histogram,
- all `129494` loads are assigned,
- there is no evidence of repair-driven over-splitting in this run.

### Runtime

Runtime is dominated by polish:

```text
FastClustering total = 234.798s
recursive partition = 0.411s
polish = 232.662s
finalize = 1.722s
```

Merge polish is especially inefficient:

```text
round 0: 76.333s for 336 merges
round 1: 74.567s for 18 merges
```

Boundary polish is also expensive:

```text
round 0: 42.936s
round 1: 38.825s
```

Given the implementation, the cause is algorithmic complexity plus inefficient implementation:

- merge and boundary repeatedly iterate over about 45k active clusters,
- each candidate search calls `SelectNearestActiveNeighbors()`,
- that function scans all clusters and sorts all active candidates even though only 8 or 12 nearest neighbors are needed,
- `CalcDraftAggregate()` also scans all active clusters repeatedly inside source loops.

The partition phase is fast enough. The current "fast" algorithm loses its runtime advantage in polish because polish behaves like a repeated all-cluster search over a large cluster set.

## Recommended Next Fix Order

1. Replace full active-neighbor sort in `SelectNearestActiveNeighbors()` with bounded nearest selection or a spatial bucket/k-nearest index.
2. Avoid recomputing full `CalcDraftAggregate()` inside every source loop; maintain or pass stable per-round aggregate when possible.
3. Add a packing/utilization-aware partition or compaction step before expensive polish, so `max_fanout=4` aims for mostly 4-load clusters unless cap/geometry rejects it.
4. Keep final legality evaluation as the correctness gate.

## Validation Addendum

The recommended fix order was implemented as P1-P4 and validated on the huge `design_clock.def` case. The final P4 F4 run reduced FastClustering from `234.798s` to `2.207s`, reduced clusters from `44962` to `32379`, improved average fanout from `2.880` to `3.999`, and completed full CTS in `120.177s`.

The fanout sweep (`max_fanout=4,8,16,32,64`) shows the packing policy generalizes: average fanout stays near the configured limit and clustering remains under `4s` on this `129494`-sink case. See `p1_p4_validation.md` for detailed per-step timing, full-flow outcomes, and the F16 evaluation sensitivity note.
