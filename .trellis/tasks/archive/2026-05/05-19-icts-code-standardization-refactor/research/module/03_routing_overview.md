# Sub-Module: `routing/`

- **Query**: routing 子模块结构、职责、命名、内部组件
- **Scope**: internal
- **Date**: 2026-05-19

## Directory Layout

`module/routing/` contains 8 sub-directories under one aggregator
`CMakeLists.txt` (`module/routing/CMakeLists.txt:1-9` is a pure
`add_subdirectory` list).

```
routing/
├── bound_skew_tree/      (BST algorithm + adapter — largest, 22 files, 5450 lines)
├── concurrent_bst_salt/  (CBS router = BST + SALT refinement)
├── database/             (empty shell, INTERFACE only, re-exports DATABASE_ROUTING)
├── flute/                (FLUTE Steiner tree adapter, 1 .cc / 1 .hh)
├── helper/               (free-function helpers, 2 .cc / 2 .hh)
├── local_legalization/   (point legalization, 1 .cc / 1 .hh)
├── router/               (TOP facade `Router.cc`, 1 .cc / 1 .hh)
├── salt/                 (SALT adapter, 1 .cc / 1 .hh)
```

## Sub-module Roles in CTS Flow

| Sub-dir                  | Role in clock tree synthesis                                                                                            |
|--------------------------|-------------------------------------------------------------------------------------------------------------------------|
| `router/`                | Top dispatching facade: build a `ClockSteinerTree` from terminals using one of the 4 algorithms; also `buildRCTree`, `legalizePins`. Located at `routing/router/Router.hh:40`. |
| `bound_skew_tree/`       | Bounded-skew (Edahiro / DME-style) tree construction. Top entry `BSTRouter::buildTree` (`bound_skew_tree/BSTRouter.hh:35`). |
| `concurrent_bst_salt/`   | Concurrent BST+SALT (CBS) router. Re-uses BST result + custom SALT refinement (`concurrent_bst_salt/CBSRouter.hh:42`).  |
| `flute/`                 | FLUTE-based RSMT adapter. Only public `FLUTERouter::buildTree` (`flute/FLUTERouter.hh:33`).                              |
| `salt/`                  | SALT adapter for Shallow-Light tree. Only public `SALTRouter::buildTree` (`salt/SALTRouter.hh:33`).                       |
| `local_legalization/`    | Point-based pin legalization (move pins to legal sites under region + block constraints) — `LocalLegalization.hh:34`.    |
| `helper/`                | Two free-function helpers: `CollectPinLocations` (`helper/PinLocationHelper.hh:34`), `BuildSaltPins` (`helper/SaltPinBuilder.hh:37`). |
| `database/`              | Empty proxy that re-exports `database/routing/` include path; no source files.                                           |

## Internal Coupling

`module/routing/router/Router.cc` is the only place that pulls in the 4 routing
algorithms simultaneously:

```
routing/router/Router.cc:45  #include "bound_skew_tree/BSTRouter.hh"
routing/router/Router.cc:46  #include "concurrent_bst_salt/CBSRouter.hh"
routing/router/Router.cc:47  #include "flute/FLUTERouter.hh"
routing/router/Router.cc:51  #include "salt/SALTRouter.hh"
routing/router/Router.cc:50  #include "local_legalization/LocalLegalization.hh"
routing/router/Router.cc:40  #include "PinLocationHelper.hh"
```

The `Router` facade depends on every algorithm via its `.cc` (CMake target
`icts_source_module_routing` links **PRIVATE** to bst / cbs / flute / salt /
helper — see `routing/router/CMakeLists.txt:12-17`).

External (cross-module) consumer: `topology/cluster_constraints/
ClusterConstraintEvaluator.cc:48` includes `router/Router.hh`. This breaks
layering: a topology sub-module depends on the routing sub-module's facade.

## Files & Top-Level Symbols

### `routing/router/`

| File                              | Symbol                          | Notes                                                              |
|-----------------------------------|---------------------------------|--------------------------------------------------------------------|
| `routing/router/Router.hh:40`     | `class Router`                  | Static facade, 7 build methods + 2 legalize overloads + RCTreeBuildOptions |
| `routing/router/Router.cc:53-348` | implementation                  | Wraps every other sub-routing module                               |

### `routing/bound_skew_tree/`

See `04_bound_skew_tree_deep_dive.md` for a deeper look. Top-level types:

| Header                                                      | Symbol                                              |
|-------------------------------------------------------------|-----------------------------------------------------|
| `bound_skew_tree/BSTRouter.hh:35`                           | `class BSTRouter` (static adapter)                  |
| `bound_skew_tree/BSTRouterInternal.hh:34-36`                | `ExportBstClockTree`, `BuildBstFromInputTopology` (free fns) |
| `bound_skew_tree/BSTTypes.hh:33-60`                         | `enum class RCPattern`, `enum class TopoType`, `struct BSTParameters` |
| `bound_skew_tree/BoundSkewTree.hh:47`                       | `class BoundSkewTree` (algorithm core, 371 lines)  |
| `bound_skew_tree/Components.hh:68/126/222/229/271`          | 5 types: `Point`, `Area`, `Match`, `Interval`, `TransformedRect` — mixed in one header |
| `bound_skew_tree/GeomCalc.hh:31/40/49/72`                   | `enum class LineType`, `IntersectType`, `RelativeType`, `class GeomCalc` (30+ static methods) |

### `routing/concurrent_bst_salt/`

| File                              | Symbol                                       |
|-----------------------------------|----------------------------------------------|
| `concurrent_bst_salt/CBSRouter.hh:42`      | `class CustomSaltBuilder` (instance class with private DFS state) |
| `concurrent_bst_salt/CBSRouter.hh:62`      | `class CBSRouter` (static adapter)           |

The same header declares **two** unrelated classes (`CustomSaltBuilder` +
`CBSRouter`). `CustomSaltBuilder` has mutable internal state
(`_nodes`, `_shortest_latency`, `_cur_latency`, `_src`) and is used only
internally by `CBSRouter::buildTree`. It could be a private nested class or
moved into an internal header.

### `routing/flute/`

| `flute/FLUTERouter.hh:33` | `class FLUTERouter` (static adapter, 1 method) |

### `routing/salt/`

| `salt/SALTRouter.hh:33` | `class SALTRouter` (static adapter, 1 method) |

### `routing/local_legalization/`

| `local_legalization/LocalLegalization.hh:34` | `class LocalLegalization` (static facade) |

`LocalLegalization` exposes 4 `legalize` overloads, plus `enum FailurePolicy`,
nested `Options/Problem/Result` and **private** nested struct `CandidateSite`
(leaks into header).

### `routing/helper/`

| File                                    | Symbol                                                |
|-----------------------------------------|-------------------------------------------------------|
| `helper/PinLocationHelper.hh:34`        | `auto CollectPinLocations(const std::vector<Pin*>&)`  |
| `helper/SaltPinBuilder.hh:37`           | `auto BuildSaltPins(driver, loads)` (returns vector<shared_ptr<salt::Pin>>) |

These are the only two routing files using free-function style.

## Naming Issues in `routing/`

| Element                                                              | Issue                                                       |
|----------------------------------------------------------------------|-------------------------------------------------------------|
| `helper/` directory                                                  | Generic "helper" bucket, not CTS-semantic                   |
| `helper/PinLocationHelper.hh`, `PinLocationHelper.cc`                | "Helper" suffix; the actual symbol is a free fn `CollectPinLocations` |
| `helper/SaltPinBuilder.hh`, `SaltPinBuilder.cc`                      | "Builder" used for a 2-line free function (BuildSaltPins)   |
| `bound_skew_tree/BSTRouterInternal.hh`                               | "Internal" suffix in header name; should be `.cc`-private (anonymous namespace) |
| `concurrent_bst_salt/CBSRouter.hh:42` `class CustomSaltBuilder`      | "Custom*Builder" naming; would be `BstSaltRefiner` in CTS terms |
| `routing/database/`                                                  | Empty sub-dir with no source; "database" name conflicts with `iCTS/source/database/` |
| `flute`, `salt`, `cbs` (CMake target names)                          | Lowercase abbreviations, OK in CTS context but compete with sibling `bst` casing inconsistency (see CMake target `icts_source_module_routing_bst` vs `_routing_local_legalization`) |
| `concurrent_bst_salt` directory                                      | Combines two algorithm names with underscore — verbose; CBS acronym already established |

## Caveats / Not Found

- No common base class or interface for routers; each has its own
  `buildTree(...)` signature with slightly different parameter shapes
  (`(driver, loads)` for FLUTE/SALT; `(loads, parameters)` for BST/CBS;
  `buildTreeFromTopology(input_topology, parameters)` only for BST).
- `Router::buildClockNetTree(const Net&)` is the only path that takes a `Net`;
  the rest take `ClockRoutingTerminal` values. This abstraction split is
  undocumented.
