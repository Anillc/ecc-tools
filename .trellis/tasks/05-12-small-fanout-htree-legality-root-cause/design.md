# small-fanout H-tree legality root-cause debugging design

## Technical Design

This task is an investigation-first debugging effort. The initial failure is already localized to H-tree candidate filtering, so the design focuses on collecting the missing legality data before choosing a fix.

## Flow Boundary

The source-of-truth reproduction flow is:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The flow reads:

- Config: `scripts/design/ics55_dev/iEDA_config/cts_default_config.json`
- Script: `scripts/design/ics55_dev/script/iCTS_script/run_iCTS_dev.tcl`
- CTS log: `scripts/design/ics55_dev/result/cts/cts.log`

Do not use `/home/liweiguo/project/ecc-tools` for this task unless explicitly comparing branches. That checkout has a separate hard-coded flow workspace and separate config.

## Current Failure Shape

The current `max_fanout = 4` run reaches these stages:

- CTS read data succeeds.
- Sink clustering is enabled and creates `3010` H-tree loads from `8751` regular sinks.
- H-tree topology generation succeeds with depth `11` and `2048` leaves.
- Segment and topology frontiers are generated successfully.
- `Filter sink-load region` removes every candidate at depths `11`, `10`, `9`, and `8`.
- Global selection sees no feasible or candidate refs and reports `no_legal_depth_candidates`.

This means the immediate problem is not characterization coverage or topology construction failure. The missing evidence is the fanout distribution and failure reason distribution inside `FilterSinkLoadRegionLegalEntries`.

## Relevant Code Paths

- Config parse and runtime report:
  - `src/operation/iCTS/source/database/config/Config.cc`
  - `src/operation/iCTS/source/database/config/Config.hh`
- Sink clustering:
  - `src/operation/iCTS/source/flow/synthesis/topology/sink/SinkLoadClustering.cc`
  - `src/operation/iCTS/source/module/topology/fast_clustering/`
- H-tree build orchestration:
  - `src/operation/iCTS/source/flow/synthesis/htree/HTree.cc`
  - `src/operation/iCTS/source/flow/synthesis/htree/plan/Plan.cc`
  - `src/operation/iCTS/source/flow/synthesis/htree/plan/DepthPlan.cc`
- Candidate filtering:
  - `src/operation/iCTS/source/flow/synthesis/htree/topology_pruning/TopologyPruning.cc`
  - `src/operation/iCTS/source/flow/synthesis/htree/region/SinkLoadRegion.cc`

## Data To Collect

For each evaluated depth:

- Raw frontier count.
- Candidate frontier count before and after sink-load-region filtering.
- Boundary-feasible raw count before and after sink-load-region filtering.
- First failure reason.
- Failure reason histogram by violation enum.
- Boundary load group count distribution:
  - min, max, mean, median.
  - count above configured `max_fanout`.
  - worst group node, anchor, load count.
- Bottom-most buffered level distribution and monotone-pruning threshold evolution.

For sink clustering:

- Input sink count.
- Output H-tree load count.
- Cluster size distribution.
- Fanout/cap legality status for generated local buffers.
- Spatial distribution of clustered local-buffer loads that feed the H-tree leaf fanout-relative algorithm.
- Comparison between original sink fanout pressure and clustered-load fanout pressure at H-tree leaf regions.

For H-tree intermediate levels:

- Whether each intermediate node/driver has an explicit fanout model during topology generation, pattern construction, or segment frontier synthesis.
- Whether candidate pattern IDs encode enough child/load count information to reject fanout violations before leaf-region filtering.
- Per-level branch fanout distribution for selected or candidate H-tree patterns where available.
- The stage at which fanout is first enforced for root, intermediate, and leaf drivers.

## Hypotheses And Evidence Plan

### H1: Current topology cannot satisfy `max_fanout = 4`

Evidence needed: boundary groups at the deepest explored legal topology still exceed 4 loads, and deeper topology or different leaf count is not currently explored.

### H2: Sink clustering makes downstream H-tree grouping infeasible

Evidence needed: clustered local buffers are legal as clusters, but their placement/load distribution causes H-tree boundary groups to exceed fanout. Compare clustering enabled vs disabled only as a diagnostic, not as a proposed default.

### H3: Leaf fanout-relative algorithm is too tightly coupled to clustering

Evidence needed: the leaf-region fanout calculation is based on clustered local-buffer loads rather than original sinks or a hierarchical split, and small `max_fanout` causes clustered load placement to create unavoidable leaf boundary fanout violations.

### H4: Intermediate H-tree levels do not consider fanout early enough

Evidence needed: topology generation, segment frontier construction, and topology pattern assembly do not reject or cost intermediate-level fanout; fanout illegality is only observed later at sink-load-region legality, causing late all-candidate rejection.

### H5: Monotone pruning is too coarse

Evidence needed: once one signature fails fanout, `max_monotone_failed_level` suppresses candidates with the same or lower bottom-most buffered level even though another segment pattern could have changed boundary groups or legality.

### H6: Depth exploration window is too shallow

Evidence needed: increasing `htree_depth_explore_window` or forcing a deeper target depth yields legal candidates, or theoretical minimum leaf capacity shows the current depths are insufficient.

### H7: Fanout is applied at the wrong abstraction boundary

Evidence needed: `max_fanout` intended for electrical branch fanout is being applied directly to sink-load-region boundary groups that do not map one-to-one to final driver fanout.

## Diagnostics Strategy

Prefer a narrow debug summary emitted by `SinkLoadRegion` / `TopologyPruning` over noisy per-candidate logs. The summary should be deterministic and small enough for `cts.log`.

Recommended diagnostic object:

- `SinkLoadRegionFilterStats`
- Count per `SinkLoadRegionViolation`.
- First failure reason.
- Worst group load count and node/anchor.
- Bottom-most buffered level min/max and prune threshold.
- Leaf fanout-relative inputs: clustered load count, original sink count when traceable, and load-group count per leaf boundary.
- Intermediate level fanout summary: node level, child count or load count proxy, and whether a fanout check was applied.

If code instrumentation is not yet desired, a temporary local debug branch can emit these values to `cts.log`, but the task should still capture whether the diagnostic is worth keeping permanently.

## Validation Boundary

A fix is not validated by process exit code alone. The flow currently can return shell exit code 0 even when CTS internal status is failed. Validation must inspect `cts.log` or structured report fields for:

- `CTS Clock Tree Synthesis Overview` status.
- `failed_clocks`.
- `CTS Key Results` status.
- Selected H-tree depth and final buffer count when successful.

Debugging does not use broad `ecc dev` checks as a gate. The task should use targeted debug commands, focused tests if a code fix is made, and the `ics55_dev` flow as the final acceptance standard.

The final acceptance command is exactly:

```bash
cd /home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev
./iEDA -script ./script/iCTS_script/run_iCTS_dev.tcl
```

The final report must explicitly answer:

- Whether the H-tree leaf fanout-relative algorithm fails because upstream sink clustering makes small fanout constraints too tight or structurally infeasible.
- Whether H-tree intermediate levels consider fanout during construction, and whether missing or delayed intermediate fanout handling contributes to the failure.

## Rollout / Rollback

Investigation-only changes should be kept in task artifacts. Any code diagnostics should be narrowly scoped and easy to remove. Functional fixes should be gated by:

- Unit test covering the failing legality/filtering case where possible.
- `ics55_dev` run for `max_fanout = 4`.
- Regression run for known-good `max_fanout = 32`.
