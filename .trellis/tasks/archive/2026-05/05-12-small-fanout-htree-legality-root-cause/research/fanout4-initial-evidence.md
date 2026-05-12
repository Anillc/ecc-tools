# fanout4 initial evidence

## Scope

This note records the initial evidence for the dedicated small-fanout H-tree legality debugging task. It is based on the dev checkout:

```bash
/home/liweiguo/project/ecc-tools-dev
```

## Flow And Config

Command under investigation:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The script hard-codes:

```tcl
set WORKSPACE "/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev"
```

and calls:

```tcl
run_cts -config $IEDA_CONFIG_DIR/cts_default_config.json -work_dir $TOOL_REPORT_DIR
```

Current dev config:

```json
"max_fanout": "4"
```

Latest CTS log confirms the runtime value:

```text
| max_fanout | 4 | fanout constraint |
```

## Observed Failure

Relevant log path:

```text
/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/result/cts/cts.log
```

Key input and topology facts:

```text
root_loads: 8751
sink_clustering_enabled: true
htree_sinks: 3010
cluster_buffers: 3010
H-tree topology depth: 11
leaf_count: 2048
valid_leaf_paths: 2048
invalid_leaf_paths: 0
```

Per-depth filtering result:

| depth | raw frontier entries | feasible raw entries | candidate after sink-load-region | feasible after sink-load-region |
|---:|---:|---:|---:|---:|
| 11 | 697140 | 371632 | 0 | 0 |
| 10 | 476812 | 253940 | 0 | 0 |
| 9 | 415982 | 222182 | 0 | 0 |
| 8 | 332461 | 177593 | 0 | 0 |

Global result:

```text
evaluated_depths: 4
global_feasible_refs: 0
global_candidate_refs: 0
reason: no_legal_depth_candidates
failed_clocks: 1
clock domain detail: H-tree build failed: no_legal_depth_candidates
```

## Source-Level Observation

`SinkLoadRegion.cc` evaluates legality by comparing each boundary group's load count against `CONFIG_INST.get_max_fanout()`:

```cpp
if (max_fanout > 0U && loads->size() > max_fanout) {
  result.violation = SinkLoadRegionViolation::kFanout;
  result.monotone_hard_fail = true;
  return result;
}
```

The same path also applies fanout through `Clustering::evaluateClusterElectrical`.

`TopologyPruning.cc` calls `FilterSinkLoadRegionLegalEntries` on both the full topology frontier and the boundary-feasible raw frontier. In the failing run, both filtered results are empty for all explored depths.

## Immediate Interpretation

The failure is not a flow path mix-up in `ecc-tools-dev`: the dev flow reads the dev config and sees `max_fanout = 4`.

The failure is also not an early topology-generation failure: the topology and large candidate frontiers are built. The immediate blocker is sink-load-region legality filtering removing every H-tree topology candidate.

The current logs are insufficient to prove the root cause. They do not show which boundary groups fail fanout, the worst fanout, failure histograms, or whether monotone hard-fail pruning hides potentially legal candidates.

## Next Evidence Needed

- Rejection reason histogram from `FilterSinkLoadRegionLegalEntries`.
- Worst boundary group load count and anchor.
- Boundary group fanout distribution per depth.
- Monotone pruning threshold evolution.
- Comparison against `max_fanout = 32`.
- Sensitivity tests for depth window and sink clustering.
