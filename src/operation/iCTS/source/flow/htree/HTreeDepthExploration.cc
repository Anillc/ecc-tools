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
 * @file HTreeDepthExploration.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree depth-candidate expansion and global frontier pool construction.
 */

#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ValueLattice.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeBuilderInternal.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree_builder {

auto ExploreDepthCandidates(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans,
                            const std::vector<unsigned>& depth_candidates,
                            const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                            BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                            const UniformValueLattice& cap_lattice, unsigned char_slew_steps, bool used_explicit_target_depth,
                            HTreeBuilder::BuildResult& result) -> HTreeDepthExplorationResult
{
  HTreeDepthExplorationResult exploration;
  exploration.candidate_evaluations.reserve(depth_candidates.size());
  exploration.actual_load_legality_context = ActualLoadLegalityContext{
      .result_by_signature = {},
      .max_monotone_failed_level = std::numeric_limits<int>::min(),
      .cap_lattice = cap_lattice,
  };

  for (const unsigned depth : depth_candidates) {
    auto candidate_result = EvaluateDepthCandidate(topology, full_level_plans, depth, entry_sets_by_length, segment_pattern_registry,
                                                   base_resolved_options, exploration.actual_load_legality_context, char_slew_steps);
    RecordDepthCandidateResult(depth, used_explicit_target_depth, candidate_result, result);
    exploration.candidate_evaluations.push_back(std::move(candidate_result.evaluation));
    const auto candidate_index = exploration.candidate_evaluations.size() - 1U;
    AppendGlobalCandidateRefs(candidate_index, exploration.candidate_evaluations.back(), exploration.global_feasible_pool,
                              exploration.global_candidate_pool);
  }

  return exploration;
}

}  // namespace icts::htree_builder
