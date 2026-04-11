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
 * @file LinearClusteringSyntheticSweep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic sweep and strategy-regression support.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "clustering/Clustering.hh"
#include "common/data/TestDataGenerator.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/linear_clustering/artifact/ClusterArtifactSupport.hh"
#include "common/linear_clustering/metrics/ClusterGeometrySupport.hh"
#include "common/types/TestDataTypes.hh"
#include "common/visualization/TestVisualization.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/ClusteringEvaluator.hh"
#include "module/topology/linear_clustering/SequenceSplitter.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic {
namespace {

struct StrategyCandidate
{
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  detail::SweepConfigSpec sweep{};
};

auto ToDetailClusterMetrics(const common::linear_clustering::ClusterMetrics& metrics) -> detail::ClusterMetrics
{
  return {
      .cluster_count = metrics.cluster_count,
      .singleton_cluster_count = metrics.singleton_cluster_count,
      .min_cluster_size = metrics.min_cluster_size,
      .max_cluster_size = metrics.max_cluster_size,
      .avg_cluster_size = metrics.avg_cluster_size,
      .min_cluster_diameter = metrics.min_cluster_diameter,
      .max_cluster_diameter = metrics.max_cluster_diameter,
  };
}

void CapturePartitionCostSummary(const icts::PartitionScore& partition, detail::StrategySweepObservation& observation)
{
  observation.partition_score = partition.total_score;
  observation.selected_rotation_offset = partition.rotation_offset;
  if (partition.clusters.empty()) {
    observation.min_cluster_score = 0.0;
    observation.max_cluster_score = 0.0;
    observation.avg_cluster_score = 0.0;
    return;
  }

  double total_cluster_score = 0.0;
  observation.min_cluster_score = partition.clusters.front().total_score;
  observation.max_cluster_score = partition.clusters.front().total_score;
  for (const auto& cluster_score : partition.clusters) {
    total_cluster_score += cluster_score.total_score;
    observation.min_cluster_score = std::min(observation.min_cluster_score, cluster_score.total_score);
    observation.max_cluster_score = std::max(observation.max_cluster_score, cluster_score.total_score);
  }
  observation.avg_cluster_score = total_cluster_score / static_cast<double>(partition.clusters.size());
}

auto EmitSweepReport(const std::filesystem::path& output_dir, const std::string& title, const std::string& report) -> void
{
  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report)) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = title, .content = report});
}

auto BuildStrategyCandidates(const std::array<icts::LinearOrderStrategy, 4>& order_strategies,
                             const std::array<icts::LinearSplitStrategy, 3>& split_strategies,
                             const std::vector<detail::SweepConfigSpec>& sweep_candidates) -> std::vector<StrategyCandidate>
{
  std::vector<StrategyCandidate> candidates;
  candidates.reserve(order_strategies.size() * split_strategies.size() * sweep_candidates.size());
  for (const auto order_strategy : order_strategies) {
    for (const auto split_strategy : split_strategies) {
      for (const auto& sweep : sweep_candidates) {
        candidates.push_back(StrategyCandidate{
            .order_strategy = order_strategy,
            .split_strategy = split_strategy,
            .sweep = sweep,
        });
      }
    }
  }
  return candidates;
}

auto EvaluateStrategyCandidate(const StrategyCandidate& candidate, const std::vector<icts::Pin*>& loads,
                               const std::filesystem::path& output_dir, std::vector<std::string>& artifact_names,
                               detail::StrategySweepObservation& observation) -> void
{
  auto config = detail::MakeConstraintConfig(detail::ConstraintConfigSpec{
      .max_fanout = detail::kOrderStrategyCoverageFanout,
      .max_diameter = detail::kOrderStrategyCoverageDiameter,
  });
  config.order_strategy = candidate.order_strategy;
  config.split_strategy = candidate.split_strategy;
  config.sweep_mode = candidate.sweep.sweep_mode;
  config.strided_sweep_count = candidate.sweep.strided_sweep_count;

  observation.order_strategy = candidate.order_strategy;
  observation.split_strategy = candidate.split_strategy;
  observation.sweep_mode = candidate.sweep.sweep_mode;
  observation.strided_sweep_count = candidate.sweep.strided_sweep_count;
  const auto sweep_resolution = icts::SequenceSplitter::resolveSweepOffsets(loads.size(), config);
  observation.effective_sweep_mode = sweep_resolution.effective_mode;
  observation.prefix_count = sweep_resolution.prefix_count;
  observation.resolved_strided_count = sweep_resolution.strided_count;
  observation.degraded_to_prefix = sweep_resolution.degraded_to_prefix;
  observation.resolved_offsets = sweep_resolution.offsets;
  observation.note = detail::BuildSweepNote(loads.size(), config);

  detail::ClusteringInvocation invocation;
  detail::CaptureConstraintExpectation(config, invocation);
  icts::ClusterResult result;
  icts::PartitionScore partition;
  detail::RunDetailedLinearClustering(loads, config, result, partition);
  ASSERT_FALSE(result.clusters.empty()) << "Strategy "
                                        << detail::MakeStrategyLabel(candidate.order_strategy, candidate.split_strategy,
                                                                     candidate.sweep.sweep_mode, candidate.sweep.strided_sweep_count)
                                        << " produced empty result.";

  std::string error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;

  std::unordered_map<const icts::Pin*, std::size_t> cluster_map;
  std::vector<icts::Point<int>> centers;
  std::vector<std::size_t> cluster_sizes;
  ASSERT_TRUE(common::linear_clustering::BuildClusterArtifacts(result, loads, cluster_map, centers, cluster_sizes, error)) << error;

  const auto strategy_label = detail::MakeStrategyLabel(candidate.order_strategy, candidate.split_strategy, candidate.sweep.sweep_mode,
                                                        candidate.sweep.strided_sweep_count);
  const auto svg_name = "strategy_" + strategy_label + ".svg";
  const auto svg_path = output_dir / svg_name;
  EXPECT_TRUE(common::visualization::WriteClusterSvg(svg_path.string(), loads, cluster_map, centers))
      << "Failed to write svg: " << svg_path.string();
  artifact_names.push_back(svg_name);

  observation.empty_result = false;
  observation.legal = true;
  observation.metrics = ToDetailClusterMetrics(common::linear_clustering::CollectClusterMetrics(result));
  observation.selection_score = partition.total_score;
  CapturePartitionCostSummary(partition, observation);
}

auto BuildSweepResolutionReport(const std::string& test_name, const std::string& config_line, const icts::SweepResolution& sweep_resolution,
                                const std::string& extra_line = std::string{}) -> std::string
{
  std::ostringstream report;
  report << "Test: " << test_name << "\n";
  report << "Mode: sequence utility regression\n";
  report << "Config: " << config_line << "\n";
  if (!extra_line.empty()) {
    report << extra_line << "\n";
  }
  report << "Resolved offsets: " << detail::FormatResolvedOffsets(sweep_resolution.offsets) << "\n";
  report << "Artifacts: report.log\n";
  return report.str();
}

auto ExpectPrefixSweepResolution(const icts::SweepResolution& sweep_resolution) -> void
{
  ASSERT_EQ(sweep_resolution.requested_mode, icts::LinearSweepMode::kPrefixSweep);
  ASSERT_EQ(sweep_resolution.effective_mode, icts::LinearSweepMode::kPrefixSweep);
  ASSERT_FALSE(sweep_resolution.degraded_to_prefix);
  EXPECT_EQ(sweep_resolution.prefix_count, detail::kSweepRegressionPrefixFanout);
  EXPECT_EQ(sweep_resolution.offsets, (std::vector<std::size_t>{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}));
}

auto ExpectFullRingOffsets(const std::vector<std::size_t>& offsets) -> void
{
  ASSERT_EQ(offsets.size(), detail::kCombinedSweepRegressionExpectedOffsets);
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    EXPECT_EQ(offsets.at(index), index);
  }
}

auto ExpectCombinedSweepResolution(const icts::SweepResolution& sweep_resolution) -> void
{
  ASSERT_EQ(sweep_resolution.requested_mode, icts::LinearSweepMode::kPrefixAndStridedSweep);
  ASSERT_EQ(sweep_resolution.effective_mode, icts::LinearSweepMode::kPrefixAndStridedSweep);
  ASSERT_FALSE(sweep_resolution.degraded_to_prefix);
  ExpectFullRingOffsets(sweep_resolution.offsets);
}

auto BuildOrderedLoadsFromGenerated(const GeneratedPins& generated) -> std::vector<icts::OrderedLoad>
{
  std::vector<icts::OrderedLoad> ordered_loads;
  ordered_loads.reserve(generated.loads.size());
  for (std::size_t index = 0; index < generated.loads.size(); ++index) {
    ordered_loads.push_back(
        {.pin = generated.loads.at(index), .location = generated.loads.at(index)->get_location(), .original_index = index});
  }
  return ordered_loads;
}

auto ExpectSingletonPenaltyPartition(const icts::PartitionScore& partition) -> void
{
  ASSERT_TRUE(partition.legal);
  ASSERT_EQ(partition.clusters.size(), 2U);
  EXPECT_DOUBLE_EQ(partition.clusters.at(0).total_score, static_cast<double>(detail::kSingletonPenaltyMaxDiameter));
  EXPECT_DOUBLE_EQ(partition.clusters.at(1).total_score, 100.0);
  EXPECT_DOUBLE_EQ(partition.total_score, static_cast<double>(detail::kSingletonPenaltyMaxDiameter) + 100.0);
}

auto BuildSingletonPenaltyReport(const icts::PartitionScore& partition) -> std::string
{
  std::ostringstream report;
  report << "Test: LinearClusteringScoringTest.MaxDiameterScoringPenalizesZeroDiameterSingletons\n";
  report << "Mode: scoring regression\n";
  report << "Config: max_diameter=" << detail::kSingletonPenaltyMaxDiameter << ", scoring_strategy=max_diameter\n";
  report << "Partition: singleton + two-pin segment\n";
  report << "Observed cluster scores: [" << partition.clusters.at(0).total_score << "," << partition.clusters.at(1).total_score << "]\n";
  report << "Expected singleton penalty: zero-diameter singleton pays max_diameter rather than 0.\n";
  report << "Artifacts: report.log\n";
  return report.str();
}

}  // namespace

auto RunRemainingOrderStrategiesProduceLegalResults() -> void
{
  auto generated = common::data::MakeGaussianMixture(detail::kOrderStrategyCoverageLoadCount,
                                                     CanvasSize{.width = detail::kCanvasWidth, .height = detail::kCanvasHeight},
                                                     detail::kOrderStrategyCoverageSeed);
  ASSERT_EQ(generated.loads.size(), detail::kOrderStrategyCoverageLoadCount);

  const std::array<icts::LinearOrderStrategy, 4> order_strategies = {
      icts::LinearOrderStrategy::kContinuousHilbert,
      icts::LinearOrderStrategy::kDiscreteHilbert,
      icts::LinearOrderStrategy::kDensityScaledContinuousHilbert,
      icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert,
  };
  const std::array<icts::LinearSplitStrategy, 3> split_strategies = {
      icts::LinearSplitStrategy::kForwardGreedy,
      icts::LinearSplitStrategy::kReverseGreedy,
      icts::LinearSplitStrategy::kBidirectionalGreedy,
  };
  const auto sweep_candidates = detail::BuildSweepCandidates(generated.loads.size());
  const auto candidates = BuildStrategyCandidates(order_strategies, split_strategies, sweep_candidates);

  std::vector<detail::StrategySweepObservation> sweep_observations;
  sweep_observations.reserve(candidates.size());
  const auto output_dir = detail::PrepareSyntheticOutputDir("remaining_order_strategies_produce_legal_results");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare strategy coverage output dir.";
  std::vector<std::string> artifact_names;

  for (const auto& candidate : candidates) {
    detail::StrategySweepObservation observation;
    EvaluateStrategyCandidate(candidate, generated.loads, output_dir, artifact_names, observation);
    sweep_observations.push_back(observation);
  }

  ASSERT_EQ(sweep_observations.size(), candidates.size());
  const auto selected_index = detail::PickBestStrategy(sweep_observations);
  ASSERT_TRUE(selected_index.has_value()) << "No legal candidate found in strategy sweep.";

  const std::string csv_name = "strategy_space.csv";
  const auto csv_path = output_dir / csv_name;
  const auto csv_content = detail::BuildStrategySweepCsv(sweep_observations, selected_index);
  EXPECT_TRUE(common::io::WriteTextLog(csv_path, csv_content)) << "Failed to write csv: " << csv_path.string();

  std::ostringstream input_summary;
  input_summary << "load_count=" << generated.loads.size() << ", distribution=gaussian_mixture, seed=" << detail::kOrderStrategyCoverageSeed
                << ", max_fanout=" << detail::kOrderStrategyCoverageFanout << ", max_diameter=" << detail::kOrderStrategyCoverageDiameter
                << ", sweep_candidates=" << detail::FormatSweepCandidates(sweep_candidates);
  auto report_artifact_names = artifact_names;
  report_artifact_names.push_back(csv_name);
  const auto report = detail::BuildStrategySweepReport("LinearClusteringTest.RemainingOrderStrategiesProduceLegalResults",
                                                       input_summary.str(), sweep_observations, selected_index, report_artifact_names);
  EmitSweepReport(output_dir, "remaining_order_strategies_produce_legal_results", report);
}

auto RunPrefixSweepResolvesSinkStyleOffsets() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("prefix_sweep_resolves_sink_style_offsets");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare prefix-sweep output dir.";

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kSweepRegressionPrefixFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.sweep_mode = icts::LinearSweepMode::kPrefixSweep;

  const auto sweep_resolution = icts::SequenceSplitter::resolveSweepOffsets(detail::kSweepRegressionLoadCount, config);
  ExpectPrefixSweepResolution(sweep_resolution);

  const auto report = BuildSweepResolutionReport("LinearClusteringSweepTest.PrefixSweepResolvesSinkStyleOffsets",
                                                 "load_count=34, max_fanout=8, sweep_mode=prefix_sweep", sweep_resolution);
  EmitSweepReport(output_dir, "prefix_sweep_resolves_sink_style_offsets", report);
}

auto RunStridedSweepSamplesDistinctRingStarts() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("strided_sweep_samples_distinct_ring_starts");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare strided-sweep output dir.";

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kSweepRegressionPrefixFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.sweep_mode = icts::LinearSweepMode::kStridedSweep;
  config.strided_sweep_count = detail::kDefaultStridedSweepCount;

  const auto sweep_resolution = icts::SequenceSplitter::resolveSweepOffsets(detail::kSweepRegressionLoadCount, config);
  ASSERT_EQ(sweep_resolution.requested_mode, icts::LinearSweepMode::kStridedSweep);
  ASSERT_EQ(sweep_resolution.effective_mode, icts::LinearSweepMode::kStridedSweep);
  ASSERT_FALSE(sweep_resolution.degraded_to_prefix);
  EXPECT_EQ(sweep_resolution.offsets, (std::vector<std::size_t>{0U, 8U, 17U, 25U}));

  const auto report = BuildSweepResolutionReport("LinearClusteringSweepTest.StridedSweepSamplesDistinctRingStarts",
                                                 "load_count=34, strided_sweep_count=4, sweep_mode=strided_sweep", sweep_resolution);
  EmitSweepReport(output_dir, "strided_sweep_samples_distinct_ring_starts", report);
}

auto RunCombinedSweepNormalizesToFullSequenceWhenAnchorWindowsCoverRing() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("combined_sweep_normalizes_to_full_sequence_when_anchor_windows_cover_ring");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare combined-sweep output dir.";

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kCombinedSweepRegressionPrefixFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.sweep_mode = icts::LinearSweepMode::kPrefixAndStridedSweep;
  config.strided_sweep_count = 2U;

  const auto sweep_resolution = icts::SequenceSplitter::resolveSweepOffsets(detail::kCombinedSweepRegressionLoadCount, config);
  ExpectCombinedSweepResolution(sweep_resolution);
  const auto report = BuildSweepResolutionReport(
      "LinearClusteringSweepTest.CombinedSweepNormalizesToFullSequenceWhenAnchorWindowsCoverRing",
      "load_count=60, max_fanout=32, strided_sweep_count=2, sweep_mode=prefix_and_strided_sweep", sweep_resolution,
      "Expected behavior: strided anchors [0,30] each expand to a 32-wide sequential window, and the window union covers the full ring so "
      "resolved_offsets normalize to [0..59].");
  EmitSweepReport(output_dir, "combined_sweep_normalizes_to_full_sequence_when_anchor_windows_cover_ring", report);
}

auto RunMaxDiameterScoringPenalizesZeroDiameterSingletons() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("max_diameter_scoring_penalizes_zero_diameter_singletons");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare scoring-regression output dir.";

  const std::vector<icts::Point<int>> points = {{0, 0}, {100, 0}, {200, 0}};
  auto generated = common::data::BuildPinsFromPoints(
      points, CanvasSize{.width = detail::kSingletonPenaltyCanvasWidth, .height = detail::kSingletonPenaltyCanvasHeight},
      "singleton_penalty");
  ASSERT_EQ(generated.loads.size(), points.size());

  const auto ordered_loads = BuildOrderedLoadsFromGenerated(generated);

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, 2U);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.max_diameter = detail::kSingletonPenaltyMaxDiameter;
  config.scoring_strategy = icts::LinearScoringStrategy::kMaxDiameter;

  icts::ClusteringEvaluator evaluator;
  const std::vector<icts::SegmentRange> segments = {
      {.begin = 0U, .end = 1U},
      {.begin = 1U, .end = 3U},
  };
  const auto partition = evaluator.evaluatePartition(ordered_loads, segments, config, false);
  ExpectSingletonPenaltyPartition(partition);
  EmitSweepReport(output_dir, "max_diameter_scoring_penalizes_zero_diameter_singletons", BuildSingletonPenaltyReport(partition));
}

}  // namespace icts_test::linear_clustering::synthetic
