# Stage-2 Routing / Timing Requirements

## Purpose

This document expands the stage-2 refactor scope for the active task. `prd.md` keeps the concise task summary and links here for detailed requirements.

## Routing Module Requirements

### Core Goals
- Remove legacy routing compatibility data structures from routing core, especially legacy routing `Pin` / `Inst` / `Net` usage under `module/routing/legacy_module`.
- Use `src/operation/iCTS/source/database/design/Pin.hh` as the top-level router terminal input carrier.
- Keep algorithm cores decoupled from CTS object-graph relationships; if an algorithm needs extra electrical information, pass it through explicit parameter objects, maps, or algorithm-local data types instead of walking CTS-level relationships.
- Keep routing high-cohesion / low-coupling and move legacy helper logic into the correct layer.

### Reused Data Structures
- Reuse `src/operation/iCTS/source/database/design/Pin.hh` for top-level router inputs.
- Reuse `src/operation/iCTS/source/database/spatial/Point.hh` as the basic point type inside shared routing results and algorithm adaptation.
- Reuse `src/operation/iCTS/source/database/routing/SteinerTree.hh`.
  - `SteinerTree<T>`: topology-only point tree, mainly for FLUTE / SALT.
  - `ClockSteinerTree<T>`: point tree with `routed_length`, mainly for BST / CBS.

### Module Decomposition
- Keep / refactor these router-facing modules:
  - `FLUTERouter`
  - `SALTRouter`
  - `BSTRouter`
  - `CBSRouter`
- The top-level `Router` does not need to force all algorithms through one generic `route()` entry right now; it can primarily forward algorithm-specific interfaces.
- Reimplement `local_legalization` as a standalone point-based legalizer:
  - input: fixed points + variable points
  - objective: minimum total displacement
  - support optional `feasible_region`
  - first supported `feasible_region` model: axis-aligned rectangle
  - emphasize performance and simple interfaces
  - this is not general placement legalization or standard-cell legalization; its first goal is only to make point locations distinct / non-overlapping in the routing sense
  - this work has lower global priority and can be deferred until the routing / timing architecture is stabilized

### Build / Layout Constraints
- Every routing submodule should have its own CMake target and CMake file.
- Each submodule directory should mostly contain only `.hh` / `.cc` files.
- BST may keep a deeper folder structure if needed.
- Remove external `bst` namespace exposure from `bound_skew_tree`.
- If code can be made namespace-clean / namespace-free under project conventions, do that during refactor.

### Algorithm Boundary Rules
- Top-level router terminal inputs use `database/design/Pin.hh` so name and location come from stable database objects.
- Router adapters extract point data from pins before entering algorithm-native cores.
- BST internally converts points to physical coordinates `Point<double>` using DB unit scaling.
- BST converts results back to `Point<int>` at the router boundary so shared routing-result types stay consistent.
- SALT / FLUTE stay on `Point<int>` after pin-to-point adaptation.
- CBS composes BST and SALT behavior under the new modular boundary.
- External module parameters such as unit RC / skew bound must be passed explicitly rather than read through hidden legacy routing objects.
- If BST / CBS need electrical initialization such as init delay or init cap, pass them explicitly as maps or algorithm-specific parameter objects.
- For the first version, prefer pin-name-string keyed maps over `Pin*` keyed maps.
- iSTA integration should live under `source/adapter/sta/Adapter.hh` instead of `CTSAPI`; source-layer modules access STA through the adapter layer.

### Architecture Shape
- Algorithm implementation modules (for example `bound_skew_tree`) should stay focused on their native algorithm data structures and explicit parameter sets.
- `{Alg}Router` layers are responsible for adapting between shared routing data structures and algorithm-native structures.
- `BSTRouter` should own conversions such as:
  - terminal inputs -> BST native structures for normal construction
  - input `ClockSteinerTree<int>` topology -> binary BST native structures for fixed-topology construction
  - BST native result -> shared routing tree structures
- `BSTRouter` should expose two standalone public flows rather than collapsing them into one oversized API:
  - normal BST construction from terminals
  - input-topology BST reconstruction from an existing `ClockSteinerTree<int>`
- Shared parameter objects such as `BSTParameters` should be used across algorithm module, router adapter, and top-level router when needed.
- For the current standalone BST refactor, input-topology electrical initialization is terminal-driven: explicit terminal delay / cap inputs are passed in, while internal Steiner-node state is recomputed during adaptation rather than imported from legacy CTS nodes.
- Binary-tree conversion for BST input-topology flow belongs to `BSTRouter`, not the BST core.
- FLUTE / SALT should be simplified to `{Alg}Router.hh/.cc` style wrappers around the existing third-party implementations.

### Behavioral Equivalence Constraints
- Refactor should preserve original algorithm behavior instead of simplifying away important logic.
- Example: BST input-topology flow still needs the binary-tree conversion path at the appropriate adapter layer.
- Legacy helper logic required by a specific router should be moved into that algorithm module or its router adapter.

### Output Constraints
- Routing outputs should stay point-based.
- BST / CBS must preserve actual routed wire length information for snake cases instead of only geometric Manhattan distance.
- For standalone BST export, `ClockSteinerTree<int>` edges should record embedded geometric Manhattan distance in `dist_length` and actual routed wirelength in `routed_length`.
- `SteinerTree` / `ClockSteinerTree` to `RCTree` conversion should use CTS API RC queries instead of unit-RC approximation.
- The first stage-2 ownership choice is to place `SteinerTree` / `ClockSteinerTree` -> `RCTree` conversion at the top-level `Router` / unified entry layer.

## Routing / Timing Boundary Decisions
- During `SteinerTree` / `ClockSteinerTree` -> `RCTree` conversion, the top-level `Router` registers vertex keys.
- `RCTree` should support key-based vertex lookup for all vertices, including internal Steiner vertices when needed.
- A key maps to exactly one vertex.
- Vertices must not be merged purely because they share the same location.
- Naming-policy generation should stay in the top-level `Router` / conversion wrapper rather than inside `RCTree` itself.
- Existing pin names should be reused where appropriate for terminal vertices.
- `RCTree` should provide lookup / registration support, but not own naming-policy decisions.

## Timing Module Requirements

### Core Goals
- Move timing code from `src/operation/iCTS/source/module/routing/timing` into `src/operation/iCTS/source/module/timing`.
- Rebuild timing as an independent module rather than a routing subdirectory.
- Keep the implementation simple, efficient, and easy to debug.

### Data / Responsibility Boundary
- Reuse `src/operation/iCTS/source/database/timing` data structures.
- Timing only computes information necessary on `RCTree`, including:
  - capacitance
  - delay
  - slew
  - skew
- Timing should not own physical wirelength or other layout-only geometry responsibilities.
- Remove old cell-related concepts, interfaces, and redundant compatibility APIs.

### Implementation Constraints
- Avoid concept-heavy abstractions that make debugging difficult.
- Avoid unnecessary recursive implementations when a simpler iterative structure is clearer and faster.

## Current Repo Facts
- `src/operation/iCTS/source/module/timing` now exists as an independent timing target.
- `src/operation/iCTS/source/module/routing/CMakeLists.txt` still keeps `local_legalization` inside the routing target.
- Reusable point / tree database types already exist:
  - `database/spatial/Point.hh`
  - `database/routing/SteinerTree.hh`
  - `database/timing/RCTree.hh`
- Routing submodules (`flute`, `salt`, `bound_skew_tree`, `concurrent_bst_salt`) are already split into independent targets in the current tree.

## Pending Confirmation
- None currently. The PRD has enough confirmed scope to proceed to implementation planning when needed.
