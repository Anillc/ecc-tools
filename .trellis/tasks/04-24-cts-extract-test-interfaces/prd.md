# Extract CTS Test Interfaces From Source Modules

## Goal

Separate CTS test-only interfaces, structures, benchmark helpers, artifact/report helpers, and observability scaffolding from production `source/` modules so CTS source code keeps a smaller runtime-facing API and tests depend on explicit test support layers instead of polluting production module boundaries.

## What I Already Know

* The request is about existing CTS modules that have accumulated too many interfaces and structures for tests.
* The relevant code is under `src/operation/iCTS/`, especially `source/module/topology`, `source/flow`, and the mirrored `test/` tree.
* Current project specs define three layers: `api/`, `source/`, and `test/`; tests may depend on source, but source must remain production implementation and must not depend on test.
* The test tree already has reusable support targets such as `test/common/types`, `test/common/io`, `test/common/visualization`, `test/common/linear_clustering`, and module-specific support folders.
* Current CTS topology source exposes or contains several areas that look partly test/benchmark-driven:
  * `source/module/topology/config/TopologyConfig.hh` contains many linear-clustering strategy knobs used heavily by benchmark/scenario tests.
  * `source/module/topology/linear_clustering/LinearClustering.cc` contains retained strategy exploration, strategy labels, candidate summaries, and run-status structures.
  * `source/module/topology/fast_clustering/FastClusteringInternal.hh` exposes many internal draft/helper structures and helper functions for cross-file implementation and likely direct unit testing.
  * `source/module/topology/clustering/Clustering.hh` exposes electrical summary/evaluation data on the production clustering result path.
  * `source/utils/logger/Schema.*` explicitly describes output as serving runtime and test artifacts.
* Current fast-clustering benchmark tests are gated by `ICTS_BUILD_SLOW_REALTECH_TESTS` and link production topology source plus test helper libraries.
* Follow-up preference: do not introduce extra `internal/` directories for `fast_clustering` and `linear_clustering`; source-private helper headers should remain flat beside their `.cc` files unless there is a stronger local reason.
* Follow-up cleanup target: inspect whether `linear_clustering` remains necessary at all. If production behavior no longer requires the linear clustering algorithm after fast clustering owns the active sink-clustering path and shared constraint logic is extracted, remove linear clustering source/test code entirely.
* Benchmark retention requirement: keep the fast clustering benchmark, but make it fast-only instead of comparing against linear clustering.
* Recent relevant commits include:
  * `4bf4eb85d feat: support fast clustering and CTS benchmark`
  * `d8d18730d feat: CTS optimizes linear clustering default strategy (based on simulation testing)`
  * `9c2c05664 refactor: decompose overweight code to improve the efficiency of ecc_dev_tools checks`

## Assumptions

* Production CTS behavior should remain unchanged unless the user explicitly wants to simplify runtime capabilities too.
* The first refactor should prefer moving test-only artifact/benchmark/analysis helpers to `test/` and tightening source headers/CMake visibility over changing algorithms.
* Configuration fields that are used by real production flows must stay in source, even if tests also exercise them.
* Source-internal helpers that are only public because tests need them should either become file-local/private source internals or move into a test support adapter that exercises production APIs.
* For this task, "source-internal" means not part of the production runtime facade, but it does not require a physical `internal/` subdirectory in modules that use a flat file layout.
* `linear_clustering` can be removed if remaining production references are only legacy facades, config naming, or reusable constraint/electrical logic that can live in a neutral shared module.

## Open Questions

* None at this point. Requirements are ready for final confirmation.

## Requirements

* Scope this task as a broad CTS source/test layering cleanup across topology, flow, logger/schema, config, and adjacent CTS modules where test scaffolding has leaked into source surfaces.
* Allow in-repo breaking cleanup: public source methods, fields, or headers may be deleted, moved, or split when they are only required by in-repo tests/benchmarks and all callers are updated in the same task.
* Apply the core boundary principle: if an interface is not required by source production code, do not keep it in `source/`; add or move the test-needed interface into `test/` instead.
* Test-only interfaces may use generic adapters, templates, or other design patterns when useful. They do not need to preserve the current CTS test support style if that style is not the best fit; the final test-side design should be unified, clear, and not force source to expose extra functionality.
* Keep source design minimal: source changes should simplify or preserve production behavior and avoid adding abstraction solely for tests.
* Identify CTS source interfaces and structures whose only consumers are tests/benchmarks.
* Move test-only logic into `src/operation/iCTS/test/...` support targets that mirror the source layout.
* Keep production CTS behavior compatible even when source-facing in-repo APIs are reshaped.
* Preserve existing tests and benchmark coverage after extraction.
* Update CMake target dependencies so test support links production source instead of source linking or exposing test support.
* Follow iCTS file naming, header, logging, and no-exception constraints.
* Keep `fast_clustering` and `linear_clustering` helper headers in a flat module directory layout; do not keep the newly introduced `internal/` folders.
* Evaluate and, when feasible, remove the `linear_clustering` module and all source/test code that only exists for that algorithm.
* Preserve only fast clustering benchmark coverage; remove linear-vs-fast benchmark comparison logic and linear benchmark metrics/artifacts.

## Acceptance Criteria

* [ ] No `source/` file depends on `test/` code.
* [ ] No source API remains solely because a test or benchmark needs direct access.
* [ ] Obvious test-only artifact/benchmark/scenario structures live under `test/`.
* [ ] Production headers expose only runtime-facing APIs or true cross-translation-unit implementation contracts.
* [ ] `fast_clustering/` and `linear_clustering/` do not contain extra `internal/` subdirectories introduced by this refactor.
* [ ] `fast_clustering` no longer links `icts_source_module_topology_linear_clustering`.
* [ ] If linear clustering is not production-required, `source/module/topology/linear_clustering` and `test/module/topology/linear_clustering` are removed from source/test builds.
* [ ] Fast clustering benchmark remains available as a fast-only benchmark.
* [ ] All in-repo callers are updated for any source API fields/methods moved, deleted, or split.
* [ ] Affected CTS tests still build and run.
* [ ] The final full `src/operation/iCTS` quality check is run during finish-work.

## Research References

* [`research/source-test-boundary-inventory.md`](research/source-test-boundary-inventory.md) — Local inventory of source surfaces consumed by tests and candidate cleanup slices.

## Definition of Done

* Tests added or updated where extraction changes compile/link boundaries.
* Build/lint/type checks pass for the affected CTS targets.
* CMake target links express the new ownership boundaries.
* Any new pattern worth preserving is recorded in `.trellis/spec/`.

## Out of Scope

* Rewriting the CTS clustering algorithms.
* Removing real runtime options that are required by production flows.
* Changing external non-iCTS modules unless required to preserve build compatibility.
* Changing generated artifacts, report formats, or benchmark semantics unless required by the layering cleanup.

## Decision (ADR-lite)

**Context**: The first scope decision was whether to do a narrow extraction, a topology-focused API cleanup, or a broader CTS source/test layering cleanup.

**Decision**: Use the broader scope: inspect and clean layering across CTS topology, flow, logger/schema, config, and adjacent modules where test-only scaffolding has leaked into source.

**Consequences**: The task needs a stronger inventory and phased implementation plan before coding. The implementation should still be split into reviewable slices so broad scope does not become an unbounded algorithm rewrite.

**Context**: The second scope decision was whether source public APIs must remain source-compatible during cleanup.

**Decision**: Allow in-repo breaking cleanup. Source public methods, fields, and headers may be removed, moved, or split when they are only required by in-repo tests/benchmarks, provided all repository callers are updated in the same change and production behavior remains compatible.

**Consequences**: The implementation can genuinely reduce source surface area instead of only wrapping tests. It also requires broader compile/test validation and careful caller inventory before each removal.

**Context**: The third scope decision was whether to implement one slice first or perform the broad cleanup directly, and what principle should govern source/test placement.

**Decision**: Proceed with the broad cleanup directly. Use the rule that interfaces not participating in source production code must not live in `source/`; tests that need extra access should add those interfaces under `test/` through support adapters, generic helpers, templates, or other suitable patterns. Existing test support style may be refactored if it is not the best fit, as long as the resulting test-side style is unified and clear.

**Consequences**: The implementation must inventory callers carefully and may touch multiple CTS areas in one task. The source side should become smaller and simpler, while test support may grow to preserve white-box coverage and benchmark/report workflows.

**Context**: A follow-up review found that the added `internal/` subdirectories made the fast/linear clustering layout deeper than desired, and that fast clustering may still be linked to linear clustering for fallback or shared helper access.

**Decision**: Use a flat helper-header layout inside `source/module/topology/{fast_clustering,linear_clustering}` while keeping those headers non-runtime-facade in intent. Separately, remove the fast-clustering dependency on the linear-clustering module if inspection shows fast clustering can own or localize the needed behavior without preserving a fallback path.

**Consequences**: The implementation should move helper headers back beside their `.cc` files, update all includes, remove empty `internal/` directories, and simplify CMake/test dependencies around fast clustering when the linear-clustering dependency is not production-required.

**Context**: The requirement was clarified again: if `linear_clustering` is not necessary, source and test code related to linear clustering should be removed entirely, while the benchmark should remain but cover only fast clustering.

**Decision**: Treat linear clustering as removable unless inspection finds a production-only behavior that cannot be handled by fast clustering plus neutral shared cluster constraint/electrical evaluation. Remove linear algorithm facades, targets, and tests when no such blocker exists. Keep fast benchmark output but drop the linear comparison leg.

**Consequences**: The topology API and clustering facade should stop exposing linear clustering entry points. `TopologyConfig` should retain only fields required by fast/shared cluster constraints or be renamed in a follow-up if broader API churn is acceptable. Test common helpers that are generic cluster artifact/geometry support should either be renamed to neutral clustering support or kept only if fast benchmark/test code still depends on them.

## Technical Approach

1. Inventory direct `test/` dependencies on source internals and classify each as production API, source-internal implementation contract, or test-only/benchmark-only interface.
2. For test-only/benchmark-only interfaces, add, extend, or refactor support helpers under `src/operation/iCTS/test/...`; use existing `test/common` and mirrored module support folders where they remain appropriate, but allow restructuring when it produces a better unified test style.
3. Update tests and benchmark code to consume the test support helpers instead of direct source internals where the direct source interface is not production-required.
4. Remove, move, or make private any source API that no source production code uses.
5. Keep production runtime behavior stable by preserving facade methods and required flow/report functionality.
6. Update CMake target dependencies so the new ownership is explicit and no source target gains a test dependency.
7. Keep clustering source helper headers flat in their module directories instead of using an `internal/` subdirectory.
8. Inspect fast clustering call paths for linear-clustering fallback or helper reuse; remove the module dependency and related tests/configuration when not required.
9. If no production blocker remains, delete the linear clustering source/test module and convert benchmarks to fast-only.

## Implementation Plan

* Slice 1: Caller inventory and source/test classification for topology, H-tree/synthesis, schema/report, config/adapter/report surfaces.
* Slice 2: Move topology clustering test/benchmark-only helpers into `test/module/topology` or `test/common/linear_clustering`; update tests and remove source-only-for-test surfaces.
* Slice 3: Split or wrap H-tree/synthesis detailed diagnostics that are only test/benchmark consumers, preserving runtime result behavior.
* Slice 4: Clarify schema/report boundaries so source provides generic runtime reporting while test artifact policy stays in `test/common/io`.
* Slice 5: CMake cleanup and verification of affected CTS build/test targets.
* Slice 6: Flatten fast/linear clustering helper headers out of `internal/` directories and update includes.
* Slice 7: Decouple fast clustering from linear clustering if feasible; update production CMake, facade behavior, and tests.
* Slice 8: Remove linear clustering source/test modules if no production blocker remains; keep fast-only benchmark coverage.

## Technical Notes

* Inspected project specs:
  * `.trellis/spec/backend/directory-structure.md`
  * `.trellis/spec/project-constraints.md`
  * `.trellis/spec/guides/code-reuse-thinking-guide.md`
* Inspected source/test files and targets:
  * `src/operation/iCTS/source/module/topology/CMakeLists.txt`
  * `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`
  * `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`
  * `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh`
  * `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.cc`
  * `src/operation/iCTS/source/module/topology/linear_clustering/LinearClusteringTypes.hh`
  * `src/operation/iCTS/source/module/topology/fast_clustering/FastClustering.hh`
  * `src/operation/iCTS/source/module/topology/fast_clustering/FastClusteringInternal.hh`
  * `src/operation/iCTS/source/module/topology/fast_clustering/CMakeLists.txt`
  * `src/operation/iCTS/test/README.md`
  * `src/operation/iCTS/test/module/topology/fast_clustering/realtech/CMakeLists.txt`
