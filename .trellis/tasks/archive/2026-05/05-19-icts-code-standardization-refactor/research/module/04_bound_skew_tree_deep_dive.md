# `routing/bound_skew_tree/` Deep Dive

- **Query**: bound_skew_tree 大模块的内部架构、文件组织、暴露/内部边界
- **Scope**: internal
- **Date**: 2026-05-19

## Scale Summary

| Metric                          | Value                                                              |
|---------------------------------|--------------------------------------------------------------------|
| Total files                     | 22 (16 .cc + 6 .hh)                                                |
| Total lines                     | ~5,450                                                              |
| Top-level classes in headers    | 9 (`Point`, `Area`, `Match`, `Interval`, `TransformedRect`, `GeomCalc`, `BoundSkewTree`, `BSTRouter`, plus free functions in `BSTRouterInternal.hh`) |
| Top-level enums                 | 5 (`RCPattern`, `TopoType`, `LineType`, `IntersectType`, `RelativeType`) |
| Top-level structs               | 3 (`BSTParameters`, `BoundingBox`, `LineDistanceResult`)            |
| Namespaces                      | `icts::bst` for algorithm; `icts` for adapter (`BSTRouter`)         |
| CMake target                    | 1 (`icts_source_module_routing_bst`), 15 sources listed inline      |

## File Inventory (sizes from `wc -l`)

```
BoundSkewTree.hh                 371   ← class declaration (mostly private)
Components.hh                    347   ← 5 types in one file
GeomCalc.hh                      155   ← static-only class with 30+ static methods

BSTRouter.hh                      49   ← public adapter
BSTRouterInternal.hh              38   ← internal free fns shared by adapter cc files
BSTTypes.hh                       62   ← enums + BSTParameters

BoundSkewTreeBalance.cc          581   ← 21 BoundSkewTree::xxx methods
BoundSkewTreeJoining.cc          517   ← 16 BoundSkewTree::xxx methods
BoundSkewTreeEmbedding.cc        439   ← 24 BoundSkewTree::xxx methods
BoundSkewTreeTopology.cc         430   ← 9  BoundSkewTree::xxx methods (biPartition, biCluster, octagon, kMeansPlus)
BoundSkewTreeFlow.cc             262   ← 8  BoundSkewTree::xxx methods (bottomUp, topDown, processBottomUpTopology…)
BoundSkewTree.cc                 205   ← constructors, run(), 9 private utility methods
BoundSkewTreeInfeasibleMerge.cc  130   ← 5  BoundSkewTree::xxx methods

BSTRouter.cc                     116   ← BSTRouter::buildTree, ::buildTreeFromTopology
BSTRouterExport.cc               156   ← icts::ExportBstClockTree (free fn used by BSTRouter)
BSTRouterBinaryTopology.cc       512   ← icts::BuildBstFromInputTopology (free fn used by BSTRouter)

Components.cc                     67   ← TransformedRect::intersect, ::check, ::correction
GeomCalc.cc                      240   ← part of GeomCalc methods
GeomCalcLine.cc                  356   ← part of GeomCalc methods (line subset)
GeomCalcPointRegion.cc           227   ← part of GeomCalc methods (point/region subset)
GeomCalcTransformedRect.cc       227   ← part of GeomCalc methods (transformed rect subset)
```

Total method definitions of `BoundSkewTree::` (one class):
**~92 methods spread across 7 .cc files**.

## Top Entries

The folder ships **two** independent public surfaces:

1. `class BSTRouter` (`BSTRouter.hh:35`) — *adapter* called by
   `module/routing/router/Router.cc`. Static, 2 methods:
   `buildTree(load_terminals, parameters)`, `buildTreeFromTopology(...)`.
   Converts `ClockRoutingTerminal` ↔ internal `bst::Area`, returns
   `ClockSteinerTree`.
2. `class BoundSkewTree` (`BoundSkewTree.hh:47`, `namespace icts::bst`) — the
   *algorithm core*. Holds all geometry/timing internal state. The adapter
   constructs it, calls `run()`, then exports the result.

`BSTRouterInternal.hh:34-36` declares two free functions
(`ExportBstClockTree`, `BuildBstFromInputTopology`) used solely by the
adapter's three .cc files.

## Inner Architecture of `BoundSkewTree`

The header `BoundSkewTree.hh:47-369` declares one giant class. All algorithm
phases are private member methods on the same class:

| Phase                                | Methods in header (sample)                                                                                                                                     | Implementation file                                  |
|--------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------|
| Flow orchestration                   | `run`, `bottomUp`, `bottomUpAllPairBased`, `bottomUpTopoBased`, `processBottomUpTopology`, `topDown`, `embedTree`, `updateEmbeddedNodeTiming`                  | `BoundSkewTreeFlow.cc`                                |
| Topology (bi-partition / bi-cluster) | `biPartition`, `buildBiPartitionTree`, `octagonDivide`, `calcOctagon`, `areaOnOctagonBound`, `biCluster`, `buildBiClusterTree`, `kMeansPlus`, `calcAreasCenter` | `BoundSkewTreeTopology.cc`                            |
| Match & merge cost                   | `getBestMatch`, `mergeCost`, `distanceCost`, `merge(Area*, Area*)`, `merge(Area*, Area*, Area*)`, `makeArea`, `areaReset`, `resetPointValues`                  | `BoundSkewTree.cc`                                    |
| Joining segment / joining region     | `initSide`, `calcJoiningSegment`, `processJoiningSegment`, `updateJoiningSegment`, `addJoiningSegmentPoints`, `calcJoiningRegion`, `addTurnPoint`, etc.        | `BoundSkewTreeJoining.cc`                             |
| Balance point                        | `calcBalancePoint`, `calcBalanceBetweenPoints`, `calcBalancePointOnLine`, `calcBalancePointOffLine`, `calcMergeDist`, `calcXBalancePosition`, `calcYBalancePosition` | `BoundSkewTreeBalance.cc`                       |
| Merging region / feasible merge      | `calcFeasibleMergeSegmentPoints`, `constructFeasibleMergeRegion`, `isJoiningRegionLine`, `addMergeRegionBetweenJoiningSegments`, `calcMergeRegionSpan`, etc.   | `BoundSkewTreeBalance.cc`                             |
| Infeasible merge                     | `constructInfeasibleMergeRegion`, `calcMinSkewSection`, `calcDetourEdgeLength`, `refineMergeRegionDelay`, `constructTransformedRectMergeRegion`               | `BoundSkewTreeInfeasibleMerge.cc`                     |
| Embedding (top-down)                 | `embedChild`, `isTransformedRectArea`, `isManhattanArea`, `mergeRegionToTransformedRect`, `calcAreaLineType`, `calcConvexHull`, `calcJoiningRegionArea`, `locateBoundarySegment`, `calcSimplePointDelays`, `calcSegmentPointDelays`, `calcPointDelays`, `pointDelayIncrease`, `calcDelayIncrease`, `pointSkew`, `getJoiningRegionLine`, etc. | `BoundSkewTreeEmbedding.cc` |

### Private nested types declared in `BoundSkewTree.hh` (lines 63-170)

The header bleeds **11** private nested types that are only used inside
`BoundSkewTree`'s .cc files:

```
KMeansConfig, MergeAreas, EmbeddingStep, BalanceRefAxis, JoiningSegmentDelayQuery,
SideDelay, BalancePointQuery, BalancePointResult, MergeDistances, MergeRegionSpan,
SideState<T>, EndState<T>, TimingState<T>, AxisDelayFactor
```

These cannot be hidden from `BoundSkewTree.hh`'s consumers because they are
referenced by private method signatures inside the class declaration. Anyone
who `#include "BoundSkewTree.hh"` pays the cost of parsing all of it.

### Private data fields declared in header (lines 345-368)

The header exposes the full mutable state of the algorithm
(`_unmerged_nodes`, `_skew_bound`, `_unit_horizontal_*`,
`_delay_quadratic_factor`, `_joining_region`, `_joining_segment`,
`_merge_segment`, `_balance_points`, `_joining_corner`,
`_feasible_merge_segment_points`). Pimpl is not used.

## `Components.hh` Type Bundling

`Components.hh:68-347` declares 5 classes in one header:

| Line | Type                | Purpose                                            |
|------|---------------------|----------------------------------------------------|
| 68   | `class Point`       | 2D point with delay/skew payload (x,y,max,min,val) |
| 126  | `class Area`        | Steiner tree node (DME-style merge region)         |
| 222  | `struct Match`      | Result of `getBestMatch`                           |
| 229  | `class Interval`    | 1-D interval used by `TransformedRect`             |
| 271  | `class TransformedRect` | 45°-rotated bbox for Manhattan geometry        |

All sit in `icts::bst` namespace. They are independent units and could be
split into 4 separate headers, plus a small `Match` struct embedded near
`BoundSkewTree`.

`Point` here **collides** semantically with `icts::Point<T>` template (from
`utils/`). This `bst::Point` is double-precision with extra delay fields,
while `icts::Point<int>` is the standard utility integer point. They are
not interchangeable but appear to overlap.

## `GeomCalc` Static-Only Class

`GeomCalc.hh:72-153` declares a class with `GeomCalc() = delete` and ~30
static methods covering: point-line distance, line intersection, transformed
rect ops, region convex hull, point sorting. Implementation is split into
**4 .cc files** by responsibility:

- `GeomCalc.cc` (240 lines) — generic helpers
- `GeomCalcLine.cc` (356 lines) — line ops
- `GeomCalcPointRegion.cc` (227 lines) — point / region ops
- `GeomCalcTransformedRect.cc` (227 lines) — transformed rect ops

This is the only place in the module layer where a static-method bag is
sliced across multiple .cc files using filename suffixes.

## Internal vs. External Boundary

Headers that **should** be exposed to outside callers (from BST):

- `BSTRouter.hh` — the public adapter
- `BSTTypes.hh` — needed because `BSTParameters` is a parameter type

Headers that look like they leak implementation detail to outside callers:

- `BoundSkewTree.hh` — full algorithm class, 371 lines, in `icts::bst` namespace
- `Components.hh` — algorithm-internal geometry/area
- `GeomCalc.hh` — algorithm-internal geometry utilities
- `BSTRouterInternal.hh` — explicitly named "Internal" but is still a `.hh`
  exposed via `target_include_directories(... PUBLIC ${ICTS_MODULE_ROUTING}/bound_skew_tree)`

The CMake `target_include_directories` (`bound_skew_tree/CMakeLists.txt:32`)
sets the entire `bound_skew_tree/` directory as **PUBLIC**, so every header
above is reachable by any consumer that links `icts_source_module_routing_bst`.

## External Consumers

```
module/routing/router/Router.cc:45                  #include "bound_skew_tree/BSTRouter.hh"     ✓
module/topology/cluster_constraints/
  ClusterConstraintEvaluator.cc:45                  #include "bound_skew_tree/BSTTypes.hh"      ✓
```

Only `BSTRouter.hh` and `BSTTypes.hh` are consumed outside `bound_skew_tree/`,
which confirms the rest could be moved into a `private/` or `internal/`
sub-folder with PRIVATE include dirs.

## Naming Issues Specific to BST

| File / Symbol                                                                | Issue                                                                                       |
|------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------|
| `BSTRouterInternal.hh`                                                       | "Internal" suffix; should be a `.cc`-private detail or in a `detail/` subdir                |
| `BSTRouterBinaryTopology.cc`                                                 | Filename mixes class name + topic; pattern reads as "split file" style                       |
| `BSTRouterExport.cc`                                                         | "Export" suffix is generic                                                                  |
| `Components.hh`                                                              | "Components" is a non-CTS umbrella term; bundles 5 types                                    |
| `bst::Point`                                                                 | Name collision with `icts::Point<T>` utility template                                       |
| `BoundSkewTreeFlow.cc`                                                       | "Flow" used loosely; the file actually contains the bottom-up / top-down driver loop        |
| `BoundSkewTreeJoining.cc` / `BoundSkewTreeInfeasibleMerge.cc`                | OK in CTS context (DME terminology), but the sibling filenames vary in style                |
| `processBottomUpTopology`, `processJoiningSegment`                           | "process" verb is generic; could be `mergeBottomUpTopology`, `extendJoiningSegment`         |
| Private struct `KMeansConfig`                                                | Redeclares cluster-count/seed/max-iter that already appear in `topology/kmeans/KMeans.hh` template |

## Recommended CTS-Semantic Replacements (suggestions only)

| Current name                           | CTS-semantic suggestion                                                |
|----------------------------------------|------------------------------------------------------------------------|
| `BSTRouterInternal.hh`                 | move free fns into `BSTRouterAdapter.cc` anonymous namespace; remove header |
| `Components.hh`                        | split into `BstNode.hh` (= `Area`), `BstPoint.hh`, `Trr.hh` (Transformed Rotated Rectangle), `Interval.hh`, `Match.hh` |
| `GeomCalc` static class                | namespace `bst::geom { ... }` with free functions; current static-method bag has no instance state |
| `BoundSkewTreeFlow.cc`                 | `BoundSkewTreeDriver.cc` or fold back into `BoundSkewTree.cc`           |
| `BoundSkewTreeInfeasibleMerge.cc`      | OK, but pair it with explicit "FeasibleMerge" file for symmetry         |
| `BoundSkewTreeJoining.cc`              | OK (DME term)                                                          |
| `BoundSkewTreeEmbedding.cc`            | OK (DME term)                                                          |
| `BoundSkewTreeTopology.cc`             | OK                                                                     |
| `BoundSkewTreeBalance.cc`              | OK                                                                     |
| `CustomSaltBuilder` (in CBSRouter.hh)  | `BstSaltRefiner` (does SALT refinement on a BST input)                  |

## Caveats / Not Found

- The class `BoundSkewTree` is essentially a **mega-class** — there is no
  state-machine / phase object split. Every nested helper struct is private,
  but they all live in the header and are visible to any reader of
  `BoundSkewTree.hh`.
- `GeomCalc` and `bst::geom` could live higher up (utils) but currently
  reside inside `bound_skew_tree/`, which makes them BST-private.
