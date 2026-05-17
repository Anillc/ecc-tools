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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file TreeBufferSizingSolve.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Greedy critical-branch clock-tree buffer sizing loop.
 */

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "buffer_sizing/BufferSizingTypes.hh"
#include "buffer_sizing/TreeBufferSizing.hh"

namespace icts::buffer_sizing {
class CharTimingLookup;
}

namespace icts::buffer_sizing {
namespace {

constexpr double kEpsilon = 1e-12;

struct Trial
{
  bool valid = false;
  std::size_t path_order = 0U;
  std::size_t node_id = kInvalidIndex;
  std::size_t buffer_id = kInvalidIndex;
  std::size_t candidate_index = kInvalidIndex;
  double improvement_ns = 0.0;
  double area_delta_um2 = 0.0;
  unsigned drive_step = 0U;
  TreeEvaluation evaluation;
};

auto buildCurrentSelection(const TreeSizingProblem& problem) -> std::vector<std::size_t>
{
  std::vector<std::size_t> selected(problem.nodes.size(), 0U);
  for (const auto& buffer : problem.buffers) {
    if (buffer.node_id < selected.size()) {
      selected.at(buffer.node_id) = buffer.current_candidate_index;
    }
  }
  return selected;
}

auto preferTrial(const Trial& candidate, const Trial& incumbent) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  if (!incumbent.valid) {
    return true;
  }
  if (candidate.improvement_ns > incumbent.improvement_ns + kEpsilon) {
    return true;
  }
  if (std::abs(candidate.improvement_ns - incumbent.improvement_ns) > kEpsilon) {
    return false;
  }
  if (candidate.area_delta_um2 < incumbent.area_delta_um2 - kEpsilon) {
    return true;
  }
  if (std::abs(candidate.area_delta_um2 - incumbent.area_delta_um2) > kEpsilon) {
    return false;
  }
  if (candidate.drive_step != incumbent.drive_step) {
    return candidate.drive_step < incumbent.drive_step;
  }
  return candidate.path_order < incumbent.path_order;
}

auto targetMet(const TreeSizingProblem& problem, const TreeEvaluation& evaluation) -> bool
{
  return evaluation.valid && problem.target_skew_ns >= 0.0 && evaluation.skew_ns <= problem.target_skew_ns + kEpsilon;
}

auto makeMutation(const TreeSizingProblem& problem, std::size_t buffer_id, std::size_t from_candidate, std::size_t to_candidate)
    -> BufferMutation
{
  const auto& buffer = problem.buffers.at(buffer_id);
  const auto& from = buffer.candidates.at(from_candidate);
  const auto& to = buffer.candidates.at(to_candidate);
  return BufferMutation{
      .buffer_id = buffer_id,
      .node_id = buffer.node_id,
      .inst_name = buffer.inst_name,
      .from_master = from.cell_master,
      .to_master = to.cell_master,
      .area_delta_um2 = to.area_um2 - from.area_um2,
  };
}

auto evaluateTrial(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup, const TreeEvaluation& current,
                   const std::vector<std::size_t>& selected, std::size_t node_id, std::size_t path_order, std::size_t candidate_index,
                   TreeSizingSummary& summary) -> Trial
{
  const auto& node = problem.nodes.at(node_id);
  if (node.buffer_id >= problem.buffers.size()) {
    return {};
  }
  const auto& buffer = problem.buffers.at(node.buffer_id);
  const auto current_candidate = selected.at(node_id);
  if (current_candidate >= buffer.candidates.size() || candidate_index >= buffer.candidates.size()
      || candidate_index == current_candidate) {
    return {};
  }

  const auto& current_info = buffer.candidates.at(current_candidate);
  const auto& candidate_info = buffer.candidates.at(candidate_index);
  if (candidate_info.drive_rank < current_info.drive_rank || candidate_info.area_um2 < current_info.area_um2 - kEpsilon) {
    return {};
  }

  auto trial_selection = selected;
  trial_selection.at(node_id) = candidate_index;
  auto trial_eval = TreeBufferSizing::evaluate(problem, timing_lookup, trial_selection);
  if (!trial_eval.valid) {
    ++summary.rejected_candidate_count;
    return {};
  }
  if (trial_eval.hasCapViolation()) {
    ++summary.rejected_candidate_count;
    ++summary.cap_rejected_count;
    return {};
  }

  const double improvement = current.skew_ns - trial_eval.skew_ns;
  if (improvement <= problem.improvement_epsilon_ns) {
    ++summary.rejected_candidate_count;
    return {};
  }

  return Trial{
      .valid = true,
      .path_order = path_order,
      .node_id = node_id,
      .buffer_id = node.buffer_id,
      .candidate_index = candidate_index,
      .improvement_ns = improvement,
      .area_delta_um2 = trial_eval.total_area_um2 - current.total_area_um2,
      .drive_step = candidate_info.drive_rank - current_info.drive_rank,
      .evaluation = std::move(trial_eval),
  };
}

auto findBestTrial(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup, const TreeEvaluation& current,
                   const std::vector<std::size_t>& selected, TreeSizingSummary& summary) -> Trial
{
  Trial best;
  const auto candidates_on_branch = TreeBufferSizing::criticalBranchCandidates(problem, current);
  for (std::size_t path_order = 0U; path_order < candidates_on_branch.size(); ++path_order) {
    const auto node_id = candidates_on_branch.at(path_order);
    if (node_id >= problem.nodes.size() || problem.nodes.at(node_id).buffer_id >= problem.buffers.size()) {
      continue;
    }
    const auto& buffer = problem.buffers.at(problem.nodes.at(node_id).buffer_id);
    for (std::size_t candidate_index = 0U; candidate_index < buffer.candidates.size(); ++candidate_index) {
      if (candidate_index == selected.at(node_id)) {
        continue;
      }
      ++summary.trial_count;
      if (summary.trial_count > problem.max_trial_count) {
        return best;
      }

      const auto trial = evaluateTrial(problem, timing_lookup, current, selected, node_id, path_order, candidate_index, summary);
      if (preferTrial(trial, best)) {
        best = trial;
      }
    }
  }
  return best;
}

auto updateStopReason(const TreeSizingProblem& problem, TreeSizingSummary& summary) -> void
{
  if (!summary.stop_reason.empty()) {
    return;
  }
  if (summary.target_met) {
    summary.stop_reason = "target_met";
  } else if (summary.iteration_count >= problem.max_iterations) {
    summary.stop_reason = "iteration_limit";
  } else {
    summary.stop_reason = "done";
  }
}

}  // namespace

auto TreeBufferSizing::solve(const TreeSizingProblem& problem, const CharTimingLookup& timing_lookup) -> TreeSizingSummary
{
  TreeSizingSummary summary;
  auto selected = buildCurrentSelection(problem);
  summary.selected_candidate_by_node = selected;
  summary.before = evaluate(problem, timing_lookup, selected);
  if (!summary.before.valid) {
    summary.stop_reason = summary.before.failure_reason;
    return summary;
  }

  auto current = summary.before;
  while (!targetMet(problem, current) && summary.iteration_count < problem.max_iterations
         && summary.trial_count < problem.max_trial_count) {
    auto best = findBestTrial(problem, timing_lookup, current, selected, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= problem.max_trial_count ? "trial_limit" : "no_improving_candidate";
      break;
    }

    const auto from_candidate = selected.at(best.node_id);
    selected.at(best.node_id) = best.candidate_index;
    summary.mutations.push_back(makeMutation(problem, best.buffer_id, from_candidate, best.candidate_index));
    current = std::move(best.evaluation);
    ++summary.iteration_count;
    ++summary.accepted_mutation_count;
  }

  summary.after = current;
  summary.valid = summary.before.valid && summary.after.valid;
  summary.target_met = targetMet(problem, summary.after);
  summary.changed = !summary.mutations.empty();
  summary.selected_candidate_by_node = std::move(selected);
  summary.total_area_delta_um2 = summary.after.total_area_um2 - summary.before.total_area_um2;
  updateStopReason(problem, summary);
  return summary;
}

}  // namespace icts::buffer_sizing
