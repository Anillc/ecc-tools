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
 * @file OptimizationScalableCandidates.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Scalable candidate generation helpers for CTS post-synthesis optimization.
 */

#include <glog/logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSta.hh"
#include "FastStaTypes.hh"
#include "Log.hh"
#include "optimization/OptimizationInternal.hh"
#include "optimization/OptimizationOptions.hh"
#include "optimization/OptimizationTypes.hh"

namespace icts::optimization_internal {

namespace {

auto MakeArrivalWindow(const FastState& current, double target_skew_ns) -> ArrivalWindow
{
  ArrivalWindow window;
  if (!current.skew.valid) {
    return window;
  }
  window.center_ns = 0.5 * (current.skew.min_arrival_ns + current.skew.max_arrival_ns);
  window.staged_skew_ns = std::max(target_skew_ns, current.skew.skew_ns * DefaultOptimizationOptions().target_window_shrink_ratio);
  const double half_window_ns = 0.5 * window.staged_skew_ns;
  window.lower_ns = window.center_ns - half_window_ns;
  window.upper_ns = window.center_ns + half_window_ns;
  return window;
}

auto AppendPostOrderFromRoot(FastStaNodeId root_id, const TopologyIndex& topology, std::vector<unsigned char>& visit_state,
                             std::vector<FastStaNodeId>& post_order) -> void
{
  if (root_id >= topology.children_by_node.size() || root_id >= visit_state.size()) {
    return;
  }
  std::vector<std::pair<FastStaNodeId, bool>> pending;
  pending.emplace_back(root_id, false);
  while (!pending.empty()) {
    const auto [node_id, expanded] = pending.back();
    pending.pop_back();
    if (node_id >= visit_state.size()) {
      continue;
    }
    if (expanded) {
      visit_state.at(node_id) = 2U;
      post_order.push_back(node_id);
      continue;
    }
    if (visit_state.at(node_id) != 0U) {
      continue;
    }
    visit_state.at(node_id) = 1U;
    pending.emplace_back(node_id, true);
    if (node_id >= topology.children_by_node.size()) {
      continue;
    }
    const auto& children = topology.children_by_node.at(node_id);
    for (const auto child_id : children) {
      if (child_id < visit_state.size() && visit_state.at(child_id) == 0U) {
        pending.emplace_back(child_id, false);
      }
    }
  }
}

auto CollectTopologyPostOrder(const FastStaClockContext& context, const TopologyIndex& topology) -> std::vector<FastStaNodeId>
{
  std::vector<FastStaNodeId> post_order;
  post_order.reserve(context.nodes.size());
  std::vector<unsigned char> visit_state(context.nodes.size(), 0U);
  AppendPostOrderFromRoot(context.source_node_id, topology, visit_state, post_order);
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    if (visit_state.at(node_id) == 0U) {
      AppendPostOrderFromRoot(node_id, topology, visit_state, post_order);
    }
  }
  return post_order;
}

auto AccumulateWindowStatsFromChild(TopologyWindowStats& stats, FastStaNodeId node_id, FastStaNodeId child_id) -> void
{
  if (node_id >= stats.sink_count_by_node.size() || child_id >= stats.sink_count_by_node.size()) {
    return;
  }
  stats.sink_count_by_node.at(node_id) += stats.sink_count_by_node.at(child_id);
  stats.late_count_by_node.at(node_id) += stats.late_count_by_node.at(child_id);
  stats.early_count_by_node.at(node_id) += stats.early_count_by_node.at(child_id);
  stats.late_violation_by_node.at(node_id) += stats.late_violation_by_node.at(child_id);
  stats.early_violation_by_node.at(node_id) += stats.early_violation_by_node.at(child_id);
  stats.min_arrival_by_node.at(node_id) = std::min(stats.min_arrival_by_node.at(node_id), stats.min_arrival_by_node.at(child_id));
  stats.max_arrival_by_node.at(node_id) = std::max(stats.max_arrival_by_node.at(node_id), stats.max_arrival_by_node.at(child_id));
}

auto BuildTopologyWindowStats(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                              const ArrivalWindow& window) -> TopologyWindowStats
{
  TopologyWindowStats stats;
  const auto* context = FastSTA::queryClockContext(clock_id);
  if (context == nullptr) {
    return stats;
  }

  const auto node_count = context->nodes.size();
  stats.sink_count_by_node.assign(node_count, 0U);
  stats.late_count_by_node.assign(node_count, 0U);
  stats.early_count_by_node.assign(node_count, 0U);
  stats.late_violation_by_node.assign(node_count, 0.0);
  stats.early_violation_by_node.assign(node_count, 0.0);
  stats.min_arrival_by_node.assign(node_count, std::numeric_limits<double>::infinity());
  stats.max_arrival_by_node.assign(node_count, -std::numeric_limits<double>::infinity());

  const auto post_order = CollectTopologyPostOrder(*context, topology);
  for (const auto node_id : post_order) {
    if (node_id >= context->nodes.size()) {
      continue;
    }
    const auto& node = context->nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kSink && node.timing.valid) {
      stats.sink_count_by_node.at(node_id) = 1U;
      stats.min_arrival_by_node.at(node_id) = node.timing.arrival_ns;
      stats.max_arrival_by_node.at(node_id) = node.timing.arrival_ns;
      if (node.timing.arrival_ns > window.upper_ns + kOptimizationEpsilon) {
        stats.late_count_by_node.at(node_id) = 1U;
        stats.late_violation_by_node.at(node_id) = node.timing.arrival_ns - window.upper_ns;
      } else if (node.timing.arrival_ns < window.lower_ns - kOptimizationEpsilon) {
        stats.early_count_by_node.at(node_id) = 1U;
        stats.early_violation_by_node.at(node_id) = window.lower_ns - node.timing.arrival_ns;
      }
    }
    if (node_id >= topology.children_by_node.size()) {
      continue;
    }
    for (const auto child_id : topology.children_by_node.at(node_id)) {
      AccumulateWindowStatsFromChild(stats, node_id, child_id);
    }
  }

  for (const auto& buffer : buffers) {
    const auto node_id = buffer.node_id;
    if (node_id >= stats.sink_count_by_node.size() || stats.sink_count_by_node.at(node_id) == 0U) {
      continue;
    }
    const auto late_count = stats.late_count_by_node.at(node_id);
    const auto early_count = stats.early_count_by_node.at(node_id);
    if (late_count == 0U && early_count == 0U) {
      ++stats.neutral_buffer_count;
    } else if (late_count > 0U && early_count == 0U) {
      ++stats.late_pure_buffer_count;
    } else if (early_count > 0U && late_count == 0U) {
      ++stats.early_pure_buffer_count;
    } else {
      ++stats.mixed_buffer_count;
    }
  }
  return stats;
}

auto ScoreWindowSizingAction(const SizingAction& action, const std::vector<OptimizableBuffer>& buffers, const TopologyWindowStats& stats,
                             bool& mixed_rejected) -> double
{
  mixed_rejected = false;
  if (action.drive_step == 0 || action.buffer_index >= buffers.size()) {
    return 0.0;
  }
  const auto node_id = buffers.at(action.buffer_index).node_id;
  if (node_id >= stats.sink_count_by_node.size() || stats.sink_count_by_node.at(node_id) == 0U) {
    return 0.0;
  }

  const bool late_action = action.drive_step > 0;
  const auto primary_count = late_action ? stats.late_count_by_node.at(node_id) : stats.early_count_by_node.at(node_id);
  const auto opposite_count = late_action ? stats.early_count_by_node.at(node_id) : stats.late_count_by_node.at(node_id);
  const double primary_violation = late_action ? stats.late_violation_by_node.at(node_id) : stats.early_violation_by_node.at(node_id);
  const double opposite_violation = late_action ? stats.early_violation_by_node.at(node_id) : stats.late_violation_by_node.at(node_id);
  if (primary_count == 0U || primary_violation <= kOptimizationEpsilon) {
    return 0.0;
  }

  const auto active_count = static_cast<double>(primary_count + opposite_count);
  const double purity = active_count <= 0.0 ? 0.0 : static_cast<double>(primary_count) / active_count;
  if (opposite_count > 0U
      && (purity < DefaultOptimizationOptions().min_branch_purity
          || opposite_violation > primary_violation * DefaultOptimizationOptions().max_opposite_violation_ratio + kOptimizationEpsilon)) {
    mixed_rejected = true;
    return 0.0;
  }

  const double drive_weight = 1.0 + 0.2 * static_cast<double>(std::max(0, std::abs(action.drive_step) - 1));
  const double count_weight = 1.0 + std::log1p(static_cast<double>(primary_count));
  const double pure_bonus = opposite_count == 0U ? 1.2 : 1.0;
  const double area_cost = late_action ? 1.0 + 0.005 * std::max(0.0, action.area_delta_um2) : 1.0;
  return primary_violation * count_weight * drive_weight * pure_bonus * purity / area_cost;
}

auto ScoreScalableBatch(const std::vector<SizingAction>& actions, const std::vector<OptimizableBuffer>& buffers,
                        const TopologyWindowStats& stats) -> double
{
  double score = 0.0;
  for (const auto& action : actions) {
    bool mixed_rejected = false;
    score += ScoreWindowSizingAction(action, buffers, stats, mixed_rejected);
  }
  return score;
}

auto BatchKey(const std::vector<SizingAction>& actions) -> std::string
{
  auto sorted_actions = actions;
  std::ranges::sort(sorted_actions, [](const SizingAction& lhs, const SizingAction& rhs) -> bool {
    if (lhs.buffer_index != rhs.buffer_index) {
      return lhs.buffer_index < rhs.buffer_index;
    }
    return lhs.to_master < rhs.to_master;
  });

  std::string key;
  for (const auto& action : sorted_actions) {
    key += std::to_string(action.buffer_index);
    key += ':';
    key += action.to_master;
    key += ';';
  }
  return key;
}

auto AppendScoredBatch(std::vector<ScoredBatchCandidate>& candidates, std::unordered_set<std::string>& seen,
                       const std::vector<SizingAction>& actions, const std::vector<OptimizableBuffer>& buffers,
                       const TopologyWindowStats& stats, std::size_t max_action_count = 0U) -> void
{
  if (actions.empty()) {
    return;
  }

  const auto action_count_limit = max_action_count == 0U ? DefaultOptimizationOptions().max_scalable_batch_actions : max_action_count;
  std::vector<SizingAction> compact;
  compact.reserve(std::min(actions.size(), action_count_limit));
  std::unordered_set<std::size_t> used_buffers;
  for (const auto& action : actions) {
    if (used_buffers.contains(action.buffer_index)) {
      continue;
    }
    used_buffers.insert(action.buffer_index);
    compact.push_back(action);
    if (compact.size() >= action_count_limit) {
      break;
    }
  }
  if (compact.empty()) {
    return;
  }

  const auto key = BatchKey(compact);
  if (!seen.insert(key).second) {
    return;
  }
  const double score = ScoreScalableBatch(compact, buffers, stats);
  if (score <= 0.0) {
    return;
  }
  candidates.push_back(ScoredBatchCandidate{.actions = std::move(compact), .score = score});
}

auto CollectScoredActions(const std::vector<OptimizableBuffer>& buffers, const TopologyWindowStats& stats) -> ScoredActionCollection
{
  ScoredActionCollection collection;
  collection.actions.reserve(buffers.size() * 4U);
  const auto& rank_steps = DefaultOptimizationOptions().rank_steps;
  for (std::size_t buffer_index = 0U; buffer_index < buffers.size(); ++buffer_index) {
    for (const auto rank_step : rank_steps) {
      for (const auto side : {FrontierSide::kLate, FrontierSide::kEarly}) {
        auto action = MakeSizingAction(buffers, buffer_index, side, rank_step);
        if (!action.has_value()) {
          continue;
        }
        ++collection.raw_action_count;
        bool mixed_rejected = false;
        const double score = ScoreWindowSizingAction(*action, buffers, stats, mixed_rejected);
        if (score > 0.0) {
          collection.actions.push_back(ScoredSizingAction{.action = std::move(*action), .score = score});
        } else if (mixed_rejected) {
          ++collection.mixed_rejected_count;
        } else {
          ++collection.no_window_rejected_count;
        }
      }
    }
  }

  std::ranges::sort(collection.actions, [](const ScoredSizingAction& lhs, const ScoredSizingAction& rhs) -> bool {
    if (std::abs(lhs.score - rhs.score) > kOptimizationEpsilon) {
      return lhs.score > rhs.score;
    }
    if (std::abs(lhs.action.area_delta_um2 - rhs.action.area_delta_um2) > kOptimizationEpsilon) {
      return lhs.action.area_delta_um2 < rhs.action.area_delta_um2;
    }
    if (lhs.action.buffer_index != rhs.action.buffer_index) {
      return lhs.action.buffer_index < rhs.action.buffer_index;
    }
    return lhs.action.to_master < rhs.action.to_master;
  });
  return collection;
}

auto IsTopologyAncestor(const TopologyIndex& topology, FastStaNodeId ancestor_id, FastStaNodeId node_id) -> bool
{
  if (ancestor_id >= topology.parent_by_node.size() || node_id >= topology.parent_by_node.size()) {
    return false;
  }
  std::size_t step_count = 0U;
  while (node_id < topology.parent_by_node.size() && step_count <= topology.parent_by_node.size()) {
    if (node_id == ancestor_id) {
      return true;
    }
    node_id = topology.parent_by_node.at(node_id);
    ++step_count;
  }
  return false;
}

auto HasTopologySelectionConflict(const TopologyIndex& topology, const std::vector<OptimizableBuffer>& buffers, const SizingAction& action,
                                  const std::vector<FastStaNodeId>& selected_nodes) -> bool
{
  if (action.buffer_index >= buffers.size()) {
    return true;
  }
  const auto candidate_node_id = buffers.at(action.buffer_index).node_id;
  for (const auto selected_node_id : selected_nodes) {
    if (selected_node_id == candidate_node_id || IsTopologyAncestor(topology, selected_node_id, candidate_node_id)
        || IsTopologyAncestor(topology, candidate_node_id, selected_node_id)) {
      return true;
    }
  }
  return false;
}

auto SelectTopActions(const std::vector<ScoredSizingAction>& scored_actions, const TopologyIndex& topology,
                      const std::vector<OptimizableBuffer>& buffers, int drive_sign, std::size_t max_action_count)
    -> std::vector<SizingAction>
{
  std::vector<SizingAction> actions;
  actions.reserve(max_action_count);
  std::unordered_set<std::size_t> used_buffers;
  std::vector<FastStaNodeId> selected_nodes;
  selected_nodes.reserve(max_action_count);
  for (const auto& scored_action : scored_actions) {
    if (drive_sign > 0 && scored_action.action.drive_step <= 0) {
      continue;
    }
    if (drive_sign < 0 && scored_action.action.drive_step >= 0) {
      continue;
    }
    if (used_buffers.contains(scored_action.action.buffer_index)) {
      continue;
    }
    if (HasTopologySelectionConflict(topology, buffers, scored_action.action, selected_nodes)) {
      continue;
    }
    used_buffers.insert(scored_action.action.buffer_index);
    selected_nodes.push_back(buffers.at(scored_action.action.buffer_index).node_id);
    actions.push_back(scored_action.action);
    if (actions.size() >= max_action_count) {
      break;
    }
  }
  return actions;
}

auto NormalizedBatchScore(const ScoredBatchCandidate& candidate) -> double
{
  return candidate.actions.empty() ? 0.0 : candidate.score / std::sqrt(static_cast<double>(candidate.actions.size()));
}

}  // namespace

auto GenerateScalableBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                     const FastState& current, double target_skew_ns) -> std::vector<ScoredBatchCandidate>
{
  std::vector<ScoredBatchCandidate> candidates;
  std::unordered_set<std::string> seen;
  const auto window = MakeArrivalWindow(current, target_skew_ns);
  const auto stats = BuildTopologyWindowStats(clock_id, buffers, topology, window);
  const auto scored_action_collection = CollectScoredActions(buffers, stats);
  const auto& scored_actions = scored_action_collection.actions;
  LOG_INFO << "Optimization: scalable target window lower=" << window.lower_ns << " ns, upper=" << window.upper_ns
           << " ns, staged_skew=" << window.staged_skew_ns << " ns, late_pure_buffers=" << stats.late_pure_buffer_count
           << ", early_pure_buffers=" << stats.early_pure_buffer_count << ", mixed_buffers=" << stats.mixed_buffer_count
           << ", neutral_buffers=" << stats.neutral_buffer_count << ", raw_actions=" << scored_action_collection.raw_action_count
           << ", scored_actions=" << scored_actions.size() << ", mixed_rejected_actions=" << scored_action_collection.mixed_rejected_count
           << ".";

  const auto& batch_sizes = DefaultOptimizationOptions().scalable_batch_sizes;
  for (const auto batch_size : batch_sizes) {
    AppendScoredBatch(candidates, seen, SelectTopActions(scored_actions, topology, buffers, 0, batch_size), buffers, stats);
    AppendScoredBatch(candidates, seen, SelectTopActions(scored_actions, topology, buffers, 1, batch_size), buffers, stats);
    AppendScoredBatch(candidates, seen, SelectTopActions(scored_actions, topology, buffers, -1, batch_size), buffers, stats);
  }

  std::ranges::sort(candidates, [](const ScoredBatchCandidate& lhs, const ScoredBatchCandidate& rhs) -> bool {
    const double lhs_normalized_score = NormalizedBatchScore(lhs);
    const double rhs_normalized_score = NormalizedBatchScore(rhs);
    if (std::abs(lhs_normalized_score - rhs_normalized_score) > kOptimizationEpsilon) {
      return lhs_normalized_score > rhs_normalized_score;
    }
    if (std::abs(lhs.score - rhs.score) > kOptimizationEpsilon) {
      return lhs.score > rhs.score;
    }
    if (lhs.actions.size() != rhs.actions.size()) {
      return lhs.actions.size() < rhs.actions.size();
    }
    return FirstActionBufferIndex(lhs.actions) < FirstActionBufferIndex(rhs.actions);
  });
  return candidates;
}

}  // namespace icts::optimization_internal
