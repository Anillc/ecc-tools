# Implement HTree Builder

## Goal

Implement `HTreeBuilder.hh/.cc` under `src/operation/iCTS/source/flow/htree/` so a caller can pass `std::vector<Pin*>` loads and obtain an H-tree synthesis result built from existing topology and characterization infrastructure. The flow should reuse `TopologyGen`, `CharBuilder`, `SegmentChar`, `BufferingPattern`, and `HTreeTopologyChar` concepts instead of introducing a second characterization stack.

## What I already know

* The user wants the implementation scoped to `source/flow/htree/`, with input interface `std::vector<Pin*>`.
* Desired flow:
  1. accept `vector<Pin*>`
  2. build H-tree topology based on `module/topology/TopologyGen`
  3. generate basic segment chars from config
  4. globally synthesize minimum-concatenation segment paths for all H-tree levels, with wire-length lattice alignment allowed
  5. stitch segment chars into an H-tree table
  6. select the best H-tree char
  7. materialize CTS objects (`Net`, `Inst`, `Pin`) where leaves reuse the original sink pins
* `source/flow/` currently exists but is disabled in `src/operation/iCTS/source/CMakeLists.txt`, so new flow code will not compile unless parent CMake is enabled.
* `TopologyGen::build(const std::vector<Pin*>&)` already builds a balanced binary `Tree` and stores per-node geometry/load subsets.
* `CharBuilder` already enumerates segment chars and stores structural segment patterns as `BufferingPattern{length_idx, pattern_id, normalized buffer positions, cell_masters}`.
* `SegmentChar` composition exists, but there is no production-side registry that maps composed `PatternId` back to a composed `BufferingPattern`.
* `HTreeTopologyChar` composition exists, but there is no production-side registry that maps topology `PatternId` back to `HTreeTopologyPattern`.
* Existing tests cover:
  * exact segment join semantics
  * exact H-tree join semantics
  * real-tech manual H-tree composition examples
  * topology generation behavior

## Assumptions (temporary)

* The new flow-local builder may own temporary synthesis products such as segment-pattern registries and topology-pattern registries inside `flow/htree`.
* The first implementation can return a flow-local result object that includes the selected H-tree char plus materialized CTS objects.
* Flow-local CMake enablement may require minimal parent `CMakeLists.txt` edits even if algorithm code remains in `flow/htree`.

## Open Questions

* Is minimal parent CMake wiring outside `source/flow/htree/` acceptable so the new builder actually compiles and tests can link it?

## Requirements (evolving)

* Implement `HTreeBuilder.hh/.cc` in `src/operation/iCTS/source/flow/htree/`.
* Reuse existing `TopologyGen` to derive the H-tree geometry/levels from the input pins.
* Reuse existing characterization data and config-driven segment enumeration instead of inventing a new char format.
* Compute all required H-tree level lengths first, then synthesize globally minimal concatenation-count segment paths with lattice alignment.
* Reuse shorter synthesized segment results when building longer segment lengths.
* Build H-tree topology chars level by level and select the best H-tree char from the resulting frontier.
* Reconstruct CTS design objects for the selected H-tree:
  * inserted buffer insts
  * inserted pins
  * inserted nets
  * original leaf pins remain the sink endpoints
* Keep non-flow algorithm changes out of other modules unless parent build wiring is unavoidable.

## Acceptance Criteria (evolving)

* [ ] `HTreeBuilder` accepts `std::vector<Pin*>` and executes the end-to-end H-tree build flow.
* [ ] Segment synthesis prefers minimal concatenation count globally, not naive repeated appends of the largest base segment.
* [ ] Level lengths can align upward to the characterization wire-length lattice when no exact segment length exists.
* [ ] The selected H-tree char can be traced back to concrete segment/topology patterns sufficient to reconstruct buffer cell masters and positions.
* [ ] Leaf nodes in the reconstructed structure reuse the original input pins.
* [ ] New code follows iCTS backend guidelines and compiles in the project build.

## Definition of Done (team quality bar)

* Tests added or updated where practical
* Build / quality checks run on touched paths
* CMake wiring updated if new files are compiled
* Handoff includes remaining limits or integration gaps

## Out of Scope (explicit)

* Reworking the existing characterization module internals outside what is required to consume them
* Adding API-layer entry points
* Persisting synthesized H-tree results into a broader global database model

## Technical Notes

* Relevant source areas inspected:
  * `src/operation/iCTS/source/module/topology/TopologyGen.hh`
  * `src/operation/iCTS/source/module/topology/TopologyGen.cc`
  * `src/operation/iCTS/source/module/characterization/CharBuilder.hh`
  * `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
  * `src/operation/iCTS/source/database/characterization/BufferingPattern.hh`
  * `src/operation/iCTS/source/database/characterization/HTreeTopologyPattern.hh`
  * `src/operation/iCTS/source/database/spatial/Tree.hh`
* Relevant test areas inspected:
  * `src/operation/iCTS/test/module/characterization/SegmentJoinTest.cc`
  * `src/operation/iCTS/test/module/characterization/HTreeJoinTest.cc`
  * `src/operation/iCTS/test/module/characterization/CharacterizationRealTechSmokeTest.cc`
  * `src/operation/iCTS/test/module/topology/topology_gen/TopologyGenTest.cc`
* Constraints from specs:
  * new source files require iCTS copyright + Doxygen headers
  * no exceptions
  * use `CTS_LOG_*`
  * new module/file wiring must update CMake before implementation
