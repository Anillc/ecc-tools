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
 * @file HTreeTopologyDepthEvaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree single-depth candidate evaluation and summary recording.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree_builder {

auto EvaluateTopologyDepthCandidate(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth,
                                    const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                                    BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                                    SinkLoadProfileLegalityContext& sink_load_profile_legality_context, unsigned char_slew_steps)
    -> HTreeTopologyDepthEvaluationResult
{
  auto candidate_levels = MakeCandidateLevelPlans(full_level_plans, depth);
  const std::size_t leaf_count = CountCandidateLeafNodes(topology, depth);
  const auto candidate_options = base_resolved_options;

  HTreeTopologyDepthEvaluationResult candidate_result;
  candidate_result.leaf_count = leaf_count;
  candidate_result.evaluation = EvaluateCandidateBuild(candidate_levels, entry_sets_by_length, segment_pattern_registry, candidate_options,
                                                       topology, sink_load_profile_legality_context, leaf_count, depth, char_slew_steps);
  return candidate_result;
}

auto RecordTopologyDepthCandidateResult(unsigned depth, bool used_explicit_target_depth,
                                        const HTreeTopologyDepthEvaluationResult& candidate_result,
                                        std::vector<HTreeTopologyDepthSummary>& depth_summaries) -> void
{
  const auto& evaluation = candidate_result.evaluation;
  depth_summaries.push_back(HTreeTopologyDepthSummary{
      .depth = depth,
      .leaf_count = candidate_result.leaf_count,
      .success = evaluation.success,
      .selected = false,
      .used_explicit_target_depth = used_explicit_target_depth,
      .failure_reason = evaluation.failure_reason,
      .final_frontier_count = evaluation.final_frontier_count,
      .candidate_solution_count = evaluation.candidate_chars.size(),
      .candidate_frontier_entry_count = evaluation.candidate_frontier_entries.size(),
      .feasible_solution_count = evaluation.feasible_chars.size(),
      .feasible_frontier_entry_count = evaluation.feasible_frontier_entries.size(),
      .used_boundary_fallback = evaluation.used_boundary_fallback,
      .selected_power_w = evaluation.best_char.has_value() ? evaluation.best_char->get_power() : 0.0,
      .selected_delay_ns = evaluation.best_char.has_value() ? evaluation.best_char->get_delay() : 0.0,
  });
}

auto AppendGlobalCandidateRefs(std::size_t candidate_index, const CandidateBuildEvaluation& evaluation,
                               std::vector<CandidateCharRef>& global_feasible_pool, std::vector<CandidateCharRef>& global_candidate_pool)
    -> void
{
  for (const auto& entry : evaluation.feasible_frontier_entries) {
    global_feasible_pool.push_back(CandidateCharRef{
        .candidate_index = candidate_index,
        .entry = &entry,
    });
  }
  for (const auto& entry : evaluation.candidate_frontier_entries) {
    global_candidate_pool.push_back(CandidateCharRef{
        .candidate_index = candidate_index,
        .entry = &entry,
    });
  }
}

}  // namespace icts::htree_builder
