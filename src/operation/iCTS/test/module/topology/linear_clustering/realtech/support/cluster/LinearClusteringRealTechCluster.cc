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
 * @file LinearClusteringRealTechCluster.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Cluster-centric helpers for real-tech linear clustering tests.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "clustering/Clustering.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/LinearOrderGenerator.hh"
#include "module/topology/linear_clustering/SequenceSplitter.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"
#include "utils/geometry/Geometry.hh"

namespace icts_test::linear_clustering::realtech::detail {

namespace {

auto ResolveClusterCenter(const std::vector<icts::Pin*>& raw_cluster, const icts::ClusterResult& result, std::size_t compact_cluster_id)
    -> icts::Point<int>
{
  if (compact_cluster_id < result.centers.size()) {
    const auto& center = result.centers.at(compact_cluster_id);
    if (center.get_x() >= 0 && center.get_y() >= 0) {
      return center;
    }
  }
  return CalcClusterCenter(raw_cluster);
}

auto AppendClusterArtifact(const std::vector<icts::Pin*>& raw_cluster, const icts::ClusterResult& result, std::size_t compact_cluster_id,
                           std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                           std::vector<std::size_t>& cluster_sizes, std::string& error) -> bool
{
  for (const auto* pin : raw_cluster) {
    if (pin == nullptr) {
      error = "null pin in cluster result";
      return false;
    }
    if (cluster_map.contains(pin)) {
      error = "duplicate pin assignment in cluster result";
      return false;
    }
    cluster_map.emplace(pin, compact_cluster_id);
  }

  cluster_sizes.push_back(raw_cluster.size());
  centers.push_back(ResolveClusterCenter(raw_cluster, result, compact_cluster_id));
  return true;
}

}  // namespace

auto CalcClusterDiameter(const std::vector<icts::Pin*>& cluster) -> int
{
  if (cluster.empty()) {
    return 0;
  }

  int min_x = cluster.front()->get_location().get_x();
  int min_y = cluster.front()->get_location().get_y();
  int max_x = min_x;
  int max_y = min_y;
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_x = std::max(max_x, location.get_x());
    max_y = std::max(max_y, location.get_y());
  }
  return (max_x - min_x) + (max_y - min_y);
}

auto CalcClusterCenter(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>
{
  if (cluster.empty()) {
    return {0, 0};
  }

  std::vector<icts::Point<int>> points;
  points.reserve(cluster.size());
  for (const auto* pin : cluster) {
    if (pin != nullptr) {
      points.push_back(pin->get_location());
    }
  }
  if (points.empty()) {
    return {0, 0};
  }

  const auto center = icts::geometry::CalcCenter(points, [](const icts::Point<int>& point) -> auto { return point; });
  return {static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y()))};
}

auto CalcClusterMedian(const std::vector<icts::Pin*>& cluster) -> icts::Point<int>
{
  if (cluster.empty()) {
    return {0, 0};
  }

  std::vector<int> x_coords;
  std::vector<int> y_coords;
  x_coords.reserve(cluster.size());
  y_coords.reserve(cluster.size());
  for (const auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    x_coords.push_back(location.get_x());
    y_coords.push_back(location.get_y());
  }
  if (x_coords.empty()) {
    return {0, 0};
  }

  const auto mid_offset = static_cast<std::ptrdiff_t>(x_coords.size() / 2U);
  auto x_mid_iter = x_coords.begin() + mid_offset;
  auto y_mid_iter = y_coords.begin() + mid_offset;
  std::nth_element(x_coords.begin(), x_mid_iter, x_coords.end());
  std::nth_element(y_coords.begin(), y_mid_iter, y_coords.end());
  return {*x_mid_iter, *y_mid_iter};
}

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
    result.centers.push_back(CalcClusterCenter(cluster));
    result.clusters.push_back(std::move(cluster));
  }
  return result;
}

auto RunDetailedLinearClustering(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config)
    -> DetailedLinearClusteringRun
{
  DetailedLinearClusteringRun run;
  const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(loads, config);
  if (ordered_loads.empty()) {
    return run;
  }

  icts::SequenceSplitter splitter;
  run.partition = splitter.split(ordered_loads, config);
  if (!run.partition.legal) {
    return run;
  }

  const auto materialized_loads = RotateOrderedLoads(ordered_loads, run.partition.rotation_offset);
  run.result = MaterializeClusterResult(materialized_loads, run.partition.segments);
  return run;
}

auto ResolveExactCapSyntheticRoot(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& config) -> icts::Point<int>
{
  return config.root_policy == icts::LinearRootPolicy::kCenter ? CalcClusterCenter(loads) : CalcClusterMedian(loads);
}

auto IsSamePoint(const icts::Point<int>& lhs, const icts::Point<int>& rhs) -> bool
{
  return lhs.get_x() == rhs.get_x() && lhs.get_y() == rhs.get_y();
}

auto HasLoadAt(const std::vector<icts::Pin*>& loads, const icts::Point<int>& point) -> bool
{
  return std::ranges::any_of(loads,
                             [&point](const icts::Pin* pin) -> bool { return pin != nullptr && IsSamePoint(pin->get_location(), point); });
}

auto FormatPoint(const icts::Point<int>& point) -> std::string
{
  std::ostringstream stream;
  stream << "(" << point.get_x() << "," << point.get_y() << ")";
  return stream.str();
}

auto GatherMetrics(const icts::ClusterResult& result) -> ClusterMetrics
{
  ClusterMetrics metrics;
  metrics.cluster_count = result.clusters.size();
  if (result.clusters.empty()) {
    return metrics;
  }

  metrics.min_cluster_size = std::numeric_limits<std::size_t>::max();
  std::size_t total_loads = 0;
  for (const auto& cluster : result.clusters) {
    if (cluster.empty()) {
      continue;
    }
    total_loads += cluster.size();
    metrics.min_cluster_size = std::min(metrics.min_cluster_size, cluster.size());
    metrics.max_cluster_size = std::max(metrics.max_cluster_size, cluster.size());
    metrics.max_cluster_diameter = std::max(metrics.max_cluster_diameter, CalcClusterDiameter(cluster));
    if (cluster.size() == kSingletonClusterSize) {
      ++metrics.singleton_cluster_count;
    }
  }
  if (metrics.min_cluster_size == std::numeric_limits<std::size_t>::max()) {
    metrics.min_cluster_size = 0;
  }
  metrics.avg_cluster_size = result.clusters.empty() ? 0.0 : static_cast<double>(total_loads) / static_cast<double>(result.clusters.size());
  return metrics;
}

auto ValidateClusterLegality(const icts::ClusterResult& result, const icts::LinearClusteringConfig& config, std::string& error) -> bool
{
  for (std::size_t cluster_id = 0; cluster_id < result.clusters.size(); ++cluster_id) {
    const auto& cluster = result.clusters.at(cluster_id);
    if (cluster.empty()) {
      continue;
    }

    if (config.max_fanout > 0 && cluster.size() > config.max_fanout) {
      std::ostringstream stream;
      stream << "fanout violation at cluster " << cluster_id << ": size=" << cluster.size() << ", max_fanout=" << config.max_fanout;
      error = stream.str();
      return false;
    }

    if (config.max_diameter > 0) {
      const auto diameter = CalcClusterDiameter(cluster);
      if (diameter > config.max_diameter) {
        std::ostringstream stream;
        stream << "diameter violation at cluster " << cluster_id << ": diameter=" << diameter << ", max_diameter=" << config.max_diameter;
        error = stream.str();
        return false;
      }
    }
  }
  return true;
}

auto BuildClusterArtifacts(const icts::ClusterResult& result, const std::vector<icts::Pin*>& loads,
                           std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                           std::vector<std::size_t>& cluster_sizes, std::string& error) -> bool
{
  cluster_map.clear();
  centers.clear();
  cluster_sizes.clear();

  if (result.clusters.empty()) {
    error = "result has no clusters";
    return false;
  }

  cluster_map.reserve(loads.size());
  for (const auto& raw_cluster : result.clusters) {
    if (raw_cluster.empty()) {
      continue;
    }
    const std::size_t compact_cluster_id = centers.size();
    if (!AppendClusterArtifact(raw_cluster, result, compact_cluster_id, cluster_map, centers, cluster_sizes, error)) {
      return false;
    }
  }

  if (cluster_map.size() != loads.size()) {
    std::ostringstream stream;
    stream << "cluster map size mismatch: expected " << loads.size() << ", got " << cluster_map.size();
    error = stream.str();
    return false;
  }
  return true;
}

}  // namespace icts_test::linear_clustering::realtech::detail
