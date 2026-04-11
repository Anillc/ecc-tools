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
 * @file LinearClusteringSyntheticCore.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Execution core for synthetic linear clustering support files.
 */

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "Point.hh"
#include "TopologyConfig.hh"
#include "clustering/Clustering.hh"
#include "common/linear_clustering/metrics/ClusterGeometrySupport.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/LinearOrderGenerator.hh"
#include "module/topology/linear_clustering/SequenceSplitter.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace {
static_assert(std::is_same_v<icts::Point<int>, icts::Point<int>>);
}  // namespace

namespace icts_test::linear_clustering::synthetic::detail {
namespace {

auto RotateOrderedLoads(const std::vector<icts::OrderedLoad>& ordered_loads, std::size_t rotation_offset) -> std::vector<icts::OrderedLoad>
{
  if (ordered_loads.empty()) {
    return {};
  }

  const auto normalized_offset = rotation_offset % ordered_loads.size();
  if (normalized_offset == 0U) {
    return ordered_loads;
  }

  std::vector<icts::OrderedLoad> rotated_loads;
  rotated_loads.reserve(ordered_loads.size());
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset), ordered_loads.end());
  rotated_loads.insert(rotated_loads.end(), ordered_loads.begin(), ordered_loads.begin() + static_cast<std::ptrdiff_t>(normalized_offset));
  return rotated_loads;
}

auto MaterializeClusterResult(const std::vector<icts::OrderedLoad>& ordered_loads, const std::vector<icts::SegmentRange>& segments)
    -> icts::ClusterResult
{
  icts::ClusterResult result;
  result.clusters.reserve(segments.size());
  result.centers.reserve(segments.size());
  for (const auto& segment : segments) {
    std::vector<icts::Pin*> cluster;
    cluster.reserve(segment.size());
    for (std::size_t index = segment.begin; index < segment.end; ++index) {
      auto* pin = ordered_loads.at(index).pin;
      if (pin != nullptr) {
        cluster.push_back(pin);
      }
    }
    if (cluster.empty()) {
      continue;
    }
    result.centers.push_back(common::linear_clustering::CalcClusterCenter(cluster));
    result.clusters.push_back(std::move(cluster));
  }
  return result;
}

}  // namespace

auto RunLinearClustering(const std::vector<icts::Pin*>& loads, std::size_t min_cluster_size, ClusteringInvocation& invocation)
    -> icts::ClusterResult
{
  invocation = {};
  icts::LinearClusteringConfig config{};
  ConfigureLinearDefaults(config, min_cluster_size);
  ConfigureSyntheticFallbackCapNeutral(config);
  config.order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  CaptureConstraintExpectation(config, invocation);
  return icts::Clustering::linearClustering(loads, config);
}

auto RunDetailedLinearClustering(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config,
                                 icts::ClusterResult& result, icts::PartitionScore& partition) -> void
{
  const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(loads, config);
  if (ordered_loads.empty()) {
    result = {};
    partition = {};
    return;
  }

  icts::SequenceSplitter splitter;
  partition = splitter.split(ordered_loads, config);
  if (!partition.legal) {
    result = {};
    return;
  }

  const auto materialized_loads = RotateOrderedLoads(ordered_loads, partition.rotation_offset);
  result = MaterializeClusterResult(materialized_loads, partition.segments);
}

}  // namespace icts_test::linear_clustering::synthetic::detail
