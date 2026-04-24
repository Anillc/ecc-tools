// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file NumericalHTreeArm9ComparisonTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief ARM9 full-sink comparison between native HTreeBuilder and numerical H-tree flow.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "HTreeTopologyChar.hh"
#include "Inst.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/numerical_htree/NumericalHTreeComparisonReport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

#if defined(ICTS_NUMERICAL_HTREE_API_AVAILABLE) && ICTS_NUMERICAL_HTREE_API_AVAILABLE \
    && __has_include("flow/numerical_htree/NumericalHTreeBuilder.hh")
#define ICTS_NUMERICAL_HTREE_COMPARISON_HAS_API 1
#include "flow/numerical_htree/NumericalHTreeBuilder.hh"
#else
#define ICTS_NUMERICAL_HTREE_COMPARISON_HAS_API 0
#endif

namespace icts_test {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;
namespace comparison = numerical_htree;

constexpr double kComparisonMaxSlewNs = 0.05;
constexpr double kComparisonMaxCapPf = 0.15;
constexpr unsigned kComparisonWireLengthIterations = 3U;
constexpr unsigned kComparisonSlewCapSteps = 15U;
constexpr std::string_view kRunArm9ComparisonEnv = "ICTS_RUN_ARM9_NUMERICAL_HTREE_COMPARISON";
constexpr std::string_view kArm9ComparisonScenario = "numerical_htree_arm9_full_sink_comparison";

struct RealClockLoadSelection
{
  std::string clock_name;
  std::vector<icts::Pin*> loads;
};

struct NativeRunResult
{
  icts::HTreeBuilder::BuildResult result;
  comparison::FlowSummary summary;
};

auto ReadEnvFlag(std::string_view env_name) -> bool
{
  const char* raw_value = std::getenv(std::string(env_name).c_str());
  if (raw_value == nullptr) {
    return false;
  }

  const std::string value = raw_value;
  return !(value.empty() || value == "0" || value == "false" || value == "FALSE" || value == "False");
}

auto SelectLoads(const std::vector<icts::Pin*>& loads, std::size_t max_count) -> std::vector<icts::Pin*>
{
  if (loads.size() <= max_count) {
    return loads;
  }

  std::vector<icts::Pin*> selected_loads;
  selected_loads.reserve(max_count);
  for (std::size_t index = 0; index < max_count; ++index) {
    selected_loads.push_back(loads.at(index * loads.size() / max_count));
  }
  return selected_loads;
}

auto SelectLargestRealClockLoads(std::size_t max_count) -> std::optional<RealClockLoadSelection>
{
  DESIGN_INST.reset();
  STA_ADAPTER_INST.updateTiming();

  for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
    auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
    DESIGN_INST.add_clock(std::move(clock));
  }

  WRAPPER_INST.read();

  RealClockLoadSelection best_selection;
  std::size_t best_source_load_count = 0U;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr || clock->get_loads().size() < 2U) {
      continue;
    }
    if (clock->get_loads().size() <= best_source_load_count) {
      continue;
    }

    best_selection.clock_name = clock->get_clock_name() + ":" + clock->get_clock_net_name();
    best_selection.loads = SelectLoads(clock->get_loads(), max_count);
    best_source_load_count = clock->get_loads().size();
  }

  if (best_selection.loads.size() < 2U) {
    return std::nullopt;
  }

  return best_selection;
}

auto CountPinsWithRealContext(const std::vector<icts::Pin*>& loads) -> std::size_t
{
  std::size_t count = 0U;
  for (const auto* pin : loads) {
    if (pin != nullptr && pin->get_inst() != nullptr && pin->get_net() != nullptr && !pin->get_name().empty()
        && !pin->get_inst()->get_name().empty()) {
      ++count;
    }
  }
  return count;
}

auto CalcCapStepPf(const icts::HTreeBuilder::BuildResult& native_result) -> double
{
  if (native_result.char_cap_steps == 0U || native_result.char_max_cap_pf <= 0.0) {
    return 0.0;
  }
  return native_result.char_max_cap_pf / static_cast<double>(native_result.char_cap_steps);
}

auto ResolveTopInputSlewNs(const icts::HTreeBuilder::BuildResult& native_result) -> double
{
  if (native_result.min_top_input_slew_ns.has_value() && *native_result.min_top_input_slew_ns > 0.0) {
    return *native_result.min_top_input_slew_ns;
  }
  if (native_result.char_slew_steps == 0U || native_result.char_max_slew_ns <= 0.0) {
    return kComparisonMaxSlewNs;
  }

  const double slew_step_ns = native_result.char_max_slew_ns / static_cast<double>(native_result.char_slew_steps);
  if (native_result.top_input_slew_covering_idx.has_value() && *native_result.top_input_slew_covering_idx > 0U) {
    return slew_step_ns * static_cast<double>(*native_result.top_input_slew_covering_idx);
  }
  return slew_step_ns;
}

auto ResolveLeafLoadCapPf(const icts::HTreeBuilder::BuildResult& native_result) -> double
{
  const double cap_step_pf = CalcCapStepPf(native_result);
  if (native_result.best_char.has_value() && cap_step_pf > 0.0) {
    return cap_step_pf * static_cast<double>(native_result.best_char->get_leaf_load_cap_idx());
  }
  return kComparisonMaxCapPf;
}

template <class SelectedDepthT>
auto HasSelectedDepth(const SelectedDepthT& selected_depth) -> bool
{
  if constexpr (requires { selected_depth.has_value(); }) {
    return selected_depth.has_value();
  } else {
    return selected_depth > 0U;
  }
}

template <class SelectedDepthT>
auto SelectedDepthValue(const SelectedDepthT& selected_depth) -> unsigned
{
  if constexpr (requires { selected_depth.value_or(0U); }) {
    return selected_depth.value_or(0U);
  } else {
    return selected_depth;
  }
}

auto BuildComparisonReportPath() -> std::filesystem::path
{
  const auto output_dir = common::io::PrepareCleanOutputDir(common::io::ResolveOutputDir() / "flow" / "numerical_htree"
                                                            / common::io::SanitizeOutputName(std::string(kArm9ComparisonScenario)));
  if (output_dir.empty()) {
    return {};
  }
  return output_dir / "matrix_report.txt";
}

auto RunNativeHTree(const std::vector<icts::Pin*>& loads) -> NativeRunResult
{
  const auto runtime_start = std::chrono::steady_clock::now();
  auto result = icts::HTreeBuilder::build(loads);
  const auto runtime_end = std::chrono::steady_clock::now();
  const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();
  auto summary = comparison::BuildNativeFlowSummary(result, runtime_s);
  return NativeRunResult{
      .result = std::move(result),
      .summary = std::move(summary),
  };
}

#if ICTS_NUMERICAL_HTREE_COMPARISON_HAS_API
auto MakeMetricSummary(const icts::NumericalHTreeModelQualitySummary& quality_summary) -> comparison::ModelMetricSummary
{
  std::string label = quality_summary.note.empty() ? "model_quality_summary" : quality_summary.note;
  label += "_models_" + std::to_string(quality_summary.model_count) + "_metrics_" + std::to_string(quality_summary.metric_count);
  return comparison::ModelMetricSummary{
      .label = std::move(label),
      .sample_count = quality_summary.min_sample_count,
      .rank = quality_summary.min_rank,
      .r2 = quality_summary.min_r2,
      .rmse = quality_summary.max_rmse,
      .max_abs_error = quality_summary.max_abs_error,
  };
}

auto MakeMetricSummary(const icts::NumericalHTreeModelMetric& metric) -> comparison::ModelMetricSummary
{
  return comparison::ModelMetricSummary{
      .label = metric.label,
      .sample_count = metric.sample_count,
      .rank = metric.rank,
      .r2 = metric.r2,
      .rmse = metric.rmse,
      .max_abs_error = metric.max_abs_error,
  };
}

auto MakeLevelSummary(const icts::NumericalHTreeLevelResult& level_result) -> comparison::LevelSummary
{
  return comparison::LevelSummary{
      .level_index = level_result.level_index,
      .segment_pattern_id = level_result.segment_pattern_id,
      .model_name = level_result.model_name,
      .has_numerical_qor = true,
      .input_slew_ns = level_result.input_slew_ns,
      .load_cap_pf = level_result.load_cap_pf,
      .output_slew_ns = level_result.output_slew_ns,
      .driven_cap_pf = level_result.driven_cap_pf,
      .delay_ns = level_result.delay_ns,
      .power_w = level_result.power_w,
      .source_boundary_switch_power_w = level_result.source_boundary_switch_power_w,
      .composed_power_contribution_w = level_result.composed_power_contribution_w,
  };
}

auto BuildNumericalSummary(const std::vector<icts::Pin*>& loads, const icts::HTreeBuilder::BuildResult& native_result)
    -> comparison::FlowSummary
{
  const auto runtime_start = std::chrono::steady_clock::now();
  if (!native_result.success || native_result.levels.empty()) {
    comparison::FlowSummary summary;
    summary.label = "numerical";
    summary.failure_reason = "native_htree_result_unavailable_for_numerical_input";
    const auto runtime_end = std::chrono::steady_clock::now();
    summary.runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();
    return summary;
  }
  if (!native_result.selected_depth.has_value()) {
    comparison::FlowSummary summary;
    summary.label = "numerical";
    summary.failure_reason = "native_selected_depth_unavailable_for_numerical_input";
    const auto runtime_end = std::chrono::steady_clock::now();
    summary.runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();
    return summary;
  }

  icts::NumericalHTreeOptions options;
  options.top_input_slew_ns = ResolveTopInputSlewNs(native_result);
  options.leaf_load_cap_pf = ResolveLeafLoadCapPf(native_result);
  options.top_k_per_level = 64U;
  options.target_depth = native_result.selected_depth;
  options.require_positive_leaf_power = true;
  const auto result = icts::NumericalHTreeBuilder::build(loads, options);
  const auto runtime_end = std::chrono::steady_clock::now();

  comparison::FlowSummary summary;
  summary.label = "numerical";
  summary.success = result.success;
  summary.failure_reason = result.failure_reason;
  summary.runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();
  summary.has_selected_depth = HasSelectedDepth(result.selected_depth);
  summary.selected_depth = SelectedDepthValue(result.selected_depth);
  if (result.success) {
    summary.has_qor = true;
    summary.delay_ns = result.selected_delay_ns;
    summary.power_w = result.selected_power_w;
  }

  summary.level_segment_pattern_ids = result.selected_segment_pattern_ids;
  if (summary.level_segment_pattern_ids.empty()) {
    summary.level_segment_pattern_ids.reserve(result.level_results.size());
    for (const auto& level : result.level_results) {
      summary.level_segment_pattern_ids.push_back(level.segment_pattern_id);
    }
  }
  summary.selected_level_summaries.reserve(result.level_results.size());
  for (const auto& level_result : result.level_results) {
    summary.selected_level_summaries.push_back(MakeLevelSummary(level_result));
  }

  if (result.model_quality_summary.available) {
    summary.model_metrics.push_back(MakeMetricSummary(result.model_quality_summary));
  }
  constexpr std::size_t detailed_metric_report_limit = 32U;
  const std::size_t metric_count = std::min(result.model_metrics.size(), detailed_metric_report_limit);
  summary.model_metrics.reserve(summary.model_metrics.size() + metric_count);
  for (std::size_t metric_index = 0U; metric_index < metric_count; ++metric_index) {
    summary.model_metrics.push_back(MakeMetricSummary(result.model_metrics.at(metric_index)));
  }
  return summary;
}
#endif

}  // namespace

TEST(NumericalHTreeArm9ComparisonTest, Arm9FullSinkComparison)
{
  if (!ReadEnvFlag(kRunArm9ComparisonEnv)) {
    GTEST_SKIP() << "Set " << kRunArm9ComparisonEnv << "=1 to run the ARM9 full-sink numerical H-tree comparison.";
    return;
  }

#if !ICTS_NUMERICAL_HTREE_COMPARISON_HAS_API
  GTEST_SKIP() << "Numerical H-tree production API is not available. Expected header: "
               << "flow/numerical_htree/NumericalHTreeBuilder.hh; expected types: NumericalHTreeOptions, NumericalHTreeResult, "
               << "NumericalHTreeLevelResult, NumericalHTreeBuilder.";
  return;
#else
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClockLoads(std::numeric_limits<std::size_t>::max());
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes at least two CTS sink pins.";
    return;
  }

  ASSERT_GE(selected_clock->loads.size(), 2U);
  ASSERT_EQ(CountPinsWithRealContext(selected_clock->loads), selected_clock->loads.size())
      << "Selected clock loads do not carry complete DEF/CTS instance context: " << selected_clock->clock_name;

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error
      = char_session.prepare(std::string(kArm9ComparisonScenario), std::nullopt, kComparisonMaxSlewNs, kComparisonMaxCapPf);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  CONFIG_INST.set_wire_length_iterations(kComparisonWireLengthIterations);
  CONFIG_INST.set_slew_steps(kComparisonSlewCapSteps);
  CONFIG_INST.set_cap_steps(kComparisonSlewCapSteps);

  comparison::ComparisonReportData report_data;
  report_data.scenario_name = std::string(kArm9ComparisonScenario);
  report_data.clock_name = selected_clock->clock_name;
  report_data.load_count = selected_clock->loads.size();
  auto native_run = RunNativeHTree(selected_clock->loads);
  report_data.native = native_run.summary;
  report_data.numerical = BuildNumericalSummary(selected_clock->loads, native_run.result);
  report_data.deltas = comparison::ComputeComparisonDeltas(report_data.native, report_data.numerical, report_data.tolerance);

  const auto report_path = BuildComparisonReportPath();
  ASSERT_FALSE(report_path.empty());
  EXPECT_TRUE(comparison::WriteComparisonReport(report_path, report_data));
  EXPECT_TRUE(std::filesystem::exists(report_path));

  EXPECT_TRUE(report_data.native.success) << report_data.native.failure_reason;
  EXPECT_TRUE(report_data.numerical.success) << report_data.numerical.failure_reason;
  if (report_data.native.success && report_data.numerical.success) {
    EXPECT_TRUE(report_data.native.has_qor);
    EXPECT_TRUE(report_data.numerical.has_qor);
    EXPECT_TRUE(report_data.native.has_selected_depth);
    EXPECT_TRUE(report_data.numerical.has_selected_depth);
    EXPECT_EQ(report_data.numerical.selected_depth, report_data.native.selected_depth);
    EXPECT_FALSE(report_data.native.level_segment_pattern_ids.empty());
    EXPECT_FALSE(report_data.numerical.level_segment_pattern_ids.empty());
    EXPECT_FALSE(report_data.numerical.model_metrics.empty());
    EXPECT_LE(report_data.deltas.delay_relative_delta, report_data.tolerance.delay_relative);
    EXPECT_LE(report_data.deltas.power_relative_delta, report_data.tolerance.power_relative);
    EXPECT_LT(report_data.numerical.runtime_s, report_data.native.runtime_s);
  }
#endif
}

}  // namespace icts_test
