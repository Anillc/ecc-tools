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
 * @file LinearClusteringRealTechSweepScenario.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Sweep-oriented real-tech linear clustering test scenarios.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Point.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "common/types/TestDataTypes.hh"
#include "common/visualization/TestVisualization.hh"
#include "module/topology/clustering/Clustering.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechShared.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::linear_clustering::realtech {

namespace {
using namespace detail;
using common::io::EmitInfoReport;
using common::realtech::EnsureRealTechSetup;
using common::realtech::MakeRealTechOrSyntheticLoads;
using common::realtech::RealTechMode;
using common::realtech::RealTechSetupState;
using common::visualization::WriteClusterSvg;

constexpr std::string_view kRealTechOrFallbackSweepTestName = "LinearClusteringTechTest.RealTechOrFallbackSweepsGenerateArtifacts";
constexpr std::string_view kRealTechOrFallbackSweepTitle = "realtech_or_fallback_sweeps_generate_artifacts";
constexpr std::string_view kRealTechDiameterLadderTestName = "LinearClusteringTechTest.RealTechDiameterLadderIsResponsive";
constexpr std::string_view kRealTechDiameterLadderTitle = "realtech_diameter_ladder_is_responsive";
constexpr std::size_t kSyntheticFallbackLoadCount = 256;
constexpr unsigned kSyntheticFallbackSeed = 1024U;

struct ClusterSizeSummary
{
  std::size_t singleton_cluster_count = 0;
  std::size_t total_loads = 0;
  double avg_cluster_size = 0.0;
  std::size_t min_cluster_size = 0;
  std::size_t max_cluster_size = 0;
};

struct SelectedSweepArtifacts
{
  const StrategySweepCandidate* selected_candidate = nullptr;
  const icts::ClusterResult* result = nullptr;
  const icts::LinearClusteringConfig* config = nullptr;
  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::vector<std::size_t> cluster_sizes;
};

struct DiameterLadderStepData
{
  std::string case_tag;
  StrategySweepSelection selection;
  SelectedSweepArtifacts artifacts;
  ClusterMetrics metrics;
  std::string svg_name;
};

auto ConfigureReportPrecision(std::ostringstream& output_stream, int precision) -> void
{
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream << std::setprecision(precision);
}

auto EmitCaseReport(const std::filesystem::path& output_dir, std::string_view title, const std::string& report) -> void
{
  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(WriteCaseLog(report_path, report)) << "Failed to write report: " << report_path.string();
  EmitInfoReport(InfoReport{.title = std::string(title), .content = report});
}

auto BuildUnavailableRealTechReport(std::string_view test_name, const RealTechSetupState& setup_state) -> std::string
{
  std::ostringstream report;
  report << "Test: " << test_name << "\n";
  report << "Mode: skipped\n";
  report << "Reason: real-tech assets are unavailable.\n";
  report << "Setup: " << setup_state.summary << "\n";
  report << "Artifacts: report.log\n";
  return report.str();
}

auto SummarizeClusterSizes(const std::vector<std::size_t>& cluster_sizes) -> ClusterSizeSummary
{
  ClusterSizeSummary summary;
  if (cluster_sizes.empty()) {
    return summary;
  }

  const auto minmax = std::ranges::minmax_element(cluster_sizes);
  summary.singleton_cluster_count = static_cast<std::size_t>(std::ranges::count(cluster_sizes, kSingletonClusterSize));
  summary.total_loads = std::accumulate(cluster_sizes.begin(), cluster_sizes.end(), std::size_t{0});
  summary.avg_cluster_size = static_cast<double>(summary.total_loads) / static_cast<double>(cluster_sizes.size());
  summary.min_cluster_size = *minmax.min;
  summary.max_cluster_size = *minmax.max;
  return summary;
}

auto TryCollectSelectedSweepArtifacts(const StrategySweepSelection& selection, const std::vector<icts::Pin*>& loads,
                                      const std::string& no_candidates_message, const std::string& no_selection_message,
                                      const std::string& empty_result_message) -> std::optional<SelectedSweepArtifacts>
{
  if (selection.candidates.empty()) {
    ADD_FAILURE() << no_candidates_message;
    return std::nullopt;
  }
  if (!selection.selected_index.has_value()) {
    ADD_FAILURE() << no_selection_message;
    return std::nullopt;
  }

  SelectedSweepArtifacts artifacts;
  const auto selected_index = *selection.selected_index;
  artifacts.selected_candidate = &selection.candidates.at(selected_index);
  artifacts.result = &selection.selected_run.result;
  artifacts.config = &selection.selected_config;
  if (artifacts.result->clusters.empty()) {
    ADD_FAILURE() << empty_result_message;
    return std::nullopt;
  }

  std::string error;
  if (!ValidateClusterLegality(*artifacts.result, *artifacts.config, error)) {
    ADD_FAILURE() << error;
    return std::nullopt;
  }
  if (!BuildClusterArtifacts(*artifacts.result, loads, artifacts.cluster_map, artifacts.centers, artifacts.cluster_sizes, error)) {
    ADD_FAILURE() << error;
    return std::nullopt;
  }
  return artifacts;
}

auto AppendRealTechArtifactSweepCase(std::ostringstream& overall_summary, const std::filesystem::path& output_dir,
                                     const RealClockLoads& real_clock_loads, std::size_t fanout_limit,
                                     std::vector<std::string>& artifact_names) -> void
{
  const auto& active_loads = real_clock_loads.loads;
  const auto case_tag = MakeFanoutCaseTag(fanout_limit, active_loads.size());
  auto selection = BuildStrategySweepSelection(active_loads, fanout_limit, real_clock_loads.bounding_box_diameter, false);

  const auto artifacts = TryCollectSelectedSweepArtifacts(
      selection, active_loads, "Strategy sweep did not generate candidates for " + case_tag, "No legal strategy selected for " + case_tag,
      "Selected strategy produced empty result for " + case_tag);
  if (!artifacts.has_value()) {
    return;
  }
  const auto& selected_artifacts = artifacts.value();

  const auto cluster_summary = SummarizeClusterSizes(selected_artifacts.cluster_sizes);
  EXPECT_EQ(cluster_summary.total_loads, active_loads.size());

  const auto svg_name = "cluster_" + case_tag + ".svg";
  const auto svg_path = output_dir / svg_name;
  EXPECT_TRUE(WriteClusterSvg(svg_path.string(), active_loads, selected_artifacts.cluster_map, selected_artifacts.centers))
      << "Failed to write svg: " << svg_path.string();
  artifact_names.push_back(svg_name);

  overall_summary << "\nCase: " << case_tag << "\n";
  overall_summary << "Requested max_fanout: " << fanout_limit << "\n";
  overall_summary << "Scoring strategy: " << ScoringStrategyName(selected_artifacts.config->scoring_strategy) << "\n";
  overall_summary << "Observed result: clusters=" << selected_artifacts.cluster_sizes.size()
                  << ", singleton_clusters=" << cluster_summary.singleton_cluster_count
                  << ", cluster_size[min/max/avg]=" << cluster_summary.min_cluster_size << "/" << cluster_summary.max_cluster_size << "/"
                  << cluster_summary.avg_cluster_size << "\n";
  overall_summary << "Selected strategy: "
                  << StrategyLabel(selected_artifacts.selected_candidate->order_strategy,
                                   selected_artifacts.selected_candidate->discrete_hilbert_encoding,
                                   selected_artifacts.selected_candidate->hilbert_transform,
                                   selected_artifacts.selected_candidate->order_bits, selected_artifacts.selected_candidate->split_strategy,
                                   selected_artifacts.selected_candidate->sweep_mode,
                                   selected_artifacts.selected_candidate->strided_sweep_count)
                  << " (selection_score=" << selected_artifacts.selected_candidate->selection_score << ")\n";
  overall_summary << "Strategy sweep\n"
                  << BuildStrategySweepSection(selection.candidates, selection.selected_index, selected_artifacts.config->scoring_strategy);
}

auto CollectDiameterLadderStepData(const RealClockLoads& real_clock_loads, std::size_t step_index, int requested_diameter,
                                   DiameterLadderStepData& step_data) -> void
{
  step_data.case_tag = "step_" + std::to_string(step_index) + "_diameter_" + std::to_string(requested_diameter);
  step_data.selection = BuildStrategySweepSelection(real_clock_loads.loads, kRealTechFanoutSweep.front(), requested_diameter, false);
  const auto artifacts = TryCollectSelectedSweepArtifacts(
      step_data.selection, real_clock_loads.loads, "Diameter ladder strategy sweep produced no candidates for " + step_data.case_tag,
      "Diameter ladder strategy sweep selected no legal candidate for " + step_data.case_tag,
      "Diameter ladder produced empty result for step " + std::to_string(step_index));
  if (!artifacts.has_value()) {
    return;
  }
  step_data.artifacts = artifacts.value();

  step_data.metrics = GatherMetrics(*step_data.artifacts.result);
  EXPECT_LE(step_data.metrics.max_cluster_diameter, step_data.artifacts.config->max_diameter);
  step_data.svg_name = step_data.case_tag + ".svg";
}

auto UpdateDiameterLadderHistory(const ClusterMetrics& metrics, std::optional<std::size_t>& previous_cluster_count,
                                 std::optional<int>& previous_max_diameter, bool& observed_response) -> void
{
  if (previous_cluster_count.has_value()) {
    EXPECT_GE(metrics.cluster_count, previous_cluster_count.value());
    observed_response = observed_response || (metrics.cluster_count != previous_cluster_count.value());
  }
  if (previous_max_diameter.has_value()) {
    EXPECT_LE(metrics.max_cluster_diameter, previous_max_diameter.value());
    observed_response = observed_response || (metrics.max_cluster_diameter != previous_max_diameter.value());
  }
  previous_cluster_count = metrics.cluster_count;
  previous_max_diameter = metrics.max_cluster_diameter;
}

auto WriteDiameterLadderArtifact(const std::filesystem::path& output_dir, const RealClockLoads& real_clock_loads,
                                 const DiameterLadderStepData& step_data) -> void
{
  EXPECT_TRUE(WriteClusterSvg((output_dir / step_data.svg_name).string(), real_clock_loads.loads, step_data.artifacts.cluster_map,
                              step_data.artifacts.centers));
}

auto AppendDiameterLadderStepReport(std::ostringstream& report, const DiameterLadderStepData& step_data) -> void
{
  report << "\nStep: " << step_data.case_tag << "\n";
  report << "Requested max_diameter: " << step_data.artifacts.config->max_diameter << "\n";
  report << "Observed result: " << FormatMetricsLine(step_data.metrics) << "\n";
  report << "Selected strategy: "
         << StrategyLabel(step_data.artifacts.selected_candidate->order_strategy,
                          step_data.artifacts.selected_candidate->discrete_hilbert_encoding,
                          step_data.artifacts.selected_candidate->hilbert_transform, step_data.artifacts.selected_candidate->order_bits,
                          step_data.artifacts.selected_candidate->split_strategy, step_data.artifacts.selected_candidate->sweep_mode,
                          step_data.artifacts.selected_candidate->strided_sweep_count)
         << " (selection_score=" << step_data.artifacts.selected_candidate->selection_score << ")\n";
  report << "Strategy sweep\n"
         << BuildStrategySweepSection(step_data.selection.candidates, step_data.selection.selected_index,
                                      step_data.artifacts.config->scoring_strategy);
}

auto AppendDiameterLadderStep(std::ostringstream& report, const std::filesystem::path& output_dir, const RealClockLoads& real_clock_loads,
                              std::size_t step_index, int requested_diameter, std::optional<std::size_t>& previous_cluster_count,
                              std::optional<int>& previous_max_diameter, bool& observed_response, std::vector<std::string>& artifact_names)
    -> void;

auto AppendDiameterLadderStep(std::ostringstream& report, const std::filesystem::path& output_dir, const RealClockLoads& real_clock_loads,
                              std::size_t step_index, int requested_diameter, std::optional<std::size_t>& previous_cluster_count,
                              std::optional<int>& previous_max_diameter, bool& observed_response, std::vector<std::string>& artifact_names)
    -> void
{
  DiameterLadderStepData step_data;
  CollectDiameterLadderStepData(real_clock_loads, step_index, requested_diameter, step_data);
  if (::testing::Test::HasFatalFailure()) {
    return;
  }
  UpdateDiameterLadderHistory(step_data.metrics, previous_cluster_count, previous_max_diameter, observed_response);
  WriteDiameterLadderArtifact(output_dir, real_clock_loads, step_data);
  artifact_names.push_back(step_data.svg_name);
  AppendDiameterLadderStepReport(report, step_data);
}

auto ValidateResponsiveDiameterLoads(const RealClockLoads& real_clock_loads) -> void
{
  ASSERT_TRUE(real_clock_loads.available);
  ASSERT_GE(real_clock_loads.loads.size(), 4U);
  ASSERT_GT(real_clock_loads.bounding_box_diameter, 0);
}

auto BuildDiameterLadderReportHeader(const RealClockLoads& real_clock_loads, const std::array<int, 3>& diameter_thresholds)
    -> std::ostringstream
{
  std::ostringstream report;
  ConfigureReportPrecision(report, 2);
  report << "Test: " << kRealTechDiameterLadderTestName << "\n";
  report << "Mode: real-tech diameter ladder\n";
  report << "Clock: " << real_clock_loads.clock_name << "\n";
  report << "Input: load_count=" << real_clock_loads.loads.size() << ", dbu_per_micron=" << real_clock_loads.dbu_per_micron
         << ", max_fanout=" << kRealTechFanoutSweep.front() << ", thresholds=[" << diameter_thresholds.at(0) << ","
         << diameter_thresholds.at(1) << "," << diameter_thresholds.at(2) << "]\n";
  return report;
}

auto AppendAllDiameterLadderSteps(std::ostringstream& report, const std::filesystem::path& output_dir,
                                  const RealClockLoads& real_clock_loads, const std::array<int, 3>& diameter_thresholds,
                                  bool& observed_response, std::vector<std::string>& artifact_names) -> void
{
  std::optional<std::size_t> previous_cluster_count = std::nullopt;
  std::optional<int> previous_max_diameter = std::nullopt;
  for (std::size_t index = 0; index < diameter_thresholds.size(); ++index) {
    AppendDiameterLadderStep(report, output_dir, real_clock_loads, index, diameter_thresholds.at(index), previous_cluster_count,
                             previous_max_diameter, observed_response, artifact_names);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
}

auto RunRealTechArtifactSweep(const RealTechSetupState& setup_state, const std::filesystem::path& output_dir, std::string& report) -> void
{
  report.clear();
  const auto& real_clock_loads = EnsureLargestRealClockLoads();
  ASSERT_TRUE(real_clock_loads.available) << "Real-tech mode is active but no CTS clock loads are available.";
  const auto& active_loads = real_clock_loads.loads;
  ASSERT_FALSE(active_loads.empty());

  std::ostringstream overall_summary;
  ConfigureReportPrecision(overall_summary, 2);
  overall_summary << "Test: " << kRealTechOrFallbackSweepTestName << "\n";
  overall_summary << "Mode: real-tech\n";
  overall_summary << "Setup: " << setup_state.summary << "\n";
  overall_summary << "Clock: " << real_clock_loads.clock_name << "\n";
  overall_summary << "Input: actual_load_count=" << active_loads.size() << ", dbu_per_micron=" << real_clock_loads.dbu_per_micron
                  << ", bounding_box_diameter=" << real_clock_loads.bounding_box_diameter << ", fanout_sweep=[8,4]"
                  << ", sweep_candidates=" << FormatSweepCandidates(BuildSweepCandidates(active_loads.size())) << "\n";

  std::vector<std::string> artifact_names;
  for (const auto fanout_limit : kRealTechFanoutSweep) {
    ASSERT_NO_FATAL_FAILURE(AppendRealTechArtifactSweepCase(overall_summary, output_dir, real_clock_loads, fanout_limit, artifact_names));
  }
  overall_summary << "\nArtifacts: " << BuildArtifactSummary(artifact_names) << ", report.log\n";
  report = overall_summary.str();
}

auto RunSyntheticFallbackArtifactSweep(const RealTechSetupState& setup_state, const std::filesystem::path& output_dir, std::string& report)
    -> void
{
  report.clear();
  std::string data_source;
  auto generated = MakeRealTechOrSyntheticLoads(kSyntheticFallbackLoadCount, kSyntheticFallbackSeed, data_source);
  ASSERT_FALSE(generated.loads.empty()) << "No loads generated in synthetic fallback mode.";
  const auto& active_loads = generated.loads;

  const auto fallback_max_diameter = std::max(1, CalcClusterDiameter(active_loads));
  const auto case_tag = "requested_" + std::to_string(kSyntheticFallbackLoadCount) + "_actual_" + std::to_string(active_loads.size());
  auto selection = BuildStrategySweepSelection(active_loads, kDefaultTargetClusterSize, fallback_max_diameter, false);

  const auto artifacts = TryCollectSelectedSweepArtifacts(selection, active_loads, "Strategy sweep did not generate fallback candidates.",
                                                          "No legal strategy selected in fallback sweep.",
                                                          "Selected fallback strategy produced empty result.");
  if (!artifacts.has_value()) {
    return;
  }
  const auto& selected_artifacts = artifacts.value();

  const auto cluster_summary = SummarizeClusterSizes(selected_artifacts.cluster_sizes);
  EXPECT_EQ(cluster_summary.total_loads, active_loads.size());

  const auto svg_name = "cluster_" + case_tag + ".svg";
  const auto svg_path = output_dir / svg_name;
  EXPECT_TRUE(WriteClusterSvg(svg_path.string(), active_loads, selected_artifacts.cluster_map, selected_artifacts.centers))
      << "Failed to write svg: " << svg_path.string();

  std::ostringstream summary_stream;
  ConfigureReportPrecision(summary_stream, 2);
  summary_stream << "Test: " << kRealTechOrFallbackSweepTestName << "\n";
  summary_stream << "Mode: synthetic fallback\n";
  summary_stream << "Setup: " << setup_state.summary << "\n";
  summary_stream << "Input: requested_load_count=" << kSyntheticFallbackLoadCount << ", actual_load_count=" << active_loads.size()
                 << ", data_source=" << data_source << ", max_diameter=" << fallback_max_diameter
                 << ", max_fanout=" << kDefaultTargetClusterSize
                 << ", sweep_candidates=" << FormatSweepCandidates(BuildSweepCandidates(active_loads.size())) << "\n";
  summary_stream << "Observed result: clusters=" << selected_artifacts.cluster_sizes.size()
                 << ", singleton_clusters=" << cluster_summary.singleton_cluster_count
                 << ", cluster_size[min/max/avg]=" << cluster_summary.min_cluster_size << "/" << cluster_summary.max_cluster_size << "/"
                 << cluster_summary.avg_cluster_size << "\n";
  summary_stream << "Selected strategy: "
                 << StrategyLabel(selected_artifacts.selected_candidate->order_strategy,
                                  selected_artifacts.selected_candidate->discrete_hilbert_encoding,
                                  selected_artifacts.selected_candidate->hilbert_transform,
                                  selected_artifacts.selected_candidate->order_bits, selected_artifacts.selected_candidate->split_strategy,
                                  selected_artifacts.selected_candidate->sweep_mode,
                                  selected_artifacts.selected_candidate->strided_sweep_count)
                 << " (selection_score=" << selected_artifacts.selected_candidate->selection_score << ")\n";
  summary_stream << "Strategy sweep\n"
                 << BuildStrategySweepSection(selection.candidates, selection.selected_index, selected_artifacts.config->scoring_strategy);
  summary_stream << "\nArtifacts: " << svg_name << ", report.log\n";
  report = summary_stream.str();
}

auto RunRealTechDiameterLadderWithAvailableAssets(const std::filesystem::path& output_dir) -> void
{
  const auto& real_clock_loads = EnsureLargestRealClockLoads();
  ValidateResponsiveDiameterLoads(real_clock_loads);
  if (::testing::Test::HasFatalFailure()) {
    return;
  }

  const auto diameter_thresholds = BuildResponsiveDiameterThresholds(real_clock_loads);
  std::vector<std::string> artifact_names;
  auto report = BuildDiameterLadderReportHeader(real_clock_loads, diameter_thresholds);
  bool observed_response = false;
  AppendAllDiameterLadderSteps(report, output_dir, real_clock_loads, diameter_thresholds, observed_response, artifact_names);
  if (::testing::Test::HasFatalFailure()) {
    return;
  }

  EXPECT_TRUE(observed_response) << "Diameter ladder should respond to realistic DBU thresholds derived from the real clock-load geometry.";
  report << "\nObserved response: " << (observed_response ? "true" : "false") << "\n";
  report << "Artifacts: " << BuildArtifactSummary(artifact_names) << ", report.log\n";
  EmitCaseReport(output_dir, std::string{kRealTechDiameterLadderTitle}, report.str());
}

}  // namespace

auto RunRealTechOrFallbackSweepsGenerateArtifacts() -> void
{
  const auto& setup_state = EnsureRealTechSetup();
  const auto output_dir = PrepareOutputDir(std::string{kRealTechOrFallbackSweepTitle});
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare artifact-sweep output dir.";

  std::string report;
  if (setup_state.mode == RealTechMode::kRealTech) {
    RunRealTechArtifactSweep(setup_state, output_dir, report);
  } else {
    RunSyntheticFallbackArtifactSweep(setup_state, output_dir, report);
  }
  EmitCaseReport(output_dir, std::string{kRealTechOrFallbackSweepTitle}, report);
}

auto RunRealTechDiameterLadderIsResponsive() -> void
{
  const auto& setup_state = EnsureRealTechSetup();
  const auto output_dir = PrepareOutputDir(std::string{kRealTechDiameterLadderTitle});
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare diameter-ladder output dir.";
  if (setup_state.mode != RealTechMode::kRealTech) {
    EmitCaseReport(output_dir, std::string{kRealTechDiameterLadderTitle},
                   BuildUnavailableRealTechReport(kRealTechDiameterLadderTestName, setup_state));
    GTEST_SKIP() << "Real-tech assets are unavailable: " << setup_state.summary;
  }
  RunRealTechDiameterLadderWithAvailableAssets(output_dir);
}

}  // namespace icts_test::linear_clustering::realtech
