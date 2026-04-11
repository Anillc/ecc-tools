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
 * @file LinearClusteringSyntheticConstraint.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic constraint-regression support.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "TopologyConfig.hh"
#include "clustering/Clustering.hh"
#include "common/data/TestDataGenerator.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/linear_clustering/metrics/ClusterGeometrySupport.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::linear_clustering::synthetic {
namespace {

struct FanoutHistory
{
  std::size_t previous_cluster_count = 0;
  std::size_t previous_max_cluster_size = std::numeric_limits<std::size_t>::max();
};

struct DiameterHistory
{
  bool previous_non_empty = false;
  std::size_t previous_cluster_count = 0;
  int previous_max_diameter = std::numeric_limits<int>::max();
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

auto EmitConstraintReport(const std::filesystem::path& output_dir, const std::string& title, const std::string& report) -> void
{
  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report)) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = title, .content = report});
}

auto EvaluateFanoutLimit(const std::vector<icts::Pin*>& loads, std::size_t fanout_limit, FanoutHistory& history,
                         detail::LadderObservation& observation) -> void
{
  auto config = detail::MakeConstraintConfig(
      detail::ConstraintConfigSpec{.max_fanout = fanout_limit, .max_diameter = detail::kFanoutNeutralDiameter});
  detail::ClusteringInvocation invocation;
  detail::CaptureConstraintExpectation(config, invocation);

  const auto result = icts::Clustering::linearClustering(loads, config);
  ASSERT_FALSE(result.clusters.empty()) << "Line geometry should remain clusterable under fanout limit " << fanout_limit;

  std::string error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;

  const auto metrics = ToDetailClusterMetrics(common::linear_clustering::CollectClusterMetrics(result));
  const auto minimum_legal_cluster_count = (detail::kLineLoadCount + fanout_limit - 1U) / fanout_limit;
  EXPECT_GE(metrics.cluster_count, minimum_legal_cluster_count);
  EXPECT_LE(metrics.max_cluster_size, fanout_limit);
  EXPECT_GE(metrics.cluster_count, history.previous_cluster_count);
  EXPECT_LE(metrics.max_cluster_size, history.previous_max_cluster_size);
  history.previous_cluster_count = metrics.cluster_count;
  history.previous_max_cluster_size = metrics.max_cluster_size;

  observation = detail::LadderObservation{.label = "fanout_" + std::to_string(fanout_limit),
                                          .max_fanout = fanout_limit,
                                          .max_diameter = detail::kFanoutNeutralDiameter,
                                          .empty_result = false,
                                          .metrics = metrics};
}

auto EvaluateDiameterLimit(const std::vector<icts::Pin*>& loads, int diameter_limit, DiameterHistory& history,
                           detail::LadderObservation& observation) -> void
{
  auto config = detail::MakeConstraintConfig(detail::ConstraintConfigSpec{
      .max_fanout = loads.size(),
      .max_diameter = static_cast<double>(diameter_limit),
  });
  detail::ClusteringInvocation invocation;
  detail::CaptureConstraintExpectation(config, invocation);

  const auto result = icts::Clustering::linearClustering(loads, config);
  if (result.clusters.empty()) {
    history.previous_non_empty = false;
    observation = detail::LadderObservation{.label = "diameter_" + std::to_string(diameter_limit),
                                            .max_fanout = loads.size(),
                                            .max_diameter = static_cast<double>(diameter_limit),
                                            .empty_result = true,
                                            .metrics = {}};
    return;
  }

  std::string error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;

  const auto metrics = ToDetailClusterMetrics(common::linear_clustering::CollectClusterMetrics(result));
  EXPECT_LE(metrics.max_cluster_diameter, diameter_limit);
  if (history.previous_non_empty) {
    EXPECT_GE(metrics.cluster_count, history.previous_cluster_count);
    EXPECT_LE(metrics.max_cluster_diameter, history.previous_max_diameter);
  }
  history.previous_non_empty = true;
  history.previous_cluster_count = metrics.cluster_count;
  history.previous_max_diameter = metrics.max_cluster_diameter;

  observation = detail::LadderObservation{.label = "diameter_" + std::to_string(diameter_limit),
                                          .max_fanout = loads.size(),
                                          .max_diameter = static_cast<double>(diameter_limit),
                                          .empty_result = false,
                                          .metrics = metrics};
}

auto EvaluatePressureCase(const std::vector<icts::Pin*>& loads, const std::string& label, std::size_t max_fanout, double max_diameter,
                          detail::LadderObservation& observation) -> void
{
  auto config = detail::MakeConstraintConfig(detail::ConstraintConfigSpec{.max_fanout = max_fanout, .max_diameter = max_diameter});
  detail::ClusteringInvocation invocation;
  detail::CaptureConstraintExpectation(config, invocation);
  const auto result = icts::Clustering::linearClustering(loads, config);
  if (result.clusters.empty()) {
    observation = detail::LadderObservation{
        .label = label,
        .max_fanout = max_fanout,
        .max_diameter = max_diameter,
        .empty_result = true,
        .metrics = {},
    };
    return;
  }

  std::string error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;
  observation = detail::LadderObservation{
      .label = label,
      .max_fanout = max_fanout,
      .max_diameter = max_diameter,
      .empty_result = false,
      .metrics = ToDetailClusterMetrics(common::linear_clustering::CollectClusterMetrics(result)),
  };
}

}  // namespace

auto RunTightConstraintsAreLegalOrEmpty() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("tight_constraints_are_legal_or_empty");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare tight-constraint output dir.";

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kTightSeedMinClusterSize);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.max_diameter = static_cast<int>(detail::kTightMaxDiameter);
  config.max_fanout = detail::kTightMaxFanout;

  auto generated = common::data::MakeWeightedQuadrants(detail::kTightLoadCount,
                                                       CanvasSize{.width = detail::kCanvasWidth, .height = detail::kCanvasHeight},
                                                       detail::kTightSeed, detail::kTightQuadrantWeights);
  ASSERT_FALSE(generated.loads.empty());

  detail::ClusteringInvocation invocation;
  detail::CaptureConstraintExpectation(config, invocation);
  const auto result = icts::Clustering::linearClustering(generated.loads, config);
  if (result.clusters.empty()) {
    std::ostringstream report;
    report << "Test: LinearClusteringTest.TightConstraintsAreLegalOrEmpty\n";
    report << "Mode: synthetic legality smoke test\n";
    report << "Input: load_count=" << generated.loads.size() << ", max_fanout=" << config.max_fanout
           << ", max_diameter=" << config.max_diameter << ", seed=" << detail::kTightSeed << "\n";
    report << "Observed result: no legal partition was found under the tight constraints.\n";
    report << "Artifacts: report.log\n";
    EmitConstraintReport(output_dir, "tight_constraints_are_legal_or_empty", report.str());
    SUCCEED() << "No legal partition was found under tight constraints; empty result is accepted.";
    return;
  }

  std::string error;
  ASSERT_TRUE(detail::ValidateClusterLegality(result, invocation, error)) << error;

  const auto metrics = ToDetailClusterMetrics(common::linear_clustering::CollectClusterMetrics(result));
  std::ostringstream report;
  report.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report << std::setprecision(2);
  report << "Test: LinearClusteringTest.TightConstraintsAreLegalOrEmpty\n";
  report << "Mode: synthetic legality smoke test\n";
  report << "Input: load_count=" << generated.loads.size() << ", max_fanout=" << config.max_fanout
         << ", max_diameter=" << config.max_diameter << ", seed=" << detail::kTightSeed << "\n";
  report << "Observed result: " << detail::FormatMetricsLine(metrics) << "\n";
  report << "Artifacts: report.log\n";
  EmitConstraintReport(output_dir, "tight_constraints_are_legal_or_empty", report.str());
}

auto RunFanoutLadderIsMonotonic() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("constraint_fanout_ladder_is_monotonic");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare fanout ladder output dir.";

  const auto generated = detail::MakeLineLoads(detail::kLineLoadCount, detail::kLineStartX, detail::kLineStepX, detail::kLineY);
  ASSERT_EQ(generated.loads.size(), detail::kLineLoadCount);

  std::vector<detail::LadderObservation> observations;
  observations.reserve(detail::kFanoutLadderLimits.size());

  FanoutHistory history;
  for (const auto fanout_limit : detail::kFanoutLadderLimits) {
    detail::LadderObservation observation;
    EvaluateFanoutLimit(generated.loads, fanout_limit, history, observation);
    observations.push_back(observation);
  }

  const auto report = detail::BuildConstraintObservationReport(
      "LinearClusteringConstraintTest.FanoutLadderIsMonotonic",
      "line_loads=64, geometry=line, diameter_limit=fanout-neutral, fanout_limits=[32,16,8,4]", observations);
  EmitConstraintReport(output_dir, "constraint_fanout_ladder_is_monotonic", report);
}

auto RunDiameterLadderIsMonotonic() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("constraint_diameter_ladder_is_monotonic");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare diameter ladder output dir.";

  const auto generated = detail::MakeSeparatedIslandLoads();
  ASSERT_EQ(generated.loads.size(), detail::kIslandLoadCount);

  std::vector<detail::LadderObservation> observations;
  observations.reserve(detail::kDiameterLadderLimits.size());

  DiameterHistory history;
  for (const auto diameter_limit : detail::kDiameterLadderLimits) {
    detail::LadderObservation observation;
    EvaluateDiameterLimit(generated.loads, diameter_limit, history, observation);
    observations.push_back(observation);
  }

  const auto report = detail::BuildConstraintObservationReport(
      "LinearClusteringConstraintTest.DiameterLadderIsMonotonic",
      "island_loads=48, geometry=three_separated_islands, fanout_limit=48, diameter_limits=[30000,12000,3000]", observations);
  EmitConstraintReport(output_dir, "constraint_diameter_ladder_is_monotonic", report);
}

auto RunTighterCombinedConstraintsIncreasePressure() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("constraint_tighter_combined_constraints_increase_pressure");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare multi-pressure output dir.";

  auto generated = common::data::MakeWeightedQuadrants(detail::kTightLoadCount,
                                                       CanvasSize{.width = detail::kCanvasWidth, .height = detail::kCanvasHeight},
                                                       detail::kTightSeed, detail::kUnevenQuadrantWeights);
  ASSERT_FALSE(generated.loads.empty());

  detail::LadderObservation relaxed;
  EvaluatePressureCase(generated.loads, "relaxed", detail::kPressureRelaxedFanout, detail::kPressureRelaxedDiameter, relaxed);
  ASSERT_FALSE(relaxed.empty_result) << "Relaxed multi-constraint case should produce a legal partition.";
  detail::LadderObservation tight;
  EvaluatePressureCase(generated.loads, "tight", detail::kPressureTightFanout, detail::kPressureTightDiameter, tight);

  const std::vector<detail::LadderObservation> observations = {relaxed, tight};
  if (tight.empty_result) {
    const auto report = detail::BuildConstraintObservationReport(
        "LinearClusteringConstraintTest.TighterCombinedConstraintsIncreasePressure",
        "weighted_quadrants=512, seed=20260409, relaxed=(fanout=24,diameter=9000), tight=(fanout=8,diameter=2500)", observations);
    EmitConstraintReport(output_dir, "constraint_tighter_combined_constraints_increase_pressure", report);
    SUCCEED() << "Tight multi-constraint case returned empty result, which is accepted for infeasible pressure cases.";
    return;
  }

  EXPECT_GE(tight.metrics.cluster_count, relaxed.metrics.cluster_count);
  EXPECT_LE(tight.metrics.max_cluster_size, relaxed.metrics.max_cluster_size);
  EXPECT_LE(tight.metrics.max_cluster_diameter, relaxed.metrics.max_cluster_diameter);

  const auto report = detail::BuildConstraintObservationReport(
      "LinearClusteringConstraintTest.TighterCombinedConstraintsIncreasePressure",
      "weighted_quadrants=512, seed=20260409, relaxed=(fanout=24,diameter=9000), tight=(fanout=8,diameter=2500)", observations);
  EmitConstraintReport(output_dir, "constraint_tighter_combined_constraints_increase_pressure", report);
}

}  // namespace icts_test::linear_clustering::synthetic
