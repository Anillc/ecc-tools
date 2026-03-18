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
 * @file Clustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Clustering for topology embedding.
 */

#include "Clustering.hh"

#include <algorithm>
#include <cmath>
#include <utility>

#include "geometry/Geometry.hh"
#include "kmeans/KMeans.hh"
#include "mcf/MinCostFlow.hh"

namespace icts {
namespace {

std::vector<std::vector<Pin*>> SplitByPosition(const std::vector<Pin*>& loads, std::size_t min_cluster_size)
{
  std::vector<Pin*> sorted = loads;
  std::sort(sorted.begin(), sorted.end(), [](Pin* lhs, Pin* rhs) {
    const auto& lp = lhs->get_location();
    const auto& rp = rhs->get_location();
    if (lp.get_x() != rp.get_x()) {
      return lp.get_x() < rp.get_x();
    }
    return lp.get_y() < rp.get_y();
  });

  std::vector<std::vector<Pin*>> clusters(2);
  if (sorted.size() <= 1) {
    clusters[0] = std::move(sorted);
    return clusters;
  }

  std::size_t left_size = std::max<std::size_t>(min_cluster_size, sorted.size() / 2);
  if (sorted.size() - left_size < min_cluster_size) {
    left_size = sorted.size() - min_cluster_size;
  }
  left_size = std::max<std::size_t>(1, left_size);

  clusters[0].assign(sorted.begin(), sorted.begin() + left_size);
  clusters[1].assign(sorted.begin() + left_size, sorted.end());
  return clusters;
}

std::vector<Point<double>> CalcCenters(const std::vector<std::vector<Pin*>>& clusters)
{
  std::vector<Point<double>> centers;
  centers.reserve(clusters.size());
  for (const auto& cluster : clusters) {
    centers.push_back(geometry::CalcCenter(cluster, [](Pin* pin) { return pin->get_location(); }));
  }
  return centers;
}

}  // namespace

Clustering::Clustering(const Config& config) : _config(config)
{
}

ClusterResult Clustering::biPartition(const std::vector<Pin*>& loads, std::size_t min_cluster_size) const
{
  ClusterResult result;
  if (loads.empty()) {
    return result;
  }
  if (loads.size() == 1) {
    result.clusters = {loads};
    result.centers = {loads.front()->get_location()};
    return result;
  }

  const std::size_t safe_min = std::max<std::size_t>(1, std::min(min_cluster_size, loads.size() / 2));

  std::size_t max_cluster_size = static_cast<std::size_t>(std::ceil(_config.max_ratio * loads.size()));
  if (max_cluster_size < safe_min) {
    max_cluster_size = safe_min;
  }
  if (max_cluster_size > loads.size() - safe_min) {
    max_cluster_size = loads.size() - safe_min;
  }
  max_cluster_size = std::max<std::size_t>(1, max_cluster_size);

  KMeans<Pin*> kmeans;
  auto kmeans_result = kmeans.run(
      loads, 2, [](Pin* pin) { return pin->get_location(); }, 5);

  auto centers = kmeans_result.centers;
  if (centers.size() < 2) {
    auto fallback_clusters = SplitByPosition(loads, safe_min);
    centers = CalcCenters(fallback_clusters);
  }

  std::vector<std::vector<Pin*>> clusters;
  for (int iter = 0; iter < _config.max_iter; ++iter) {
    MinCostFlow<Pin*> mcf;
    for (auto* pin : loads) {
      const auto& loc = pin->get_location();
      mcf.addNode(loc.get_x(), loc.get_y(), pin);
    }
    for (const auto& center : centers) {
      mcf.addCenter(center.get_x(), center.get_y());
    }

    clusters = mcf.run(max_cluster_size);
    if (clusters.size() < 2) {
      clusters = SplitByPosition(loads, safe_min);
    }

    auto new_centers = CalcCenters(clusters);
    if (new_centers.size() < 2) {
      break;
    }

    double max_shift = 0.0;
    const std::size_t loop_count = std::min(centers.size(), new_centers.size());
    for (std::size_t i = 0; i < loop_count; ++i) {
      max_shift = std::max(max_shift, geometry::Manhattan(centers[i], new_centers[i]));
    }
    centers = std::move(new_centers);
    if (max_shift < _config.converge_threshold) {
      break;
    }
  }

  result.clusters = clusters;
  if (result.clusters.empty()) {
    result.clusters = SplitByPosition(loads, safe_min);
  }

  if (centers.size() != result.clusters.size()) {
    centers = CalcCenters(result.clusters);
  }
  result.centers.reserve(centers.size());
  for (const auto& center : centers) {
    result.centers.emplace_back(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
  }

  return result;
}

}  // namespace icts
