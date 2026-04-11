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
 * @file LinearClusteringRealTechInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Internal helpers shared by real-tech linear clustering test scenarios.
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

#include "Pin.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/spatial/Point.hh"
#include "module/topology/clustering/Clustering.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/LinearClusteringTypes.hh"

namespace icts_test::linear_clustering::realtech::detail {

inline constexpr std::size_t kDefaultTargetClusterSize = 16;
inline constexpr std::size_t kSingletonClusterSize = 1;
inline constexpr double kDiameterLadderMediumScale = 0.55;
inline constexpr double kDiameterLadderTightScale = 0.30;
inline constexpr double kMilliOhmPerOhm = 1000.0;
inline constexpr std::array<std::size_t, 2> kRealTechFanoutSweep = {8U, 4U};
inline constexpr std::size_t kDefaultStridedSweepCount = 4U;
inline constexpr std::array<icts::LinearOrderStrategy, 4> kStrategyOrderSweep = {
    icts::LinearOrderStrategy::kContinuousHilbert,
    icts::LinearOrderStrategy::kDiscreteHilbert,
    icts::LinearOrderStrategy::kDensityScaledContinuousHilbert,
    icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert,
};
inline constexpr std::array<icts::LinearSplitStrategy, 3> kStrategySplitSweep = {
    icts::LinearSplitStrategy::kForwardGreedy,
    icts::LinearSplitStrategy::kReverseGreedy,
    icts::LinearSplitStrategy::kBidirectionalGreedy,
};

struct RealClockLoads
{
  bool available = false;
  std::string clock_name;
  std::vector<icts::Pin*> loads;
  int dbu_per_micron = 1;
  int bounding_box_diameter = 0;
};

struct ClusterMetrics
{
  std::size_t cluster_count = 0;
  std::size_t singleton_cluster_count = 0;
  std::size_t min_cluster_size = 0;
  std::size_t max_cluster_size = 0;
  double avg_cluster_size = 0.0;
  int max_cluster_diameter = 0;
};

struct StrategySweepCandidate
{
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
  bool empty_result = false;
  bool legal = false;
  ClusterMetrics metrics;
  double selection_score = std::numeric_limits<double>::infinity();
  std::string note;
};

struct SweepConfigSpec
{
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
};

struct DetailedLinearClusteringRun
{
  icts::ClusterResult result;
  icts::PartitionScore partition;
};

struct FanoutConfigSpec
{
  std::size_t fanout_limit = 0;
  int max_diameter = 0;
  bool enable_exact_cap = false;
  icts::LinearOrderStrategy order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  icts::LinearSplitStrategy split_strategy = icts::LinearSplitStrategy::kBidirectionalGreedy;
  icts::LinearSweepMode sweep_mode = icts::LinearSweepMode::kPrefixSweep;
  std::size_t strided_sweep_count = 0;
};

struct StrategySweepSelection
{
  std::vector<StrategySweepCandidate> candidates;
  std::optional<std::size_t> selected_index = std::nullopt;
  DetailedLinearClusteringRun selected_run;
  icts::LinearClusteringConfig selected_config;
};

struct WireRcPerUm
{
  int routing_layer = 1;
  std::optional<double> wire_width_um = std::nullopt;
  double resistance_per_um_ohm = 0.0;
  double capacitance_per_um = 0.0;
};

auto CalcClusterDiameter(const std::vector<icts::Pin*>& cluster) -> int;
auto CalcClusterCenter(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>;
auto CalcClusterMedian(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>;
auto RotateOrderedLoads(const std::vector<icts::OrderedLoad>& ordered_loads, std::size_t rotation_offset) -> std::vector<icts::OrderedLoad>;
auto MaterializeClusterResult(const std::vector<icts::OrderedLoad>& ordered_loads, const std::vector<icts::SegmentRange>& segments)
    -> icts::ClusterResult;
auto RunDetailedLinearClustering(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config)
    -> DetailedLinearClusteringRun;
auto ResolveExactCapSyntheticRoot(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config) -> icts::Point<int>;
auto IsSamePoint(const icts::Point<int>& lhs, const icts::Point<int>& rhs) -> bool;
auto HasLoadAt(const std::vector<icts::Pin*>& loads, const icts::Point<int>& point) -> bool;
auto FindNonDegenerateMedianCollisionSubset(const std::vector<icts::Pin*>& loads) -> std::vector<icts::Pin*>;
auto FormatPoint(const icts::Point<int>& point) -> std::string;
auto GatherMetrics(const icts::ClusterResult& result) -> ClusterMetrics;
auto ValidateClusterLegality(const icts::ClusterResult& result, const icts::LinearClusteringConfig& config, std::string& error) -> bool;
auto PrepareOutputDir(const std::string& case_name) -> std::filesystem::path;
auto WriteCaseLog(const std::filesystem::path& path, const std::string& content) -> bool;
auto OrderStrategyName(icts::LinearOrderStrategy strategy) -> const char*;
auto SplitStrategyName(icts::LinearSplitStrategy strategy) -> const char*;
auto SweepModeName(icts::LinearSweepMode mode) -> const char*;
auto MakeSweepLabel(icts::LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string;
auto StrategyLabel(icts::LinearOrderStrategy order_strategy, icts::LinearSplitStrategy split_strategy, icts::LinearSweepMode sweep_mode,
                   std::size_t strided_sweep_count) -> std::string;
auto ScoringStrategyName(icts::LinearScoringStrategy strategy) -> const char*;
auto FormatResolvedOffsets(const std::vector<std::size_t>& offsets) -> std::string;
auto BuildSweepCandidates(std::size_t load_count) -> std::vector<SweepConfigSpec>;
auto BuildSweepNote(std::size_t load_count, const icts::LinearClusteringConfig& config) -> std::string;
auto FormatSweepCandidates(const std::vector<SweepConfigSpec>& candidates) -> std::string;
auto AppendCandidateNote(const std::string& base_note, const std::string& extra_note) -> std::string;
auto PickBestStrategyCandidate(const std::vector<StrategySweepCandidate>& candidates) -> std::optional<std::size_t>;
auto FormatMetricsLine(const ClusterMetrics& metrics) -> std::string;
auto BuildArtifactSummary(const std::vector<std::string>& artifact_names) -> std::string;
auto BuildStrategySweepSection(const std::vector<StrategySweepCandidate>& candidates, std::optional<std::size_t> selected_index,
                               icts::LinearScoringStrategy scoring_strategy) -> std::string;
auto EnsureLargestRealClockLoads() -> const RealClockLoads&;
auto CountPinsWithExactCapContext(const std::vector<icts::Pin*>& loads) -> std::size_t;
auto BuildResponsiveDiameterThresholds(const RealClockLoads& real_clock_loads) -> std::array<int, 3>;
auto BuildRealTechFanoutConfig(const FanoutConfigSpec& spec) -> icts::LinearClusteringConfig;
auto MakeFanoutCaseTag(std::size_t fanout_limit, std::size_t load_count) -> std::string;
auto EstimateExactElectrical(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config) -> icts::ElectricalEstimate;
auto BuildClusterArtifacts(const icts::ClusterResult& result, const std::vector<icts::Pin*>& loads,
                           std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                           std::vector<std::size_t>& cluster_sizes, std::string& error) -> bool;
auto ResolveEffectiveRoutingLayer(const icts::LinearClusteringConfig& config) -> int;
auto ResolveEffectiveWireWidth(const icts::LinearClusteringConfig& config) -> std::optional<double>;
auto QueryEffectiveWireRcPerUm(const icts::LinearClusteringConfig& config) -> WireRcPerUm;
auto EvaluateStrategySweepCandidate(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config,
                                    StrategySweepCandidate& candidate) -> std::optional<DetailedLinearClusteringRun>;
auto BuildStrategySweepSelection(const std::vector<icts::Pin*>& loads, std::size_t fanout_limit, int max_diameter, bool enable_exact_cap)
    -> StrategySweepSelection;

}  // namespace icts_test::linear_clustering::realtech::detail
