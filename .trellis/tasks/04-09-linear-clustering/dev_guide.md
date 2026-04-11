# Linear Clustering Dev Guide

## Frozen Scope

- Public entry stays at `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`.
- Config contract lives in `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`.
- Main implementation lives in `src/operation/iCTS/source/module/topology/linear_clustering`.
- Internal debug state must preserve both `synthetic_root` and `routed_root`; public `ClusterResult` stays `clusters + centers`.
- `max_cap` legality must support `max_fanout + max_diameter + max_cap` together.
- Real-tech loading is encapsulated as a dedicated test target with hardcoded ICS55 path formulas; when required files are unavailable, that target falls back to synthetic validation instead of failing hard.
- This task delivers the linear clustering module, router/electrical support it depends on, and tests; it does not expand CTS top-level flow beyond the named interface boundary.

## File Plan

- `.../topology/linear_clustering/LinearClusteringTypes.hh`: internal enums, spans, metrics, trace, partition score, cache keys.
- `.../topology/linear_clustering/LinearOrderGenerator.hh/.cc`: `continuous_hilbert`, `discrete_hilbert`, `density_scaled_continuous_hilbert`, `density_scaled_discrete_hilbert`, deterministic `original_index` tie-breaks, zero-span guards.
- `.../topology/linear_clustering/ConstraintEvaluator.hh/.cc`: fast legality checks and exact legality checks; returns structured violation reasons and metrics.
- `.../topology/linear_clustering/ClusterElectricalEstimator.hh/.cc`: build cluster terminals, dispatch router, build `RCTree`, call timing engine, preserve dual-root trace.
- `.../topology/linear_clustering/ClusteringEvaluator.hh/.cc`: cluster-level and partition-level score breakdown; legal partitions only.
- `.../topology/linear_clustering/SequenceSplitter.hh/.cc`: forward / reverse / dual-ended greedy, offset sweep, shared cache usage.
- `.../topology/linear_clustering/LinearClustering.hh/.cc`: orchestrate order -> split -> eval -> materialize `ClusterResult`.
- `.../topology/config/TopologyConfig.hh`: add `LinearClusteringConfig`, order/split/router/root enums, evaluator knobs.
- `.../topology/clustering/Clustering.hh/.cc`: add public linear-clustering API and keep existing `biPartition` intact.
- `.../module/routing/Router.hh/.cc`: expose clock-aware `Flute/Salt` construction and reusable routed-evaluation seam for clustering.
- `.../module/routing/flute/FLUTERouter.hh/.cc`: add clock-aware export path.
- `.../module/routing/salt/SALTRouter.hh/.cc`: add clock-aware export path.
- `.../test/common/RealTechSetup.hh/.cc`: one-time ICS55 tech/design init helper using hardcoded path formulas with explicit validation and synthetic fallback signaling.
- `.../test/common/TestUtils.hh/.cc`: output path split, stats serialization, SVG fixes, adjacency-aware palette, larger points, dashed root spokes.
- `.../test/module/topology/LinearClusteringTest.cc`: synthetic regression, degenerate geometry, exhaustive small-sequence oracle, multi-distribution sweeps.
- `.../test/module/topology/LinearClusteringTechTest.cc`: real-tech ICS55 clustering sweeps and artifact export.
- `.../test/CMakeLists.txt` and `.../test/README.md`: register a dedicated real-tech target and document fallback behavior.

## Implementation Order

1. Contract layer
- Add `LinearClusteringConfig` and public API shape.
- Add `linear_clustering` target and wiring in topology CMake.

2. Core data and order stage
- Land `LinearClusteringTypes.hh`.
- Implement `LinearOrderGenerator` with deterministic fallback and diagnostics.

3. Legality and electrical stage
- Implement `ConstraintEvaluator` fast-path using fanout, exact L1 diameter, and cap lower bound.
- Extend `Router` / `FLUTERouter` / `SALTRouter` so all four router kinds can feed clock-aware `max_cap`.
- Implement `ClusterElectricalEstimator` exact path on `Pin* -> terminals -> tree -> RCTree -> TimingEngine`.

4. Search and scoring stage
- Implement `ClusteringEvaluator`.
- Implement `SequenceSplitter` forward / reverse / dual-ended greedy with shared cache.
- Implement `LinearClustering` orchestration and public result materialization.

5. Test and visualization stage
- Add synthetic correctness tests before real-tech tests.
- Add `RealTechSetup` and a dedicated ICS55 test target that tries hardcoded paths first and falls back to synthetic sweeps when assets are unavailable.
- Fix SVG writer and add cluster statistics outputs under `icts_test_output/linear_clustering`.

6. Validation and review stage
- Run path-scoped `ecc_dev_tools` on touched topology / routing / test paths.
- Run structure checks for public-header and CMake changes.
- Run full `src/operation/iCTS` regression check and clean in-scope findings before handoff.

## Review Gates

- No skipped offset-0 solution, no missing trailing-cluster score, no zero-span normalization bug.
- `max_cap` exact mode uses real routed tree metrics, not pairwise heuristics.
- `LinearOrderGenerator` remains geometry-only; STA/router/timing stay out of it.
- Sequence legality and partition scoring stay separated.
- Real-tech tests are isolated in their own target, use hardcoded ICS55 path formulas, and degrade to synthetic mode when those assets are unavailable.
