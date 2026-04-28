# Research: HTree / CharBuilder Debug Output

- Query: Research HTreeBuilder, CharBuilder, topology generation, and sink clustering log output for CTS log optimization. Focus on which tables are algorithm decisions vs debug detail, which repeated fields can be collapsed, and how large detail tables such as `Cluster Center vs H-Tree Leaf Distance Details` should be summarized or moved to artifacts.
- Scope: internal
- Date: 2026-04-27

## Findings

### Files Found

- `.trellis/tasks/04-27-cts-log-optimization/prd.md` - task requirements; calls out table-heavy output, inconsistent hierarchy, repeated fields, and large detail tables.
- `.trellis/tasks/04-27-cts-log-optimization/research/log-analysis.md` - prior CTS log analysis with observed table meanings and suspicious metrics.
- `.trellis/spec/backend/logging-guidelines.md` - logging policy: use schema/report helpers for `cts.log`, label fallbacks, keep dense summaries as titled schema tables, and emit artifacts for structured output.
- `.trellis/spec/project-constraints.md` - repository-wide iCTS constraints; confirms schema/report helpers are the expected `cts.log` path.
- `scripts/design/ics55_dev/result/cts/cts.log` - reference log under analysis; current file is 746 lines and the cluster distance details table spans `cts.log:342` through `cts.log:665`.
- `src/operation/iCTS/source/utils/logger/Schema.hh` - schema writer API includes tables, key-value tables, detail blocks, diagnostics, and artifact records.
- `src/operation/iCTS/source/utils/logger/Schema.cc` - concrete schema/stage behavior; `ScopedStage` always appends `outcome` and `elapsed_s` summaries, creating some repeated stage/result output.
- `src/operation/iCTS/source/module/topology/TopologyGen.cc` - emits topology load distribution, root-to-leaf path summary, and topology build stage summary.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - HTreeBuilder orchestration; topology generation, characterization flow, depth exploration, selection, materialization, and build summary are sequenced here.
- `src/operation/iCTS/source/flow/htree/HTreeLevelPlan.cc` - derives characterization grid fallback, requested level lengths, direct characterization length indices, and depth candidates.
- `src/operation/iCTS/source/flow/htree/HTreeCharacterizationFlow.cc` - emits HTreeBuilder characterization grid plan and characterization summary.
- `src/operation/iCTS/source/flow/htree/HTreeBuildSummary.cc` - emits selected topology/build summary with policy, candidate counts, selected metrics, and inserted object counts.
- `src/operation/iCTS/source/module/characterization/CharBuilderConfig.cc` - emits CharBuilder runtime configuration, initialization parameters, routing/wire RC, sweep grids, and sorted buffers.
- `src/operation/iCTS/source/module/characterization/CharBuilderBuild.cc` - emits per-wirelength sweep progress, observed sample bounds, and CharBuilder stage summary.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - owns sink clustering materialization and cluster-center to H-tree-leaf distance tables.
- `src/operation/iCTS/source/module/topology/clustering/Clustering.cc` - clustering facade; no schema tables currently emitted.
- `src/operation/iCTS/source/module/topology/fast_clustering/FastClustering.cc` - fast clustering emits console `LOG_INFO` only, not structured `cts.log` tables.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechArtifactAssertions.cc` - tests assert cluster distance summary/details are present in clustered mode and absent in non-clustered mode.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` - tests assert HTree/CharBuilder summary tables are present and detailed Pareto output is omitted from `cts.log`.
- `src/operation/iCTS/test/module/characterization/CharacterizationRealTechFallbackTest.cc` - tests assert CharBuilder initialization and observed-bound fields are present, with exact values parsed for overflow fields.

### Current Emission Pattern

- Schema tables are written through `schema::EmitTable` and `schema::EmitKeyValueTable`; both write to console and active schema report (`Schema.cc:421`, `Schema.cc:428`).
- `ScopedStage` emits a final `"<module> <stage> Summary"` key-value table with `outcome` and `elapsed_s` for every stage (`Schema.cc:400`). This is useful for timing, but it duplicates values already present in richer summary tables for CharBuilder and HTreeBuilder.
- `SchemaWriter` already has artifact support through `emitArtifact` and `appendStandaloneArtifact` (`Schema.hh:63`, `Schema.hh:77`; `Schema.cc:323`, `Schema.cc:353`). This is the natural hook for moving large debug detail tables out of the main log.
- HTree standalone smoke artifacts already use this artifact pattern for topology SVG, materialized SVG, Pareto SVG, and report log (`HTreeVisualizationSupport.cc:62`, `HTreeVisualizationSupport.cc:77`). The corresponding smoke test expects `details omitted from cts.log` and verifies detailed Pareto output is in `report.log`, not in `cts.log` (`HTreeBuilderRealTechSmokeTest.cc:160`).

### Algorithm Decision Tables To Keep In Main Log

- `HTreeBuilder Diagnostic` should stay in the main log. It is a user-visible fallback decision and is emitted at the decision point when runtime `wirelength_unit` is absent or collapsed (`HTreeLevelPlan.cc:157`, `HTreeLevelPlan.cc:166`).
- `HTreeBuilder Characterization Grid Plan` should stay, but should be compacted. It records algorithm-critical setup: grid source, requested level lengths, configured/effective wirelength unit, required covering iterations, direct-char cap, distinct bins, and fallback flags (`HTreeCharacterizationFlow.cc:102`, `HTreeCharacterizationFlow.cc:121`).
- `HTreeBuilder Build Summary` should stay. It is the main selected-topology decision table: selected levels/depth, topology pattern id, selection policy, frontier/candidate/feasible counts, materialized object counts, selected power/delay, selected root driver, load-cap distribution, and boundary fallback state (`HTreeBuildSummary.cc:51`, `HTreeBuildSummary.cc:114`).
- `TopologyGen Build H-tree topology ... Summary` should stay as a compact stage/decision summary because it reports nodes, depth, and leaf count for the generated topology (`TopologyGen.cc:172`).
- `CTS Flow Summary` and `CTS Flow Sink Groups` should stay in the main log because they summarize user-visible success/failure and sink-group handling (`FlowManager.cc:141`, `FlowManager.cc:153`).

### Context Tables To Keep But Collapse

- `TopologyGen Load Distribution Summary` is useful input context, not an algorithm decision. It reports load count, bbox, area, center, and median (`TopologyGen.cc:210`). Keep it, but group it under a topology/input section with the root-to-leaf path summary.
- `TopologyGen Root-To-Leaf Path Summary` is derived topology quality context, not a selected solution detail. It should remain as a short summary, preferably merged with topology build output: min/max/avg path length, valid/invalid leaf paths (`TopologyGen.cc:281`).
- `CharBuilder Runtime Configuration`, `CharBuilder Initialization Parameters`, `CharBuilder Sweep Grids`, and `CharBuilder Routing / Wire RC` should be collapsed into one "Characterization Setup" table. Today the same values are repeated across these tables: max slew, max cap, wirelength unit, wirelength iterations/points, routing layer, wire width, and unit RC (`CharBuilderConfig.cc:369`, `CharBuilderConfig.cc:460`, `CharBuilderConfig.cc:479`, `CharBuilderConfig.cc:481`).
- `HTreeBuilder Characterization Summary` should be collapsed with CharBuilder result output. It repeats wirelength unit, iterations, max slew, max cap, segment char count, and buffer pattern count that are already available from CharBuilder setup/build (`HTreeCharacterizationFlow.cc:156`).
- `CharBuilder Observed Sample Bounds` should remain as a main-log quality summary, but should be merged into a "Characterization Results" table with generated chars/patterns, overflow counts, max observed slew/cap, and overflow ratios (`CharBuilderBuild.cc:151`).
- `CharBuilder Sorted Buffers` is only four rows in the observed log, so it is not a size problem now. It is still mostly configuration/debug detail. Keep a compact `buffer_count` and candidate list in the main setup; move per-buffer cap/input-cap rows to a detail artifact if the configured buffer list grows.

### Debug Detail Tables To Summarize Or Move To Artifacts

- `Cluster Center vs H-Tree Leaf Distance Details` is debug detail, not an algorithm decision. It is generated after a successful clustered synthesis by walking `result.cluster_buffers`, resolving the leaf driver location, computing Manhattan distance, and appending one row per cluster (`ClockSynthesis.cc:249`, `ClockSynthesis.cc:286`). In the reference log it is 319 rows and about 324 physical log lines out of 746 total lines (`cts.log:342`).
- The main log should keep `Cluster Center vs H-Tree Leaf Distance Summary`, but add enough distribution context that users do not need the full row dump: p90, p95, p99, count over configured thresholds, and top-N worst clusters (`ClockSynthesis.cc:315` currently emits only count/min/max/mean/median).
- The full cluster distance details should move to a generated artifact, preferably CSV for post-processing:
  - Suggested artifact name: `cluster_leaf_distance.csv` or `cluster_leaf_distance_details.csv`.
  - Suggested columns: `cluster_index,sink_count,cluster_center_x,cluster_center_y,htree_leaf_x,htree_leaf_y,manhattan_distance_dbu`.
  - Main `cts.log` should emit a `Generated Artifact` entry pointing to the file using schema artifact helpers (`Schema.cc:323`).
- If a text artifact is preferred over CSV, use `SchemaWriter::appendStandaloneTable` or a dedicated `report.log` section and keep only an artifact pointer in `cts.log` (`Schema.cc:335`, `Schema.cc:353`).
- `CharBuilder Sweep Progress` is currently only three rows in the reference log (`cts.log:231`), so it can remain during the first cleanup. However, it is per-wirelength debug/progress detail and should be capped or moved to an artifact if `wirelength_iterations` grows. The main log can summarize totals and worst overflow row while preserving full per-length rows in an artifact (`CharBuilderBuild.cc:88`, `CharBuilderBuild.cc:143`).
- Fast clustering currently only logs `Fast clustering done: loads=..., clusters=..., strategy=recursive_spatial_bisect` to console (`FastClustering.cc:78`). Main `cts.log` lacks a structured sink clustering decision summary. Add a compact "Sink Clustering Summary" before HTreeBuilder with input sink count, cluster count, fanout/cap constraints, selected cluster buffer cell master, min/max/mean/median sink count, and any legalization/fallback status. This is an algorithm decision summary and should not be confused with the row-by-row leaf-distance debug artifact.

### Repeated Fields That Can Be Collapsed

- `wirelength_unit` appears as runtime config, HTree grid configured/effective value, CharBuilder runtime configuration, CharBuilder initialization parameter, and HTreeBuilder characterization summary (`Config.cc:312`, `HTreeCharacterizationFlow.cc:106`, `CharBuilderConfig.cc:376`, `CharBuilderConfig.cc:465`, `HTreeCharacterizationFlow.cc:157`). Keep runtime requested value once and effective characterization value once.
- `wirelength_iterations` appears in runtime config, grid plan, CharBuilder runtime config, CharBuilder initialization parameters, sweep grids, and HTreeBuilder characterization summary (`HTreeCharacterizationFlow.cc:110`, `CharBuilderConfig.cc:378`, `CharBuilderConfig.cc:467`, `CharBuilderConfig.cc:481`, `HTreeCharacterizationFlow.cc:161`). Collapse into requested cap, effective direct points, and reason/source.
- `max_slew_ns` and `max_cap_pf` appear in runtime config, CharBuilder runtime configuration, CharBuilder initialization parameters, HTreeBuilder characterization summary, and observed bounds (`CharBuilderConfig.cc:374`, `CharBuilderConfig.cc:460`, `HTreeCharacterizationFlow.cc:162`, `CharBuilderBuild.cc:154`). Keep requested limits in setup and observed maxima in results.
- `routing_layer`, `wire_width_um`, and unit RC appear in both runtime routing/wire RC and CharBuilder routing/wire RC (`CharBuilderConfig.cc:383`, `CharBuilderConfig.cc:479`). Keep one effective routing/RC report and make later tables reference the same effective routing layer.
- `segment_chars`, `patterns`/`buffer_patterns`, and overflow counts appear in CharBuilder sweep progress, observed sample bounds, CharBuilder build summary, and HTreeBuilder characterization summary (`CharBuilderBuild.cc:151`, `CharBuilderBuild.cc:165`, `HTreeCharacterizationFlow.cc:164`). Keep totals in one "Characterization Results" table; keep per-wirelength rows only as detail.
- `inserted_insts` and `inserted_nets` appear in HTreeBuilder build summary and HTreeBuilder stage summary (`HTreeBuildSummary.cc:76`, `HTreeBuilder.cc:267`). Keep the richer HTreeBuilder build summary as the algorithm table; reserve stage summary for elapsed/outcome or move stage timing into a consolidated runtime section.
- `levels`, `depth`, `selected_depth`, and `max_level_of_clock_tree` are easy to confuse because topology depth, H-tree selected depth, selected level count, and evaluation level metric use different meanings (`TopologyGen.cc:172`, `HTreeBuildSummary.cc:51`, `ClockTreeEvaluator.cc:226`). Rename or annotate units/definitions in the main log so users can distinguish topology depth from selected H-tree levels and final clock-tree path depth.

### Suggested Main-Log Hierarchy

1. Run context and runtime configuration.
2. Input clock/sink summary.
3. Sink clustering decision summary, only when clustering is enabled.
4. Topology summary: load distribution, topology nodes/depth/leaves, root-to-leaf path distribution.
5. Characterization setup and results: effective grid, limits, wire RC, generated chars/patterns, overflow ratios.
6. HTreeBuilder selected solution: selection policy, selected depth/levels, candidates/feasible counts, inserted H-tree buffers/nets, selected root driver, power/delay, boundary fallback.
7. Cluster-to-leaf distance summary: distribution percentiles and top-N outliers; full details as artifact.
8. Flow/evaluation summary and runtime summary.
9. Generated artifacts.

### Test Contracts To Update If Implementation Follows This Research

- `ClockSynthesisRealTechArtifactAssertions.cc:74` and `ClockSynthesisRealTechArtifactAssertions.cc:75` currently require both cluster distance summary and details in `cts.log`. If details move to an artifact, update clustered-mode assertions to require summary, percentiles/top-N fields, and a generated artifact path; update non-clustered assertions to keep both summary and artifact absent (`ClockSynthesisRealTechArtifactAssertions.cc:96`).
- `HTreeBuilderRealTechSmokeTest.cc:147` through `HTreeBuilderRealTechSmokeTest.cc:160` assert exact HTree/CharBuilder table names. If Characterization tables are renamed/merged, update these string contracts while preserving the important checks: build summary present, units present, selected metric fields present, and detailed Pareto output absent from `cts.log`.
- `CharacterizationRealTechFallbackTest.cc:119` and `CharacterizationRealTechFallbackTest.cc:328` assert CharBuilder table names and field names. If `CharBuilder Observed Sample Bounds` is merged into `Characterization Results`, keep parsable field names for `output_slew_overflow_samples`, `driven_cap_overflow_samples`, `max_observed_output_slew_idx`, and `max_observed_driven_cap_idx` or update the parser/test together.
- `ClockSynthesisTest.cc:286` asserts runtime config output includes `enable_sink_clustering` and `htree_topology_tolerance`. Keep these fields in runtime configuration even if clustering gets its own decision summary.

## Caveats / Not Found

- No code was edited. This research only inspected code, tests, specs, and the existing `cts.log`.
- I did not rerun the full iCTS flow or test suite. Line counts and row counts are from the checked-in `scripts/design/ics55_dev/result/cts/cts.log`.
- No external references were needed; findings are based on internal code and Trellis specs.
- I did not find a current structured `cts.log` sink clustering summary. Fast clustering currently writes a console `LOG_INFO` line, while ClockSynthesis writes only the post-HTree cluster-to-leaf distance summary/details.
- I did not find a production CTS artifact path for cluster distance details. Existing artifact helpers are available in `SchemaWriter`, and HTree test artifact code demonstrates the pattern, but cluster distance details currently write directly to the active `cts.log`.
