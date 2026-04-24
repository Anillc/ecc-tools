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
 * @file HTreeBuildObservation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Test-side observation helpers for compact HTreeBuilder build results.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "flow/htree/HTreeBuilder.hh"

namespace icts_test::htree {

struct HTreeBuildObservation
{
  bool success = false;
  bool has_best_char = false;
  unsigned best_pattern_id = 0U;
  double best_delay_ns = 0.0;
  double best_power_w = 0.0;
  bool has_selected_depth = false;
  unsigned selected_depth = 0U;
  std::size_t selected_level_count = 0U;
  std::size_t depth_candidate_count = 0U;
  std::size_t selected_final_frontier_count = 0U;
  std::size_t selected_candidate_solution_count = 0U;
  std::size_t selected_candidate_frontier_entry_count = 0U;
  std::size_t selected_feasible_solution_count = 0U;
  std::size_t selected_feasible_frontier_entry_count = 0U;
  std::size_t htree_load_group_count = 0U;
  double htree_load_cap_min_pf = 0.0;
  double htree_load_cap_max_pf = 0.0;
  double htree_load_cap_mean_pf = 0.0;
  double htree_load_cap_median_pf = 0.0;
  bool used_boundary_fallback = false;
  std::optional<double> boundary_fallback_score = std::nullopt;
  std::string boundary_fallback_reason;
};

inline auto ObserveHTreeBuild(const icts::HTreeBuilder::BuildResult& result) -> HTreeBuildObservation
{
  HTreeBuildObservation observation{
      .success = result.success,
      .has_best_char = result.best_char.has_value(),
      .has_selected_depth = result.selected_depth.has_value(),
      .selected_depth = result.selected_depth.value_or(0U),
      .selected_level_count = result.levels.size(),
      .depth_candidate_count = result.depth_candidate_count,
      .selected_final_frontier_count = result.selected_final_frontier_count,
      .selected_candidate_solution_count = result.selected_candidate_solution_count,
      .selected_candidate_frontier_entry_count = result.selected_candidate_frontier_entry_count,
      .selected_feasible_solution_count = result.selected_feasible_solution_count,
      .selected_feasible_frontier_entry_count = result.selected_feasible_frontier_entry_count,
      .htree_load_group_count = result.htree_load_group_count,
      .htree_load_cap_min_pf = result.htree_load_cap_min_pf,
      .htree_load_cap_max_pf = result.htree_load_cap_max_pf,
      .htree_load_cap_mean_pf = result.htree_load_cap_mean_pf,
      .htree_load_cap_median_pf = result.htree_load_cap_median_pf,
      .used_boundary_fallback = result.used_boundary_fallback,
      .boundary_fallback_score = result.boundary_fallback_score,
      .boundary_fallback_reason = result.boundary_fallback_reason,
  };
  if (result.best_char.has_value()) {
    observation.best_pattern_id = result.best_char->get_pattern_id().local_id;
    observation.best_delay_ns = result.best_char->get_delay();
    observation.best_power_w = result.best_char->get_power();
  }
  return observation;
}

}  // namespace icts_test::htree
