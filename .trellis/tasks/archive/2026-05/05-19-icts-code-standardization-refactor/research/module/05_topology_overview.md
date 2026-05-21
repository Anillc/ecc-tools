# Sub-Module: `topology/`

- **Query**: topology 子模块结构、职责、命名、内部组件
- **Scope**: internal
- **Date**: 2026-05-19

## Directory Layout

`module/topology/` has 6 sub-directories. Top-level files: `TopologyGen.hh`,
`TopologyGen.cc`. CMake target `icts_source_module_topology` is the
aggregate facade.

```
topology/
├── TopologyGen.hh / .cc        (top-level facade, builds `Tree`)
├── config/                     (1 header `TopologyConfig.hh`, INTERFACE target)
├── clustering/                 (Clustering.hh / .cc)
├── cluster_constraints/        (ClusterConstraintEvaluator.hh + ClusterConstraintTypes.hh + .cc)
├── fast_clustering/            (FastClustering.hh + FastClusteringInternal.hh + 10 .cc files + 1 .md)
├── kmeans/                     (header-only template KMeans.hh)
└── mcf/                        (header-only template MinCostFlow.hh, lemon-based)
```

## Sub-module Roles in CTS Flow

| Sub-dir                  | CTS role                                                                                                    |
|--------------------------|-------------------------------------------------------------------------------------------------------------|
| top-level `TopologyGen`  | Build the abstract topology (`Tree`) for an H-tree-like clock tree: depth selection, full binary template, position embedding, balance check. Facade `TopologyGen.hh:41`. |
| `clustering/Clustering`  | Cluster a set of `Pin*` loads with one of two modes: bi-partition (recursive k-means + MCF) or fast clustering. Facade `clustering/Clustering.hh:73`. |
| `cluster_constraints/`   | Evaluate whether a candidate cluster respects fanout / diameter / capacitance limits, and (optionally) build a routed RC-tree for exact cap. Class `ClusterConstraintEvaluator` (`cluster_constraints/ClusterConstraintEvaluator.hh:39`). |
| `fast_clustering/`       | High-performance spatial clustering (boundary search/polish, merge polish, partition, finalize). Facade `fast_clustering/FastClustering.hh:35`. |
| `kmeans/`                | Generic K-means++ template (`topology/kmeans/KMeans.hh:37`). Header-only.                                   |
| `mcf/`                   | Generic Min-Cost-Flow assignment using lemon (`topology/mcf/MinCostFlow.hh:47`). Header-only template.       |
| `config/TopologyConfig`  | Shared configuration structs (`BiPartitionConfig`, `ClusterConfig`) consumed by every sub-target.            |

## Public Entries

| Header                                          | Class / Struct                                                                                            |
|-------------------------------------------------|-----------------------------------------------------------------------------------------------------------|
| `topology/TopologyGen.hh:41`                    | `class TopologyGen` (static), with nested `enum LoadCountKind`, `struct BuildOptions`, `struct BuildCursor`. |
| `topology/config/TopologyConfig.hh:31/42/50/56/62` | `struct BiPartitionConfig`, `enum ClusterRouterKind`, `enum ClusterRootPolicy`, `enum ClusterScoringStrategy`, `struct ClusterConfig`. |
| `topology/clustering/Clustering.hh:37/49/59/66/73` | `struct ClusterElectricalSummary`, `enum ClusterElectricalViolation`, `struct ClusterElectricalEvaluation`, `struct ClusterResult`, `class Clustering` (static). |
| `topology/cluster_constraints/ClusterConstraintTypes.hh:33/46/56/66` | `struct ElectricalEstimate`, `struct ClusterConstraintMetrics`, `enum ConstraintViolation`, `struct ConstraintEvaluation`, `IsFiniteCapLimit()` free fn. |
| `topology/cluster_constraints/ClusterConstraintEvaluator.hh:39` | `class ClusterConstraintEvaluator` (instance class, mutable `_pin_cap_cache`). |
| `topology/fast_clustering/FastClustering.hh:35` | `class FastClustering` (static).                                                                          |
| `topology/fast_clustering/FastClusteringInternal.hh:37` | `namespace icts::fast_clustering` with `LoadEntry`, `Bounds`, `ClusterDraft`, `DraftAggregate`, `BoundaryMove`, `NeighborGraph` + ~40 free helper functions. |
| `topology/kmeans/KMeans.hh:37`                  | `template <typename Value> class KMeans` (header-only).                                                   |
| `topology/mcf/MinCostFlow.hh:47`                | `template <typename Value> class MinCostFlow` (header-only).                                              |

## Internal Coupling Inside `topology/`

```
TopologyGen.cc:44  #include "clustering/Clustering.hh"
TopologyGen.cc:45  #include "config/TopologyConfig.hh"
TopologyGen.cc:46  #include "fast_clustering/FastClustering.hh"

clustering/Clustering.cc:33                          #include "ClusterConstraintEvaluator.hh"
fast_clustering/FastClusteringFinalize.cc:35-37      #include "ClusterConstraintEvaluator.hh",
                                                         "ClusterConstraintTypes.hh", "Clustering.hh"
cluster_constraints/ClusterConstraintEvaluator.cc:42 #include "TimingEngine.hh"  (module/timing)
cluster_constraints/ClusterConstraintEvaluator.cc:45 #include "bound_skew_tree/BSTTypes.hh"  (module/routing/bound_skew_tree)
cluster_constraints/ClusterConstraintEvaluator.cc:47 #include "local_legalization/LocalLegalization.hh"  (module/routing/local_legalization)
cluster_constraints/ClusterConstraintEvaluator.cc:48 #include "router/Router.hh"  (module/routing/router)
```

`topology/cluster_constraints/` cross-depends on **3 other module-level
sub-modules** (`timing/`, `routing/bound_skew_tree/`,
`routing/local_legalization/`, `routing/router/`). This makes `topology/`
a meta-package that pulls in routing and timing.

Looking at `cluster_constraints/CMakeLists.txt:7-20`, the `target_link_libraries`
declares these dependencies explicitly:

```
icts_source_module_routing
icts_source_module_routing_helper
icts_source_module_timing
```

## Duplicate Cluster Result Types

The same conceptual data structure exists in two parallel forms:

| In `topology/clustering/Clustering.hh`            | In `topology/cluster_constraints/ClusterConstraintTypes.hh` |
|---------------------------------------------------|-------------------------------------------------------------|
| `struct ClusterElectricalSummary` (8 fields)      | `struct ElectricalEstimate` (8 fields, similar)             |
| `enum class ClusterElectricalViolation`           | `enum class ConstraintViolation`                            |
| `struct ClusterElectricalEvaluation`              | `struct ConstraintEvaluation`                               |

These are 1-to-1 mappings with renamed members; conversion happens inside
`Clustering.cc`. The duplication exists because `cluster_constraints/` is the
lower-level evaluator and `clustering/` re-exports a higher-level view.

## `fast_clustering/` Structure

`fast_clustering/FastClustering.hh:35` exposes a 3-method static class
(`run / runDefault / buildElectricalBaseConfig`). The bulk of logic lives in
`FastClusteringInternal.hh:37` (`namespace icts::fast_clustering`) and **10**
.cc files:

```
FastClustering.cc                  ← top wrapper
FastClusteringPartition.cc         ← initial partition (split)
FastClusteringFinalize.cc          ← cluster finalization
FastClusteringGeometry.cc          ← bounds / distance helpers
FastClusteringPolish.cc            ← split-polish loop
FastClusteringPolishShared.cc      ← shared polish utilities (most lines, 12949)
FastClusteringMergePolish.cc       ← merge-polish loop
FastClusteringBoundaryCandidates.cc ← boundary entry candidates
FastClusteringBoundarySearch.cc    ← boundary-move search
FastClusteringBoundaryPolish.cc    ← boundary polish loop
```

Plus `FastClusteringAlgorithm.md` (algorithm doc, 10160 bytes).

Like `BoundSkewTree`, this is **one logical algorithm split across many .cc
files** sharing the internal header `FastClusteringInternal.hh`.

`FastClusteringInternal.hh` is a 153-line file in `namespace icts::fast_clustering`
exposing constants, structs (`LoadEntry`, `Bounds`, `ClusterDraft`,
`DraftAggregate`, `BoundaryMove`, `NeighborGraph`) and ~40 helper functions —
mostly stateless. The `target_include_directories` is `PUBLIC` on the
fast_clustering directory, so the "Internal" naming is documentary, not
enforced.

## `TopologyGen` Surface

`TopologyGen.hh:41-93` declares the class with:

- 3 overloads of `static auto build(...)` returning `Tree`.
- 3 static cluster wrappers (`fastClustering / defaultFastClustering /
  buildFastClusteringElectricalConfig`) — pure pass-through to `Clustering`.
- Private helpers (`reportLoadDistribution`, `reportRootToLeafLengths`,
  `calcMaxDepth`, `calcLeafCount`, `buildFullTree`, `embedPositions`,
  `balanceTopology`, `BuildCursor` struct).
- Nested `enum class LoadCountKind { kSink, kLocalBuffer }`.
- Nested `struct BuildOptions` (8 fields).

The cluster wrappers (`fastClustering`, `defaultFastClustering`,
`buildFastClusteringElectricalConfig`) duplicate the public surface of
`Clustering` — they are simple forwarders.

## Naming Issues in `topology/`

| Element                                                                  | Issue                                                                                   |
|--------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| `TopologyGen` (`TopologyGen.hh`)                                         | "Gen" abbreviation; CTS-semantic name might be `ClockTopology`, `HTreeTopologyBuilder`  |
| `FastClusteringInternal.hh`                                              | "Internal" suffix but header is `PUBLIC` per CMake                                       |
| `FastClusteringPolish.cc`, `MergePolish.cc`, `BoundaryPolish.cc`, `PolishShared.cc` | "Polish" is generic; CTS-semantic might be `Refine`, `Rebalance`, `LocalImprove`     |
| `BoundaryCandidates.cc`, `BoundarySearch.cc`                             | "Boundary" is OK; reads as `Frontier` in iterative-improvement literature                |
| `ClusterDraft`, `DraftAggregate`                                         | "Draft" is generic; could be `CandidateCluster`, `ClusterCandidate`                      |
| `BuildCursor` (in `TopologyGen.hh`)                                      | Web/database term; CTS would use `BuildContext`, `BuildState`                            |
| `LoadCountKind`, `ClusterRouterKind`, `ClusterRootPolicy`                | OK ("Kind" / "Policy" are widely accepted)                                               |
| `topology/config/` containing one file `TopologyConfig.hh`               | Sub-directory for one header is heavyweight; CTS would inline as `TopologyTypes.hh`     |

## Code Duplication / Similar Functionality

- `Clustering::fastClustering` is a pure forwarder to
  `FastClustering::run` (`clustering/Clustering.cc:?`), and
  `TopologyGen::fastClustering` is also a pure forwarder to
  `Clustering::fastClustering`. **Three levels of indirection** for the same
  algorithm.
- `Bounds` (in `fast_clustering/FastClusteringInternal.hh:64`) duplicates a
  bounding-box concept that also exists as `BoundingBox` in
  `routing/bound_skew_tree/GeomCalc.hh:58` (different fields).
- `CalcManhattanDistance` (`fast_clustering/FastClusteringInternal.hh:107`) is
  re-implemented locally even though `utils/geometry/Geometry.hh` provides
  `geometry::Manhattan(...)`.

## Caveats / Not Found

- No `topology/` sub-module ever produces a `ClockSteinerTree`; that is the
  routing layer's deliverable. `TopologyGen::build` returns the abstract
  `Tree` (from `database/spatial`). The split between topology and routing is
  clear at the data-structure level.
- `clustering/Clustering` is a *facade* over both `FastClustering` and the
  bi-partition path (which uses `KMeans` + `MinCostFlow`). Bi-partition is
  implemented inside `clustering/Clustering.cc` directly (not in a separate
  sub-folder), so the algorithm boundary is asymmetric with the
  `fast_clustering/` sub-folder.
