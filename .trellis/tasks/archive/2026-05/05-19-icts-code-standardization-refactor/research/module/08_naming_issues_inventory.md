# Naming Issues Inventory (CTS Semantic vs. Internet-Style)

- **Query**: 命名规范化建议、互联网化命名扫描
- **Scope**: internal
- **Date**: 2026-05-19

## Method

Searched `module/` for: `Internal`, `Support`, `Helper`, `Util`, `Manager`,
`Handler`, `Snapshot`, `Request`, `Response`, `Builder`, `Wrapper`, `Engine`,
`Frontier`, `Polish`, `Draft`, `Boundary`, `Desc`. Listed below are concrete
occurrences with file:line evidence and CTS-semantic suggestions.

## "Internal" Suffix (Generic, Should Be Anonymous Namespace)

| Path                                                                                | Symbol                                | Suggestion                                                                |
|-------------------------------------------------------------------------------------|---------------------------------------|---------------------------------------------------------------------------|
| `module/routing/bound_skew_tree/BSTRouterInternal.hh:34-36`                          | Free fns `ExportBstClockTree`, `BuildBstFromInputTopology` | Move into anonymous namespace of the consuming `.cc` files; delete the header. |
| `module/topology/fast_clustering/FastClusteringInternal.hh:37`                       | `namespace icts::fast_clustering { ... }` constants, structs, ~40 fns | Already in a sub-namespace; keep the header but rename file `FastClusteringDetail.hh` and move to a `detail/` subdir (PRIVATE include). |

Both headers are reachable via `PUBLIC` include directories — the "Internal"
marker is documentary, not enforced.

## "Helper" Bucket (Generic Container Name)

| Path                                                            | Symbol                  | Suggestion                                                               |
|-----------------------------------------------------------------|-------------------------|--------------------------------------------------------------------------|
| `module/routing/helper/` (directory)                            | —                       | Rename per content: `pin_location/` (one file) + `salt_adapter/` (one file). Or fold each into its consumer. |
| `module/routing/helper/PinLocationHelper.hh:34`                 | `CollectPinLocations` free fn | Drop "Helper" suffix: just `PinLocation.hh` with `CollectPinLocations`. |
| `module/routing/helper/SaltPinBuilder.hh:37`                    | `BuildSaltPins` free fn | Rename to `SaltAdapter.hh` (`BuildSaltPins` is the SALT input adapter).  |

## "Builder" Misuse vs. Genuine Builder Pattern

| Path / Symbol                                              | Verdict                                                          | Suggestion                                          |
|------------------------------------------------------------|------------------------------------------------------------------|-----------------------------------------------------|
| `module/characterization/CharBuilder.hh:57` `class CharBuilder` | Genuine builder (long-lived mutable state, init→build→read)   | Keep, but inconsistency: 11 .cc files use "split-file" naming `CharBuilderXxx.cc`. |
| `module/routing/helper/SaltPinBuilder.hh:37` (1 free fn)   | Not a builder; just a converter                                  | `BuildSaltPins` → can stay as free fn; rename file to `SaltAdapter.hh`. |
| `module/routing/concurrent_bst_salt/CBSRouter.hh:42` `class CustomSaltBuilder` | Not really a builder (DFS-based refiner with internal stacks) | `class BstSaltRefiner`; "Custom" reveals nothing.  |

## "Engine" Suffix (Generic Service Language)

| Path / Symbol                                                | CTS reading                  | Suggestion                                            |
|--------------------------------------------------------------|------------------------------|-------------------------------------------------------|
| `module/timing/TimingEngine.hh:30`                           | RC-tree timing propagation   | Keep (domain-correct) or `RCTreeTimingPropagator`     |
| `module/characterization/HashJoinEngine.hh:37`               | Generic DB-style hash join   | `HashJoinConcat` (already the fn name) — drop "Engine" |

## "Frontier" / "Polish" / "Draft" / "Boundary" (Generic Iteration Terms)

| Path / Symbol                                                        | Where it appears                                                                                       | CTS suggestion                                                          |
|----------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------|
| `module/characterization/Frontier.hh:42-300`                         | `StateFrontierPruner`, `BuildSegmentStateFrontier`, etc.                                               | `Frontier` → `ParetoFront`; CTS literature term                          |
| `module/topology/fast_clustering/FastClusteringPolish.cc` and 3 sibling Polish files | `PolishBoundaryLoads`, `PolishSmallClusters`, `MergeDraftsIfUseful`                            | `Polish` → `Refine`, `Rebalance`; e.g. `FastClusteringRefine.cc`         |
| `module/topology/fast_clustering/FastClusteringInternal.hh:72` `struct ClusterDraft` | Candidate cluster                                                                              | `Draft` → `Candidate`: `struct ClusterCandidate`                         |
| `module/topology/fast_clustering/FastClusteringInternal.hh:80` `struct DraftAggregate` | Aggregate stats of candidates                                                                | `DraftAggregate` → `ClusterCandidateStats` / `CandidateAggregate`        |
| `module/topology/fast_clustering/FastClusteringInternal.hh:86` `struct BoundaryMove` | A move of one entry across cluster boundary                                                      | `Boundary` reads OK in this context; could be `ClusterCrossMove`         |
| `module/topology/fast_clustering/FastClusteringBoundarySearch.cc` etc. | Boundary-entry candidate generation                                                                   | Keep "Boundary" (border between clusters); CTS-meaningful.               |

## "Topology" Term Used Both Modules

`module/topology/TopologyGen.hh:41` `class TopologyGen` — generates the abstract
clock-tree topology (full binary tree, then collapse). The "Gen" abbreviation
is unusual; CTS papers usually say "topology builder" or "topology generator".

`module/characterization/CharBuilder.hh:146` `struct TopologyDesc` — internal
plan of one buffering pattern at a wire-length grid step. This is *not* a
clock-tree topology; the same word means two different things across the
two sub-modules.

Suggestion: rename the characterization-side type to `BufferingPlan` or
`SegmentPlan` to avoid collision.

## "Wrapper" / "Manager" / "Handler" / "Snapshot" / "Request" / "Response" / "Support"

Searched all `.hh`/`.cc` under `module/`:

- **Wrapper**: only reference inside `module/` is `#include "io/Wrapper.hh"`
  in `routing/router/Router.cc:49` and
  `topology/cluster_constraints/ClusterConstraintEvaluator.cc:46`. That is
  in the **database** layer (`io::WRAPPER_INST`), not module — but the dependency
  is unavoidable since the module needs DBU access.
- **Manager**: zero hits inside `module/`.
- **Handler**: zero hits inside `module/`.
- **Snapshot**: zero hits inside `module/`.
- **Request / Response**: zero hits inside `module/`.
- **Support**: zero hits inside `module/`.

Conclusion: classic web/service language is largely absent. The main
"internet-style" smell is concentrated in:

1. `Helper` (1 dir + 2 files)
2. `Internal` (2 files)
3. `Builder` (3 classes, only 1 of them is genuinely a builder)
4. `Engine` (2 classes, 1 generic)
5. `Frontier` / `Polish` / `Draft` / "Boundary" cluster (across fast_clustering + characterization)
6. `Desc` abbreviation (`TopologyDesc`, `BuildCursor`)

## File-Naming Inconsistencies

| Pattern                                                                                              | Examples                                                                                                                                                                                            | Issue                                                                                          |
|------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------|
| "Split-file" convention: `ClassNameTopic.cc`                                                         | `CharBuilderBuild.cc`, `CharBuilderConfig.cc`, `CharBuilderSampling.cc`, …; `BoundSkewTreeFlow.cc`, `BoundSkewTreeBalance.cc`, …; `BSTRouterExport.cc`, `BSTRouterBinaryTopology.cc`; `FastClusteringPartition.cc`, `FastClusteringMergePolish.cc`, … | Each `*.cc` is *one chapter* of the same class. Reads as if files are sections of a textbook rather than self-contained units. |
| Mixed casing: directory snake_case vs. file PascalCase                                              | `bound_skew_tree/BoundSkewTree.cc`, `analytical_characterization/AnalyticalModel.cc`, `cluster_constraints/ClusterConstraintEvaluator.cc`                                                            | Consistent across the project, but worth confirming as the deliberate style.                    |
| Inconsistent class-vs-namespace grouping                                                            | `icts::bst::BoundSkewTree` (in subdir); `icts::analytical::AnalyticalCharacterization` (in subdir); `icts::fast_clustering` (only for internal helpers) — but `icts::FastClustering` for the public class. `icts::Clustering`, `icts::TopologyGen`, etc. are flat. | Mix of "domain sub-namespace" vs "flat" without rule.                                          |

## Suggested CTS-Semantic Vocabulary Map

| Web-style                | CTS-style replacement (suggestion)                                       |
|--------------------------|--------------------------------------------------------------------------|
| `Internal`               | move to `.cc` anonymous namespace or `detail/` subdir w/ PRIVATE include |
| `Helper`                 | name by content (e.g., `pin_location/`, `salt_adapter/`)                |
| `Manager`                | none in CTS — use specific role (`Driver`, `Scheduler`)                  |
| `Handler`                | not applicable                                                          |
| `Snapshot`               | `ClockTreeState`, `RoutingState`                                         |
| `Request` / `Response`   | not applicable                                                          |
| `Builder`                | retain when there is real lifecycle state; otherwise `Adapter` or fn    |
| `Wrapper`                | `Adapter`, `Bridge`, `Facade`                                            |
| `Engine`                 | `Propagator`, `Evaluator`, `Solver`                                      |
| `Frontier`               | `ParetoFront`                                                           |
| `Polish`                 | `Refine`, `Rebalance`, `LocalImprove`                                    |
| `Draft`                  | `Candidate`                                                              |
| `Desc`                   | spell out (`Plan`, `Spec`, `Definition`)                                 |

## Caveats / Not Found

- No occurrences of `Service`, `Controller`, `Repository`, `Provider`,
  `Listener`, `Observer`, `Subscriber`, `Publisher`, `Factory` — the module
  layer is **not** carrying web framework jargon.
- The CTS-domain vocabulary actually present (skew, joining, embedding,
  merge region, Steiner tree, RC tree, slew, cap, fanout, sink, terminal,
  driver, buffering pattern, characterization) is *strong*. The naming
  problems are localized to the 6 categories above.
