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
 * @file AnalyticalSolverTrim.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree solver segment and candidate trimming helpers.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "PatternId.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalHTreeCandidateSearch.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::analytical_solver {

auto PreferScoredSegment(const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool
{
  if (lhs.score != rhs.score) {
    return lhs.score < rhs.score;
  }
  if (lhs.delay_upper_ns != rhs.delay_upper_ns) {
    return lhs.delay_upper_ns < rhs.delay_upper_ns;
  }
  if (lhs.power_upper_w != rhs.power_upper_w) {
    return lhs.power_upper_w < rhs.power_upper_w;
  }
  return lhs.pattern_id.pack() < rhs.pattern_id.pack();
}

namespace {

auto PreferPartialCandidate(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.conservative_delay_ns != rhs.conservative_delay_ns) {
    return lhs.conservative_delay_ns < rhs.conservative_delay_ns;
  }
  if (lhs.conservative_power_w != rhs.conservative_power_w) {
    return lhs.conservative_power_w < rhs.conservative_power_w;
  }
  if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
    return lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf;
  }
  return LexicographicalPatternIdLess(lhs.level_segment_pattern_ids, rhs.level_segment_pattern_ids);
}

template <typename T, typename EqualFn>
auto PushUnique(std::vector<T>& target, T value, EqualFn equal) -> void
{
  const auto it = std::ranges::find_if(target, [&](const T& existing) -> bool { return equal(existing, value); });
  if (it == target.end()) {
    target.push_back(std::move(value));
  }
}

auto SameScoredSegmentTrace(const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool
{
  return lhs.pattern_id == rhs.pattern_id && lhs.unit_pattern_ids == rhs.unit_pattern_ids;
}

}  // namespace

auto TrimScoredSegments(std::vector<ScoredSegment> scored_segments, std::size_t max_size) -> std::vector<ScoredSegment>
{
  if (max_size == 0U || scored_segments.size() <= max_size) {
    std::ranges::sort(scored_segments, PreferScoredSegment);
    return scored_segments;
  }

  std::vector<ScoredSegment> trimmed;
  trimmed.reserve(max_size);
  const std::size_t delay_anchor_limit = std::max<std::size_t>(1U, max_size / 4U);
  const std::size_t slew_anchor_limit = std::max(delay_anchor_limit, max_size / 2U);
  const std::size_t low_cap_anchor_limit = std::max(slew_anchor_limit, (3U * max_size) / 4U);
  std::ranges::sort(scored_segments, PreferScoredSegment);
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= delay_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.output_slew_ns != rhs.output_slew_ns) {
      return lhs.output_slew_ns < rhs.output_slew_ns;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= slew_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.source_cap_pf != rhs.source_cap_pf) {
      return lhs.source_cap_pf < rhs.source_cap_pf;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= low_cap_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(scored_segments, [](const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool {
    if (lhs.source_cap_pf != rhs.source_cap_pf) {
      return lhs.source_cap_pf > rhs.source_cap_pf;
    }
    return PreferScoredSegment(lhs, rhs);
  });
  for (const auto& scored : scored_segments) {
    PushUnique(trimmed, scored, SameScoredSegmentTrace);
    if (trimmed.size() >= max_size) {
      break;
    }
  }

  std::ranges::sort(trimmed, PreferScoredSegment);
  return trimmed;
}

namespace {

auto SamePartialCandidateTrace(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  return lhs.level_segment_pattern_ids == rhs.level_segment_pattern_ids;
}

auto PartialDominates(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.has_composition_state != rhs.has_composition_state) {
    return false;
  }
  if (lhs.has_composition_state
      && (lhs.composition_state.terminal_semantic != rhs.composition_state.terminal_semantic
          || !(lhs.composition_state.monotonic_boundary_state == rhs.composition_state.monotonic_boundary_state)
          || lhs.composition_state.source_exposed_load_count != rhs.composition_state.source_exposed_load_count)) {
    return false;
  }

  constexpr double epsilon = 1e-12;
  const bool no_worse
      = lhs.conservative_delay_ns <= rhs.conservative_delay_ns + epsilon && lhs.conservative_power_w <= rhs.conservative_power_w + epsilon
        && lhs.upstream_load_cap_pf <= rhs.upstream_load_cap_pf + epsilon && lhs.current_slew_ns <= rhs.current_slew_ns + epsilon;
  const bool strictly_better
      = lhs.conservative_delay_ns < rhs.conservative_delay_ns - epsilon || lhs.conservative_power_w < rhs.conservative_power_w - epsilon
        || lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf - epsilon || lhs.current_slew_ns < rhs.current_slew_ns - epsilon;
  return no_worse && strictly_better;
}

auto SamePartialStructuralState(const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool
{
  if (lhs.has_composition_state != rhs.has_composition_state) {
    return false;
  }
  if (!lhs.has_composition_state) {
    return true;
  }
  return lhs.composition_state.terminal_semantic == rhs.composition_state.terminal_semantic
         && lhs.composition_state.monotonic_boundary_state == rhs.composition_state.monotonic_boundary_state
         && lhs.composition_state.source_exposed_load_count == rhs.composition_state.source_exposed_load_count;
}

auto BuildPartialParetoFront(std::vector<PartialAnalyticalCandidate> candidates) -> std::vector<PartialAnalyticalCandidate>
{
  std::ranges::sort(candidates, PreferPartialCandidate);
  std::vector<PartialAnalyticalCandidate> frontier;
  frontier.reserve(candidates.size());
  for (auto& candidate : candidates) {
    bool dominated = false;
    for (const auto& kept : frontier) {
      if (PartialDominates(kept, candidate)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      frontier.push_back(std::move(candidate));
    }
  }
  return frontier;
}

auto PushBestPerPartialStructuralState(std::vector<PartialAnalyticalCandidate>& target,
                                       const std::vector<PartialAnalyticalCandidate>& candidates, std::size_t max_size) -> void
{
  for (const auto& candidate : candidates) {
    const auto same_state_it = std::ranges::find_if(
        target, [&](const PartialAnalyticalCandidate& existing) -> bool { return SamePartialStructuralState(existing, candidate); });
    if (same_state_it == target.end()) {
      PushUnique(target, candidate, SamePartialCandidateTrace);
    }
    if (target.size() >= max_size) {
      return;
    }
  }
}

}  // namespace

auto TrimPartialCandidates(std::vector<PartialAnalyticalCandidate> candidates, std::size_t max_size)
    -> std::vector<PartialAnalyticalCandidate>
{
  if (max_size == 0U || candidates.size() <= max_size) {
    std::ranges::sort(candidates, PreferPartialCandidate);
    return candidates;
  }

  auto pareto_front = BuildPartialParetoFront(std::move(candidates));
  if (pareto_front.size() <= max_size) {
    return pareto_front;
  }

  std::vector<PartialAnalyticalCandidate> trimmed;
  trimmed.reserve(max_size);
  auto candidates_for_anchors = pareto_front;
  std::ranges::sort(candidates_for_anchors, PreferPartialCandidate);
  PushBestPerPartialStructuralState(trimmed, candidates_for_anchors, std::max<std::size_t>(1U, max_size / 4U));

  const std::size_t delay_anchor_limit = std::max<std::size_t>(trimmed.size(), max_size / 3U);
  const std::size_t power_anchor_limit = std::max(delay_anchor_limit, max_size / 2U);
  const std::size_t slew_anchor_limit = std::max(power_anchor_limit, (2U * max_size) / 3U);
  const std::size_t low_cap_anchor_limit = std::max(slew_anchor_limit, (5U * max_size) / 6U);
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= delay_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.conservative_power_w != rhs.conservative_power_w) {
      return lhs.conservative_power_w < rhs.conservative_power_w;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= power_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.current_slew_ns != rhs.current_slew_ns) {
      return lhs.current_slew_ns < rhs.current_slew_ns;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= slew_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
      return lhs.upstream_load_cap_pf < rhs.upstream_load_cap_pf;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= low_cap_anchor_limit) {
      break;
    }
  }

  std::ranges::sort(candidates_for_anchors, [](const PartialAnalyticalCandidate& lhs, const PartialAnalyticalCandidate& rhs) -> bool {
    if (lhs.upstream_load_cap_pf != rhs.upstream_load_cap_pf) {
      return lhs.upstream_load_cap_pf > rhs.upstream_load_cap_pf;
    }
    return PreferPartialCandidate(lhs, rhs);
  });
  for (const auto& candidate : candidates_for_anchors) {
    PushUnique(trimmed, candidate, SamePartialCandidateTrace);
    if (trimmed.size() >= max_size) {
      break;
    }
  }

  std::ranges::sort(trimmed, PreferPartialCandidate);
  return trimmed;
}

auto SegmentHasAnyBuffer(const AnalyticalHTreeSolveProblem& solve_problem, PatternId pattern_id) -> bool
{
  const auto* pattern = solve_problem.segment_pattern_library->find(pattern_id);
  return pattern != nullptr && pattern->isBufferPattern();
}

auto MakeSegmentChoice(std::size_t level_index, const ScoredSegment& selected) -> AnalyticalSegmentChoice
{
  return AnalyticalSegmentChoice{
      .level_index = level_index,
      .length_idx = selected.length_idx,
      .segment_pattern_id = selected.pattern_id,
      .input_slew_ns = selected.input_slew_ns,
      .downstream_load_cap_pf = selected.downstream_load_cap_pf,
      .output_slew_ns = selected.output_slew_ns,
      .source_cap_pf = selected.source_cap_pf,
      .delay_ns = selected.delay_ns,
      .power_w = selected.power_w,
      .source_boundary_power_w = selected.source_boundary_power_w,
      .slew_upper_ns = selected.slew_upper_ns,
      .delay_upper_ns = selected.delay_upper_ns,
      .power_upper_w = selected.power_upper_w,
  };
}

auto AccumulateHTreePower(double accumulated_power_w, std::size_t level_index, const ScoredSegment& selected) -> double
{
  if (level_index == 0U) {
    return accumulated_power_w + selected.power_w;
  }
  const double level_multiplicity = std::ldexp(1.0, static_cast<int>(level_index));
  return accumulated_power_w + level_multiplicity * (selected.power_w - selected.source_boundary_power_w);
}

namespace {

auto ResolveMergedSourceExposedLoadCount(const PatternCompositionState& upstream_state, const PatternCompositionState& downstream_state)
    -> std::size_t
{
  if (upstream_state.monotonic_boundary_state.source.has_buffer) {
    return 1U;
  }
  if (downstream_state.source_exposed_load_count > std::numeric_limits<std::size_t>::max() / 2U) {
    return std::numeric_limits<std::size_t>::max();
  }
  return downstream_state.source_exposed_load_count * 2U;
}

}  // namespace

auto TryPrependCompositionState(const AnalyticalHTreeSolveProblem& solve_problem, const PartialAnalyticalCandidate& downstream,
                                PatternId upstream_segment_pattern_id) -> std::optional<PatternCompositionState>
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr) {
    return std::nullopt;
  }
  const auto upstream_state = segment_pattern_library->getCompositionState(upstream_segment_pattern_id);
  if (!downstream.has_composition_state) {
    return upstream_state;
  }
  if (!upstream_state.monotonic_boundary_state.canComposeWith(downstream.composition_state.monotonic_boundary_state)) {
    return std::nullopt;
  }
  if (!IsBinarySourceFanoutLegal(downstream.composition_state.source_exposed_load_count, solve_problem.fanout_options.max_fanout)) {
    return std::nullopt;
  }
  return PatternCompositionState{
      .terminal_semantic = downstream.composition_state.terminal_semantic,
      .monotonic_boundary_state
      = MonotonicBoundaryState::compose(upstream_state.monotonic_boundary_state, downstream.composition_state.monotonic_boundary_state),
      .source_exposed_load_count = ResolveMergedSourceExposedLoadCount(upstream_state, downstream.composition_state),
  };
}

auto CanAppendUnitPattern(const BufferPatternLibrary& segment_pattern_library, const std::vector<PatternId>& unit_pattern_ids,
                          PatternId next_pattern_id) -> bool
{
  const auto* next_pattern = segment_pattern_library.find(next_pattern_id);
  if (next_pattern == nullptr) {
    return false;
  }
  if (unit_pattern_ids.empty()) {
    return true;
  }

  const auto* upstream_pattern = segment_pattern_library.find(unit_pattern_ids.back());
  if (upstream_pattern == nullptr) {
    return false;
  }
  return upstream_pattern->get_monotonic_boundary_state().canComposeWith(next_pattern->get_monotonic_boundary_state());
}

auto IsFunctionalSequenceAllowedForLevel(const AnalyticalHTreeSolveProblem& solve_problem, const HTree::LevelPlan& level,
                                         PatternId segment_pattern_id) -> bool
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(solve_problem);
  if (segment_pattern_library == nullptr) {
    return false;
  }
  if (solve_problem.boundary_constraints.force_branch_buffer
      && segment_pattern_library->getTerminalSemantic(segment_pattern_id) != TerminalSemantic::kBranchBuffered) {
    return false;
  }
  if (level.is_leaf_level && solve_problem.fanout_options.max_fanout > 0U && !SegmentHasAnyBuffer(solve_problem, segment_pattern_id)) {
    return false;
  }
  return true;
}

}  // namespace icts::htree::analytical_solver
