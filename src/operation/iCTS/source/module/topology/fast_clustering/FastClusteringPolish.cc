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
 * @file FastClusteringPolish.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Merge and boundary polishing orchestration for fast topology clustering.
 */

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <vector>

#include "FastClusteringInternal.hh"

namespace icts {
struct ClusterConfig;
}  // namespace icts

namespace icts::fast_clustering {

auto PolishSmallClusters(std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries, const ClusterConfig& config) -> void
{
  for (std::size_t round = 0; round < kMergeRoundCount; ++round) {
    bool changed = false;
    std::vector<std::size_t> cluster_order(clusters.size());
    std::iota(cluster_order.begin(), cluster_order.end(), 0U);
    std::ranges::sort(cluster_order, [&clusters](std::size_t lhs, std::size_t rhs) -> bool {
      if (clusters.at(lhs).entry_ids.size() != clusters.at(rhs).entry_ids.size()) {
        return clusters.at(lhs).entry_ids.size() < clusters.at(rhs).entry_ids.size();
      }
      return lhs < rhs;
    });

    for (const auto cluster_id : cluster_order) {
      if (!clusters.at(cluster_id).active || clusters.at(cluster_id).entry_ids.empty()) {
        continue;
      }
      changed = MergeDraftsIfUseful(cluster_id, clusters, entries, config) || changed;
    }
    if (!changed) {
      break;
    }
  }

  PolishBoundaryLoads(clusters, entries, config);

  const auto inactive_tail
      = std::ranges::remove_if(clusters, [](const ClusterDraft& cluster) -> bool { return !cluster.active || cluster.entry_ids.empty(); });
  clusters.erase(inactive_tail.begin(), inactive_tail.end());
}

}  // namespace icts::fast_clustering
