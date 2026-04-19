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
 * @file LinearClusteringSyntheticInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Internal helpers shared by synthetic linear clustering support files.
 */

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"

namespace icts {
class Pin;
template <typename T>
class Point;
struct ClusterResult;
}  // namespace icts

namespace icts_test {
struct GeneratedPins;
}  // namespace icts_test

namespace icts_test::linear_clustering::synthetic::detail {

inline constexpr int kCanvasWidth = 10000;
inline constexpr int kCanvasHeight = 8000;
inline constexpr std::size_t kDefaultMinClusterSize = 16;
inline constexpr double kDefaultMaxDiameter = 3000.0;
inline constexpr std::size_t kSingletonClusterSize = 1;
inline constexpr std::size_t kTightSeedMinClusterSize = 8;
inline constexpr double kTightMaxDiameter = 120.0;
inline constexpr std::size_t kTightMaxFanout = 6;
inline constexpr std::size_t kTightLoadCount = 512;
inline constexpr unsigned kTightSeed = 20260409U;
inline constexpr std::array<double, 4> kTightQuadrantWeights = {0.70, 0.10, 0.10, 0.10};
inline constexpr std::array<double, 4> kUnevenQuadrantWeights = {0.55, 0.15, 0.20, 0.10};
inline constexpr std::size_t kLineLoadCount = 64;
inline constexpr int kLineStartX = 200;
inline constexpr int kLineStepX = 120;
inline constexpr int kLineY = 1800;
inline constexpr double kFanoutNeutralDiameter = 200000.0;
inline constexpr std::array<std::size_t, 4> kFanoutLadderLimits = {32U, 16U, 8U, 4U};
inline constexpr std::size_t kIslandLoadCount = 48;
inline constexpr std::array<int, 3> kIslandXOffsets = {1200, 9200, 17200};
inline constexpr int kIslandBaseY = 1800;
inline constexpr int kIslandGridStep = 400;
inline constexpr int kIslandGridSide = 4;
inline constexpr int kIslandCanvasWidth = 22000;
inline constexpr int kIslandCanvasHeight = 6000;
inline constexpr std::array<int, 3> kDiameterLadderLimits = {30000, 12000, 3000};
inline constexpr std::size_t kPressureRelaxedFanout = 24U;
inline constexpr double kPressureRelaxedDiameter = 9000.0;
inline constexpr std::size_t kPressureTightFanout = 8U;
inline constexpr double kPressureTightDiameter = 2500.0;
inline constexpr std::size_t kSyntheticSweepNormalSmallLoadCount = 256U;
inline constexpr unsigned kSyntheticSweepNormalSmallSeed = 112233U;
inline constexpr std::size_t kSyntheticSweepNormalLargeLoadCount = 1024U;
inline constexpr unsigned kSyntheticSweepNormalLargeSeed = 445566U;
inline constexpr std::size_t kSyntheticSweepMixtureLoadCount = 768U;
inline constexpr unsigned kSyntheticSweepMixtureSeed = 778899U;
inline constexpr std::size_t kSyntheticSweepQuadrantUnevenLoadCount = 640U;
inline constexpr unsigned kSyntheticSweepQuadrantUnevenSeed = 991122U;
inline constexpr std::size_t kOrderStrategyCoverageLoadCount = 384U;
inline constexpr unsigned kOrderStrategyCoverageSeed = 20260410U;
inline constexpr std::size_t kOrderStrategyCoverageFanout = 12U;
inline constexpr double kOrderStrategyCoverageDiameter = 3500.0;
inline constexpr std::size_t kDefaultStridedSweepCount = 4U;
inline constexpr std::size_t kSweepRegressionLoadCount = 34U;
inline constexpr std::size_t kSweepRegressionPrefixFanout = 8U;
inline constexpr std::size_t kCombinedSweepRegressionLoadCount = 60U;
inline constexpr std::size_t kCombinedSweepRegressionPrefixFanout = 32U;
inline constexpr std::size_t kCombinedSweepRegressionExpectedOffsets = kCombinedSweepRegressionLoadCount;
inline constexpr int kSingletonPenaltyCanvasWidth = 240;
inline constexpr int kSingletonPenaltyCanvasHeight = 100;
inline constexpr int kSingletonPenaltyMaxDiameter = 120;
inline constexpr int kReferenceCanvasExtent = 120;
inline constexpr int kDensityScaledDiscreteRegressionCanvasExtent = 128;
inline constexpr int kBalancedMarginalCellSpan = 32;
inline constexpr int kBalancedMarginalLocalOffsetNear = 4;
inline constexpr int kBalancedMarginalLocalOffsetMid = 12;
inline constexpr int kBalancedMarginalLocalOffsetFar = 24;
inline constexpr double kSinkReferenceTerminalTheta = 0.5;
inline constexpr double kSinkReferenceCenterCoord = 0.5;
inline constexpr double kSinkReferenceAxisScale = 2.0;
inline constexpr double kSinkReferenceQuadrantDivisor = 4.0;
inline constexpr double kSinkReferencePhaseOffset = 7.0 / 8.0;

struct ClusterMetrics
{
  std::size_t cluster_count = 0;
  std::size_t singleton_cluster_count = 0;
  std::size_t min_cluster_size = 0;
  std::size_t max_cluster_size = 0;
  double avg_cluster_size = 0.0;
  int min_cluster_diameter = 0;
  int max_cluster_diameter = 0;
};

struct ClusteringInvocation
{
  bool used_linear_api = true;
  bool has_constraint_expectation = false;
  std::size_t max_fanout = 0;
  double max_diameter = 0.0;
  std::string api_name = "Clustering::linearClustering(loads, config)";
};

struct LadderObservation
{
  std::string label;
  std::size_t max_fanout = 0;
  double max_diameter = 0.0;
  bool empty_result = false;
  ClusterMetrics metrics;
};

struct ConstraintConfigSpec
{
  std::size_t max_fanout = 0;
  double max_diameter = 0.0;
};

struct StrategySweepObservation
{
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::DiscreteHilbertEncoding discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kSinkThetaCell;
  icts::HilbertTransform hilbert_transform = icts::HilbertTransform::kIdentity;
  int order_bits = 16;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  icts::LinearSweepMode effective_sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
  std::size_t prefix_count = 0;
  std::size_t resolved_strided_count = 0;
  bool degraded_to_prefix = false;
  std::vector<std::size_t> resolved_offsets;
  bool empty_result = false;
  bool legal = false;
  ClusterMetrics metrics;
  double selection_score = std::numeric_limits<double>::infinity();
  double partition_score = std::numeric_limits<double>::infinity();
  std::size_t selected_rotation_offset = 0;
  double min_cluster_score = 0.0;
  double max_cluster_score = 0.0;
  double avg_cluster_score = 0.0;
  std::string note;
};

struct SweepConfigSpec
{
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
};

struct ReferenceDensityScaledContinuousConfig
{
  std::size_t density_grid_size = 1;
  double density_scale_power = 1.0;
};

struct ReferenceDensityScaledDiscreteConfig
{
  icts::DiscreteHilbertEncoding discrete_hilbert_encoding = icts::DiscreteHilbertEncoding::kSinkThetaCell;
  icts::HilbertTransform hilbert_transform = icts::HilbertTransform::kIdentity;
  int order_bits = 16;
  std::size_t density_grid_size = 1;
  double density_scale_power = 1.0;
};

auto ConfigureLinearDefaults(icts::LinearClusteringConfig& config, std::size_t max_fanout) -> void;
auto ConfigureSyntheticFallbackCapNeutral(icts::LinearClusteringConfig& config) -> void;
auto CaptureConstraintExpectation(const icts::LinearClusteringConfig& config, ClusteringInvocation& invocation) -> void;
auto RunLinearClustering(const std::vector<icts::Pin*>& loads, std::size_t min_cluster_size, ClusteringInvocation& invocation)
    -> icts::ClusterResult;
auto GenerateSyntheticCase(const SyntheticSweepCase& test_case) -> GeneratedPins;
auto BuildBalancedMarginalJointSkewPoints() -> std::vector<icts::Point<int>>;
auto BuildMarginalDensitySkewPoints() -> std::vector<icts::Point<int>>;
auto ExtractOriginalIndices(const std::vector<icts::OrderedLoad>& ordered_loads) -> std::vector<std::size_t>;
auto FormatOrderIndices(const std::vector<std::size_t>& indices) -> std::string;
auto FindFirstOrderDifference(const std::vector<std::size_t>& lhs, const std::vector<std::size_t>& rhs) -> std::optional<std::size_t>;
auto BuildSinkReferenceContinuousOrder(const std::vector<icts::Pin*>& loads) -> std::vector<std::size_t>;
auto BuildReferenceDensityScaledDiscreteOrder(const std::vector<icts::Pin*>& loads, const ReferenceDensityScaledDiscreteConfig& config)
    -> std::vector<std::size_t>;
auto BuildReferenceDensityScaledContinuousOrder(const std::vector<icts::Pin*>& loads, const ReferenceDensityScaledContinuousConfig& config)
    -> std::vector<std::size_t>;
auto MakeLineLoads(std::size_t count, int start_x, int step_x, int y_coord) -> GeneratedPins;
auto MakeSeparatedIslandLoads() -> GeneratedPins;
auto RunDetailedLinearClustering(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config,
                                 icts::ClusterResult& result, icts::PartitionScore& partition) -> void;
auto PrepareSyntheticOutputDir(const std::string& case_name) -> std::filesystem::path;
auto FormatMetricsLine(const ClusterMetrics& metrics) -> std::string;
auto BuildArtifactSummary(const std::vector<std::string>& artifact_names) -> std::string;
auto BuildConstraintObservationReport(const std::string& test_name, const std::string& input_summary,
                                      const std::vector<LadderObservation>& observations) -> std::string;
auto MakeConstraintConfig(const ConstraintConfigSpec& spec) -> icts::LinearClusteringConfig;
auto OrderStrategyName(icts::LinearOrderStrategy strategy) -> const char*;
auto DiscreteHilbertEncodingName(icts::DiscreteHilbertEncoding encoding) -> const char*;
auto HilbertTransformName(icts::HilbertTransform transform) -> const char*;
auto SplitStrategyName(icts::LinearSplitStrategy strategy) -> const char*;
auto SweepModeName(icts::LinearSweepMode mode) -> const char*;
auto MakeSweepLabel(icts::LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string;
auto MakeStrategyLabel(icts::LinearOrderStrategy order_strategy, icts::DiscreteHilbertEncoding discrete_hilbert_encoding,
                       icts::HilbertTransform hilbert_transform, int order_bits, icts::LinearSplitStrategy split_strategy,
                       icts::LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string;
auto FormatResolvedOffsets(const std::vector<std::size_t>& offsets) -> std::string;
auto BuildSweepCandidates(std::size_t load_count) -> std::vector<SweepConfigSpec>;
auto BuildSweepNote(std::size_t load_count, const icts::LinearClusteringConfig& config) -> std::string;
auto FormatSweepCandidates(const std::vector<SweepConfigSpec>& candidates) -> std::string;
auto PickBestStrategy(const std::vector<StrategySweepObservation>& observations) -> std::optional<std::size_t>;
auto BuildStrategySweepReport(const std::string& test_name, const std::string& input_summary,
                              const std::vector<StrategySweepObservation>& observations, std::optional<std::size_t> selected_index,
                              const std::vector<std::string>& artifact_names) -> std::string;
auto BuildStrategySweepCsv(const std::vector<StrategySweepObservation>& observations, std::optional<std::size_t> selected_index)
    -> std::string;
auto ValidateClusterLegality(const icts::ClusterResult& result, const ClusteringInvocation& invocation, std::string& error) -> bool;

}  // namespace icts_test::linear_clustering::synthetic::detail
