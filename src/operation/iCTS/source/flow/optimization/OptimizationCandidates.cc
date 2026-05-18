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
 * @file OptimizationCandidates.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Candidate generation helpers for CTS post-synthesis optimization.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastStaAdapter.hh"
#include "FastStaTypes.hh"
#include "optimization/OptimizationInternal.hh"
#include "optimization/OptimizationOptions.hh"
#include "optimization/OptimizationTypes.hh"

namespace icts::optimization_internal {

namespace {

auto DriveStep(const BufferMasterInfo& from, const BufferMasterInfo& to) -> int
{
  return static_cast<int>(to.drive_rank) - static_cast<int>(from.drive_rank);
}

}  // namespace

auto BuildTopologyIndex(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> TopologyIndex
{
  TopologyIndex topology;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return topology;
  }

  topology.parent_by_node.assign(context->nodes.size(), kInvalidFastStaNodeId);
  std::unordered_map<std::string, FastStaNodeId> input_by_inst;
  input_by_inst.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kBufferInput && !node.inst_name.empty()) {
      input_by_inst[node.inst_name] = node_id;
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kBufferOutput) {
      const auto input_iter = input_by_inst.find(node.inst_name);
      if (input_iter != input_by_inst.end()) {
        topology.parent_by_node.at(node_id) = input_iter->second;
        topology.buffer_input_by_output[node_id] = input_iter->second;
      }
      continue;
    }
    if (node.incoming_net_id < context->nets.size()) {
      topology.parent_by_node.at(node_id) = context->nets.at(node.incoming_net_id).driver_node_id;
    }
  }
  for (std::size_t buffer_index = 0U; buffer_index < buffers.size(); ++buffer_index) {
    topology.buffer_index_by_output_node[buffers.at(buffer_index).node_id] = buffer_index;
  }
  topology.children_by_node.assign(context->nodes.size(), {});
  for (FastStaNodeId node_id = 0U; node_id < topology.parent_by_node.size(); ++node_id) {
    const auto parent_id = topology.parent_by_node.at(node_id);
    if (parent_id < topology.children_by_node.size()) {
      topology.children_by_node.at(parent_id).push_back(node_id);
    }
  }
  return topology;
}

namespace {

auto CollectFrontierSinks(FastStaClockId clock_id, const FastState& current, FrontierSide side) -> std::vector<FastStaNodeId>
{
  std::vector<std::pair<FastStaNodeId, double>> sinks;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr || !current.skew.valid) {
    return {};
  }

  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kSink || !node.timing.valid) {
      continue;
    }
    sinks.emplace_back(node_id, node.timing.arrival_ns);
  }
  if (sinks.empty()) {
    const auto skew_extreme_sink_id = side == FrontierSide::kLate ? current.skew.max_sink_node_id : current.skew.min_sink_node_id;
    if (skew_extreme_sink_id < context->nodes.size()) {
      sinks.emplace_back(skew_extreme_sink_id, context->nodes.at(skew_extreme_sink_id).timing.arrival_ns);
    }
  }

  std::ranges::sort(sinks, [context, side](const auto& lhs, const auto& rhs) -> bool {
    if (std::abs(lhs.second - rhs.second) > kOptimizationEpsilon) {
      return side == FrontierSide::kLate ? lhs.second > rhs.second : lhs.second < rhs.second;
    }
    return context->nodes.at(lhs.first).name < context->nodes.at(rhs.first).name;
  });
  if (sinks.size() > DefaultOptimizationOptions().max_frontier_sinks) {
    sinks.resize(DefaultOptimizationOptions().max_frontier_sinks);
  }

  std::vector<FastStaNodeId> sink_ids;
  sink_ids.reserve(sinks.size());
  for (const auto& [node_id, _] : sinks) {
    sink_ids.push_back(node_id);
  }
  return sink_ids;
}

auto CollectPathBufferIndices(const TopologyIndex& topology, FastStaNodeId sink_node_id) -> std::vector<std::size_t>
{
  std::vector<std::size_t> path;
  FastStaNodeId node_id = sink_node_id;
  std::unordered_set<FastStaNodeId> visited;
  while (node_id < topology.parent_by_node.size() && !visited.contains(node_id)) {
    visited.insert(node_id);
    const auto buffer_iter = topology.buffer_index_by_output_node.find(node_id);
    if (buffer_iter != topology.buffer_index_by_output_node.end()) {
      path.push_back(buffer_iter->second);
    }
    node_id = topology.parent_by_node.at(node_id);
  }
  std::ranges::reverse(path);
  return path;
}

}  // namespace

auto MakeSizingAction(const std::vector<OptimizableBuffer>& buffers, std::size_t buffer_index, FrontierSide side, unsigned rank_step)
    -> std::optional<SizingAction>
{
  if (buffer_index >= buffers.size() || rank_step == 0U) {
    return std::nullopt;
  }
  const auto& buffer = buffers.at(buffer_index);
  const auto* from = FindMasterInfo(buffer.candidates, buffer.current_master);
  if (from == nullptr) {
    return std::nullopt;
  }
  const auto from_rank = static_cast<int>(from->drive_rank);
  const auto target_rank = side == FrontierSide::kLate ? from_rank + static_cast<int>(rank_step) : from_rank - static_cast<int>(rank_step);
  if (target_rank < 0 || static_cast<std::size_t>(target_rank) >= buffer.candidates.size()) {
    return std::nullopt;
  }
  const auto& to = buffer.candidates.at(static_cast<std::size_t>(target_rank));
  if (to.cell_master == from->cell_master) {
    return std::nullopt;
  }
  return SizingAction{.buffer_index = buffer_index,
                      .from_master = from->cell_master,
                      .to_master = to.cell_master,
                      .drive_step = DriveStep(*from, to),
                      .area_delta_um2 = to.area_um2 - from->area_um2};
}

namespace {

auto AppendBatchCandidate(std::vector<std::vector<SizingAction>>& candidates, std::unordered_set<std::string>& seen,
                          std::vector<SizingAction> actions) -> void
{
  if (actions.empty() || candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
    return;
  }
  std::ranges::sort(actions, [](const SizingAction& lhs, const SizingAction& rhs) -> bool {
    if (lhs.buffer_index != rhs.buffer_index) {
      return lhs.buffer_index < rhs.buffer_index;
    }
    return lhs.to_master < rhs.to_master;
  });

  std::vector<SizingAction> compact;
  compact.reserve(std::min(actions.size(), DefaultOptimizationOptions().max_batch_actions));
  std::unordered_set<std::size_t> used_buffers;
  for (const auto& action : actions) {
    if (used_buffers.contains(action.buffer_index)) {
      continue;
    }
    used_buffers.insert(action.buffer_index);
    compact.push_back(action);
    if (compact.size() >= DefaultOptimizationOptions().max_batch_actions) {
      break;
    }
  }
  if (compact.empty()) {
    return;
  }

  std::string key;
  for (const auto& action : compact) {
    key += std::to_string(action.buffer_index);
    key += ':';
    key += action.to_master;
    key += ';';
  }
  if (!seen.insert(key).second) {
    return;
  }
  candidates.push_back(std::move(compact));
}

auto GeneratePathSegmentBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::size_t>& path, FrontierSide side,
                                unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                std::unordered_set<std::string>& seen) -> void
{
  const auto& segment_lengths = DefaultOptimizationOptions().path_segment_lengths;
  for (std::size_t start = 0U; start < path.size() && candidates.size() < DefaultOptimizationOptions().max_batch_trials_per_iteration;
       ++start) {
    for (const auto length : segment_lengths) {
      std::vector<SizingAction> actions;
      for (std::size_t offset = 0U; offset < length && start + offset < path.size(); ++offset) {
        auto action = MakeSizingAction(buffers, path.at(start + offset), side, rank_step);
        if (action.has_value()) {
          actions.push_back(std::move(*action));
        }
      }
      AppendBatchCandidate(candidates, seen, std::move(actions));
      if (candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
        break;
      }
    }
  }
}

auto GenerateFrontierLevelBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::vector<std::size_t>>& paths,
                                  FrontierSide side, unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                  std::unordered_set<std::string>& seen) -> void
{
  std::size_t max_path_size = 0U;
  for (const auto& path : paths) {
    max_path_size = std::max(max_path_size, path.size());
  }
  for (std::size_t depth_index = 0U;
       depth_index < max_path_size && candidates.size() < DefaultOptimizationOptions().max_batch_trials_per_iteration; ++depth_index) {
    std::vector<SizingAction> actions;
    for (const auto& path : paths) {
      if (depth_index >= path.size()) {
        continue;
      }
      auto action = MakeSizingAction(buffers, path.at(depth_index), side, rank_step);
      if (action.has_value()) {
        actions.push_back(std::move(*action));
      }
    }
    AppendBatchCandidate(candidates, seen, std::move(actions));
  }
}

auto GenerateFrontierPrefixBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::vector<std::size_t>>& paths,
                                   FrontierSide side, unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                   std::unordered_set<std::string>& seen) -> void
{
  const auto& path_counts = DefaultOptimizationOptions().frontier_prefix_path_counts;
  const auto& prefix_lengths = DefaultOptimizationOptions().frontier_prefix_lengths;
  for (const auto path_count : path_counts) {
    if (candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
      break;
    }
    const auto selected_path_count = std::min(path_count, paths.size());
    if (selected_path_count == 0U) {
      continue;
    }
    for (const auto prefix_length : prefix_lengths) {
      std::vector<SizingAction> actions;
      for (std::size_t path_index = 0U; path_index < selected_path_count && actions.size() < DefaultOptimizationOptions().max_batch_actions;
           ++path_index) {
        const auto& path = paths.at(path_index);
        for (std::size_t depth_index = 0U; depth_index < prefix_length && depth_index < path.size(); ++depth_index) {
          auto action = MakeSizingAction(buffers, path.at(depth_index), side, rank_step);
          if (action.has_value()) {
            actions.push_back(std::move(*action));
          }
        }
      }
      AppendBatchCandidate(candidates, seen, std::move(actions));
      if (candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
        break;
      }
    }
  }
}

}  // namespace

auto GenerateBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                             const FastState& current) -> std::vector<std::vector<SizingAction>>
{
  std::vector<std::vector<SizingAction>> candidates;
  std::unordered_set<std::string> seen;
  const auto& rank_steps = DefaultOptimizationOptions().rank_steps;
  for (const auto side : {FrontierSide::kLate, FrontierSide::kEarly}) {
    const auto frontier_sinks = CollectFrontierSinks(clock_id, current, side);
    std::vector<std::vector<std::size_t>> paths;
    paths.reserve(frontier_sinks.size());
    for (const auto sink_id : frontier_sinks) {
      auto path = CollectPathBufferIndices(topology, sink_id);
      if (!path.empty()) {
        paths.push_back(std::move(path));
      }
    }
    for (const auto rank_step : rank_steps) {
      GenerateFrontierPrefixBatches(buffers, paths, side, rank_step, candidates, seen);
      GenerateFrontierLevelBatches(buffers, paths, side, rank_step, candidates, seen);
      for (const auto& path : paths) {
        GeneratePathSegmentBatches(buffers, path, side, rank_step, candidates, seen);
        if (candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
          break;
        }
      }
      if (candidates.size() >= DefaultOptimizationOptions().max_batch_trials_per_iteration) {
        break;
      }
    }
  }
  return candidates;
}

}  // namespace icts::optimization_internal
