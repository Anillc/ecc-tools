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
 * @file LinearClusteringRealTechElectricalScenario.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Electrical real-tech linear clustering test scenarios.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
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
#include "module/topology/linear_clustering/LinearClusteringTypes.hh"
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
using common::realtech::RealTechMode;
using common::realtech::RealTechSetupState;
using common::visualization::WriteClusterSvg;

constexpr std::string_view kExactCapRegressionTestName = "LinearClusteringTechTest.RealTechExactCapRegression";
constexpr std::string_view kExactCapRegressionTitle = "realtech_exact_cap_regression";
constexpr std::string_view kRootCollisionTestName = "LinearClusteringTechTest.RealTechExactCapRootCollisionIsLegalizedBeforeRouting";
constexpr std::string_view kRootCollisionTitle = "realtech_exact_cap_root_collision_is_legalized_before_routing";
constexpr int kMinimumLocalShiftDbu = 8;
constexpr int kLocalShiftMicronWindow = 10;

struct PreparedRealTechCase
{
  const RealTechSetupState* setup_state = nullptr;
  std::filesystem::path output_dir;
};

struct SelectedExactCapArtifacts
{
  const StrategySweepCandidate* selected_candidate = nullptr;
  const icts::ClusterResult* result = nullptr;
  const icts::LinearClusteringConfig* config = nullptr;
  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::vector<std::size_t> cluster_sizes;
};

struct RootCollisionEvaluation
{
  std::vector<icts::Pin*> subset;
  icts::LinearClusteringConfig config;
  icts::Point<int> requested_root;
  icts::ElectricalEstimate estimate;
  int max_expected_local_shift = 0;
  int legalization_manhattan = 0;
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

auto TryPrepareRealTechCase(std::string_view title, std::string_view test_name, const char* output_dir_failure_message,
                            PreparedRealTechCase& prepared) -> bool
{
  prepared.setup_state = &EnsureRealTechSetup();
  prepared.output_dir = PrepareOutputDir(std::string{title});
  if (prepared.output_dir.empty()) {
    ADD_FAILURE() << output_dir_failure_message;
    return false;
  }
  if (prepared.setup_state->mode != RealTechMode::kRealTech) {
    EmitCaseReport(prepared.output_dir, std::string{title}, BuildUnavailableRealTechReport(test_name, *prepared.setup_state));
    return false;
  }
  return true;
}

auto TryCollectSelectedExactCapArtifacts(const StrategySweepSelection& selection, const std::vector<icts::Pin*>& loads,
                                         const std::string& no_candidates_message, const std::string& no_selection_message,
                                         const std::string& empty_result_message) -> std::optional<SelectedExactCapArtifacts>
{
  if (selection.candidates.empty()) {
    ADD_FAILURE() << no_candidates_message;
    return std::nullopt;
  }
  if (!selection.selected_index.has_value()) {
    ADD_FAILURE() << no_selection_message;
    return std::nullopt;
  }

  SelectedExactCapArtifacts artifacts;
  const auto selected_index = selection.selected_index.value_or(0U);
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

auto AppendExactCapClusterDetails(std::ostringstream& overall_summary, const icts::ClusterResult& result,
                                  const icts::LinearClusteringConfig& config, std::size_t fanout_limit) -> void
{
  for (std::size_t cluster_id = 0; cluster_id < result.clusters.size(); ++cluster_id) {
    const auto& cluster = result.clusters.at(cluster_id);
    const auto estimate = EstimateExactElectrical(cluster, config);
    ASSERT_TRUE(estimate.route_success) << "Exact-cap routing failed for cluster " << cluster_id << " under fanout " << fanout_limit;

    overall_summary << "- cluster[" << cluster_id << "] size=" << cluster.size() << ", diameter=" << CalcClusterDiameter(cluster)
                    << ", synthetic_root=" << FormatPoint(estimate.synthetic_root)
                    << ", legalized_root=" << FormatPoint(estimate.legalized_root) << ", routed_root=" << FormatPoint(estimate.routed_root)
                    << ", total_cap=" << estimate.total_cap << "\n";
  }
}

auto AppendExactCapRegressionCase(std::ostringstream& overall_summary, const std::filesystem::path& output_dir,
                                  const RealClockLoads& real_clock_loads, std::size_t fanout_limit,
                                  std::vector<std::string>& artifact_names) -> void
{
  const auto case_tag = MakeFanoutCaseTag(fanout_limit, real_clock_loads.loads.size());
  auto selection = BuildStrategySweepSelection(real_clock_loads.loads, fanout_limit, real_clock_loads.bounding_box_diameter, true);

  const auto artifacts = TryCollectSelectedExactCapArtifacts(
      selection, real_clock_loads.loads, "Exact-cap strategy sweep produced no candidates for " + case_tag,
      "Exact-cap strategy sweep selected no legal candidate for " + case_tag,
      "Exact-cap regression produced empty result for fanout " + std::to_string(fanout_limit));
  if (!artifacts.has_value()) {
    return;
  }
  const auto& selected_artifacts = *artifacts;
  ASSERT_GE(selected_artifacts.result->clusters.size(), 2U) << "Full real load set should split under exact-cap limits.";

  const auto svg_name = case_tag + ".svg";
  EXPECT_TRUE(WriteClusterSvg((output_dir / svg_name).string(), real_clock_loads.loads, selected_artifacts.cluster_map,
                              selected_artifacts.centers));
  artifact_names.push_back(svg_name);

  overall_summary << "\nCase: " << case_tag << "\n";
  overall_summary << "Config: max_fanout=" << selected_artifacts.config->max_fanout
                  << ", max_diameter=" << selected_artifacts.config->max_diameter
                  << ", enable_exact_cap=true, scoring=" << ScoringStrategyName(selected_artifacts.config->scoring_strategy) << "\n";
  overall_summary << "Selected strategy: "
                  << StrategyLabel(selected_artifacts.selected_candidate->order_strategy,
                                   selected_artifacts.selected_candidate->discrete_hilbert_encoding,
                                   selected_artifacts.selected_candidate->hilbert_transform,
                                   selected_artifacts.selected_candidate->order_bits, selected_artifacts.selected_candidate->split_strategy,
                                   selected_artifacts.selected_candidate->sweep_mode,
                                   selected_artifacts.selected_candidate->strided_sweep_count)
                  << " (selection_score=" << selected_artifacts.selected_candidate->selection_score << ")\n";
  overall_summary << "Observed result: cluster_count=" << selected_artifacts.result->clusters.size() << "\n";
  overall_summary << "Strategy sweep\n"
                  << BuildStrategySweepSection(selection.candidates, selection.selected_index, selected_artifacts.config->scoring_strategy);
  AppendExactCapClusterDetails(overall_summary, *selected_artifacts.result, *selected_artifacts.config, fanout_limit);
}

auto BuildRootCollisionConfig(std::size_t subset_size) -> icts::LinearClusteringConfig
{
  icts::LinearClusteringConfig config{};
  config.router_kind = icts::LinearRouterKind::kFlute;
  config.root_policy = icts::LinearRootPolicy::kMedian;
  config.enable_exact_cap = true;
  config.always_build_exact_cap = true;
  config.max_fanout = subset_size;
  config.max_diameter = std::numeric_limits<int>::max() / 4;
  config.max_cap = std::numeric_limits<double>::infinity();
  return config;
}

auto CalcAbsoluteValue(int value) -> int
{
  return value >= 0 ? value : -value;
}

auto CalcLegalizationManhattanDistance(const icts::ElectricalEstimate& estimate) -> int
{
  const int legalization_dx = estimate.legalized_root.get_x() - estimate.synthetic_root.get_x();
  const int legalization_dy = estimate.legalized_root.get_y() - estimate.synthetic_root.get_y();
  return CalcAbsoluteValue(legalization_dx) + CalcAbsoluteValue(legalization_dy);
}

auto ValidateExactCapRealClockLoads(const RealClockLoads& real_clock_loads, std::string_view failure_message) -> void
{
  ASSERT_TRUE(real_clock_loads.available);
  ASSERT_EQ(CountPinsWithExactCapContext(real_clock_loads.loads), real_clock_loads.loads.size()) << failure_message;
}

auto TryLoadExactCapRealClockLoads(std::string_view failure_message, const RealClockLoads*& real_clock_loads) -> bool
{
  real_clock_loads = &EnsureLargestRealClockLoads();
  ValidateExactCapRealClockLoads(*real_clock_loads, failure_message);
  return !::testing::Test::HasFatalFailure();
}

auto BuildExactCapRegressionHeader(const RealClockLoads& real_clock_loads) -> std::ostringstream
{
  const std::array<std::size_t, 2> fanout_sweep = kRealTechFanoutSweep;
  std::ostringstream overall_summary;
  ConfigureReportPrecision(overall_summary, 4);
  overall_summary << "Test: " << kExactCapRegressionTestName << "\n";
  overall_summary << "Mode: real-tech exact-cap regression\n";
  overall_summary << "Clock: " << real_clock_loads.clock_name << "\n";
  overall_summary << "Input: load_count=" << real_clock_loads.loads.size() << ", fanout_sweep=[" << fanout_sweep.at(0) << ","
                  << fanout_sweep.at(1) << "], router_kind=flute"
                  << ", sweep_candidates=" << FormatSweepCandidates(BuildSweepCandidates(real_clock_loads.loads.size())) << "\n";
  return overall_summary;
}

auto AppendExactCapRegressionSweep(std::ostringstream& overall_summary, const std::filesystem::path& output_dir,
                                   const RealClockLoads& real_clock_loads, std::vector<std::string>& artifact_names) -> void
{
  for (const auto fanout_limit : kRealTechFanoutSweep) {
    AppendExactCapRegressionCase(overall_summary, output_dir, real_clock_loads, fanout_limit, artifact_names);
    if (::testing::Test::HasFatalFailure()) {
      return;
    }
  }
}

auto TryBuildExactCapRegressionReport(const std::filesystem::path& output_dir, const RealClockLoads& real_clock_loads, std::string& report)
    -> bool
{
  std::vector<std::string> artifact_names;
  auto overall_summary = BuildExactCapRegressionHeader(real_clock_loads);
  AppendExactCapRegressionSweep(overall_summary, output_dir, real_clock_loads, artifact_names);
  if (::testing::Test::HasFatalFailure()) {
    return false;
  }

  overall_summary << "\nArtifacts: " << BuildArtifactSummary(artifact_names) << ", report.log\n";
  report = overall_summary.str();
  return true;
}

auto CollectRootCollisionSubset(const RealClockLoads& real_clock_loads, std::vector<icts::Pin*>& subset) -> void
{
  subset = FindNonDegenerateMedianCollisionSubset(real_clock_loads.loads);
  ASSERT_EQ(subset.size(), 3U) << "Failed to find a non-degenerate median collision subset.";
}

auto ValidateRequestedRootCollision(const std::vector<icts::Pin*>& subset, const icts::Point<int>& requested_root) -> void
{
  ASSERT_TRUE(HasLoadAt(subset, requested_root)) << "Precondition violated: requested median root does not collide with subset loads.";
}

auto ValidateRootCollisionRoots(const std::vector<icts::Pin*>& subset, const icts::Point<int>& requested_root,
                                const icts::ElectricalEstimate& estimate) -> void
{
  ASSERT_TRUE(estimate.route_success) << "FLUTE exact-cap route failed on root-collision subset.";
  EXPECT_TRUE(IsSamePoint(estimate.synthetic_root, requested_root));
  EXPECT_FALSE(IsSamePoint(estimate.legalized_root, requested_root)) << "Legalized root should move away from a colliding synthetic root.";
  EXPECT_FALSE(HasLoadAt(subset, estimate.legalized_root)) << "Legalized root still collides with one of the load coordinates.";
}

auto CalcMaxExpectedLocalShift(int dbu_per_micron) -> int
{
  const int dbu_shift = dbu_per_micron * kLocalShiftMicronWindow;
  return dbu_shift > kMinimumLocalShiftDbu ? dbu_shift : kMinimumLocalShiftDbu;
}

auto ValidateRootCollisionShift(const icts::ElectricalEstimate& estimate, int max_expected_local_shift) -> int
{
  const int legalization_manhattan = CalcLegalizationManhattanDistance(estimate);
  EXPECT_GT(legalization_manhattan, 0);
  EXPECT_LE(legalization_manhattan, max_expected_local_shift)
      << "Legalized root should be a local move near synthetic root, not an unrelated default point.";
  return legalization_manhattan;
}

auto BuildRootCollisionSummary(const std::vector<icts::Pin*>& subset, const icts::Point<int>& requested_root,
                               const icts::ElectricalEstimate& estimate, int legalization_manhattan, int max_expected_local_shift)
    -> std::string
{
  std::ostringstream summary;
  summary << "Test: " << kRootCollisionTestName << "\n";
  summary << "Mode: real-tech root-collision legalization regression\n";
  summary << "Router kind: flute\n";
  summary << "Subset size: " << subset.size() << "\n";
  summary << "Subset diameter: " << CalcClusterDiameter(subset) << "\n";
  summary << "Requested root: " << FormatPoint(requested_root) << "\n";
  summary << "Synthetic root: " << FormatPoint(estimate.synthetic_root) << "\n";
  summary << "Legalized root: " << FormatPoint(estimate.legalized_root) << "\n";
  summary << "Routed root: " << FormatPoint(estimate.routed_root) << "\n";
  summary << "Legalization manhattan distance: " << legalization_manhattan << "\n";
  summary << "Max expected local shift: " << max_expected_local_shift << "\n";
  summary << "Route success: " << (estimate.route_success ? "true" : "false") << "\n";
  summary << "Pin cap: " << estimate.pin_cap << "\n";
  summary << "Wire cap: " << estimate.wire_cap << "\n";
  summary << "Total cap: " << estimate.total_cap << "\n";
  summary << "Artifacts: report.log\n";
  return summary.str();
}

auto TryEvaluateRootCollision(const RealClockLoads& real_clock_loads, RootCollisionEvaluation& evaluation) -> bool
{
  CollectRootCollisionSubset(real_clock_loads, evaluation.subset);
  if (::testing::Test::HasFatalFailure()) {
    return false;
  }

  evaluation.config = BuildRootCollisionConfig(evaluation.subset.size());
  evaluation.requested_root = ResolveExactCapSyntheticRoot(evaluation.subset, evaluation.config);
  ValidateRequestedRootCollision(evaluation.subset, evaluation.requested_root);
  if (::testing::Test::HasFatalFailure()) {
    return false;
  }

  evaluation.estimate = EstimateExactElectrical(evaluation.subset, evaluation.config);
  ValidateRootCollisionRoots(evaluation.subset, evaluation.requested_root, evaluation.estimate);
  if (::testing::Test::HasFatalFailure()) {
    return false;
  }

  evaluation.max_expected_local_shift = CalcMaxExpectedLocalShift(real_clock_loads.dbu_per_micron);
  evaluation.legalization_manhattan = ValidateRootCollisionShift(evaluation.estimate, evaluation.max_expected_local_shift);
  return !::testing::Test::HasFatalFailure();
}
}  // namespace

auto RunRealTechExactCapRegression() -> void
{
  PreparedRealTechCase prepared;
  if (!TryPrepareRealTechCase(kExactCapRegressionTitle, kExactCapRegressionTestName, "Failed to prepare exact-cap output dir.", prepared)) {
    GTEST_SKIP() << "Real-tech assets are unavailable: " << prepared.setup_state->summary;
  }

  const RealClockLoads* real_clock_loads = nullptr;
  if (!TryLoadExactCapRealClockLoads("Exact-cap regression requires real CTS loads with inst/net semantics.", real_clock_loads)) {
    return;
  }

  std::string report;
  if (!TryBuildExactCapRegressionReport(prepared.output_dir, *real_clock_loads, report)) {
    return;
  }

  EmitCaseReport(prepared.output_dir, std::string{kExactCapRegressionTitle}, report);
}

auto RunRealTechExactCapRootCollisionIsLegalizedBeforeRouting() -> void
{
  PreparedRealTechCase prepared;
  if (!TryPrepareRealTechCase(kRootCollisionTitle, kRootCollisionTestName, "Failed to prepare root-collision output dir.", prepared)) {
    GTEST_SKIP() << "Real-tech assets are unavailable: " << prepared.setup_state->summary;
  }

  const RealClockLoads* real_clock_loads = nullptr;
  if (!TryLoadExactCapRealClockLoads("Exact-cap root legalization regression requires real CTS loads with inst/net semantics.",
                                     real_clock_loads)) {
    return;
  }

  RootCollisionEvaluation evaluation;
  if (!TryEvaluateRootCollision(*real_clock_loads, evaluation)) {
    return;
  }

  EmitCaseReport(prepared.output_dir, std::string{kRootCollisionTitle},
                 BuildRootCollisionSummary(evaluation.subset, evaluation.requested_root, evaluation.estimate,
                                           evaluation.legalization_manhattan, evaluation.max_expected_local_shift));
}

}  // namespace icts_test::linear_clustering::realtech
