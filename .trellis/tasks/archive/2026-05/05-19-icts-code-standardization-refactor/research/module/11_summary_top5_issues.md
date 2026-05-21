# Summary: Top 5 Most Severe Issues in `module/`

- **Query**: 最严重的 5 个问题
- **Scope**: internal
- **Date**: 2026-05-19

## TL;DR

The module layer's CTS-domain vocabulary is largely correct (skew, joining,
embedding, characterization, …). The pain is structural, not naming:

1. Three mega-classes with implementations sliced across 7–11 .cc files by
   filename suffix (no abstraction).
2. Public headers leak full algorithm internals (private nested structs +
   data members + 80+ private methods).
3. Sub-module directory granularity is inconsistent (6 nested vs. 0 nested).
4. Cross-sub-module coupling breaks layering: `topology/cluster_constraints/`
   pulls in `routing/` and `timing/` to do exact-cap evaluation.
5. Generic / non-CTS naming concentrated in 6 patterns (`Internal`, `Helper`,
   `Engine`, `Frontier`, `Polish`, `Draft`/`Desc`).

## The Five Severities

### 1. Mega-class spread across many .cc files

- `class BoundSkewTree` (1 header, 7 .cc files, ~92 methods) — see
  `04_bound_skew_tree_deep_dive.md`.
- `class CharBuilder` (1 header, 11 .cc files, 26 methods) — see
  `06_characterization_and_analytical.md`.
- `class FastClustering` + `namespace icts::fast_clustering` helpers
  (1 facade header + 1 internal header + 10 .cc files) — see
  `05_topology_overview.md`.

These three are responsible for the user's "implementation scattered"
complaint. They share the pattern `ClassNameTopic.cc` (e.g.
`CharBuilderBuild.cc`, `BoundSkewTreeBalance.cc`,
`FastClusteringMergePolish.cc`) — files act as textbook chapters of one
mega-class instead of self-contained units.

### 2. Headers leak algorithm internals

- `BoundSkewTree.hh:47-369` declares 11 private nested types, ~80 private
  methods, and the full mutable state of the algorithm. Anyone including it
  pays parse cost and is locked to the internal shape.
- `CharBuilder.hh:113-228` similarly exposes 6 private nested structs,
  18 private methods, 24 private data members.
- "Internal" headers (`BSTRouterInternal.hh`, `FastClusteringInternal.hh`)
  are documentary-only — they sit under `PUBLIC` include directories per
  `CMakeLists.txt`, so any link to `_routing_bst` or `_topology_fast_clustering`
  reaches them.

### 3. Sub-module directory granularity is inconsistent

From `01_top_level_cmake_and_architecture.md`:

- `topology/` has 6 sub-dirs (`config`, `clustering`, `cluster_constraints`,
  `fast_clustering`, `kmeans`, `mcf`).
- `routing/` has 8 sub-dirs.
- `characterization/` has **0** sub-dirs (20 files at root).
- `analytical_characterization/` has **0** sub-dirs (6 files at root).
- `timing/` has **0** sub-dirs (3 files at root).

CMake granularity is equally inconsistent: `topology/` and `routing/` use
nested `add_library` per sub-dir (some INTERFACE, some plain), while
`characterization/` and `analytical_characterization/` use one library each.

Also: `analytical_characterization` is **not** linked into the
`icts_source_module` INTERFACE aggregator (`module/CMakeLists.txt:13-23`),
asymmetric with the other 4 sub-modules.

### 4. Cross-sub-module coupling that breaks layering

`module/topology/cluster_constraints/ClusterConstraintEvaluator.cc` includes:

```
"TimingEngine.hh"                       (timing/)
"bound_skew_tree/BSTTypes.hh"           (routing/bound_skew_tree)
"local_legalization/LocalLegalization.hh" (routing/local_legalization)
"router/Router.hh"                      (routing/router)
"PinLocationHelper.hh"                  (routing/helper)
```

This makes `topology/` a meta-package that pulls in routing + timing. There
is no top-level "Module" abstraction or registry to mediate this — the
cross-dependency is hand-coded.

Also: duplicate cluster-evaluation types
(`Clustering.hh::ClusterElectricalEvaluation` vs
`ClusterConstraintTypes.hh::ConstraintEvaluation`) are field-for-field
renames, with the conversion happening inside `Clustering.cc`.

### 5. Generic / non-CTS naming concentrated in 6 patterns

From `08_naming_issues_inventory.md`:

| Pattern               | Notable hits                                                                                                     |
|-----------------------|------------------------------------------------------------------------------------------------------------------|
| `Internal` suffix     | `BSTRouterInternal.hh`, `FastClusteringInternal.hh` (PUBLIC despite the name)                                     |
| `Helper` bucket       | `module/routing/helper/` dir; `PinLocationHelper`, `SaltPinBuilder` (genuine builder semantics absent in latter)  |
| `Engine` suffix       | `TimingEngine`, `HashJoinEngine` (the latter is a free fn wrapped in a struct)                                    |
| `Frontier` / `Polish` | `Frontier.hh`, `FastClusteringPolish.cc`, `FastClusteringMergePolish.cc`, `FastClusteringBoundaryPolish.cc`, `FastClusteringPolishShared.cc` |
| `Draft` / `Desc`      | `ClusterDraft`, `DraftAggregate`, `TopologyDesc`, `BuildCursor`                                                   |
| `Builder` misuse      | `SaltPinBuilder` (free fn wrapped); `CustomSaltBuilder` (DFS-based refiner, not a builder)                       |

No `Manager`, `Handler`, `Snapshot`, `Request`, `Response`, `Support`,
`Service`, `Controller` — the worst "internet" jargon is absent. The CTS
vocabulary already in use is strong; the cleanup is local.

## Other Observations Worth Noting (Not in Top 5)

- **Type duplication**: 3 implementations of Manhattan distance, 2 different
  bounding-box structs, 2 nearly-identical "Point" types (`icts::Point<T>`
  vs `icts::bst::Point`).
- **K-means redundancy**: `topology/kmeans/KMeans.hh` is a reusable template,
  but `bst::BoundSkewTree::kMeansPlus` (`BoundSkewTreeTopology.cc:405`)
  reimplements the algorithm directly instead of instantiating the template.
- **Three-level forwarding**: `TopologyGen::fastClustering` →
  `Clustering::fastClustering` → `FastClustering::run`.
- **`module/routing/database/`** is a CMake-only proxy that re-exports
  the database routing include path with zero source files.
- **`Components.hh`** name collides semantically with the global
  `icts::Point<T>` (defines a separate `bst::Point` with delay fields).

## Caveats / Not Found

- This research did **not** read every algorithm `.cc`; it only sampled
  enough of each to confirm the structural pattern. Detailed correctness
  questions about the BST / FastClustering / CharBuilder algorithms are out
  of scope for this naming/structure audit.
- No automated dependency-graph tool was used. The cross-module include
  table was built by `grep`; transitive include chains (via `Schema.hh`,
  `Net.hh`, `Pin.hh`) are not analyzed.
- Tests under `module/` were not inspected (there are none in the sub-dirs;
  the project's tests live elsewhere).
