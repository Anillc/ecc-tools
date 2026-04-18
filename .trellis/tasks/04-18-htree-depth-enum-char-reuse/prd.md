# Flow-level H-Tree depth enumeration and characterization reuse

## Goal

Refactor the flow-level H-Tree construction pipeline so synthesis can evaluate multiple bounded H-Tree depths derived from the current load set, compute depth-aware electrical inputs, reuse characterization data across depth candidates, and pick the best feasible solution without introducing runtime regression, architectural drift, or patch-style coupling across modules.

## What I already know

* The user wants H-Tree depth exploration based on the current load set maximum depth `D`, with candidate depths enumerated from `[D, D-1, D-2, D-3]` and the exact window size aligned with an existing configurable strategy.
* `TopologyGen` should support an explicit target depth/level count, while preserving existing "build to full depth" behavior as the default.
* `min_input_slew` remains fixed, while `driven_cap` must be recomputed per candidate depth from the candidate leaf count and the average routing capacitance of load clusters produced by linear clustering.
* Characterization must stay generic and reusable. The user expects the `D`-level characterization pass to cover the lower-depth candidates as much as possible, and wants lifecycle analysis of characterization entries to ensure lower-depth data is not prematurely discarded.
* Candidate selection needs a design decision between per-depth local best followed by cross-depth comparison versus a global feasible-set optimization.
* The implementation may touch synthesis, H-Tree builder, and characterization modules and must end as a clear, maintainable flow-level design rather than incremental patching.
* Tests, runtime analysis, and solution-quality analysis are required. `ecc_dev_tools` should be run only after the implementation and tests are accepted locally.

## Assumptions (temporary)

* The current flow already has a single-depth H-Tree build path that can be generalized into a multi-candidate evaluation pipeline.
* Existing characterization tables are keyed in a way that may already allow reuse across depth-bounded candidates, but pruning or ownership rules may limit that reuse.
* Existing linear clustering infrastructure can be reused to derive candidate-depth leaf buckets and estimate average routing capacitance without introducing a separate clustering implementation.

## Open Questions

* Confirm whether the depth enumeration window should be fixed at 4 candidates or exposed/configured with an internal default.
* Confirm the exact optimality policy after codebase research: global feasible-set comparison or staged per-depth reduction.

## Requirements (evolving)

* Compute the maximum feasible H-Tree depth `D` for the current loads and evaluate a bounded set of candidate depths descending from `D`.
* Add flow/topology support for explicit target H-Tree depth while keeping the default behavior unchanged for existing callers.
* Derive candidate-specific `driven_cap` from:
  * candidate leaf count
  * linear clustering of the input loads into that number of leaves
  * average routing capacitance of the resulting clusters
* Keep characterization generic and cacheable across depth candidates.
* Preserve or improve runtime relative to the current flow.
* Add/extend tests that cover depth-bounded H-Tree generation, depth-aware electrical input derivation, characterization reuse, and solution selection.
* Produce runtime and quality analysis for the final candidate-selection flow.

## Acceptance Criteria (evolving)

* [ ] Flow can evaluate multiple H-Tree depth candidates for a given load set without regressing existing default behavior.
* [ ] `TopologyGen` can build either to the natural full depth or to a caller-provided target depth.
* [ ] Candidate-specific `driven_cap` is derived from clustering/routing-cap analysis rather than a fixed shared value.
* [ ] Characterization data needed by lower-depth candidates is reused from a higher-depth characterization pass when equivalent data already exists.
* [ ] Tests cover the new flow behavior and pass locally.
* [ ] Runtime and solution-quality analysis show no unacceptable regressions.
* [ ] `ecc_dev_tools` full check passes with no in-scope findings after implementation is validated.

## Definition of Done (team quality bar)

* Tests added or updated for new behavior.
* Local validation includes runtime and quality comparison on representative cases.
* Docs/spec notes are updated if contracts or reusable patterns change.
* Final check pass includes `ecc_dev_tools` only after implementation and tests are complete.

## Out of Scope (explicit)

* Introducing external dependencies or non-project naming from reference implementations.
* Reworking unrelated CTS clustering, routing, or optimization flows beyond what is required for the new H-Tree candidate-evaluation design.

## Technical Notes

* Initial likely impact areas:
  * `src/operation/iCTS/source/flow/synthesis/`
  * `src/operation/iCTS/source/flow/htree/`
  * `src/operation/iCTS/source/module/characterization/`
  * `src/operation/iCTS/source/module/topology/`
  * `src/operation/iCTS/test/flow/synthesis/`
  * `src/operation/iCTS/test/flow/htree/`
  * `src/operation/iCTS/test/module/characterization/`

## Research Notes

### Relevant Specs

* `.trellis/spec/project-constraints.md`: repository-wide C++/iCTS constraints, validation policy, and naming constraints.
* `.trellis/spec/backend/directory-structure.md`: placement rules for new flow/topology/utility code and required CMake updates.
* `.trellis/spec/backend/quality-guidelines.md`: header/include discipline and dependency visibility for the refactor.
* `.trellis/spec/backend/database-guidelines.md`: singleton and ownership boundaries for `CONFIG_INST`, `WRAPPER_INST`, and `STA_ADAPTER_INST`.
* `.trellis/spec/backend/logging-guidelines.md`: log/schema expectations for new runtime and analysis summaries.
* `.trellis/spec/backend/error-handling.md`: return-vs-fatal behavior for flow skips, fallback, and invalid states.
* `.trellis/spec/guides/cross-layer-thinking-guide.md`: relevant because the task crosses flow, topology, characterization, config, and adapter boundaries.
* `.trellis/spec/guides/code-reuse-thinking-guide.md`: relevant because the task should extract reusable candidate-planning/cache logic instead of adding duplicated flow-side code.

### Code Patterns Found

* Single-depth H-tree orchestration:
  * `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
  * Current flow: topology build -> characterization -> segment frontier synthesis -> H-tree composition -> feasible filtering -> final selection -> materialization.
* Synthesis-to-builder boundary propagation:
  * `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc`
  * Current `buildHtreeOptions()` maps synthesis constraints into `min_top_input_slew_ns` and `min_leaf_driven_cap_pf`.
* Topology generation and depth derivation:
  * `src/operation/iCTS/source/module/topology/TopologyGen.cc`
  * Current topology depth is implicitly derived from `floor_pow2(load_count)` and cannot yet be caller-bounded.
* Exact routing-cap estimation inside linear clustering:
  * `src/operation/iCTS/source/module/topology/linear_clustering/ConstraintEvaluator.cc`
  * Existing exact-cap path already computes routed wirelength / wire cap / total cap per cluster candidate.
* Segment characterization lifetime:
  * `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc`
  * `src/operation/iCTS/source/module/characterization/CharBuilder.cc`
  * `SynthesizeSegmentEntrySets()` retains both raw and frontier segment entries by aligned length, which is reusable across depth candidates if owned outside a one-shot build.

### Constraints From Existing Code

* Current `TopologyGen::build()` recomputes bipartition using target leaf demand, so independently rebuilding `D` and `D-1` trees would not guarantee aligned upper-level lengths.
* Current linear clustering API controls legality by `max_fanout/max_cap/max_diameter`, but does not guarantee an exact cluster count equal to a requested H-tree leaf count.
* Current `HTreeBuilder::build()` owns characterization and segment-entry synthesis internally, so lower-depth candidates cannot reuse them unless that state is lifted into an explicit cache/context object.
* Current final selection is local to one frontier; extending to multi-depth candidates requires an explicit cross-candidate selection pool and reporting structure.

### Proposed Direction

* Build one deepest candidate scaffold and derive shallower flow candidates from it, so lower-depth candidates can reuse aligned level lengths and the same characterization cache.
* Add a reusable exact-cluster-count linear clustering path for leaf-count-driven cap estimation instead of approximating with `max_fanout`.
* Extract reusable characterization/segment-entry ownership into a flow-level cache/context that is consumed by candidate evaluations.
* Keep per-depth candidate summaries for runtime/quality analysis, but perform the final choice on the global union of feasible representatives from all depths.
