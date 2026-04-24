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
 * @file FastClusteringPolishShared.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Shared merge and boundary-polish helpers for fast topology clustering.
 */

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "FastClusteringInternal.hh"

namespace icts {
struct ClusterConfig;
}  // namespace icts

namespace icts::fast_clustering {

auto BuildMergedDraft(const ClusterDraft& lhs, const ClusterDraft& rhs, const std::vector<LoadEntry>& entries, const ClusterConfig& config)
    -> ClusterDraft
{
  std::vector<std::size_t> merged_entry_ids;
  merged_entry_ids.reserve(lhs.entry_ids.size() + rhs.entry_ids.size());
  merged_entry_ids.insert(merged_entry_ids.end(), lhs.entry_ids.begin(), lhs.entry_ids.end());
  merged_entry_ids.insert(merged_entry_ids.end(), rhs.entry_ids.begin(), rhs.entry_ids.end());
  return BuildDraft(std::move(merged_entry_ids), entries, config);
}

auto SelectNearestActiveNeighbors(std::size_t cluster_id, const std::vector<ClusterDraft>& clusters, std::size_t max_candidate_count)
    -> std::vector<std::size_t>
{
  struct NeighborCandidate
  {
    std::size_t cluster_id = 0;
    long long distance = 0;
  };

  std::vector<NeighborCandidate> candidates;
  candidates.reserve(clusters.size());
  const auto& cluster = clusters.at(cluster_id);
  for (std::size_t neighbor_id = 0; neighbor_id < clusters.size(); ++neighbor_id) {
    if (neighbor_id == cluster_id || !clusters.at(neighbor_id).active || clusters.at(neighbor_id).entry_ids.empty()) {
      continue;
    }
    candidates.push_back(NeighborCandidate{
        .cluster_id = neighbor_id,
        .distance = CalcBoundsDistance(cluster.bounds, clusters.at(neighbor_id).bounds),
    });
  }

  std::ranges::sort(candidates, [](const NeighborCandidate& lhs, const NeighborCandidate& rhs) -> bool {
    if (lhs.distance != rhs.distance) {
      return lhs.distance < rhs.distance;
    }
    return lhs.cluster_id < rhs.cluster_id;
  });

  std::vector<std::size_t> neighbor_ids;
  neighbor_ids.reserve(std::min(max_candidate_count, candidates.size()));
  for (const auto& candidate : candidates) {
    if (neighbor_ids.size() == max_candidate_count) {
      break;
    }
    neighbor_ids.push_back(candidate.cluster_id);
  }
  return neighbor_ids;
}

auto PairObjective(const ClusterDraft& lhs, const ClusterDraft& rhs, const ClusterConfig& config, double target_routing_cap_proxy,
                   double routing_cap_balance_weight) -> double
{
  return DraftObjective(lhs, config, target_routing_cap_proxy, routing_cap_balance_weight)
         + DraftObjective(rhs, config, target_routing_cap_proxy, routing_cap_balance_weight);
}

}  // namespace icts::fast_clustering
