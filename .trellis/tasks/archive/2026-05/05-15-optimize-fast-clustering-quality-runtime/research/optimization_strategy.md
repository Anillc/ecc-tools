# FastClustering Optimization Strategy

## Current Pipeline and Why Each Step Exists

The current production path is:

```text
CollectEntries
  -> BuildSpatialRecursiveClusters
  -> PolishSmallClusters
       -> merge polish
       -> boundary load polish
  -> FinalizeClusters
       -> exact legality evaluation
       -> repair split if needed
```

### 1. Recursive Spatial Bisection

This is the base clustering step. It recursively partitions the sink set along the current bbox longest axis.

Current behavior:

- Resolve an internal packing fanout limit from `config.max_fanout`, or `kDefaultPackingFanout=32` when fanout is unconstrained.
- For every recursive node, compute the target child cluster count from `ceil(entry_count / fanout_limit)`.
- Sort entries by the current longest axis.
- Search split candidates around the ideal split position within `kSplitCandidateWindow=4`.
- Score each split using:
  - geometric compactness through bbox diameter,
  - routing-cap proxy balance across the child subtree.

Motivation:

- Keep the algorithm spatial and deterministic.
- Avoid expensive exact routing during clustering.
- Prefer cuts at spatial gaps rather than blindly packing by count.
- Produce clusters that follow the physical sink distribution.

Important detail:

Pure size-only recursive splitting would produce almost ideal packing. For the huge case:

| F | Ideal size-only cluster count | Avg fanout | Shape |
| ---: | ---: | ---: | --- |
| 4 | 32374 | 3.9999 | almost all fanout 4 |
| 8 | 16187 | 7.9999 | almost all fanout 8 |
| 16 | 8094 | 15.9988 | almost all fanout 16 |
| 32 | 4047 | 31.9975 | almost all fanout 32 |
| 64 | 2024 | 63.9792 | almost all fanout 64 |

Therefore the observed low utilization is not caused by recursive bisection alone. It is caused by the current split score allowing local split-size deviations for compactness/proxy balance without any explicit occupancy/utilization cost.

### 2. Merge Polish

`PolishSmallClusters()` runs two fixed merge rounds.

Current behavior:

- Process active clusters in ascending size order.
- For each source cluster, find nearest active neighbors.
- Try merging with a legal neighbor.
- If either side is singleton, merge can be forced when legal.
- Otherwise merge only if pair objective improves.

Motivation:

- Repair recursive over-partitioning.
- Remove singleton or very small clusters.
- Recover some utilization without turning the base algorithm into full agglomerative clustering.
- Keep the number of merge rounds bounded to protect runtime.

Measured behavior on the huge case:

| Round | Runtime | Active Before | Active After | Merges |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 76.333s | 45316 | 44980 | 336 |
| 1 | 74.567s | 44980 | 44962 | 18 |

This shows the step is extremely expensive for very small benefit on this input.

### 3. Boundary Load Polish

`PolishBoundaryLoads()` runs two fixed rounds after merge.

Current behavior:

- Sort clusters by descending routing-cap proxy.
- For each cap-heavy source cluster, compute the current global mean proxy.
- Find nearest active neighbors.
- Keep only light targets below the proxy mean.
- Choose boundary entry candidates:
  - closest to target bbox,
  - farther from source root when tied.
- Move one entry if the source-target pair objective improves.

Motivation:

- Reduce routing-cap proxy variance across clusters.
- Smooth artifacts introduced by longest-axis cuts.
- Move only boundary sinks to preserve locality and avoid global reshuffling.
- Improve downstream H-tree load balance and local cluster wire/cap distribution.

Measured behavior on the huge case:

| Round | Runtime | Considered Sources | Moved Loads |
| ---: | ---: | ---: | ---: |
| 0 | 42.936s | 44962 | 11850 |
| 1 | 38.825s | 44962 | 3710 |

This step changes load assignment but does not reduce cluster count. It should not be used to fix packing/utilization.

### 4. Finalize and Repair

`FinalizeClusters()` materializes `Pin*` clusters and runs final legality evaluation.

Current behavior:

- Sort pins in each materialized cluster by name for deterministic output.
- Evaluate fanout, diameter, pin-cap lower bound, and exact cap/routing when enabled.
- If a non-singleton cluster fails, split it by longest axis and evaluate recursively.
- If a singleton still fails, return failure.
- Check assigned load count equals input entry count.

Motivation:

- Keep draft-phase heuristics cheap.
- Preserve correctness with final exact/legal evaluation.
- Localize repair to failed clusters rather than using exact routing during every split/merge candidate.

Measured behavior on the huge case:

```text
finalize = 1.722s
clusters = 44962
assigned_loads = 129494
fanout_histogram = 2=13412,3=23530,4=8020
```

Finalize did not split the huge case further, so it is not the source of low fanout.

## Refined Diagnosis

### Quality Problem

The low average fanout is created before polish:

```text
recursive partition:
  drafts=45316
  avg_fanout=2.858
  histogram=2=17378,3=17014,4=10924

after polish/finalize:
  clusters=44962
  avg_fanout=2.880
  histogram=2=13412,3=23530,4=8020
```

The partition score optimizes compactness and routing-cap proxy balance, but does not price wasted fanout capacity. With `F=4`, a fixed `±4` split window is too large relative to the allowed cluster size. It can shift a recursive split by an entire cluster worth of loads, and those local shifts compound down the recursion tree.

For `F=32+`, the same `±4` window is only a small relative deviation, so the historical large-fanout use case can benefit from spatial-gap-aware splitting without losing much utilization. This explains why an optimization that only targets `F=4` would be risky: the old behavior was likely tuned for large-fanout configs.

### Runtime Problem

The runtime bottleneck is polish:

```text
FastClustering total = 234.798s
partition = 0.411s
polish = 232.662s
finalize = 1.722s
```

The polish implementation does not match its local-repair intent:

- It needs a small number of local neighbors.
- It instead scans all active clusters.
- It sorts all active candidates.
- It repeats this for nearly every cluster and for multiple rounds.

This is an algorithmic complexity issue exposed by implementation:

```text
current neighbor search ~= O(k^2 log k)
desired local repair ~= O(k log k + k * candidate_count)
```

## Optimization Principles

The fix must generalize across `max_fanout=4`, `8`, `16`, `32`, and larger.

### Preserve Hard Constraints

- Every valid load assigned exactly once.
- `cluster.size() <= max_fanout` when fanout is configured.
- diameter legal.
- cap legal.
- exact routing legal when requested.
- deterministic output.

### Keep Soft Objectives Explicit

The current soft objectives should remain, but their costs should be made explicit and normalized:

- compactness: low bbox diameter / wire proxy,
- utilization: low wasted fanout capacity,
- cap/proxy balance: low variance across clusters,
- locality: only move/merge near spatial neighbors,
- runtime: avoid global scans in local repairs.

### Scale by Fanout

Any new packing term must be normalized by `F`.

For small `F`, wasting one slot is expensive.
For large `F`, moving a few loads to exploit a physical gap can be valid and should remain allowed.

## Proposed Design

### Phase A: Keep Diagnostics and Build a Fanout Sweep Benchmark

Before optimizing behavior, keep the internal phase timing and fanout histogram logs.

Add focused synthetic/benchmark cases sweeping:

```text
F = 4, 8, 16, 32, 64
```

Track:

- cluster count,
- average fanout,
- fanout histogram,
- singleton count,
- max diameter,
- routing-cap proxy mean/stddev,
- total score,
- runtime split by partition/merge/boundary/finalize,
- final legality.

This prevents a fix that only helps `F=4` but hurts `F=32+`.

### Phase B: Replace Full Sort Neighbor Search

Current:

```text
SelectNearestActiveNeighbors(cluster_id):
  scan all active clusters
  sort all candidates by bbox distance
  take first K
```

Better first step:

```text
scan all active clusters
maintain bounded max-heap or partial top-K
do not sort all candidates
```

This keeps behavior nearly identical but changes complexity from:

```text
O(k log k) per query
```

to:

```text
O(k log K) per query
```

It is still all-scan, but much cheaper and low risk.

Better second step:

Build a spatial neighbor graph once per polish round:

- Use cluster centers or bbox centers.
- Bucket clusters into a uniform grid sized from local cluster span or target cluster pitch.
- For each cluster, search own and nearby buckets until K neighbor candidates are found.
- Fallback to bounded all-scan if the bucket neighborhood is sparse.
- Store symmetric or directed K-nearest lists for the round.

Expected complexity:

```text
O(k log k) or O(k) to build/index
O(k * K) to use candidate graph
```

This matches the goal: merge/boundary polish are local operations.

### Phase C: Avoid Recomputing Aggregate Per Source

Current boundary loop computes `CalcDraftAggregate(clusters)` inside each source iteration.

Better:

- Compute aggregate once at round start.
- When a boundary move occurs, update total proxy incrementally:

```text
total_proxy += source_after.proxy + target_after.proxy - source_before.proxy - target_before.proxy
```

- Active count does not change during boundary moves.
- Mean proxy updates in O(1).

This preserves behavior more closely than freezing the mean for the whole round and removes an O(k^2) component.

### Phase D: Add Occupancy-Aware Split Scoring

Current split score:

```text
compactness(lhs) + compactness(rhs)
  + routing_cap_balance_weight * cap_proxy_variance
```

It should become:

```text
compactness_norm
  + cap_balance_norm
  + utilization_weight(F) * utilization_loss
```

Candidate utilization terms:

1. Leaf average size imbalance:

```text
lhs_avg = lhs_size / lhs_child_cluster_count
rhs_avg = rhs_size / rhs_child_cluster_count
target_avg = parent_size / target_cluster_count
utilization_loss = variance(lhs_avg, rhs_avg, target_avg) / F^2
```

2. Wasted slots:

```text
lhs_waste = lhs_child_cluster_count * F - lhs_size
rhs_waste = rhs_child_cluster_count * F - rhs_size
utilization_loss = (lhs_waste + rhs_waste) / (target_cluster_count * F)
```

3. Split deviation:

```text
utilization_loss = abs(split_size - ideal_split_size) / max(1, F)
```

Recommended starting point:

Use split deviation plus child-average imbalance. This is cheap and directly targets the observed failure mode.

Fanout scaling:

```text
relative_deviation = abs(split_size - ideal_split_size) / max(1, F)
```

This makes a 4-load deviation very expensive when `F=4`, but only moderate when `F=32`.

Window scaling:

Current `kSplitCandidateWindow=4` should become an effective window:

```text
effective_window = min(kSplitCandidateWindow, max(1, F / 4))
```

or use the full window but strongly penalize relative deviation.

Safer approach:

- Keep candidate window unchanged for initial compatibility.
- Add normalized utilization penalty.
- Use benchmark sweep to decide whether to also cap effective window.

### Phase E: Make Merge Polish Utilization-Aware and Selective

Current merge only forces singleton-related merges. For `F=4`, fanout-2 clusters are underfilled, but not treated specially.

Better:

Define an underfilled threshold:

```text
underfilled if size < utilization_floor(F)
utilization_floor(F) = max(1, ceil(alpha * F))
```

Use `alpha` around `0.5` or `0.66`, but evaluate by sweep.

Do not blindly force all underfilled merges. Instead:

- Try only spatial graph neighbors.
- Only merge if legal.
- Allow a merge when it improves utilization with bounded compactness/cap degradation.
- Stop when cluster count or low-utilization histogram no longer improves.

For high fanout:

- `ceil(0.5 * 32)=16`, so this targets genuinely small clusters only.
- It should not aggressively merge reasonably packed 24/32 clusters.

### Phase F: Limit Boundary Polish to Its Real Goal

Boundary polish should not scan every cluster when many are not cap-heavy.

Better:

- Compute mean and stddev of routing-cap proxy.
- Only consider source clusters above a threshold, for example:

```text
source.proxy > mean + beta * stddev
```

- Or cap the number of source clusters per round by percentile, e.g. top 20%-30% heavy clusters.
- Use spatial neighbor graph and only consider light local targets.
- Preserve a minimum source fanout after move, preferably avoid making clusters underfilled unless cap balance improvement is significant.

For high fanout, this retains the original purpose: balance heavy clusters. For low fanout, it prevents boundary polish from spending tens of seconds moving loads without reducing cluster count.

### Phase G: Keep Finalize as Correctness Gate

Do not move exact cap/routing into partition/merge candidate evaluation by default.

Keep:

- final legality evaluation,
- repair split for failed clusters,
- assigned-load completeness check.

Optionally add summary counters:

- clusters evaluated,
- clusters repaired,
- repair split count,
- violation type counts.

This helps distinguish future constraint-driven splits from algorithmic under-utilization.

## Recommended Implementation Order

### P1: Measurement and Low-Risk Runtime Reduction

1. Keep internal timers and histograms.
2. Replace full sort in nearest-neighbor selection with bounded top-K selection.
3. Move boundary aggregate calculation to O(1) incremental updates.
4. Validate behavior is near-identical:
   - cluster count should stay the same or very close,
   - fanout histogram should stay the same or very close,
   - runtime should improve.

This isolates runtime optimization from QoR policy changes.

### P2: Spatial Neighbor Graph

1. Build per-round spatial index over active clusters.
2. Serve merge/boundary candidate lists from the index.
3. Keep bounded all-scan fallback for sparse or degenerate layouts.
4. Validate against F sweep and huge case.

This should be the main runtime fix.

### P3: Occupancy-Aware Partition Score

1. Add normalized split utilization penalty.
2. Keep compactness and cap balance terms.
3. Sweep F values and inspect:
   - average fanout,
   - diameter/compactness,
   - proxy variance,
   - downstream HTree runtime and QoR.

Expected result:

- F=4: average fanout moves much closer to 4 if constraints allow.
- F=32+: average fanout remains near 32, while spatial gap behavior remains available.

### P4: Selective Merge and Boundary Polish

1. Make merge focus on genuinely underfilled clusters.
2. Make boundary focus on statistically heavy clusters.
3. Add round-level early-stop based on actual improvement:
   - cluster count reduction,
   - underfilled count reduction,
   - proxy variance reduction.

This prevents expensive no-op rounds like the measured second merge round:

```text
74.567s for only 18 merges
```

## Validation Matrix

For every behavioral change, run:

| Case | Metrics |
| --- | --- |
| synthetic clustered points | completeness, legality, deterministic facade behavior |
| realtech benchmark | runtime, score, proxy variance, singleton count |
| huge `design_clock.def`, F=4 | cluster count, avg fanout, histogram, CTS runtime |
| huge config sweep F=8/16/32/64 if feasible | generality and regression protection |

For huge CTS, compare:

- FastClustering phase timing,
- `Prepare sink loads`,
- downstream HTree runtime,
- inserted buffer distribution,
- CTS total runtime,
- evaluation delay/skew/QoR where available.

## Non-Goals

- Do not remove final exact legality evaluation.
- Do not tune only for `F=4`.
- Do not replace FastClustering with full agglomerative clustering.
- Do not use exact routing inside every split/merge/boundary candidate.

