# CTS H-tree Topology Tolerance Code Analysis

## Current Flow

1. `HTreeBuilder::build(loads)` creates topology with `TopologyGen::build(loads)`.
2. `TopologyGen::build`:
   - creates a full binary tree by target/deepest leaf count;
   - embeds nodes from recursive bi-partition cluster centers;
   - calls `balanceTopology` to regularize each level;
   - reports root-to-leaf path lengths.
3. `balanceTopology` currently computes one average parent-child Manhattan distance per level, then projects every child to that exact L1 radius from its parent.
4. `BuildLevelPlans` recomputes one average requested segment length per level, maps it to `aligned_length_idx`, and the H-tree char/selection pipeline uses that length bin per level.
5. `MaterializeCTSObjects` places buffer instances using selected normalized buffer positions along the actual topology edge.

## Problem Location

`balanceTopology` is the main source of the reported issue. It turns clustered child positions into exact per-level baseline-radius points. This can move a leaf away from the center of its assigned loads even if the original clustered position was a better physical match.

## Recommended Change

Add tolerant balancing: keep an edge if its Manhattan length already falls within the level tolerance window; only project edges outside the window to the nearest boundary. With default `0.15`, a baseline of 1000 DBU permits 850..1150 DBU.

## Files Likely Touched

- `src/operation/iCTS/source/database/config/Config.hh`
- `src/operation/iCTS/source/database/config/Config.cc`
- `src/operation/iCTS/source/module/topology/TopologyGen.cc`
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisTest.cc`
- `src/operation/iCTS/test/module/topology/topology_gen/TopologyGenTest.cc` or a focused new topology-gen test file

## Validation Ideas

- Config default/parse/report test.
- Synthetic topology test with intentionally uneven clustered child distances:
  - set tolerance to `0.0`, assert level distances collapse near baseline;
  - set tolerance to a wider value, assert in-window edge locations are preserved and out-of-window edge distances are clamped to bounds.
