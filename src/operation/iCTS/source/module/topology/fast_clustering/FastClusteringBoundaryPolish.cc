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
 * @file FastClusteringBoundaryPolish.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Boundary-load polish orchestration for fast topology clustering.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include "FastClusteringInternal.hh"

namespace icts {
struct ClusterConfig;
}  // namespace icts

namespace icts::fast_clustering {
namespace {

auto BuildCapHeavyClusterOrder(const std::vector<ClusterDraft>& clusters) -> std::vector<std::size_t>
{
  std::vector<std::size_t> cluster_order(clusters.size());
  std::iota(cluster_order.begin(), cluster_order.end(), 0U);
  std::ranges::sort(cluster_order, [&clusters](std::size_t lhs, std::size_t rhs) -> bool {
    if (std::abs(clusters.at(lhs).routing_cap_proxy - clusters.at(rhs).routing_cap_proxy) > kScoreEpsilon) {
      return clusters.at(lhs).routing_cap_proxy > clusters.at(rhs).routing_cap_proxy;
    }
    return lhs < rhs;
  });
  return cluster_order;
}

}  // namespace

auto PolishBoundaryLoads(std::vector<ClusterDraft>& clusters, const std::vector<LoadEntry>& entries, const ClusterConfig& config) -> void
{
  for (std::size_t round = 0; round < kBoundaryPolishRoundCount; ++round) {
    bool changed = false;
    const auto cluster_order = BuildCapHeavyClusterOrder(clusters);
    for (const auto source_id : cluster_order) {
      if (source_id >= clusters.size() || !clusters.at(source_id).active || clusters.at(source_id).entry_ids.size() <= 1U) {
        continue;
      }

      const auto aggregate = CalcDraftAggregate(clusters);
      auto best_move = FindBestBoundaryMove(source_id, clusters, entries, config, aggregate);
      if (!best_move.has_value()) {
        continue;
      }

      clusters.at(source_id) = std::move(best_move->source_after);
      clusters.at(best_move->target_id) = std::move(best_move->target_after);
      changed = true;
    }
    if (!changed) {
      break;
    }
  }
}

}  // namespace icts::fast_clustering
