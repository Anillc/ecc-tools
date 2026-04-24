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
 * @file LinearClustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Linear clustering orchestration facade.
 */

#include "LinearClustering.hh"

#include <glog/logging.h>

#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "Geometry.hh"
#include "LinearClusteringTypes.hh"
#include "LinearOrderGenerator.hh"
#include "Log.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SequenceSplitter.hh"
#include "TopologyConfig.hh"

namespace icts {
namespace {

auto OrderStrategyName(LinearOrderStrategy strategy) -> const char*
{
  switch (strategy) {
    case LinearOrderStrategy::kContinuousHilbert:
      return "continuous_hilbert";
    case LinearOrderStrategy::kDiscreteHilbert:
      return "discrete_hilbert";
    case LinearOrderStrategy::kDensityScaledContinuousHilbert:
      return "density_scaled_continuous_hilbert";
    case LinearOrderStrategy::kDensityScaledDiscreteHilbert:
      return "density_scaled_discrete_hilbert";
  }
  return "unknown";
}

auto SplitStrategyName(LinearSplitStrategy strategy) -> const char*
{
  switch (strategy) {
    case LinearSplitStrategy::kForwardGreedy:
      return "forward_greedy";
    case LinearSplitStrategy::kReverseGreedy:
      return "reverse_greedy";
    case LinearSplitStrategy::kBidirectionalGreedy:
      return "bidirectional_greedy";
  }
  return "unknown";
}

auto DiscreteHilbertEncodingName(DiscreteHilbertEncoding encoding) -> const char*
{
  switch (encoding) {
    case DiscreteHilbertEncoding::kSinkThetaCell:
      return "sink_theta_cell";
    case DiscreteHilbertEncoding::kSinkThetaCellTangent:
      return "sink_theta_cell_tangent";
    case DiscreteHilbertEncoding::kClassicIndex:
      return "classic_index";
    case DiscreteHilbertEncoding::kClassicIndexTangent:
      return "classic_index_tangent";
  }
  return "unknown";
}

auto HilbertTransformName(HilbertTransform transform) -> const char*
{
  switch (transform) {
    case HilbertTransform::kIdentity:
      return "identity";
    case HilbertTransform::kMirrorX:
      return "mirror_x";
    case HilbertTransform::kMirrorY:
      return "mirror_y";
    case HilbertTransform::kMirrorXY:
      return "mirror_xy";
    case HilbertTransform::kSwapXY:
      return "swap_xy";
    case HilbertTransform::kSwapMirrorX:
      return "swap_mirror_x";
    case HilbertTransform::kSwapMirrorY:
      return "swap_mirror_y";
    case HilbertTransform::kSwapMirrorXY:
      return "swap_mirror_xy";
  }
  return "unknown";
}

auto SweepModeName(LinearSweepMode mode) -> const char*
{
  switch (mode) {
    case LinearSweepMode::kPrefixSweep:
      return "prefix_sweep";
    case LinearSweepMode::kStridedSweep:
      return "strided_sweep";
    case LinearSweepMode::kPrefixAndStridedSweep:
      return "prefix_and_strided_sweep";
  }
  return "unknown";
}

auto UsesDiscreteHilbertOrder(LinearOrderStrategy strategy) -> bool
{
  return strategy == LinearOrderStrategy::kDiscreteHilbert || strategy == LinearOrderStrategy::kDensityScaledDiscreteHilbert;
}

auto FormatOffsets(const std::vector<std::size_t>& offsets) -> std::string
{
  std::string text = "[";
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    if (index > 0U) {
      text += ",";
    }
    text += std::to_string(offsets.at(index));
  }
  text += "]";
  return text;
}

auto MakeSweepLabel(LinearSweepMode sweep_mode, std::size_t strided_sweep_count) -> std::string
{
  auto label = std::string(SweepModeName(sweep_mode));
  if (sweep_mode != LinearSweepMode::kPrefixSweep) {
    label += "__strided_count_" + std::to_string(strided_sweep_count);
  }
  return label;
}

auto MakeStrategyLabel(const LinearClusteringConfig& config) -> std::string
{
  auto label = std::string(OrderStrategyName(config.order_strategy));
  if (UsesDiscreteHilbertOrder(config.order_strategy)) {
    label += "__";
    label += DiscreteHilbertEncodingName(config.discrete_hilbert_encoding);
    label += "__";
    label += HilbertTransformName(config.hilbert_transform);
    label += "__bits_";
    label += std::to_string(config.order_bits);
  }
  label += "__";
  label += SplitStrategyName(config.split_strategy);
  label += "__";
  label += MakeSweepLabel(config.sweep_mode, config.strided_sweep_count);
  return label;
}

auto CalcClusterCenter(const std::vector<Pin*>& cluster) -> Point<int>
{
  if (cluster.empty()) {
    return {0, 0};
  }
  const auto center = geometry::CalcCenter(cluster, [](Pin* pin) -> auto { return pin->get_location(); });
  return {static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y()))};
}

auto MaterializeClusterResult(const std::vector<OrderedLoad>& ordered_loads, const std::vector<SegmentRange>& segments,
                              std::size_t rotation_offset) -> ClusterResult
{
  ClusterResult result;
  result.clusters.reserve(segments.size());
  result.centers.reserve(segments.size());
  const auto load_count = ordered_loads.size();
  if (load_count == 0U) {
    return result;
  }

  const auto normalized_offset = rotation_offset % load_count;

  for (const auto& segment : segments) {
    std::vector<Pin*> cluster;
    cluster.reserve(segment.size());
    for (std::size_t i = segment.begin; i < segment.end; ++i) {
      auto* pin = ordered_loads.at((i + normalized_offset) % load_count).pin;
      if (pin != nullptr) {
        cluster.push_back(pin);
      }
    }
    if (cluster.empty()) {
      continue;
    }
    result.centers.push_back(CalcClusterCenter(cluster));
    result.clusters.push_back(std::move(cluster));
  }

  return result;
}

auto BuildElectricalSummaries(const PartitionScore& partition) -> std::vector<ClusterElectricalSummary>
{
  std::vector<ClusterElectricalSummary> summaries;
  summaries.reserve(partition.clusters.size());
  for (const auto& cluster_score : partition.clusters) {
    const auto& metrics = cluster_score.constraint.metrics;
    summaries.push_back(ClusterElectricalSummary{
        .exact = metrics.electrical.exact,
        .route_success = metrics.electrical.route_success,
        .sink_count = metrics.fanout,
        .diameter_dbu = metrics.diameter,
        .pin_cap_pf = metrics.electrical.pin_cap,
        .wire_cap_pf = metrics.electrical.wire_cap,
        .total_cap_pf = metrics.electrical.total_cap,
        .wirelength_dbu = metrics.wirelength,
    });
  }
  return summaries;
}

constexpr std::size_t kDefaultRetainedStridedSweepCount = 4U;
constexpr int kDefaultRetainedDiscreteBits = 10;

enum class LinearClusteringRunStatus
{
  kSuccess,
  kEmptyOrder,
  kIllegalPartition,
};

struct LinearClusteringExecution
{
  LinearClusteringRunStatus status = LinearClusteringRunStatus::kEmptyOrder;
  std::vector<OrderedLoad> ordered_loads;
  SweepResolution sweep_resolution;
  PartitionScore partition;
  ClusterResult result;
};

struct RetainedStrategyRun
{
  std::string label;
  LinearClusteringConfig config;
  LinearClusteringExecution execution;
};

auto BuildRetainedStrategyConfigs(const LinearClusteringConfig& base_config) -> std::array<LinearClusteringConfig, 4>
{
  auto discrete_classic = base_config;
  discrete_classic.order_strategy = LinearOrderStrategy::kDiscreteHilbert;
  discrete_classic.discrete_hilbert_encoding = DiscreteHilbertEncoding::kClassicIndex;
  discrete_classic.hilbert_transform = HilbertTransform::kSwapXY;
  discrete_classic.order_bits = kDefaultRetainedDiscreteBits;
  discrete_classic.split_strategy = LinearSplitStrategy::kBidirectionalGreedy;
  discrete_classic.sweep_mode = LinearSweepMode::kPrefixAndStridedSweep;
  discrete_classic.strided_sweep_count = kDefaultRetainedStridedSweepCount;

  auto discrete_classic_tangent = discrete_classic;
  discrete_classic_tangent.discrete_hilbert_encoding = DiscreteHilbertEncoding::kClassicIndexTangent;

  auto continuous_reverse = base_config;
  continuous_reverse.order_strategy = LinearOrderStrategy::kContinuousHilbert;
  continuous_reverse.split_strategy = LinearSplitStrategy::kReverseGreedy;
  continuous_reverse.sweep_mode = LinearSweepMode::kPrefixAndStridedSweep;
  continuous_reverse.strided_sweep_count = kDefaultRetainedStridedSweepCount;

  auto continuous_forward = base_config;
  continuous_forward.order_strategy = LinearOrderStrategy::kContinuousHilbert;
  continuous_forward.split_strategy = LinearSplitStrategy::kForwardGreedy;
  continuous_forward.sweep_mode = LinearSweepMode::kPrefixAndStridedSweep;
  continuous_forward.strided_sweep_count = kDefaultRetainedStridedSweepCount;

  return {discrete_classic, discrete_classic_tangent, continuous_reverse, continuous_forward};
}

auto ExecuteLinearClustering(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> LinearClusteringExecution
{
  LinearClusteringExecution execution;
  execution.ordered_loads = LinearOrderGenerator::generateOrder(loads, config);
  if (execution.ordered_loads.empty()) {
    execution.status = LinearClusteringRunStatus::kEmptyOrder;
    return execution;
  }

  SequenceSplitter splitter;
  execution.partition = splitter.split(execution.ordered_loads, config);
  if (!execution.partition.legal) {
    execution.status = LinearClusteringRunStatus::kIllegalPartition;
    return execution;
  }

  execution.sweep_resolution = SequenceSplitter::resolveSweepOffsets(execution.ordered_loads.size(), config);
  execution.result = MaterializeClusterResult(execution.ordered_loads, execution.partition.segments, execution.partition.rotation_offset);
  execution.result.electrical_summaries = BuildElectricalSummaries(execution.partition);
  execution.status = LinearClusteringRunStatus::kSuccess;
  return execution;
}

auto BuildRetainedCandidateSummary(const std::string& label, const LinearClusteringExecution& execution) -> std::string
{
  if (execution.status == LinearClusteringRunStatus::kEmptyOrder) {
    return label + "(empty_order)";
  }
  if (execution.status == LinearClusteringRunStatus::kIllegalPartition) {
    return label + "(illegal_partition)";
  }

  std::ostringstream stream;
  stream << label << "(score=" << execution.partition.total_score << ",clusters=" << execution.result.clusters.size()
         << ",rotation=" << execution.partition.rotation_offset << ")";
  return stream.str();
}

auto JoinCandidateSummaries(const std::vector<std::string>& summaries) -> std::string
{
  std::ostringstream stream;
  for (std::size_t index = 0; index < summaries.size(); ++index) {
    if (index > 0U) {
      stream << "; ";
    }
    stream << summaries.at(index);
  }
  return stream.str();
}

}  // namespace

auto LinearClustering::buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> LinearClusteringConfig
{
  LinearClusteringConfig config;
  config.max_fanout = max_fanout;
  config.max_diameter = std::numeric_limits<int>::max();
  config.max_cap = max_cap;
  return config;
}

auto LinearClustering::runDefault(const std::vector<Pin*>& loads, const LinearClusteringConfig& base_config) -> ClusterResult
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }

  const auto retained_configs = BuildRetainedStrategyConfigs(base_config);
  std::vector<std::string> candidate_summaries;
  candidate_summaries.reserve(retained_configs.size());
  std::optional<RetainedStrategyRun> best_run = std::nullopt;

  for (const auto& config : retained_configs) {
    auto execution = ExecuteLinearClustering(loads, config);
    const auto label = MakeStrategyLabel(config);
    candidate_summaries.push_back(BuildRetainedCandidateSummary(label, execution));
    if (execution.status != LinearClusteringRunStatus::kSuccess) {
      continue;
    }

    if (!best_run.has_value() || execution.partition.total_score < best_run->execution.partition.total_score
        || (execution.partition.total_score == best_run->execution.partition.total_score && label < best_run->label)) {
      best_run = RetainedStrategyRun{
          .label = label,
          .config = config,
          .execution = std::move(execution),
      };
    }
  }

  if (!best_run.has_value()) {
    LOG_WARNING << "Linear clustering default exploration failed: loads=" << loads.size()
                << ", candidates=" << JoinCandidateSummaries(candidate_summaries);
    return result;
  }

  LOG_INFO << "Linear clustering default exploration selected: loads=" << loads.size()
           << ", retained_candidate_count=" << retained_configs.size() << ", strategy=" << best_run->label
           << ", partition_score=" << best_run->execution.partition.total_score
           << ", cluster_count=" << best_run->execution.result.clusters.size()
           << ", selected_rotation_offset=" << best_run->execution.partition.rotation_offset
           << ", resolved_offsets=" << FormatOffsets(best_run->execution.sweep_resolution.offsets)
           << ", candidates=" << JoinCandidateSummaries(candidate_summaries);
  return best_run->execution.result;
}

auto LinearClustering::run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }

  auto execution = ExecuteLinearClustering(loads, config);
  if (execution.status == LinearClusteringRunStatus::kEmptyOrder) {
    LOG_WARNING << "Linear clustering skipped: no valid load pins after order generation.";
    return result;
  }
  if (execution.status == LinearClusteringRunStatus::kIllegalPartition) {
    LOG_WARNING << "Linear clustering failed: no legal greedy partition found for loads=" << execution.ordered_loads.size()
                << ". Returning empty result.";
    return result;
  }

  result = std::move(execution.result);
  LOG_INFO << "Linear clustering done: loads=" << execution.ordered_loads.size() << ", clusters=" << result.clusters.size()
           << ", strategy=" << MakeStrategyLabel(config) << ", sweep_mode=" << SweepModeName(execution.sweep_resolution.requested_mode)
           << ", effective_sweep_mode=" << SweepModeName(execution.sweep_resolution.effective_mode)
           << ", prefix_count=" << execution.sweep_resolution.prefix_count
           << ", strided_sweep_count=" << execution.sweep_resolution.strided_count
           << ", degraded_to_prefix=" << (execution.sweep_resolution.degraded_to_prefix ? "true" : "false")
           << ", resolved_offsets=" << FormatOffsets(execution.sweep_resolution.offsets)
           << ", selected_rotation_offset=" << execution.partition.rotation_offset
           << ", partition_score=" << execution.partition.total_score;
  return result;
}

}  // namespace icts
