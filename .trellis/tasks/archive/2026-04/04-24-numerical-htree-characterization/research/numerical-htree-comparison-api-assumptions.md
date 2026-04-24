# Numerical H-Tree Comparison API Assumptions

- Date: 2026-04-24
- Scope: Worker C comparison test/report support

## Current Blocker

At the start of this comparison-test slice, the repository did not contain the
production numerical source directories:

- `src/operation/iCTS/source/module/numerical_characterization`
- `src/operation/iCTS/source/flow/numerical_htree`

During implementation, concurrent work added numerical characterization files, parent
CMake wiring, and `NumericalHTreeBuilder.hh/.cc`.

Because concurrent production flow work may still be incomplete in other checkouts,
the ARM9 comparison test under `src/operation/iCTS/test/flow/numerical_htree` is gated by:

- CMake target presence: `icts_source_flow_numerical_htree`
- Header presence: `flow/numerical_htree/NumericalHTreeBuilder.hh`
- Runtime env: `ICTS_RUN_ARM9_NUMERICAL_HTREE_COMPARISON=1`

When the production target/header are unavailable, the test compiles as a skip if the
test directory is wired into CMake. In the current checkout the header is present, so
the test adapts to the actual lower-level API:

- `NumericalHTreeBuilder::build(const NumericalHTreeBuildInput& input)`
- `NumericalHTreeBuildInput`
- `NumericalHTreeLevelInput`
- `NumericalHTreePatternModel`
- `NumericalHTreeModelQualitySummary`
- `NumericalHTreeResult`
- `NumericalHTreeBuilder::build(const NumericalCharLibrary& library, const std::vector<unsigned>& level_length_indices, const NumericalHTreeOptions& options)`
- `NumericalHTreeBuilder::build(const std::vector<Pin*>& loads, const NumericalHTreeOptions& options)` exists in the current checkout but returns a placeholder failure indicating load-based numerical H-tree requires characterization models.

Focused CMake build/test execution was temporarily blocked by another concurrent
CMake entry outside this worker's write scope:

- `src/operation/iCTS/test/module/numerical_characterization/CMakeLists.txt` referenced
  missing source file `NumericalCharacterizationTest.cc`.

That missing file appeared later in the session, and the focused comparison target
then built successfully.

## Actual / Assumed Production API Names

The comparison test now uses the production names found in the concurrent flow header
and still expects them to remain in namespace `icts`:

- `NumericalHTreeOptions`
- `NumericalHTreeLevelResult`
- `NumericalHTreeResult`
- `NumericalHTreeBuildInput`
- `NumericalHTreeLevelInput`
- `NumericalHTreePatternModel`
- `NumericalHTreeBuilder`
- `NumericalHTreeBuilder::build(const NumericalHTreeBuildInput& input)`
- `NumericalHTreeBuilder::build(const NumericalCharLibrary& library, const std::vector<unsigned>& level_length_indices, const NumericalHTreeOptions& options)`

The test currently assumes `NumericalHTreeResult` has:

- `bool success`
- `std::string failure_reason`
- `selected_depth` as either `unsigned` or `std::optional<unsigned>`; the test supports both because the concurrent API changed while this slice was being implemented
- `double selected_delay_ns`
- `double selected_power_w`
- `std::vector<PatternId> selected_segment_pattern_ids`
- `std::vector<NumericalHTreeLevelResult> level_results`
- `std::vector<NumericalHTreeModelMetric> model_metrics`
- `NumericalHTreeModelQualitySummary model_quality_summary`

The test currently assumes `NumericalHTreeLevelResult` has:

- `PatternId segment_pattern_id`

The test currently assumes `NumericalHTreeModelQualitySummary` has:

- `bool available`
- `std::size_t model_count`
- `std::size_t metric_count`
- `std::size_t min_sample_count`
- `unsigned min_rank`
- `double min_r2`
- `double max_rmse`
- `double max_abs_error`
- `std::string note`

Because the current load-based production overload returns a placeholder failure, the
test builds a `NumericalCharLibrary` in test code and calls the library/level-index
production overload. The library is derived from:

- native `HTreeBuilder::BuildResult` level length bins
- `CharBuilder` segment samples for those bins
- `NumericalCharLibrary::buildFromSegmentChars`

## Comparison Behavior

The comparison test:

- skips unless `ICTS_RUN_ARM9_NUMERICAL_HTREE_COMPARISON=1`
- skips unless real ARM9 assets are available through the existing realtech setup
- selects the largest real DEF-derived clock and uses full clock sinks
- measures native and numerical runtimes externally with `std::chrono::steady_clock`
- compares numerical QoR against native `HTreeBuilder::build` with default tolerances:
  - relative delay delta <= 20%
  - relative power delta <= 25%
- requires numerical runtime to be lower than native runtime when both flows succeed
- writes `flow/numerical_htree/numerical_htree_arm9_full_sink_comparison/matrix_report.txt`
  with runtime, selected depth, delay, power, level segment pattern IDs, model metrics,
  and deltas.
