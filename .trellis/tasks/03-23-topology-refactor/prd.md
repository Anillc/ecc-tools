# Refactor topology module static API and CMake layout

## Goal

Refactor `src/operation/iCTS/source/module/topology` so the public entry points are static-style utilities instead of stateful objects, remove persistent private member state from the topology layer, and split the topology subdirectories into separate CMake targets while preserving current behavior as much as possible.

## What I already know

- `TopologyGen` currently exposes constructors, `build`, and `updateConfig`, and stores `_config` plus `_clustering` as private members.
- `Clustering` currently exposes constructors plus `biPartition`, and stores `_config` as a private member.
- `TopologyGen` depends on `Clustering` during recursive/iterative embedding.
- The topology module is currently built mainly through a single target in `src/operation/iCTS/source/module/topology/CMakeLists.txt`.
- `mcf/` already has its own CMake target, but `clustering/` and `kmeans/` do not.
- Current external usages found so far are limited: `TopologyGen` is referenced in `TopologyGenTest.cc`, and `Clustering` is currently only used inside topology implementation.

## Assumptions (temporary)

- This refactor should prioritize API cleanup and build-graph cleanup, not algorithm redesign.
- Existing clustering and topology behavior should stay semantically equivalent unless a small internal rewrite is required by the stateless API.
- The root `topology/` directory will keep `icts_source_module_topology` as the concrete target for `TopologyGen`, while each subdirectory also gets its own target.

## Open Questions

- None at the moment.

## Requirements (evolving)

- `TopologyGen` public API should expose only `build`.
- `TopologyGen::build` should be static-style and use config parameters instead of stored object state.
- `TopologyGen` should provide both `build(loads)` and `build(loads, config)` static overloads.
- `Clustering` should also become static-style instead of requiring instance state.
- `Clustering` should provide static `biPartition(loads, min_cluster_size)` and `biPartition(loads, min_cluster_size, config)` overloads.
- Runtime configuration should be passed through static-call parameters rather than stored on objects.
- Remove private member variables used only to cache configuration or helper objects; set up temporary state when needed.
- Each folder under `src/operation/iCTS/source/module/topology` should have its own CMake target.
- The existing root target `icts_source_module_topology` should remain the concrete target for the root `topology/` folder and link the subdirectory targets.
- Update dependent code and tests to match the refactored interfaces.

## Acceptance Criteria (evolving)

- [ ] `TopologyGen.hh` exposes only the intended static `build` entry point.
- [ ] `TopologyGen::build` no longer relies on constructors or `updateConfig`, and supports both default-config and explicit-config static overloads.
- [ ] `Clustering.hh` exposes a static entry point and no persistent config member.
- [ ] Topology implementation no longer relies on object-held `_config` / `_clustering` state.
- [ ] Topology CMake is split so each subdirectory has an explicit target.
- [ ] The parent topology target links the new subtargets correctly.
- [ ] Existing topology tests compile against the new API.
- [ ] Relevant quality checks pass for the touched topology paths.

## Definition of Done (team quality bar)

- Tests added or updated where needed
- Lint / quality checks pass for modified files
- CMake structure remains minimal and follows current iCTS conventions
- No unnecessary API surface remains after the refactor

## Out of Scope (explicit)

- Adding new topology algorithms
- Changing high-level CTS flow behavior outside the topology module
- Broad refactors outside files required by this topology API/CMake change

## Technical Approach

- Follow existing iCTS static-builder patterns like `TimingEngine`, `SALTRouter`, `BSTRouter`, and `Router` overload delegation.
- Convert `TopologyGen` into a static utility class with only `build(loads)` and `build(loads, config)` overloads.
- Convert `Clustering` into a static utility class with matching default-config and explicit-config overloads.
- Remove cached `_config` / `_clustering` members and thread config explicitly through helper calls such as position embedding.
- Keep `icts_source_module_topology` as the concrete root target for `TopologyGen`, and split `clustering`, `kmeans`, and `mcf` into their own leaf targets linked by the root target.
- Update tests from object-style invocation to static invocation.

## Decision (ADR-lite)

**Context**: The topology module currently mixes stateful object APIs with a flat CMake layout, while the rest of iCTS already prefers static utility/builders and hierarchical target composition.

**Decision**: Use stateless static APIs for both `TopologyGen` and `Clustering`, with both default-config and explicit-config overloads. Keep the existing root topology target as the concrete target for the root folder, and add separate subtargets for `clustering`, `kmeans`, and `mcf`.

**Consequences**: Public APIs become smaller and clearer, config flow becomes explicit, and CMake dependencies become more modular. The main cost is updating the few remaining callers/tests and carefully keeping include/link visibility minimal.

## Implementation Plan

- Step 1: Refactor `TopologyGen.hh/.cc` and `Clustering.hh/.cc` to static, stateless APIs.
- Step 2: Split topology CMake into root + `clustering` + `kmeans` + `mcf` targets with minimal visibility.
- Step 3: Update tests/callers to the new API.
- Step 4: Run focused builds/checks and use `ecc_dev_tools` on the topology path until findings converge.

## Technical Notes

- Files inspected:
  - `src/operation/iCTS/source/module/topology/TopologyGen.hh`
  - `src/operation/iCTS/source/module/topology/TopologyGen.cc`
  - `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`
  - `src/operation/iCTS/source/module/topology/clustering/Clustering.cc`
  - `src/operation/iCTS/source/module/topology/CMakeLists.txt`
  - `src/operation/iCTS/source/module/topology/mcf/CMakeLists.txt`
  - `src/operation/iCTS/source/module/topology/kmeans/KMeans.hh`
- Relevant constraints:
  - iCTS uses `.hh` / `.cc`
  - Prefer minimal public interfaces and proper CMake dependency visibility
  - Use separate targets where logical submodules have distinct dependency boundaries
