# Implementation Scattering: One Class Across Many .cc Files

- **Query**: 大模块的声明在 .hh，实现却散落在 .hh+多个子目录 .cc 中
- **Scope**: internal
- **Date**: 2026-05-19

The user's pain point: a single class declared in one `.hh` is implemented
across many `.cc` files. Below: every concrete occurrence inside `module/`.

## Confirmed Mega-Classes With Split-File Implementations

### `icts::bst::BoundSkewTree`

- Declared at `module/routing/bound_skew_tree/BoundSkewTree.hh:47-369`.
- One class, ~92 methods (member + private static).
- Implementation distributed across **7 .cc files** (same `bound_skew_tree/`
  directory):
  - `BoundSkewTree.cc` — constructors, `run()`, match/merge primitives (9 methods)
  - `BoundSkewTreeFlow.cc` — bottom-up / top-down driver (8 methods)
  - `BoundSkewTreeTopology.cc` — biPartition / biCluster / octagon / kMeansPlus (9 methods)
  - `BoundSkewTreeJoining.cc` — joining segment + joining region (16 methods)
  - `BoundSkewTreeBalance.cc` — balance points + feasible merge (21 methods)
  - `BoundSkewTreeEmbedding.cc` — top-down embedding (24 methods)
  - `BoundSkewTreeInfeasibleMerge.cc` — infeasible-merge handling (5 methods)
- Plus the auxiliary types in `Components.hh` (Point/Area/Match/Interval/TransformedRect)
  and `GeomCalc.hh` (single static class), the latter split into:
  - `GeomCalc.cc`, `GeomCalcLine.cc`, `GeomCalcPointRegion.cc`,
    `GeomCalcTransformedRect.cc` (4 .cc files for one static class).
- Adapter `BSTRouter.hh` (2 public methods) split across:
  - `BSTRouter.cc`, `BSTRouterExport.cc`, `BSTRouterBinaryTopology.cc`.

### `icts::CharBuilder`

- Declared at `module/characterization/CharBuilder.hh:57-229`.
- One class, 26 public+private methods.
- Implementation across **11 .cc files** (same `characterization/` directory):
  - `CharBuilder.cc` (0 methods, empty TU anchor)
  - `CharBuilderBuild.cc` (1 method, plus helpers)
  - `CharBuilderCircuit.cc` (3 methods)
  - `CharBuilderConfig.cc` (7 methods)
  - `CharBuilderFeasibility.cc` (2)
  - `CharBuilderPatternEnumeration.cc` (7)
  - `CharBuilderPatternStorage.cc` (1)
  - `CharBuilderSampleStorage.cc` (1)
  - `CharBuilderSampling.cc` (1)
  - `CharBuilderSlewSampling.cc` (1)
  - `CharBuilderStaSampling.cc` (1)
  - `CharBuilderTopology.cc` (1)

### `icts::FastClustering` (algorithm core in `namespace icts::fast_clustering`)

- Public class declared at
  `module/topology/fast_clustering/FastClustering.hh:35` (3 static methods).
- Internal helpers declared at
  `module/topology/fast_clustering/FastClusteringInternal.hh:37-152`
  (~40 free fns in `namespace icts::fast_clustering`).
- Implementation across **10 .cc files**:
  - `FastClustering.cc`, `FastClusteringPartition.cc`,
    `FastClusteringFinalize.cc`, `FastClusteringGeometry.cc`,
    `FastClusteringPolish.cc`, `FastClusteringPolishShared.cc`,
    `FastClusteringMergePolish.cc`, `FastClusteringBoundaryCandidates.cc`,
    `FastClusteringBoundarySearch.cc`, `FastClusteringBoundaryPolish.cc`.

### `icts::analytical::AnalyticalCharacterization`

- Declared at
  `module/analytical_characterization/AnalyticalCharacterization.hh:82`.
- Implementation **only in `AnalyticalCharacterization.cc`** — single file,
  no split. (This is the well-organized counter-example in module/.)

## Adapters With 2–3 File Splits

| Adapter / Facade                                                 | .hh                            | .cc count |
|------------------------------------------------------------------|--------------------------------|----------:|
| `icts::Router` (routing facade)                                  | `routing/router/Router.hh`     | 1 (clean) |
| `icts::BSTRouter` (BST adapter)                                  | `bound_skew_tree/BSTRouter.hh` | 3         |
| `icts::FLUTERouter`, `icts::SALTRouter`, `icts::CBSRouter`       | 1 each                         | 1 each    |
| `icts::LocalLegalization` (point legalization)                   | `LocalLegalization.hh`         | 1         |
| `icts::TopologyGen`                                              | `TopologyGen.hh`               | 1         |
| `icts::Clustering`                                               | `Clustering.hh`                | 1         |
| `icts::ClusterConstraintEvaluator`                               | `ClusterConstraintEvaluator.hh`| 1         |
| `icts::TimingEngine`                                             | `TimingEngine.hh`              | 1         |
| `icts::analytical::AnalyticalCharacterization`                   | `AnalyticalCharacterization.hh`| 1         |
| `icts::analytical::AnalyticalModel*` types                       | `AnalyticalModel.hh`           | 1         |

So the split-file pattern is **localized to 3 mega-classes**:
`BoundSkewTree`, `CharBuilder`, `FastClustering`. The rest of the module
layer follows a clean one-class / one-file rule.

## Headers That Bundle Multiple Top-Level Types

| Header                                                                           | Top-level types in same file                                                        |
|----------------------------------------------------------------------------------|-------------------------------------------------------------------------------------|
| `module/routing/bound_skew_tree/Components.hh`                                   | `Point`, `Area`, `Match`, `Interval`, `TransformedRect` (5 types)                    |
| `module/routing/bound_skew_tree/BSTTypes.hh`                                     | `RCPattern`, `TopoType`, `BSTParameters` (3, OK)                                     |
| `module/routing/bound_skew_tree/GeomCalc.hh`                                     | `LineType`, `IntersectType`, `RelativeType`, `BoundingBox`, `LineDistanceResult`, `GeomCalc` (6) |
| `module/topology/config/TopologyConfig.hh`                                       | `BiPartitionConfig`, `ClusterRouterKind`, `ClusterRootPolicy`, `ClusterScoringStrategy`, `ClusterConfig` (5) |
| `module/topology/clustering/Clustering.hh`                                       | `ClusterElectricalSummary`, `ClusterElectricalViolation`, `ClusterElectricalEvaluation`, `ClusterResult`, `Clustering` (5) |
| `module/topology/cluster_constraints/ClusterConstraintTypes.hh`                  | `ElectricalEstimate`, `ClusterConstraintMetrics`, `ConstraintViolation`, `ConstraintEvaluation`, `IsFiniteCapLimit` (5) |
| `module/characterization/Frontier.hh`                                            | `TerminalSemantic`, `PatternCompositionState`, `SegmentFrontierStateKey`, `HTreeFrontierStateKey`, hash structs, `StateFrontierPruner`, 6 free templates (12+) |
| `module/analytical_characterization/AnalyticalModel.hh`                          | `AnalyticalMetric`, `AnalyticalModelBasis`, `AnalyticalDomain`, `AnalyticalFitQuality`, `AnalyticalSurfaceModel`, `StructuralCapOperator`, `AnalyticalModelKey`, `AnalyticalModelKeyHash`, `AnalyticalModelSet`, `AnalyticalModelCatalog` (10) |

## Headers Exposing Algorithm-Private Detail

| Header                                                            | Why it leaks                                                                                    |
|-------------------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| `module/routing/bound_skew_tree/BoundSkewTree.hh`                  | 11 private nested structs + ~80 private methods + all data members visible to every includer    |
| `module/routing/bound_skew_tree/Components.hh`                     | Pure algorithm data shapes, but PUBLIC include path                                              |
| `module/routing/bound_skew_tree/GeomCalc.hh`                       | Static-only geometry; PUBLIC include path though only BST uses it (and ClusterConstraintEvaluator includes `BSTTypes.hh` but not `GeomCalc.hh`) |
| `module/routing/bound_skew_tree/BSTRouterInternal.hh`              | "Internal" by name but PUBLIC by CMake                                                          |
| `module/topology/fast_clustering/FastClusteringInternal.hh`        | "Internal" by name but PUBLIC by CMake                                                          |
| `module/characterization/CharBuilder.hh`                           | 6 private nested structs, 18 private methods, 24 private data members                            |
| `module/characterization/Frontier.hh`                              | Template-only header; symbols are reachable to any TU that includes it                          |
| `module/characterization/HashJoinEngine.hh`                        | `namespace icts::detail`; template-only header                                                  |

## Caveats / Not Found

- No abstract base / interface class for any of the algorithm cores, so the
  scattered implementations have no shared "phase" abstraction
  (`class BoundSkewTreePhase` with virtual `run()`). Each method on
  `BoundSkewTree` directly mutates the shared private state.
- The `.cc` files do **not** use `// MARK:` / `// SECTION:` style section
  banners — splitting is achieved purely via filename suffixes.
