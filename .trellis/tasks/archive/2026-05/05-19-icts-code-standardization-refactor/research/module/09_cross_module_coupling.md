# Cross-Module Coupling & Boundary Issues

- **Query**: 模块边界、内部耦合、flow vs module、重复实现
- **Scope**: internal
- **Date**: 2026-05-19

## Cross-Sub-Module Includes Inside `module/`

| Where (`module/<sub>/...`)                                                                   | What it includes                                                | Direction                                       |
|----------------------------------------------------------------------------------------------|-----------------------------------------------------------------|-------------------------------------------------|
| `routing/router/Router.cc:45`                                                                | `bound_skew_tree/BSTRouter.hh`                                  | router → bst (parent → child inside routing)     |
| `routing/router/Router.cc:46`                                                                | `concurrent_bst_salt/CBSRouter.hh`                              | router → cbs                                    |
| `routing/router/Router.cc:47`                                                                | `flute/FLUTERouter.hh`                                          | router → flute                                  |
| `routing/router/Router.cc:51`                                                                | `salt/SALTRouter.hh`                                            | router → salt                                   |
| `routing/router/Router.cc:50`                                                                | `local_legalization/LocalLegalization.hh`                       | router → local_legalization                     |
| `routing/router/Router.cc:40`                                                                | `PinLocationHelper.hh`                                          | router → helper                                  |
| `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc:42`                       | `TimingEngine.hh` (from timing/)                                | **topology → timing**                            |
| `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc:45`                       | `bound_skew_tree/BSTTypes.hh` (from routing/)                   | **topology → routing/bound_skew_tree**           |
| `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc:47`                       | `local_legalization/LocalLegalization.hh` (from routing/)        | **topology → routing/local_legalization**        |
| `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc:48`                       | `router/Router.hh` (from routing/)                              | **topology → routing/router**                    |
| `module/topology/cluster_constraints/ClusterConstraintEvaluator.cc:38`                       | `PinLocationHelper.hh` (from routing/)                          | **topology → routing/helper**                    |
| `module/analytical_characterization/AnalyticalCharacterization.cc:37`                        | `CharBuilder.hh` (from characterization/)                       | analytical_char → characterization               |

### Graph (high level)

```
        +-------------------+
        |  analytical_char  |
        +---------+---------+
                  | (depends on)
                  v
        +---------+---------+
        |  characterization |
        +-------------------+

        +-------------------+
        |     topology      |---+
        |   (cluster_       |   | (depends on)
        |    constraints)   |   |
        +---------+---------+   |
                  |             v
                  +----> routing (router, bst, local_leg, helper)
                  |
                  v
                timing
```

`topology/cluster_constraints/` is the **only** place that crosses three
sub-module boundaries at once (timing + routing + ...).

`routing/router/` correctly stays at the top of routing's internal tree and
does not depend on `topology/` or `characterization/`.

## Module ↔ Flow Boundary

Flow-layer includes of module headers, from `flow/`:

```
flow/synthesis/htree/HTree.cc:39                       #include "CharBuilder.hh"
flow/synthesis/htree/HTree.cc:50                       #include "TopologyGen.hh"
flow/synthesis/htree/region/SinkLoadRegion.cc:52       #include "topology/TopologyGen.hh"
flow/synthesis/topology/sink/SinkLoadClustering.cc:37  #include "Clustering.hh"
flow/synthesis/topology/sink/SinkLoadClustering.cc:45  #include "topology/TopologyGen.hh"
flow/synthesis/topology/trunk/SourceTrunkSegment.cc:39 #include "CharBuilder.hh"
flow/synthesis/htree/characterization/Characterization.cc:35  #include "CharBuilder.hh"
flow/synthesis/htree/characterization/library/CharacterizationLibrary.cc:30  #include "CharBuilder.hh"
flow/synthesis/htree/analytical_solver/AnalyticalSelection.cc:40-42  #include "AnalyticalCharacterization.hh", "AnalyticalModel.hh", "CharBuilder.hh"
flow/synthesis/htree/analytical_solver/AnalyticalSolverRequest.cc:33   #include "AnalyticalModel.hh"
flow/synthesis/htree/analytical_solver/AnalyticalCandidate.hh:34       #include "analytical_characterization/AnalyticalModel.hh"
flow/synthesis/htree/analytical_solver/AnalyticalCandidate.cc:34
flow/synthesis/htree/analytical_solver/AnalyticalSolverModel.cc:35
flow/synthesis/htree/analytical_solver/AnalyticalSolverCandidateBuild.cc:32
flow/synthesis/htree/analytical_solver/AnalyticalSolverShortlist.cc:32
flow/synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh:43

flow/synthesis/htree/constraint/Constraint.cc:28       #include "CharBuilder.hh"
flow/synthesis/htree/compensation/RootDriverCompensationLoad.cc:56  #include "routing/router/Router.hh"
flow/synthesis/trace/layout/ClockLayoutBuilder.cc:38   #include "router/Router.hh"
flow/evaluation/qor/QorEvaluationMetrics.cc:47-48      #include "routing/router/Router.hh", "timing/TimingEngine.hh"
flow/report/visualization/drawing/Drawing.cc:47        #include "router/Router.hh"
flow/optimization/preparation/OptimizationPreparation.cc:56   #include "router/Router.hh"
flow/optimization/model/OptimizationTypes.hh:32              #include "router/Router.hh"
```

### Findings

- The flow layer **does** call module via the documented top-level headers
  (`Router.hh`, `TimingEngine.hh`, `TopologyGen.hh`, `Clustering.hh`,
  `CharBuilder.hh`, `AnalyticalCharacterization.hh`, `AnalyticalModel.hh`).
- However, flow files include them with **two different path styles**:
  - With prefix: `routing/router/Router.hh`, `topology/TopologyGen.hh`,
    `analytical_characterization/AnalyticalModel.hh`, `timing/TimingEngine.hh`.
  - Without prefix: `Router.hh`, `Clustering.hh`, `CharBuilder.hh`,
    `TopologyGen.hh`, `AnalyticalModel.hh`.

  Both work because `${ICTS_MODULE}` is on the PUBLIC include path **and**
  each sub-module also exposes its own dir. The mixed style is a smell, not
  an error.
- The flow layer does **not** dip into `bound_skew_tree/Components.hh`,
  `GeomCalc.hh`, `BoundSkewTree.hh`, `FastClusteringInternal.hh`, `Frontier.hh`,
  `HashJoinEngine.hh`, etc. So the public surface of the module layer to flow
  is much narrower than the sub-module exposure suggests — the internals are
  *de facto* private but not enforced.

## Duplicate / Overlapping Concepts

| Concept                                       | Where (multiple)                                                                                          |
|-----------------------------------------------|-----------------------------------------------------------------------------------------------------------|
| 2-D point                                     | `icts::Point<T>` (utils template), `icts::bst::Point` (`Components.hh:68`, double precision + delay payload). `bst::Point` shadows the global one inside `bound_skew_tree/`. |
| Bounding box                                  | `icts::bst::BoundingBox` (`GeomCalc.hh:58`); `icts::fast_clustering::Bounds` (`FastClusteringInternal.hh:64`) — same idea, different fields. |
| Manhattan distance                            | `geometry::Manhattan` (utils); also `bst::Geom::distance` (`GeomCalc.hh:81`); also `icts::fast_clustering::CalcManhattanDistance` (`FastClusteringInternal.hh:107`). Three implementations. |
| K-means                                       | Template `icts::KMeans<>` (`topology/kmeans/KMeans.hh:37`); reused by `topology/clustering/Clustering.cc` and by `bst::BoundSkewTree::kMeansPlus` (`BoundSkewTreeTopology.cc:405`) — the second one is a private re-implementation that **doesn't reuse the template**. |
| Cluster evaluation result                     | `topology/clustering/Clustering.hh:37-66` (`ClusterElectricalSummary`, `ClusterElectricalViolation`, `ClusterElectricalEvaluation`) vs `topology/cluster_constraints/ClusterConstraintTypes.hh:33-71` (`ElectricalEstimate`, `ConstraintViolation`, `ConstraintEvaluation`). 1-to-1 renamed structs at different layers. |
| Free fns vs static-class wrappers             | `Clustering::fastClustering()` is a forwarder to `FastClustering::run()`, which is also wrapped by `TopologyGen::fastClustering()`. Three-level forwarding. |
| "Topology" with two meanings                  | `module/topology/` (clock-tree topology); `characterization/CharBuilder` `TopologyDesc` (buffering plan). Same word; different semantic. |
| Steiner-tree node                             | `database` `ClockSteinerTree::NodeType`; `bst::Area` (`Components.hh:126`); `BinaryTopologyNode` (anonymous in `bound_skew_tree/BSTRouterBinaryTopology.cc:61`). Each conversion is hand-rolled. |

## Algorithm Cores That Should Be Reusable but Aren't

- **K-means**: `topology/kmeans/KMeans.hh` is a clean reusable template. But
  `bst::BoundSkewTree::kMeansPlus` (`BoundSkewTreeTopology.cc:405-430`)
  duplicates the algorithm directly instead of instantiating
  `KMeans<bst::Area*>`.
- **MCF**: `topology/mcf/MinCostFlow.hh` is reusable, currently only called
  from `topology/clustering/Clustering.cc` (bi-partition path). No other
  sub-module uses it (correct, no false dup).
- **Manhattan distance**: 3 copies (see table above).
- **Buffering pattern combination**: `HashJoinEngine` (in
  `characterization/`) is a generic relational-style join that could live
  in `utils/`.

## Error Handling, Logging, Parameter Passing

- All sub-modules use `glog`/`Log.hh` macros (`LOG_FATAL`, `LOG_FATAL_IF`,
  `LOG_WARNING`). Consistent.
- Parameter-passing style is consistent: each public class takes a strongly
  typed `Options` / `Parameters` struct (`Router::RCTreeBuildOptions`,
  `BSTParameters`, `LocalLegalization::Options`, `Clustering::ClusterConfig`,
  `TopologyGen::BuildOptions`, `CharBuilder::InitOptions`,
  `AnalyticalCharacterizationOptions`).
- However, the `Options` structs are **defined inside each public class**
  (nested types). There is no shared "common options" base. Each sub-module
  redeclares `routing_layer`, `wire_width`, `dbu_per_um` independently.

## Build-Order / Layering Constraints

- `cluster_constraints/CMakeLists.txt` links `icts_source_module_routing`,
  `icts_source_module_routing_helper`, `icts_source_module_timing`. This
  declares the cross-sub-module dependency cleanly at the CMake level.
- The `module/CMakeLists.txt:13-23` aggregate INTERFACE link order is
  manually maintained. Since topology depends on routing+timing, and
  characterization+analytical do not depend on topology, the **transitive**
  dependency graph is acyclic.

## Caveats / Not Found

- No subsystem dispatches by an enum that maps to a sub-module (no
  `RouterKind::kFlute → FluteRouter::buildTree` table). Dispatch is hand-coded
  inside `Router::buildClockNetTree` (which only forwards to `buildFluteTree`).
- The relationship between `ClusterConfig::router_kind` (enum
  `kFlute / kSalt / kBst / kCbs` in `TopologyConfig.hh:42`) and the actual
  router classes is implicit, encoded inside
  `ClusterConstraintEvaluator.cc` (string-style switches), not in
  `Router.hh`.
