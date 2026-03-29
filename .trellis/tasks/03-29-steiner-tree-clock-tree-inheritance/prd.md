# brainstorm: refactor steiner tree clock tree inheritance

## Goal

Refactor the iCTS routing tree model so `SteinerTree` becomes template-capable over node type, `ClockSteinerTree` changes from a parallel design to inheritance over `SteinerTree`, and clock-specific node/terminal state moves into derived helper types instead of being threaded through side maps in routing/build parameters.

## What I already know

* The user wants to change [`src/operation/iCTS/source/database/routing/SteinerTree.hh`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/routing/SteinerTree.hh).
* `SteinerTree` should introduce a `NodeType` template parameter.
* `ClockSteinerTree` should derive from `SteinerTree` and override the necessary/different behavior, mainly `addNode`.
* `ClockSteinerNode` should be introduced as a helper type derived from `SteinerNode`.
* `ClockSteinerNode` needs extra clock-tree members required by `src/operation/iCTS/source/module/routing/Router.hh`: `pin_cap` and `insertion_delay`.
* `ClockRoutingTerminal` should derive from the routing terminal type and add `pin_cap` and `insertion_delay`.
* `RCTreeBuildOptions` should remove `lumped_cap_map`.
* `BSTParameters` should remove `init_delay_map` and `init_cap_map`.
* Callers using those removed maps must be updated to use the new object model.
* Current code facts from inspection:
  * [`src/operation/iCTS/source/database/routing/SteinerTree.hh`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/database/routing/SteinerTree.hh) currently hardcodes `NodeType = SteinerNode<T>` inside `SteinerTree<T, EdgeT>` and defines `ClockSteinerTree` as a type alias over `ClockSteinerEdge<T>`.
  * [`src/operation/iCTS/source/module/routing/database/RoutingTerminal.hh`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/routing/database/RoutingTerminal.hh) currently contains only `name` and `location`.
  * [`src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc) consumes `init_delay_map` and `init_cap_map` in `BuildLoadArea`, `CreateBinaryNode`, and topology rebuild logic.
  * [`src/operation/iCTS/source/module/routing/concurrent_bst_salt/CBSRouter.cc`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/routing/concurrent_bst_salt/CBSRouter.cc) consumes `init_cap_map` when constructing SALT pins and rebuilding the SALT tree.
  * [`src/operation/iCTS/source/module/routing/Router.cc`](/home/liweiguo/project/ecc-tools-dev/src/operation/iCTS/source/module/routing/Router.cc) consumes `lumped_cap_map` during `ClockSteinerTree` -> `RCTree` conversion.
  * No external production call sites of `Router::buildBstTree`, `Router::buildCbsTree`, or `Router::buildRCTree(..., RCTreeBuildOptions)` were found outside `src/operation/iCTS/source/module/routing`.

## Assumptions (temporary)

* `pin_cap` and `insertion_delay` are clock-routing metadata and should stay in clock-specialized node/terminal types rather than leaking into generic `SteinerNode` / `RoutingTerminal`.
* The refactor should preserve FLUTE/SALT generic routing behavior; only clock-routing APIs should adopt the specialized terminal/node types.
* `pin_cap` on `ClockSteinerNode` will replace `lumped_cap_map` lookups during `ClockSteinerTree` -> `RCTree` conversion.
* `insertion_delay` is required mainly to replace terminal initialization flow previously carried by `BSTParameters::init_delay_map`; internal BST temporary structures can still keep min/max delay ranges as local algorithm state.

## Open Questions

* For exported non-terminal `ClockSteinerNode`s, should `pin_cap` / `insertion_delay` be left at default zero or populated from BST-exported electrical state when that data is already available? Current evidence suggests only terminal values are functionally required, but this should be verified during implementation.

## Requirements (evolving)

* Make `SteinerTree` reusable across node specializations by templating node type in addition to edge type.
* Convert `ClockSteinerTree` from an alias to a real derived class over the generic `SteinerTree`.
* Introduce `ClockSteinerNode` and `ClockRoutingTerminal` derived types carrying `pin_cap` and `insertion_delay`.
* Keep generic routing APIs (`FLUTERouter`, `SALTRouter`) on `RoutingTerminal`, and move clock-routing APIs (`BSTRouter`, `CBSRouter`, `Router::buildBstTree`, `Router::buildCbsTree`) to `ClockRoutingTerminal`.
* Remove `lumped_cap_map` from `Router::RCTreeBuildOptions`.
* Remove `init_delay_map` and `init_cap_map` from `BSTParameters`.
* Replace all old side-map consumers with object-owned clock metadata on terminals or clock tree nodes.
* Preserve current tree validation, node naming, routed-distance semantics, and BST/CBS topology-adaptation behavior.

## Acceptance Criteria (evolving)

* [ ] `SteinerTree` supports a node-type template without breaking current generic tree behavior used by FLUTE/SALT.
* [ ] `ClockSteinerTree` is implemented as a derived class over `SteinerTree` and only customizes clock-specific node construction / behavior.
* [ ] `ClockSteinerNode` and `ClockRoutingTerminal` carry `pin_cap` and `insertion_delay`, with no dependence on `BSTParameters` init maps or `RCTreeBuildOptions` lumped-cap maps.
* [ ] `BSTRouter` uses `ClockRoutingTerminal` / `ClockSteinerNode` metadata instead of `init_delay_map` / `init_cap_map`.
* [ ] `CBSRouter` uses `ClockSteinerNode::pin_cap` instead of `init_cap_map` when constructing SALT pins.
* [ ] `Router::buildRCTree` for clock trees uses node-owned capacitance instead of `lumped_cap_map`.
* [ ] `RCTreeBuildOptions` no longer defines `lumped_cap_map`.
* [ ] `BSTParameters` no longer defines `init_delay_map` or `init_cap_map`.
* [ ] All impacted call sites and any necessary tests compile against the new signatures and data flow.

## Definition of Done (team quality bar)

* Tests added/updated where appropriate
* Lint / quality checks green for touched paths
* Docs/notes updated if behavior changes
* Rollout/rollback considered if risky

## Out of Scope (explicit)

* Algorithmic changes to Steiner tree generation beyond what the new type hierarchy requires
* Unrelated iCTS routing cleanups not necessary for this refactor

## Research Notes

### Cross-layer flow

`ClockRoutingTerminal` -> `BSTRouter` / `CBSRouter` -> `ClockSteinerTree` / `ClockSteinerNode` -> `Router::buildRCTree` -> `RCTree`

### What the current code does

* `BSTRouter` treats terminal delay/cap as external lookup data and threads them into BST `Area` / binary-node construction.
* `CBSRouter` only needs capacitance from the external map when converting terminals into SALT pins.
* `Router::buildRCTree` currently injects lumped capacitance by vertex name lookup at conversion time, even though validated tree nodes already carry stable names.
* Generic routing flows (`FLUTE`, `SALT`) do not use any of the removed side maps.

### Feasible approaches here

**Approach A: Typed tree/terminal specialization with inheritance** (Recommended)

* `SteinerTree<T, NodeT, EdgeT>` becomes the reusable base.
* `ClockSteinerTree<T>` derives from `SteinerTree<T, ClockSteinerNode<T>, ClockSteinerEdge<T>>`.
* `ClockRoutingTerminal` derives from `RoutingTerminal`.
* Clock-specific metadata moves onto objects and conversion helpers read it directly.

Pros:

* Matches the requested design direction.
* Removes indirect side-map state from public routing APIs.
* Keeps FLUTE/SALT on the simpler generic contracts.

Cons:

* Touches public headers and template signatures.
* Requires careful node-construction design to avoid duplicating base tree validation logic.

**Approach B: Keep alias-based tree and hide metadata in helper maps**

* Leave `ClockSteinerTree` as an alias and replace removed maps with new internal adapters.

Pros:

* Smaller code churn.

Cons:

* Violates the requested inheritance refactor.
* Keeps the core problem of side-channel metadata flow.

## Technical Approach

* Change the generic tree declaration to support explicit node specialization, likely in the form `SteinerTree<T, NodeT = SteinerNode<T>, EdgeT = SteinerEdge<T>>`.
* Introduce constructors or protected factory helpers for node creation so derived `ClockSteinerNode` can be created without duplicating duplicate-name validation and ID assignment logic.
* Implement `ClockSteinerTree` as a real class deriving from the generic base tree and provide clock-aware `addNode` overloads / overrides that initialize `pin_cap` and `insertion_delay`.
* Add `ClockRoutingTerminal` near the existing routing terminal type and switch clock-router entry points to `std::vector<ClockRoutingTerminal>`.
* Replace BSTRouter map lookups with direct reads from `ClockRoutingTerminal` for initial terminals and from `ClockSteinerNode` for topology rebuild/export.
* Replace CBSRouter map lookups with direct reads from `ClockSteinerNode::pin_cap`.
* Replace `Router::buildRCTree` lumped-cap lookup with a node-property helper so generic trees still produce `0.0` while clock trees emit `node.pin_cap`.
* Add or update focused routing tests for the new type hierarchy and object-owned metadata path. Existing routing test coverage is currently thin and mostly limited to legalization.

## Decision (ADR-lite)

**Context**: Clock-routing electrical metadata is currently passed via separate name-keyed maps even though the routing flow already builds terminal and tree node objects that could own that state directly. At the same time, `ClockSteinerTree` is only an edge-specialized alias, which blocks a cleaner clock-specific extension point.

**Decision**: Use typed inheritance for both terminals and nodes. `SteinerTree` becomes node-type aware, `ClockSteinerTree` becomes a derived class, `ClockRoutingTerminal` carries input metadata for clock-routing entry points, and `ClockSteinerNode` carries the clock-specific state used by BST/CBS construction and RCTree conversion.

**Consequences**: Public routing headers will change, but the blast radius is contained mostly within `module/routing` and the shared routing data types. The design becomes more explicit, removes side-channel maps, and creates a cleaner extension point for future clock-only metadata without burdening generic Steiner trees.

## Implementation Plan

* Task 1: Refactor shared routing data structures
  * Main files: `SteinerTree.hh`, `RoutingTerminal.hh`, router headers using these types
  * Outcome: generic node-specialized base tree, derived clock node/tree/terminal types, updated type aliases
  * Child task: `03-29-steiner-clock-hierarchy`
* Task 2: Migrate BST/CBS metadata flow
  * Main files: `BSTTypes.hh`, `BSTRouter.hh/.cc`, `CBSRouter.hh/.cc`
  * Outcome: remove init maps from parameters, initialize/load clock metadata from terminal/node objects, preserve topology rebuild semantics
  * Child task: `03-29-bst-cbs-electrical-state-migration`
* Task 3: Update RCTree conversion and call sites
  * Main files: `Router.hh/.cc`, any impacted tests or routing integration code
  * Outcome: remove `lumped_cap_map`, read lumped cap from clock nodes, align signatures and compile surface
  * Child task: `03-29-router-rctree-callsite-update`
* Verification
  * Run focused build/tests for iCTS routing targets
  * Run `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS/source/database/routing`
  * Run `python3 ./.trellis/ecc_dev_tools/check.py check --path src/operation/iCTS/source/module/routing`

## Technical Notes

* Session context gathered via `.trellis/scripts/get_context.py`
* Trellis workflow and backend/guides index have been reviewed
* Reviewed guides:
  * `.trellis/spec/guides/cross-layer-thinking-guide.md`
  * `.trellis/spec/guides/code-reuse-thinking-guide.md`
  * `.trellis/spec/backend/directory-structure.md`
  * `.trellis/spec/backend/quality-guidelines.md`
* Files inspected:
  * `src/operation/iCTS/source/database/routing/SteinerTree.hh`
  * `src/operation/iCTS/source/module/routing/database/RoutingTerminal.hh`
  * `src/operation/iCTS/source/module/routing/Router.hh`
  * `src/operation/iCTS/source/module/routing/Router.cc`
  * `src/operation/iCTS/source/module/routing/bound_skew_tree/BSTTypes.hh`
  * `src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.hh`
  * `src/operation/iCTS/source/module/routing/bound_skew_tree/BSTRouter.cc`
  * `src/operation/iCTS/source/module/routing/bound_skew_tree/Components.hh`
  * `src/operation/iCTS/source/module/routing/concurrent_bst_salt/CBSRouter.hh`
  * `src/operation/iCTS/source/module/routing/concurrent_bst_salt/CBSRouter.cc`
  * `src/operation/iCTS/source/module/routing/flute/FLUTERouter.hh`
  * `src/operation/iCTS/source/module/routing/flute/FLUTERouter.cc`
  * `src/operation/iCTS/source/module/routing/salt/SALTRouter.hh`
  * `src/operation/iCTS/source/module/routing/salt/SALTRouter.cc`
  * `src/operation/iCTS/source/database/timing/RCTree.hh`
