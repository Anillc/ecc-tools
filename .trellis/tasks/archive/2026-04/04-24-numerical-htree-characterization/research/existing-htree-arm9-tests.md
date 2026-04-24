# Research: existing H-tree builder and ARM9 tests

- Query: Existing iCTS H-tree builder and ARM9 realtech/full-design comparison tests, focusing on HTreeBuilder inputs/outputs, selected segment pattern reporting, QoR metrics, runtime/report artifacts, and ARM9 full-sink test enable/skip behavior.
- Scope: internal
- Date: 2026-04-24

## Findings

### Files Found

- `.trellis/tasks/04-24-numerical-htree-characterization/prd.md` - Task requirements for a numerical H-tree alternative and ARM9 full-design/full-sink comparison.
- `.trellis/spec/backend/index.md` - Backend spec index for `src/operation/iCTS/` and relevant authority docs.
- `.trellis/spec/project-constraints.md` - Repository-wide iCTS constraints for logging, reports, terminology, and validation.
- `.trellis/spec/backend/logging-guidelines.md` - Runtime logging and structured schema/report output rules.
- `.trellis/spec/backend/quality-guidelines.md` - Naming, include, dependency, and validation rules for iCTS backend code.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh` - Public HTreeBuilder options and result contract.
- `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc` - Native H-tree synthesis pipeline, candidate-depth evaluation, segment-pattern selection, materialization, and summary reporting.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh` - ClockSynthesis result contract wrapping optional sink clustering plus HTreeBuilder output.
- `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc` - ClockSynthesis orchestration, non-clustered passthrough, clustered buffers, and source-to-root net materialization.
- `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc` - Real-tech HTreeBuilder smoke tests plus env-gated ARM9 full-sink matrices.
- `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.hh` - HTree artifact path contract.
- `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc` - HTree SVG, Pareto plot, and `report.log` artifact generation.
- `src/operation/iCTS/test/flow/htree/CMakeLists.txt` - HTree real-tech test target and slow-regression target wiring.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc` - Real-tech ClockSynthesis clustered/non-clustered tests plus non-env-gated ARM9 full-sink non-clustered matrix.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.hh` - ClockSynthesis artifact path contract.
- `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.cc` - ClockSynthesis SVG and `report.log` artifact generation.
- `src/operation/iCTS/test/flow/synthesis/CMakeLists.txt` - ClockSynthesis real-tech test target wiring.
- `src/operation/iCTS/test/CMakeLists.txt` - Common iCTS test executable helper, `REALTECH` linkage, and `ICTS_BUILD_SLOW_REALTECH_TESTS` option.
- `src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.hh` - Real-tech setup state contract and mode enum.
- `src/operation/iCTS/test/common/realtech/support/RealTechSetupSupport.cc` - Shared facade for cached real-tech setup.
- `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc` - ARM9 DEF/Verilog path probing and real-tech versus synthetic fallback selection.
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.hh` - Shared real-tech characterization defaults, session API, and scenario-log helper.
- `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc` - RealTechCharSession preparation, config restore, and `characterization/realtech/<scenario>` log writing.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.hh` - Char-only STA facade surface used by CharBuilder.
- `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc` - Full-design timing preparation and SDC loading behavior.

### HTreeBuilder Inputs And Outputs

- HTreeBuilder public inputs are `std::vector<Pin*> loads` plus optional `BuildOptions`: `force_branch_buffer`, `min_top_input_slew_ns`, `target_depth`, and `depth_explore_window` in `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:45`.
- Public entry points are `build(loads)` and `build(loads, options)` in `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:139`; the overload without options delegates to default options in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2296`.
- BuildResult carries both decision data and materialized CTS objects: success/failure, topology, level plans, `best_char`, `best_pattern`, candidate/feasible frontier entries, char-grid metrics, option state, selected depth, depth candidate summaries, boundary fallback state, pruned leaf buffer count, owned storage, inserted object vectors, and root pins in `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:67`.
- Each `DepthCandidateSummary` records depth, leaf count, success/selection flags, H-tree external load group count, cap min/max/mean/median, frontier/candidate/feasible counts, boundary fallback, and selected delay/power in `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:69`.
- The build pipeline rejects empty loads, creates a topology with `TopologyGen::build(loads)`, rejects topology without H-tree levels, resolves/adapts the characterization grid, runs `CharBuilder::init/build`, records char-grid metadata, derives level plans, resolves depth candidates, synthesizes required segment frontier sets, evaluates each candidate depth, globally selects the best entry, materializes CTS objects, emits an HTreeBuilder Build Summary, and finishes a schema stage in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2301`.
- `force_branch_buffer` defaults from `CONFIG_INST.is_force_branch_buffer()` unless provided, and `min_top_input_slew_ns` is converted to a covering slew lattice index in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:903`.
- Depth exploration uses `options.target_depth` as a single clamped candidate when present; otherwise it evaluates descending depths from max depth over `options.depth_explore_window` or `CONFIG_INST.get_htree_depth_explore_window()` in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:1289`.
- Segment frontiers are selected as branch-buffered only when `force_branch_buffer` is enabled; otherwise all Pareto frontier segment entries are eligible in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:1661`.
- Global selection Pareto-filters candidate entries by delay/power and chooses the lower median in power order in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:1863`; the summary labels this as `global_frontier_pareto_power_median` unless it uses boundary fallback in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2596`.
- Materialization reads selected per-level segment pattern IDs from `best_pattern`, resolves each `BufferingPattern`, creates root input/output pins, materializes per-edge buffer chains and nets, connects the root output to entry loads, and prunes redundant leaf single-load buffers in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2203`.

### Selected Segment Pattern Reporting

- The selected topology pattern is materialized from the selected char entry, then each `LevelPlan` receives `segment_pattern_id`, `selected_has_any_buffer`, `selected_leaf_buffer_cell_master`, `selected_has_terminal_branch_buffer`, and `selected_terminal_cell_master` from the resolved segment pattern in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2559`.
- `HTreeVisualizationSupport::BuildReport` writes per-level selected segment pattern IDs with requested/aligned lengths and leaf-level marker: `level[index] requested_dbu=..., requested_um=..., aligned_idx=..., aligned_um=..., is_leaf=..., segment_pattern_id=...` in `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:947`.
- The current HTree artifact report does not print per-level selected buffer masters, even though BuildResult has them. It only prints buffer master aggregate counts separately in `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:928`.
- ClockSynthesis wraps `HTreeBuilder::BuildResult` but its `report.log` currently reports only high-level synthesis counts and artifact paths, not H-tree per-level segment pattern IDs in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.cc:632`.
- Tests assert that `best_pattern->get_level_segment_pattern_ids().size()` equals the selected H-tree level count in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:422`, so per-level pattern identity is already exposed for comparison tests.

### QoR Metrics And Selection Data

- Native H-tree QoR terms exposed in BuildResult include `best_char` delay/power, candidate/feasible chars, frontier entries, char wire-length unit/iterations, max slew/cap, slew/cap steps, depth summaries, selected-depth cap distribution, and boundary fallback state in `src/operation/iCTS/source/flow/htree/HTreeBuilder.hh:97`.
- The HTreeBuilder schema Build Summary emits selected levels, depth candidate count, selected depth, selected topology pattern ID, selection policy, frontier/candidate/feasible counts, inserted inst/net counts, pruned leaf single-load buffers, selected power, selected delay, selected cap/slew indices, force-branch-buffer state, top input slew covering index, selected H-tree load group count, cap min/max/mean/median, and boundary fallback diagnostics in `src/operation/iCTS/source/flow/htree/HTreeBuilder.cc:2588`.
- Real-tech HTree smoke tests explicitly verify cts.log contains CharBuilder runtime/configuration details, HTreeBuilder Build Summary, selected `leaf_load_cap_idx`, H-tree load cap distribution metrics, power units, sweep progress, and a details-omitted pointer to report.log in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:453`.
- HTree `report.log` emits char grid details, char limits, candidate/feasible/frontier/Pareto counts, `delay_power_selection_summary`, selected pattern ID, selected delay/power, selected/pareto solution rows, per-level segment pattern IDs, and artifact paths in `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:882`.
- ClockSynthesis result adds wrapper metrics: clustering enabled flag, optional `ClusterResult`, cluster buffer metadata, inserted objects, and `source_to_root_net` in `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.hh:62`.
- ClockSynthesis derives HTreeBuilder `min_top_input_slew_ns` from half of `CONFIG_INST.get_max_buf_tran()` when configured in `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:415`.
- ClockSynthesis clustered mode reports cluster center versus H-tree leaf distance summary/detail tables into cts.log, including min/max/mean/median distance in DBU in `src/operation/iCTS/source/flow/synthesis/ClockSynthesis.cc:351`; tests assert these tables are present only for clustered artifacts in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc:390`.

### Runtime And Report Artifacts

- HTree smoke artifacts are prepared under the common test output directory as `flow/htree/<case_name>/`, with `cts.log`, `topology.svg`, `materialized_htree.svg`, `pareto_delay_power.svg`, and `report.log` in `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:969`.
- `WriteHTreeArtifacts` emits schema artifact references for HTree topology SVG, materialized SVG, delay-power Pareto SVG, and HTree report in `src/operation/iCTS/test/flow/htree/HTreeVisualizationSupport.cc:985`.
- HTree real-tech tests assert all HTree artifacts exist after writing in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:317`.
- HTree ARM9 matrix rows record `iter`, `step`, `runtime_s`, success, frontier count, selected depth, best pattern ID, best delay, best power, char wire-length unit/iterations, char-grid adapted flag, boundary fallback flag, and failure reason in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:125`.
- HTree ARM9 matrix execution measures runtime around `HTreeBuilder::build(selected_clock->loads)` and enforces a 600-second budget per matrix case in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:523`.
- HTree ARM9 matrix reports are written by `realtech_support::WriteScenarioLog(..., "matrix_report.txt", ...)` under `characterization/realtech/<scenario>/matrix_report.txt` in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:562` and `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:438`.
- Each HTree ARM9 matrix case also prepares a RealTechCharSession, whose `cts.log` goes under `characterization/realtech/<case_scenario>/cts.log` in `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:396`.
- ClockSynthesis artifacts are prepared under `flow/synthesis/<case_name>/`, with `cts.log`, `synthesis_topology.svg`, and `report.log` in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.cc:657`.
- ClockSynthesis `report.log` includes scenario, clock, success, sink clustering flag, input sink count, H-tree node count, inserted inst/net counts, cluster buffer count, sink-level edge count, source pin, source-to-root net, artifact list, and output dir in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisVisualizationSupport.cc:632`.
- ClockSynthesis ARM9 matrix rows use the same runtime/QoR columns as HTree, but write to `flow/synthesis/clock_synthesis_arm9_full_sink_matrix/matrix_report.txt` via `WriteClockSynthesisMatrixReport` in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc:117`.

### ARM9 Realtech / Full-Sink Enable And Skip Behavior

- Real-tech tests are normal CTest targets added through `icts_add_test_executable`; `REALTECH` targets link `icts_test_realtech_base`, which links `idm`, in `src/operation/iCTS/test/CMakeLists.txt:20` and `src/operation/iCTS/test/CMakeLists.txt:43`.
- Slow real-tech tests are controlled at build time by CMake option `ICTS_BUILD_SLOW_REALTECH_TESTS` in `src/operation/iCTS/test/CMakeLists.txt:32`.
- The default HTree real-tech target sets `ICTS_ENABLE_SLOW_REALTECH_REGRESSION=0`; when `ICTS_BUILD_SLOW_REALTECH_TESTS` is ON, an additional target compiles the same source with `ICTS_ENABLE_SLOW_REALTECH_REGRESSION=1` in `src/operation/iCTS/test/flow/htree/CMakeLists.txt:24`.
- The HTree ARM9 full-sink matrices are not controlled by `ICTS_ENABLE_SLOW_REALTECH_REGRESSION`; they are present in the regular HTree real-tech binary but skip unless runtime env flags are set.
- `HTreeBuilderRealTechSmokeTest.Arm9FullSinkExperimentMatrix` requires `ICTS_RUN_ARM9_HTREE_MATRIX` to be truthy; otherwise it `GTEST_SKIP`s with an instruction to set the env var in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:480`.
- `HTreeBuilderRealTechSmokeTest.Arm9FullSinkExperimentMatrixAutoWireLengthUnit` requires `ICTS_RUN_ARM9_HTREE_MATRIX_AUTO_UNIT` to be truthy; otherwise it skips in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:567`.
- Both HTree ARM9 matrices then require `EnsureRealTechSetup()` to return `RealTechMode::kRealTech` and `setup_succeeded=true`; otherwise they skip with the setup summary in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:487` and `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:575`.
- Both HTree ARM9 matrices select the largest real clock with no sampling limit by passing `std::numeric_limits<std::size_t>::max()` to `SelectLargestRealClockLoads`, and skip if no clock exposes at least two CTS sink pins in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:493` and `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:581`.
- HTree ARM9 matrices sweep wire-length iterations `{2,3,4,5}` and slew/cap steps `{10,15}` for eight total cases per test in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:87`.
- The auto-unit HTree ARM9 matrix calls `char_session.prepare(..., omit_wire_length_unit=true)` and asserts the effective char wire-length unit is positive and `char_wire_length_iterations <= wire_length_iterations` in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:600`.
- The non-auto HTree ARM9 matrix calls `char_session.prepare(..., omit_wire_length_unit=false)` and then overwrites `CONFIG_INST` wire-length iterations and slew/cap steps for each case in `src/operation/iCTS/test/flow/htree/HTreeBuilderRealTechSmokeTest.cc:512`.
- `ClockSynthesisRealTechSmokeTest.Arm9FullSinkNonClusteredExperimentMatrix` is not env-gated in current code. It skips only when real-tech setup is unavailable, no DEF-derived clock net exposes source plus at least two sinks, or characterization preparation fails in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc:722`.
- The ClockSynthesis ARM9 matrix uses full sinks by selecting the largest real clock with max count `std::numeric_limits<std::size_t>::max()`, disables sink clustering through `BuildOptions`, measures runtime around `ClockSynthesis::build(source, sinks, options)`, asserts non-clustered output, and writes `matrix_report.txt` in `src/operation/iCTS/test/flow/synthesis/ClockSynthesisRealTechSmokeTest.cc:730`.
- Real-tech setup probes `ICTS_REALTECH_WORKSPACE`, `ICTS_REALTECH_PDK_DIR`, legacy `PDK_DIR`, default workspace paths, and ARM9 DEF/Verilog candidates such as `result/arm9_place.def(.gz)` and `result/arm9_place.v` in `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:80`.
- Real-tech setup returns `kRealTech` only after validating and loading assets; otherwise it records a synthetic-fallback summary in `src/operation/iCTS/test/common/realtech/asset/RealTechAssetLoader.cc:520`. The ARM9 full-sink tests explicitly skip fallback mode rather than running synthetic fallback.
- `RealTechCharSession::prepare` also refuses non-real-tech setup, requires a valid CTS config path, applies a real-tech characterization config with max slew/cap and optional omitted wire-length unit, creates `characterization/realtech/<scenario>/cts.log`, and restores config/STA state on destruction in `src/operation/iCTS/test/module/characterization/support/CharacterizationRealTechTestSupport.cc:372`.

### Full-Design Timing Context Notes

- The task PRD asks for ARM9 full-design/full-sink comparison, but no native-vs-numerical comparison harness exists yet. Existing full-sink ARM9 coverage is matrix-style native HTreeBuilder and ClockSynthesis runs, not a side-by-side numerical comparison.
- STAAdapter has explicit full-design timing preparation support: `LoadConfiguredLiberty` links liberty metadata without forcing full-design DB conversion in `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:201`, SDC loading is skipped with warnings if absent in `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:210`, and `prepareFullDesignTiming()` requires prior `STAAdapter::init()` before full-design timing setup in `src/operation/iCTS/source/database/adapter/sta/STAAdapter.cc:1538`.
- CharBuilder/HTreeBuilder matrix tests primarily use `RealTechCharSession` and the char-only STA path for characterization samples; their "full design" aspect comes from loading ARM9 real-tech assets and full real clock sink pins, not from an explicit test-level call to `STAAdapter::prepareFullDesignTiming()`.

### Related Specs

- Backend spec index routes iCTS work to project constraints, logging, error-handling, quality, and database docs in `.trellis/spec/backend/index.md:12`.
- Project constraints require repository `LOG_*` macros for console logging, iCTS schema/report helpers for structured output such as `cts.log`, established terms like `inst`, `net`, `pin`, `cell_master`, `dbu`, `loads`, `clock_source`, and no default-loop `ecc_dev_tools` usage in `.trellis/spec/project-constraints.md:60`.
- Logging guidelines require schema/report helpers for structured file output, info/warning/error/fatal level semantics, titled schema tables/detail blocks for dense summaries, fallback labeling, and API/test-flow schema lifecycle management in `.trellis/spec/backend/logging-guidelines.md:11`.
- Quality guidelines require existing target linkage instead of duplicated include paths, default-private dependencies, no `../` include traversal, and no default-loop `ecc_dev_tools` use except final verification in `.trellis/spec/backend/quality-guidelines.md:36`.

### Implementation Implications For Numerical Comparison

- A numerical comparison test can reuse the existing ARM9 full-sink selection and skip gates: require `EnsureRealTechSetup().mode == kRealTech`, at least two real clock sinks, and `RealTechCharSession::prepare` success.
- If the comparison should be cheap by default, follow the HTree ARM9 matrix precedent and guard it with a runtime env var. The ClockSynthesis ARM9 matrix currently lacks such an env gate and may run by default whenever real-tech assets are available.
- For native comparison data, consume `HTreeBuilder::BuildResult` fields directly: `best_char`, `best_pattern`, `levels[*].segment_pattern_id`, `levels[*].selected_*`, `depth_candidates`, `char_*`, `used_boundary_fallback`, inserted object counts, and runtime measured externally.
- For artifact compatibility, write a native-vs-numerical `matrix_report.txt` or `report.log` with the existing columns plus numerical columns for selected segment pattern IDs, delay/power deltas, model quality metrics, and runtime ratio.
- Existing per-level selected segment IDs are only written in HTree `report.log`, not in ClockSynthesis `report.log`. A new comparison artifact should emit per-level segment patterns explicitly rather than relying on ClockSynthesis artifact support.

## Caveats / Not Found

- I did not find an existing numerical H-tree implementation or native-vs-numerical comparison test. The current ARM9 coverage is native HTreeBuilder and native ClockSynthesis only.
- I did not run tests or build targets; this research is static code inspection only.
- The HTree ARM9 full-sink matrices are env-gated, but the ClockSynthesis ARM9 full-sink matrix is not env-gated. That difference matters if new tests should avoid expensive default real-tech runs.
- The native HTree report exposes per-level `segment_pattern_id` but not per-level selected buffer master names; those names are available in `BuildResult::LevelPlan` and would need explicit report fields if required.
- Real-tech setup falls back to synthetic for general helpers, but the ARM9 full-sink tests skip fallback mode. Any comparison test that must be ARM9-specific should keep the same skip behavior.
- External references: none. No web search or third-party docs were needed; all findings are from repository files and Trellis specs.
