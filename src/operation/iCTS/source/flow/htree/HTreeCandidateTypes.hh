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
 * @file HTreeCandidateTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief H-tree depth exploration and candidate selection data types.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "htree/HTreeActualLoadTypes.hh"
#include "htree/HTreeBuilder.hh"
#include "htree/HTreeCharacterizationTypes.hh"
#include "htree/HTreePatternRegistry.hh"

namespace icts::htree_builder {

struct CandidateBuildEvaluation
{
  unsigned depth = 0U;
  std::size_t leaf_count = 0U;
  ResolvedBuildOptions resolved_options;
  std::vector<HTreeBuilder::LevelPlan> levels;
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> failure_level = std::nullopt;
  std::optional<unsigned> failure_length_idx = std::nullopt;
  std::size_t final_frontier_count = 0U;
  std::vector<HTreeTopologyChar> candidate_chars;
  std::vector<HTreeTopologyChar> candidate_frontier_entries;
  std::vector<HTreeTopologyChar> feasible_chars;
  std::vector<HTreeTopologyChar> feasible_frontier_entries;
  std::optional<HTreeTopologyChar> best_char = std::nullopt;
  bool used_boundary_fallback = false;
  std::optional<double> boundary_fallback_score = std::nullopt;
  std::string boundary_fallback_reason;
  TopologyPatternRegistry topology_pattern_registry;
};

struct HTreeDepthCandidateSummary
{
  unsigned depth = 0U;
  std::size_t leaf_count = 0U;
  bool success = false;
  bool selected = false;
  bool used_explicit_target_depth = false;
  std::string failure_reason;
  std::size_t htree_load_group_count = 0U;
  double htree_load_cap_min_pf = 0.0;
  double htree_load_cap_max_pf = 0.0;
  double htree_load_cap_mean_pf = 0.0;
  double htree_load_cap_median_pf = 0.0;
  std::size_t final_frontier_count = 0U;
  std::size_t candidate_solution_count = 0U;
  std::size_t candidate_frontier_entry_count = 0U;
  std::size_t feasible_solution_count = 0U;
  std::size_t feasible_frontier_entry_count = 0U;
  bool used_boundary_fallback = false;
  double selected_power_w = 0.0;
  double selected_delay_ns = 0.0;
};

struct ActualLoadEntryFilterResult
{
  std::vector<HTreeTopologyChar> entries;
  std::string first_failure_reason;
};

struct CandidateCharRef
{
  std::size_t candidate_index = 0U;
  const HTreeTopologyChar* entry = nullptr;
};

struct CandidateCharRefFilterResult
{
  std::vector<CandidateCharRef> entries;
  std::string first_failure_reason;
};

struct HTreeDepthExplorationResult
{
  std::vector<CandidateBuildEvaluation> candidate_evaluations;
  std::vector<HTreeDepthCandidateSummary> depth_summaries;
  std::vector<CandidateCharRef> global_feasible_pool;
  std::vector<CandidateCharRef> global_candidate_pool;
  ActualLoadLegalityContext actual_load_legality_context;
};

struct HTreeDepthCandidateEvaluationResult
{
  CandidateBuildEvaluation evaluation;
  std::size_t leaf_count = 0U;
};

}  // namespace icts::htree_builder
