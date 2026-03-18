# Build iCTS routing and timing modules

## Goal

Stage-1 migrated the legacy routing implementation from `src/operation/iCTS_bak/source/solver/tools/tree_builder` into `src/operation/iCTS/source/module/routing` and made it build. Stage-2 now refactors that result into a cleaner module architecture with explicit routing / timing separation, `design/Pin.hh`-based top-level router inputs, and removal of legacy routing compatibility data structures from algorithm boundaries.

## Requirements

### Stage-1 Status
- [x] Stage-1 routing migration completed and builds successfully.
- [x] Current routing facade exists under `src/operation/iCTS/source/module/routing`.
- [x] Stage-1 still contains legacy compatibility structures and mixed responsibilities.

### Stage-2 Routing Requirements
- Remove legacy routing compatibility data structures from routing algorithm boundaries, especially legacy routing `Pin` / `Inst` / `Net` coupling under `module/routing/legacy_module`.
- Use `src/operation/iCTS/source/database/design/Pin.hh` as the top-level router terminal input carrier so terminal identity and naming are preserved naturally.
- Keep algorithm cores decoupled from CTS object-graph relationships; `Pin.hh` usage should stop at the top-level `Router` / `{Alg}Router` adaptation boundary rather than flowing through algorithm-native internals.
- Reuse `database/spatial/Point.hh` and `database/routing/SteinerTree.hh` as the shared routing-result data structures.
- Keep four router-facing modules: `FLUTERouter`, `SALTRouter`, `BSTRouter`, `CBSRouter`.
- Reimplement `local_legalization` as a standalone point-based legalizer.
- Split routing submodules into independent CMake targets / CMake files.
- Remove public `bst` namespace exposure.
- Move algorithm-required external values to explicit parameter objects instead of hidden legacy CTS-level lookups.
- For BST / CBS style electrical initialization, allow explicit inputs such as init-delay maps, init-cap maps, or algorithm-specific parameter objects.
- Preserve original algorithm behavior during refactor, including topology-adaptation logic such as BST input-topology conversion.

### Stage-2 Timing Requirements
- Move timing from `src/operation/iCTS/source/module/routing/timing` to `src/operation/iCTS/source/module/timing`.
- Reuse `database/timing` data structures.
- Limit timing responsibilities to `RCTree`-oriented electrical computation such as cap / delay / slew / skew.
- Remove cell-related concepts and redundant compatibility interfaces.
- Keep the new timing implementation simple, efficient, and easy to debug.

## Acceptance Criteria
- [ ] Routing no longer exposes or requires legacy routing compatibility `Pin` / `Inst` / `Net` structures at router algorithm boundaries.
- [ ] Top-level router APIs accept `database/design/Pin.hh` terminals, while shared routing results remain centered on `Point` / `SteinerTree` / `ClockSteinerTree` data structures.
- [ ] Algorithm cores do not depend on `Pin` object-graph relationships such as `Inst*` / `Net*`.
- [ ] `FLUTERouter`, `SALTRouter`, `BSTRouter`, and `CBSRouter` are organized as router-specific submodules over shared routing interfaces / helpers.
- [ ] BST / CBS style electrical initialization can be provided explicitly through maps or algorithm-specific parameter objects.
- [ ] `local_legalization` is rebuilt as an independent point-based legalizer with optional feasible-region support.
- [ ] Routing submodules are split into separate CMake targets.
- [ ] `bound_skew_tree` no longer exposes `bst` namespace publicly.
- [ ] Timing is moved to `src/operation/iCTS/source/module/timing` and built independently from routing.
- [ ] Timing only operates on `RCTree`-relevant electrical information and no longer carries cell-related or layout-only responsibilities.
- [ ] `SteinerTree` / `ClockSteinerTree` based routing results can be converted into `RCTree` using CTS API RC queries.
- [ ] Refactored algorithms remain behaviorally equivalent to the legacy implementations for supported flows.

## Decision (ADR-lite)
**Context**: Stage-2 needs a clear ownership boundary for converting routing results into timing data structures, a practical top-level router API that does not over-abstract current needs, and a way to preserve logical terminal identity without keeping legacy routing compatibility structures alive.

**Decision**:
- `SteinerTree` / `ClockSteinerTree` -> `RCTree` conversion will first be owned by the top-level `Router` / unified entry layer.
- The top-level `Router` will primarily expose forwarded algorithm-specific entry points rather than forcing all algorithms through a single generic `route()` API.
- The top-level router input carrier will shift from pure ordered points to `src/operation/iCTS/source/database/design/Pin.hh` terminals.
- Public result types may differ by algorithm family:
  - FLUTE / SALT -> `SteinerTree<int>`
  - BST / CBS -> `ClockSteinerTree<int>`
- `RCTree` will be extended to support string-key lookup for vertices.
- A key maps to exactly one `RCTree` vertex; conversion must not merge vertices only because they share the same location.
- Naming-policy generation should stay in the top-level `Router` / conversion wrapper, while `RCTree` only offers registration and lookup support.

**Consequences**:
- `{Alg}Router` implementations stay focused on algorithm-native inputs/outputs and shared routing-tree contracts.
- Top-level router APIs can reuse existing pin names instead of inventing a new terminal naming convention.
- RC-tree construction logic is centralized instead of duplicated across routers.
- The top-level router becomes the first integration point with CTS API RC queries and key registration.
- Current stage-2 avoids spending time on a premature fully unified router-dispatch abstraction.
- Distinct logical terminals may remain distinct vertices in `RCTree` even if they share coordinates.
- `Pin.hh` is treated as an input carrier at the adaptation boundary, not as a dependency that leaks into algorithm cores.
- iSTA access is owned by `source/adapter/sta/Adapter.hh` rather than by `CTSAPI`, so source-layer modules can depend on a dedicated adapter instead of the API tier.

## Technical Approach
- Use `database/design/Pin.hh` as the top-level router terminal input carrier.
- Keep shared routing results on database-layer `Point` / `SteinerTree` / `ClockSteinerTree` data contracts.
- Keep algorithm-native internal data types inside each algorithm implementation, with adapter logic placed in router-specific wrappers.
- Restrict `Pin.hh` usage to the top-level `Router` / `{Alg}Router` adaptation boundary instead of the algorithm core.
- Let the top-level `Router` own shared `SteinerTree` / `ClockSteinerTree` -> `RCTree` conversion.
- Let the top-level `Router` provide algorithm-specific forwarding APIs instead of a forced single generic dispatch entry.
- Extend `RCTree` with key-based vertex lookup for all vertices, without changing vertex identity semantics to location-based merging.
- Reuse pin names where possible for vertex-key registration, while keeping naming-policy generation in the top-level `Router` / conversion wrapper rather than inside `RCTree` itself.
- For the first version of BST / CBS electrical initialization, prefer pin-name-string keyed maps over `Pin*` keyed maps.
- Let `RCTree` provide lookup / registration capabilities, but not own the naming convention policy.
- Treat the first `local_legalization` scope as a lightweight point de-overlap / distinct-location adjustment problem rather than general placement legalization or standard-cell legalization.
- For the first supported `feasible_region` model, use axis-aligned rectangles.
- `local_legalization` is lower global priority and can be deferred until the routing / timing architecture has converged.
- Separate routing, timing, and legalization responsibilities by module and CMake target.
- Remove stage-1 legacy compatibility responsibilities incrementally, one submodule at a time.
- Keep two standalone BST router flows: normal construction from terminals and fixed-topology reconstruction from an input `ClockSteinerTree<int>`.
- Keep BST input-topology electrical initialization terminal-driven for the current refactor: explicit terminal delay / cap inputs are provided, while internal Steiner-node state is recomputed during adaptation instead of imported from legacy CTS nodes.
- Let `BSTRouter` own binary-tree conversion for BST input-topology flow.
- Export both BST flows through a unified `ClockSteinerTree<int>` contract where `dist_length` is embedded Manhattan distance and `routed_length` is actual routed wirelength.

## Out of Scope
- Large cross-module redesigns unrelated to routing / timing decoupling.
- Reintroducing CTS-level legacy objects into new routing interfaces.
- Over-complicating abstractions beyond what stage-2 needs.

## Technical Notes
- Existing stage-1 PRD was replaced because stage-2 changes the task goal from migration to architectural refactor.
- Detailed staged requirements are recorded in `stage-2-requirements.md`.
- Current reusable data structures already present in repo:
  - `src/operation/iCTS/source/database/spatial/Point.hh`
  - `src/operation/iCTS/source/database/routing/SteinerTree.hh`
  - `src/operation/iCTS/source/database/timing/RCTree.hh`
- `src/operation/iCTS/source/module/timing` is now built independently from routing.
- `src/operation/iCTS/source/module/routing/CMakeLists.txt` still keeps `local_legalization` in the routing target; this can be split further if stricter module separation is needed.
