# Entry-Point Style Inconsistency Across Sub-Modules

- **Query**: 每个 sub-module 对外暴露的核心入口
- **Scope**: internal
- **Date**: 2026-05-19

## Five Different Styles Coexist

Every sub-module advertises a different "shape" of public API. The inconsistency
makes the module layer feel like 5 unrelated libraries glued together.

| Sub-module                  | Top header                                                                                                              | Entry style                                                       | Instance / static               |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------|---------------------------------|
| `routing/router`            | `Router.hh` (76 lines)                                                                                                  | Static-only facade class with 7 `buildXxxTree` / `legalizeXxx`     | `static` (`Router() = delete`)  |
| `routing/bound_skew_tree`   | `BSTRouter.hh` (49 lines)                                                                                               | Static adapter with `buildTree` / `buildTreeFromTopology`          | `static` (`BSTRouter() = delete`) |
| `routing/local_legalization`| `LocalLegalization.hh` (104 lines)                                                                                      | Static facade with 4 `legalize` overloads + nested `Problem/Options/Result/CandidateSite` | `static` |
| `routing/concurrent_bst_salt`| `CBSRouter.hh` (75 lines)                                                                                              | Static adapter `CBSRouter::buildTree` **+** instance class `CustomSaltBuilder` (mutable, with internal state) | mixed     |
| `routing/flute`             | `FLUTERouter.hh` (46 lines)                                                                                             | Static adapter `FLUTERouter::buildTree`                            | `static`                        |
| `routing/salt`              | `SALTRouter.hh` (46 lines)                                                                                              | Static adapter `SALTRouter::buildTree`                            | `static`                        |
| `routing/helper`            | `PinLocationHelper.hh`, `SaltPinBuilder.hh`                                                                              | Free function in `namespace icts` (`CollectPinLocations`, `BuildSaltPins`) | free function           |
| `topology`                  | `TopologyGen.hh` (95 lines)                                                                                             | Static facade `TopologyGen::build` (3 overloads) + helpers         | `static` (`TopologyGen() = delete`) |
| `topology/clustering`       | `Clustering.hh` (91 lines)                                                                                              | Static facade `Clustering::biPartition / fastClustering / evaluateClusterElectrical` | `static`         |
| `topology/cluster_constraints` | `ClusterConstraintEvaluator.hh` (60 lines)                                                                            | **Instance class** with member cache `_pin_cap_cache`              | instance                        |
| `topology/fast_clustering`  | `FastClustering.hh` (47 lines)                                                                                          | Static facade `FastClustering::run / runDefault / buildElectricalBaseConfig` | `static`               |
| `topology/kmeans`           | `KMeans.hh` (151 lines)                                                                                                 | **Header-only templated class** `KMeans<Value>` with `run(...)`    | instance template               |
| `topology/mcf`              | `MinCostFlow.hh` (197 lines)                                                                                            | **Header-only templated class** `MinCostFlow<Value>` with `run(max_cluster_size)` | instance template     |
| `timing`                    | `TimingEngine.hh` (61 lines)                                                                                            | Static facade `TimingEngine::update/evaluate/calcXxx`              | `static` (`TimingEngine() = delete`) |
| `characterization`          | `CharBuilder.hh` (231 lines)                                                                                            | **Instance class** with mutable state (`init/build/get_xxx`)       | instance                        |
| `analytical_characterization` | `AnalyticalCharacterization.hh` (97 lines) + `AnalyticalModel.hh`/`AnalyticalFit.hh`                                  | Static class `AnalyticalCharacterization::buildFromCharBuilder / buildFromSegmentChars` + free function `BuildBucketCompatibleStructuralCapOperator` | mixed   |

## Observations

1. **Static facade is the dominant pattern** (10 of 15 top-level classes use
   it: `Router`, `BSTRouter`, `SALTRouter`, `FLUTERouter`, `CBSRouter`,
   `LocalLegalization`, `TopologyGen`, `Clustering`, `FastClustering`,
   `TimingEngine`, `AnalyticalCharacterization`).
2. **Instance classes** appear without obvious reason:
   - `CharBuilder` (mutable state, multi-stage `init/build/get_xxx`).
   - `ClusterConstraintEvaluator` (mutable `_pin_cap_cache`).
   - `CustomSaltBuilder` (private DFS state).
   - `KMeans<>` / `MinCostFlow<>` (template instance, justified).
3. **Free function** entry only in `helper/` (`CollectPinLocations`,
   `BuildSaltPins`).
4. **Sub-namespaces** are inconsistent:
   - `icts::bst` exists for `BoundSkewTree`/`Components`/`GeomCalc` but
     `BSTRouter`/`BSTRouterInternal` live in `namespace icts` (one level up).
   - `icts::analytical` exists for analytical_characterization.
   - `icts::fast_clustering` exists for FastClustering internal helpers.
   - All other modules sit in `namespace icts` (flat).
5. There is **no shared base class** (no `class Module { virtual run() ... }`),
   no registry / factory pattern, no common lifecycle. Each sub-module is
   independent.

## Caveats / Not Found

- Even within `routing/`, sub-modules disagree about whether the constructor
  should be `delete`d. `BSTRouter() = delete`, but its sibling instance class
  `CustomSaltBuilder` is default-constructible.
- `LocalLegalization` exposes a private nested struct `CandidateSite` in its
  public header — `CandidateSite` only contains a `Point<int>`, and is only
  referenced by private static methods; it leaks an implementation detail.
