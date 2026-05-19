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
 * @file AnalyticalSolverShortlist.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree solver per-level analytical segment shortlisting.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnalyticalModel.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::analytical_solver {
namespace {

auto ShortlistFrontierFunctionalSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level,
                                                 std::size_t level_index, double input_slew_ns, double downstream_cap_pf,
                                                 FunctionalComposeContext& context, AnalyticalSolverResult& result)
    -> std::vector<ScoredSegment>
{
  const ScoredSegmentCacheKey cache_key{
      .length_idx = level.aligned_length_idx,
      .leaf_level = level.is_leaf_level,
      .force_branch_buffer = request.boundary_constraints.force_branch_buffer,
      .input_slew_ns = input_slew_ns,
      .downstream_cap_pf = downstream_cap_pf,
  };
  if (const auto cache_it = context.scored_segments_by_key.find(cache_key); cache_it != context.scored_segments_by_key.end()) {
    const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
    for (const auto& scored : cache_it->second) {
      RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
    }
    return cache_it->second;
  }

  if (request.segment_frontier_catalog == nullptr) {
    return {};
  }
  const SegmentFrontierKind frontier_kind
      = request.boundary_constraints.force_branch_buffer ? SegmentFrontierKind::kTerminalBranchBuffered : SegmentFrontierKind::kAll;
  const auto* frontier = request.segment_frontier_catalog->find(level.aligned_length_idx, frontier_kind);
  if (frontier == nullptr || frontier->empty()) {
    return {};
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(frontier->size());
  const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
  for (const auto& segment_char : *frontier) {
    ++result.evaluated_segment_count;
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kFrontier, result);
    if (!IsFunctionalSequenceAllowedForLevel(request, level, segment_char.get_pattern_id())) {
      continue;
    }
    auto unit_pattern_ids = DecomposePatternToUnitSequence(segment_char.get_pattern_id(), request, context);
    if (unit_pattern_ids.empty()) {
      ++result.decomposition_rejected_count;
      continue;
    }
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kDecomposed, result);
    auto scored = ScoreFunctionalUnitSequence(request, unit_pattern_ids, segment_char.get_pattern_id(), segment_char.get_length_idx(),
                                              input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring, result);
    if (scored.has_value()) {
      scored_segments.push_back(std::move(*scored));
      ++result.scored_segment_count;
      RecordDiagnosticPatternStage(diagnostic_pattern_ids, segment_char.get_pattern_id(), DiagnosticPatternStage::kScored, result);
    }
  }
  auto shortlist = TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
  for (const auto& scored : shortlist) {
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
  }
  return context.scored_segments_by_key.emplace(cache_key, std::move(shortlist)).first->second;
}

auto ShortlistFunctionalSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level, double input_slew_ns,
                                         std::size_t level_index, double downstream_cap_pf, FunctionalComposeContext& context,
                                         AnalyticalSolverResult& result) -> std::vector<ScoredSegment>
{
  if (request.segment_frontier_catalog != nullptr) {
    auto frontier_segments
        = ShortlistFrontierFunctionalSegmentsForLevel(request, level, level_index, input_slew_ns, downstream_cap_pf, context, result);
    if (!frontier_segments.empty()) {
      return frontier_segments;
    }
  }

  auto* mutable_segment_pattern_library = request.mutable_segment_pattern_library;
  if (mutable_segment_pattern_library == nullptr || context.unit_models.empty() || level.aligned_length_idx == 0U
      || request.options.unit_length_idx == 0U || level.aligned_length_idx % request.options.unit_length_idx != 0U) {
    return {};
  }

  const unsigned unit_count = level.aligned_length_idx / request.options.unit_length_idx;
  struct FunctionalSequenceState
  {
    std::vector<PatternId> unit_pattern_ids;
    ScoredSegment scored;
  };

  std::vector<FunctionalSequenceState> beam = {FunctionalSequenceState{}};
  const std::size_t beam_width
      = std::max<std::size_t>(1U, std::min(request.options.unit_compose_beam_size, request.options.per_level_shortlist_size));
  for (unsigned unit_index = 0U; unit_index < unit_count; ++unit_index) {
    std::vector<FunctionalSequenceState> next_beam;
    for (const auto& partial : beam) {
      for (const auto& unit_model : context.unit_models) {
        if (!CanAppendUnitPattern(*mutable_segment_pattern_library, partial.unit_pattern_ids, unit_model.pattern_id)) {
          continue;
        }
        auto unit_pattern_ids = partial.unit_pattern_ids;
        unit_pattern_ids.push_back(unit_model.pattern_id);
        PatternId scoring_pattern_id = unit_pattern_ids.back();
        if (unit_pattern_ids.size() == unit_count) {
          auto materialized_pattern_id = MaterializeFunctionalSegmentPattern(unit_pattern_ids, context, *mutable_segment_pattern_library);
          if (!materialized_pattern_id.has_value() || !IsFunctionalSequenceAllowedForLevel(request, level, *materialized_pattern_id)) {
            continue;
          }
          scoring_pattern_id = *materialized_pattern_id;
        }
        ++result.evaluated_segment_count;
        auto scored = ScoreFunctionalUnitSequence(request, unit_pattern_ids, scoring_pattern_id,
                                                  static_cast<unsigned>(unit_pattern_ids.size() * request.options.unit_length_idx),
                                                  input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring, result);
        if (!scored.has_value()) {
          continue;
        }
        ++result.scored_segment_count;
        next_beam.push_back(FunctionalSequenceState{.unit_pattern_ids = std::move(unit_pattern_ids), .scored = std::move(*scored)});
      }
    }
    std::ranges::sort(next_beam, [](const FunctionalSequenceState& lhs, const FunctionalSequenceState& rhs) -> bool {
      return PreferScoredSegment(lhs.scored, rhs.scored);
    });
    if (next_beam.size() > beam_width) {
      std::vector<ScoredSegment> scored_for_trim;
      scored_for_trim.reserve(next_beam.size());
      for (const auto& state : next_beam) {
        scored_for_trim.push_back(state.scored);
      }
      auto trimmed_scored = TrimScoredSegments(std::move(scored_for_trim), beam_width);
      std::vector<FunctionalSequenceState> trimmed_next_beam;
      trimmed_next_beam.reserve(trimmed_scored.size());
      for (auto& scored : trimmed_scored) {
        trimmed_next_beam.push_back(FunctionalSequenceState{.unit_pattern_ids = scored.unit_pattern_ids, .scored = std::move(scored)});
      }
      next_beam = std::move(trimmed_next_beam);
    }
    beam = std::move(next_beam);
    if (beam.empty()) {
      return {};
    }
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(beam.size());
  for (auto& state : beam) {
    if (state.scored.length_idx == level.aligned_length_idx) {
      scored_segments.push_back(std::move(state.scored));
    }
  }
  return TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
}

}  // namespace

auto ShortlistSegmentsForLevel(const AnalyticalSolverRequest& request, const HTree::LevelPlan& level, double input_slew_ns,
                               std::size_t level_index, double downstream_cap_pf, FunctionalComposeContext* functional_context,
                               AnalyticalSolverResult& result) -> std::vector<ScoredSegment>
{
  if (request.options.use_functional_unit_compose && functional_context != nullptr) {
    return ShortlistFunctionalSegmentsForLevel(request, level, input_slew_ns, level_index, downstream_cap_pf, *functional_context, result);
  }

  const SegmentFrontierKind frontier_kind
      = request.boundary_constraints.force_branch_buffer ? SegmentFrontierKind::kTerminalBranchBuffered : SegmentFrontierKind::kAll;
  const auto* frontier = request.segment_frontier_catalog->find(level.aligned_length_idx, frontier_kind);
  if (frontier == nullptr || frontier->empty()) {
    return {};
  }

  std::vector<ScoredSegment> scored_segments;
  scored_segments.reserve(frontier->size());
  for (const auto& segment_char : *frontier) {
    ++result.evaluated_segment_count;
    if (level.is_leaf_level && request.fanout_options.max_fanout > 0U && !SegmentHasAnyBuffer(request, segment_char.get_pattern_id())) {
      continue;
    }
    const auto* model_set = request.model_catalog->find(AnalyticalModelKey{
        .pattern_id = segment_char.get_pattern_id(),
        .length_idx = segment_char.get_length_idx(),
    });
    if (model_set == nullptr || !model_set->isComplete()) {
      ++result.missing_model_count;
      continue;
    }
    auto scored = ScoreSegment(segment_char, *model_set, input_slew_ns, downstream_cap_pf, request.options.use_conservative_scoring);
    if (scored.has_value()) {
      scored_segments.push_back(*scored);
      ++result.scored_segment_count;
    } else {
      ++result.metric_evaluation_rejected_count;
    }
  }

  auto shortlist = TrimScoredSegments(std::move(scored_segments), request.options.per_level_shortlist_size);
  const auto diagnostic_pattern_ids = DiagnosticPatternIds(request, level_index);
  for (const auto& scored : shortlist) {
    RecordDiagnosticPatternStage(diagnostic_pattern_ids, scored.pattern_id, DiagnosticPatternStage::kShortlisted, result);
  }
  return shortlist;
}

}  // namespace icts::htree::analytical_solver
