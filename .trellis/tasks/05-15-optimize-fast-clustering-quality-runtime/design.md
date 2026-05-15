# design.md

## Technical Design

### Problem Boundary

This task targets `src/operation/iCTS/source/module/topology/fast_clustering` and the CTS sink-load clustering call path in `src/operation/iCTS/source/flow/synthesis/topology/sink`.

The relevant production path is:

1. `SinkLoadClustering::PrepareSinkTreeLoads`
2. `TopologyGen::defaultFastClustering`
3. `Clustering::defaultFastClustering`
4. `FastClustering::run`
5. `CollectEntries`
6. `BuildSpatialRecursiveClusters`
7. `PolishSmallClusters`
8. `PolishBoundaryLoads`
9. `FinalizeClusters`

### Current Root-Cause Hypotheses

#### Low Average Fanout

The current algorithm first creates legal draft clusters using recursive longest-axis partitioning. The stop condition accepts any cluster with `size <= max_fanout`. For `max_fanout=4`, any recursive subtree containing 5 loads is split into `3 + 2`; 6 loads into `3 + 3`; 7 loads into `4 + 3`. These clusters are already legal and cannot always be merged because most neighboring legal pairs exceed fanout 4.

The observed final distribution has no singleton clusters, but is dominated by fanout 2 and 3. That points to partition shape and merge policy, not a small number of electrical repair splits.

The current merge polish only merges when the objective improves, except singleton-related forced merges. It does not treat underfilled non-singleton clusters as a utilization problem. As a result, many legal but inefficient 2/3 clusters remain.

#### Runtime

`FastClustering::run()` currently logs only one final line. Existing huge-case timing brackets show almost all `Prepare sink loads` time is before `Fast clustering done`.

The strongest code-level runtime risk is `SelectNearestActiveNeighbors`: every merge and boundary-polish query scans all clusters and then sorts the full candidate list to keep only a small bounded number of neighbors. With roughly 45k clusters, this produces an avoidable `O(k^2 log k)` behavior.

Secondary risks:

- `CalcDraftAggregate` is recomputed repeatedly inside boundary polish.
- `BuildDraft` recalculates bounds/root/routing-cap proxy for many small candidate moves.
- `FinalizeClusters` may run exact cap/routing evaluation for every final cluster because `max_cap` is finite, but fanout is tiny and this should be measured before optimizing.

### Measurement Plan

Add or use focused stage timing for:

- `CollectEntries`
- `BuildSpatialRecursiveClusters`
- `PolishSmallClusters`, including merge rounds and neighbor selection
- `PolishBoundaryLoads`, including order build, aggregate calculation, neighbor selection, boundary candidate evaluation
- `FinalizeClusters`, including repair split count and final fanout distribution

The timing should be low-noise and useful for future CTS large-case diagnosis.

### Candidate Fix Direction

1. First make runtime visible with internal timers and final fanout histogram.
2. Replace full active-neighbor sorting with a bounded nearest-neighbor selection or spatial bucket/k-nearest index.
3. Adjust partition/merge policy so cluster count is driven by target utilization under constraints, not merely by `size <= max_fanout`.
4. Preserve final legal evaluation as the correctness gate.

## Rollout / Rollback

FastClustering is isolated behind `TopologyGen::defaultFastClustering`. Changes can be rolled back by reverting files under `module/topology/fast_clustering` plus any associated diagnostics/tests.
