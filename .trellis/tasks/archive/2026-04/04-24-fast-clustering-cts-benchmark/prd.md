# Fast Clustering CTS Benchmark

## Goal

Create an independent `fast_clustering` topology module for iCTS that matches `linear_clustering`'s public input/output contract and design constraints while using a new runtime-oriented clustering algorithm. Add a CTS clustering benchmark over 20 placement-stage ICS55 designs to compare fast and linear clustering on runtime and clustering quality, then iterate on the fast implementation until it is both complete and competitively better.

## What I Already Know

* The new module should live under `src/operation/iCTS/source/module/topology/fast_clustering`.
* The existing reference module lives under `src/operation/iCTS/source/module/topology/linear_clustering`.
* The public linear interface is `LinearClustering::buildElectricalBaseConfig(std::size_t max_fanout, double max_cap)`, `LinearClustering::runDefault(const std::vector<Pin*>&, const LinearClusteringConfig&)`, and `LinearClustering::run(const std::vector<Pin*>&, const LinearClusteringConfig&)`.
* The shared output type is `ClusterResult`, containing `clusters`, `centers`, and `electrical_summaries`.
* The shared clustering constraints include max fanout, max diameter, max capacitance, root policy, routing kind, scoring strategy, exact-cap behavior, routing layer, and wire width through `LinearClusteringConfig`.
* CTS default design constraints for ICS55 include `max_fanout=32`, `max_cap=0.05`, routing layers `[4, 5]`, and buffer types `BUFX8H7L`, `BUFX12H7L`, `BUFX16H7L`, `BUFX20H7L`.
* ICS55 technology inputs are derivable from `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh`: `TECH_LEF`, standard-cell LEFs, Liberty files, and SDC under that workspace, with PDK root under `~/pdk/`.
* The benchmark source designs must come only from `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008`, using placement-stage `*.def` or `*.def.gz` files and matching Verilog files after verifying placement output exists.
* Existing iCTS tests already have real-tech setup helpers, artifact/logging helpers, and linear clustering real-tech tests that can be reused or extended.

## Assumptions

* `fast_clustering` should expose a separate `FastClustering` facade with the same call shape as `LinearClustering`, while sharing `LinearClusteringConfig` and `ClusterResult` for direct comparison.
* The benchmark should be a CTest/GTest target in the iCTS test tree, with generated artifacts written under the existing test artifact/output conventions.
* The benchmark should be deterministic: fixed case selection, stable ordering, and stable score formulas.
* "Performance优异" should be evaluated as a dual objective: fast clustering must improve both aggregate runtime and clustering-quality score because the current `linear_clustering` baseline is not considered strong enough to justify quality trade-offs.
* The benchmark may skip unavailable external cases with explicit logging only if the external path or placement artifacts are missing on the machine running tests.

## Open Questions

* None.

## Requirements

* Add an independent `fast_clustering` module under `src/operation/iCTS/source/module/topology`.
* Match linear clustering's public input/output contract: `std::vector<Pin*>` input, `LinearClusteringConfig` configuration, `ClusterResult` output.
* Add topology facade accessors such as `Clustering::fastClustering` and `TopologyGen::fastClustering` for benchmark and future CTS integration.
* Switch the default CTS sink-clustering flow in `ClockSynthesis` to use `fast_clustering` after benchmark validation.
* Preserve design-constraint behavior against linear clustering: fanout, diameter, capacitance, routing/root/scoring configuration must be honored consistently.
* Use an algorithm that considers both global structure and local locality, with runtime as a primary optimization target.
* Compare `fast_clustering` against `linear_clustering` in a dedicated CTS clustering benchmark.
* Discover 20 benchmark cases from `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008` using only placement-stage DEF/Verilog files.
* Use ICS55 technology information from `~/pdk/` and `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh`; use CTS constraints from `cts_default_config.json`.
* Ensure the benchmark first logs each case's unit/count statistics to `cts.log`.
* Save ranking and per-case comparison data to CSV.
* Save SVG visualizations of the clustering structures for every benchmark case, reusing the repository's existing cluster SVG style.
* Each case visualization must place linear clustering and fast clustering in one SVG as left/right subplots so the cluster topology can be compared directly.
* Summarize benchmark statistics and final analysis in `cts.log`.
* Add an algorithm document for `fast_clustering` that captures the implemented pipeline, constraint semantics, complexity, benchmark observations, and follow-up optimization directions.
* Iterate on implementation if benchmark analysis reveals issues, regressions, or clear optimization opportunities; optimization must target both runtime and score, not one at the expense of the other.
* Do not run `ecc_dev_tools` during development; only after self-validation, run full iCTS checks and ensure there are no in-scope findings.

## Acceptance Criteria

* [ ] `fast_clustering` builds as an independent topology submodule.
* [ ] `FastClustering` exposes the same effective API shape and output semantics as `LinearClustering`.
* [ ] Fast clustering never returns clusters that violate configured max fanout, max diameter, or max capacitance when linear clustering can satisfy the same case.
* [ ] Benchmark discovers exactly 20 valid placement-stage cases when the external benchmark directory is available.
* [ ] Benchmark logs unit/count statistics for every selected case to `cts.log` before algorithm comparisons.
* [ ] Benchmark writes per-case and ranking CSV output for linear vs fast clustering.
* [ ] Benchmark writes one clustering-structure SVG per benchmark case, with linear and fast shown as side-by-side subplots in the same file.
* [ ] Benchmark records aggregate runtime and quality analysis in `cts.log`.
* [ ] Benchmark records cluster-level total routing-cap proxy variance for linear and fast clustering, and reports the aggregate comparison in CSV and `cts.log`.
* [ ] `fast_clustering` has a code-adjacent algorithm document that matches the current implementation.
* [ ] Fast clustering beats linear clustering on aggregate runtime.
* [ ] Fast clustering beats linear clustering on aggregate quality score.
* [ ] Fast clustering reduces aggregate cluster-to-cluster total routing-cap proxy variance compared with linear clustering.
* [ ] Fast clustering has no additional fanout, diameter, capacitance, or routing-failure violations compared with linear clustering.
* [ ] Default CTS source flow uses `TopologyGen::defaultFastClustering` for sink clustering.
* [ ] iCTS in-scope checks pass after implementation and benchmark validation.

## Definition of Done

* Tests added or updated for the new module and benchmark.
* Build, lint/type-check, and relevant iCTS tests pass.
* Benchmark artifacts include `cts.log`, CSV ranking outputs, and per-case clustering-structure SVG visualization output.
* Algorithm documentation is available beside the module source and reflects the validated implementation.
* Final report includes algorithm summary, benchmark results, observed trade-offs, and any remaining limitations.
* Spec or project notes are updated if new reusable conventions are learned.

## Technical Approach

Candidate implementation direction: a two-level spatial clustering algorithm. First build a deterministic spatial grid/order to account for macro-scale sink distribution; then use local nearest packing and bounded split/merge repair under fanout/diameter/cap constraints. Avoid expensive multi-strategy sweeps and repeated exact routing where conservative lower-bound filters are sufficient, but still perform exact electrical validation before finalizing clusters when configured.

Benchmark scoring should treat legality as a hard gate, then compare runtime and quality separately. A combined ranking can be reported, but fast clustering is not accepted unless both runtime and quality score improve in aggregate.

Integration decision: implement `fast_clustering` as an independent topology submodule, expose it through `Clustering` / `TopologyGen` same-shape facade APIs, then switch `ClockSynthesis` sink clustering to the fast facade once the benchmark passes runtime, score, legality, and routing-cap variance acceptance.

Benchmark case selection should iterate direct children of `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008` in lexicographic order, accept only complete `place_dreamplace/output/*_place.def(.gz)` plus sibling `*_place.v` pairs, verify DEF `DESIGN` and Verilog top module names match, and take the first 20 accepted cases. The inspected tree currently has 37 valid placement pairs, so the benchmark should require exactly 20 when the root is available.

## Decision (ADR-lite)

**Context**: The benchmark needs a direct way to compare linear and fast clustering. After the fast implementation passed runtime, score, legality, and routing-cap variance acceptance, the default CTS path can safely use it.

**Decision**: Add independent `FastClustering` module and facade methods on `Clustering` / `TopologyGen`; switch `ClockSynthesis` default sink clustering from `TopologyGen::defaultLinearClustering` to `TopologyGen::defaultFastClustering`.

**Consequences**: The default source flow now receives fast clustering's runtime and routing-cap variance improvements, while explicit linear facade calls remain available for regression tests and benchmark comparison.

The benchmark should reuse the existing real-tech setup path where possible, but add a multi-design case discovery layer specific to the 20260422 benchmark tree. The case selector should only accept directories with placement output under `place_dreamplace/output` and matching Verilog under the same placement step.

## Out of Scope

* Changing the existing `linear_clustering` algorithm except where shared interfaces or benchmark plumbing require strictly scoped compatibility adjustments.
* Using non-placement benchmark outputs such as CTS, route, legalization, filler, DRC, or origin data as benchmark inputs.
* Referencing external benchmark configuration files outside the explicitly allowed DEF/Verilog inputs and ICS55 technology/config sources.
* Running `ecc_dev_tools` before local implementation validation is complete.

## Technical Notes

* Public API reference: `src/operation/iCTS/source/module/topology/linear_clustering/LinearClustering.hh`.
* Shared config reference: `src/operation/iCTS/source/module/topology/config/TopologyConfig.hh`.
* Shared output/reference utilities: `src/operation/iCTS/source/module/topology/clustering/Clustering.hh`.
* Existing real-tech tests: `src/operation/iCTS/test/module/topology/linear_clustering/realtech`.
* Existing real-tech asset loader: `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc`.
* ICS55 flow/config sources: `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/run_iEDA.sh` and `/home/liweiguo/project/ecc-tools-dev/scripts/design/ics55_dev/iEDA_config/cts_default_config.json`.
* Benchmark source root: `/nfs/share/home/huangzhipeng/code-new/ecc-benchmark/runs/20260422_125008`.

## Research References

* [`research/fast-clustering-algorithm.md`](research/fast-clustering-algorithm.md) — linear contract, constraint semantics, runtime hotspots, and recommended grid-seeded local packing algorithm with exact validation/repair.
* [`research/cts-benchmark-integration.md`](research/cts-benchmark-integration.md) — 20 placement-case selector, allowed ICS55 tech/config inputs, test target location, artifact conventions, and non-placement guardrails.
