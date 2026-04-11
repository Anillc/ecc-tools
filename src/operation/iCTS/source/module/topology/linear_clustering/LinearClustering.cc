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

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "Geometry.hh"
#include "LinearClusteringTypes.hh"
#include "LinearOrderGenerator.hh"
#include "Logger.hh"
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

auto CalcClusterCenter(const std::vector<Pin*>& cluster) -> Point<int>
{
  if (cluster.empty()) {
    return {0, 0};
  }
  const auto center = geometry::CalcCenter(cluster, [](Pin* pin) -> auto { return pin->get_location(); });
  return {static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y()))};
}

auto MaterializeClusterResult(const std::vector<OrderedLoad>& ordered_loads, const std::vector<SegmentRange>& segments) -> ClusterResult
{
  ClusterResult result;
  result.clusters.reserve(segments.size());
  result.centers.reserve(segments.size());

  for (const auto& segment : segments) {
    std::vector<Pin*> cluster;
    cluster.reserve(segment.size());
    for (std::size_t i = segment.begin; i < segment.end; ++i) {
      auto* pin = ordered_loads.at(i).pin;
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

auto RotateOrderedLoads(const std::vector<OrderedLoad>& ordered_loads, std::size_t rotation_offset) -> std::vector<OrderedLoad>
{
  if (ordered_loads.empty()) {
    return {};
  }

  const auto normalized_offset = rotation_offset % ordered_loads.size();
  if (normalized_offset == 0U) {
    return ordered_loads;
  }

  std::vector<OrderedLoad> rotated_loads;
  rotated_loads.reserve(ordered_loads.size());
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset), ordered_loads.end());
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset));
  return rotated_loads;
}

}  // namespace

auto LinearClustering::run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }

  auto ordered_loads = LinearOrderGenerator::generateOrder(loads, config);
  if (ordered_loads.empty()) {
    CTS_LOG_WARNING << "Linear clustering skipped: no valid load pins after order generation.";
    return result;
  }

  SequenceSplitter splitter;
  auto partition = splitter.split(ordered_loads, config);
  if (!partition.legal) {
    CTS_LOG_WARNING << "Linear clustering failed: no legal greedy partition found for loads=" << ordered_loads.size()
                    << ". Returning empty result.";
    return result;
  }

  const auto sweep_resolution = SequenceSplitter::resolveSweepOffsets(ordered_loads.size(), config);
  const auto materialized_loads = RotateOrderedLoads(ordered_loads, partition.rotation_offset);
  result = MaterializeClusterResult(materialized_loads, partition.segments);
  CTS_LOG_INFO << "Linear clustering done: loads=" << ordered_loads.size() << ", clusters=" << result.clusters.size()
               << ", order_strategy=" << OrderStrategyName(config.order_strategy)
               << ", split_strategy=" << SplitStrategyName(config.split_strategy)
               << ", sweep_mode=" << SweepModeName(sweep_resolution.requested_mode)
               << ", effective_sweep_mode=" << SweepModeName(sweep_resolution.effective_mode)
               << ", prefix_count=" << sweep_resolution.prefix_count << ", strided_sweep_count=" << sweep_resolution.strided_count
               << ", degraded_to_prefix=" << (sweep_resolution.degraded_to_prefix ? "true" : "false")
               << ", resolved_offsets=" << FormatOffsets(sweep_resolution.offsets)
               << ", selected_rotation_offset=" << partition.rotation_offset << ", partition_score=" << partition.total_score;
  return result;
}

}  // namespace icts
