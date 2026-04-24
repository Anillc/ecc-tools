# CTS Source/Test Boundary Inventory

## Purpose

Inventory CTS source surfaces that are currently consumed by tests or that carry test/benchmark/reporting concerns in production-facing modules. This is local repository research for planning the broad CTS source/test layering cleanup.

## Relevant Specs

* `.trellis/spec/backend/directory-structure.md`: iCTS has `api/`, `source/`, and `test/`; source contains internal implementation, tests mirror source layout.
* `.trellis/spec/project-constraints.md`: iCTS uses `.hh`/`.cc`, no exceptions, schema/report helpers for structured file output, CMake updated before new files/modules.
* `.trellis/spec/guides/code-reuse-thinking-guide.md`: search existing helpers before extracting shared code; express CMake reuse through target links.

## Existing Test Support Structure

The test layer already has reusable support libraries that can receive extracted test-only logic:

* `test/common/types`: shared POD-style test data types.
* `test/common/data`: synthetic load and point generation.
* `test/common/io`: artifact path resolution and report emission.
* `test/common/topology`: tree and cluster analysis helpers.
* `test/common/visualization`: SVG writers.
* `test/common/logging`: scoped logging redirection.
* `test/common/linear_clustering/{metrics,artifact}`: cluster metrics and artifact materialization.
* `test/common/realtech/{asset,load,support}`: real-tech setup, asset loading, and load extraction.
* module-specific topology/flow support folders already exist for linear clustering, fast clustering, H-tree, synthesis, and topology generation.

This means most extraction should reuse existing test support targets rather than introduce a parallel test framework.

## Test Include Pressure Points

High-frequency includes from `src/operation/iCTS/test` show where tests rely on source internals or production headers:

* `common/types/TestDataTypes.hh`: 30 includes.
* `common/io/TestArtifactIO.hh`: 27 includes.
* `Point.hh` / `database/spatial/Point.hh`: common geometry dependency.
* `Pin.hh` / `database/design/Pin.hh`: common design object dependency.
* `utils/logger/Schema.hh`: 15 includes.
* `module/topology/config/TopologyConfig.hh`: 11 rooted includes plus 8 short-path includes.
* `module/topology/clustering/Clustering.hh`: 9 rooted includes plus 6 short-path includes.
* `linear_clustering/LinearClusteringTypes.hh`: 8 short-path includes plus 4 rooted includes.
* `module/topology/linear_clustering/SequenceSplitter.hh`: 5 rooted includes.
* `module/topology/linear_clustering/LinearOrderGenerator.hh`: 4 rooted includes.
* `module/topology/linear_clustering/ConstraintEvaluator.hh` and `ClusteringEvaluator.hh`: lower count, but direct white-box algorithm evaluator access.
* `HTreeBuilder.hh`: several flow/synthesis tests consume detailed `BuildResult` fields, including `DepthCandidateSummary`.

Not all direct source includes are wrong. The concern is where tests require source to expose internal algorithm steps, benchmark-only summaries, or artifact/report plumbing that production callers do not need.

## Candidate Boundaries

### Topology Linear/Fast Clustering

Observed source surfaces:

* `LinearClustering.hh` and `FastClustering.hh` expose runtime facades.
* `TopologyConfig.hh` exposes many strategy knobs: ordering strategy, Hilbert encoding, transform, split strategy, sweep mode, strided sweep count, scoring strategy, exact-cap switches, and routing parameters.
* `LinearClusteringTypes.hh` defines `OrderedLoad`, `SegmentRange`, `ClusterSpanMetrics`, `ElectricalEstimate`, `ConstraintEvaluation`, `PartitionScore`, cache keys, and related internal/evaluation structs.
* `SequenceSplitter.hh` exposes sweep offset resolution and full partition splitting.
* `LinearOrderGenerator.hh` exposes order generation.
* `ConstraintEvaluator.hh` and `ClusteringEvaluator.hh` expose legality/scoring internals.
* `FastClusteringInternal.hh` exposes fast clustering draft structures and helper declarations because the implementation is split across multiple `.cc` files.

Likely production needs:

* `LinearClustering::runDefault`, `LinearClustering::run`, `FastClustering::runDefault`, `FastClustering::run`.
* `Clustering` facade methods.
* The subset of `LinearClusteringConfig` required by runtime config and production flow choices.
* `ClusterResult` and runtime electrical summary if downstream flow uses it for correctness/reporting.

Likely test-only or white-box pressure:

* Direct testing of `SequenceSplitter::resolveSweepOffsets`.
* Direct synthetic/realtech strategy experiments that call `LinearOrderGenerator`, `SequenceSplitter`, `ConstraintEvaluator`, or `ClusteringEvaluator`.
* Benchmark strategy labels, candidate tables, and experiment ranking logic that re-derives details from source internals.
* Fast-clustering benchmark notes in `FastClusteringAlgorithm.md` that mix algorithm documentation with benchmark observations.

Recommended extraction direction:

* Keep runtime facades stable.
* Introduce test support adapters under `test/module/topology/.../support` for sweep explanation, strategy labels, and benchmark report formatting.
* Move or wrap direct white-box calls behind test support helpers where possible.
* Avoid making `FastClusteringInternal.hh` public to tests; it should remain source-private implementation glue unless a production module needs the contract.
* Consider moving shared electrical cluster evaluation out of `linear_clustering` naming if fast clustering depends on it conceptually. This is a source design cleanup, not test extraction.

### H-Tree Flow

Observed source surfaces:

* `HTreeBuilder::BuildResult` contains both runtime materialization state and detailed diagnostics:
  * candidate chars/frontier entries/feasible entries,
  * characterization grid fields,
  * depth exploration fields,
  * nested `DepthCandidateSummary`,
  * fallback diagnostics,
  * materialized inserted objects.
* Multiple test support files and synthesis tests inspect `DepthCandidateSummary` and detailed result vectors.

Likely production needs:

* Build success/failure.
* Materialized topology, inserted objects, root pins/insts.
* Best selected characterization result/pattern.
* Runtime fallback status if used by production reporting.

Likely test-only or over-exposed pressure:

* Full candidate/frontier vectors and detailed depth exploration summary are primarily useful for regression assertions, matrix reports, and visualization.
* Selection helper functions in test support use `DepthCandidateSummary` directly.

Recommended extraction direction:

* Treat this as a compatibility-sensitive area. Do not remove `BuildResult` fields until all in-repo consumers are mapped.
* Prefer adding a test support report adapter that consumes current `BuildResult` and centralizes detailed assertions.
* If production API changes are allowed, split `BuildResult` into a compact runtime result plus optional diagnostics object.

### Logger / Schema / Artifact Boundary

Observed source surfaces:

* `Schema.hh` explicitly describes runtime and test artifacts.
* Production source and test support both use `EmitArtifact`, `appendStandaloneArtifact`, `emitOrAppend*`, diagnostics, and scoped stages.
* `test/common/io/TestArtifactIO.cc` already centralizes per-test artifact handling but still calls source schema helpers.

Likely production needs:

* Runtime structured reports, diagnostics, and stage summaries.
* A general artifact record primitive may be useful for production-generated files.

Likely test-only pressure:

* Per-test artifact root behavior belongs in `test/common/io`, not source schema.
* The source schema docs/terminology should not frame the module as test-artifact-specific if the implementation is shared runtime reporting.

Recommended extraction direction:

* Keep source schema generic: reports, diagnostics, and artifact references.
* Keep per-test artifact policy in `test/common/io`.
* Rename comments/docs from "runtime and test artifacts" to a runtime-neutral "structured reports and generated artifact references" if no behavior change is required.

### Config / STA Adapter / Design Report APIs

Observed source report APIs:

* `Config::emitRuntimeConfigReport`.
* `STAAdapter::emitUnitWireRcReport` and `emitConfiguredUnitWireRcReport`.
* `Design::emitClockDistributionSummary`.
* `TopologyGen::reportLoadDistribution` and `reportRootToLeafLengths`.

Likely production needs:

* These report APIs are used by production characterization/topology flows to emit runtime diagnostics.

Likely test-only pressure:

* Tests may call report APIs to assert log/artifact behavior, but that alone does not make the APIs test-only.
* The main concern is whether report APIs should be public source methods or local flow-level helpers.

Recommended extraction direction:

* Do not move these solely because tests call them.
* Reclassify only if a report method has no production caller.
* If retained, keep names and comments production-focused.

## Proposed Cleanup Slices

1. Documentation and naming cleanup:
   * clarify source schema/report wording;
   * document source/test ownership boundary in spec if not already explicit enough.

2. Test support consolidation:
   * centralize linear/fast clustering strategy labels, sweep report formatting, benchmark scoring summaries, and artifact formatting under `test/module/topology/.../support` or `test/common/linear_clustering`.

3. Source API review:
   * audit all `source/*Internal.hh` includes from tests;
   * keep internals source-private unless they are real production cross-translation-unit contracts.

4. Result diagnostics split:
   * for `HTreeBuilder::BuildResult` and clustering result diagnostics, decide whether to keep detailed diagnostics inline for compatibility or split into a separate diagnostics/report object.

5. CMake boundary cleanup:
   * ensure test targets depend on precise source and test support targets;
   * avoid broad include directories or accidental public exposure where a target-private include is enough.

## Open Design Choice

The main unresolved choice is compatibility level:

* Conservative: keep current source public methods/fields source-compatible, move only obvious test-only formatting/benchmark helpers and clean comments/CMake visibility.
* Breaking in-repo cleanup: remove or split public fields/methods that are only needed by in-repo tests after updating those tests to use test support adapters.

