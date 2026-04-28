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
 * @file HTreeBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief End-to-end H-tree synthesis flow built from topology and characterization modules.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "characterization/HTreeTopologyChar.hh"
#include "characterization/HTreeTopologyPattern.hh"
#include "characterization/PatternId.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "spatial/Tree.hh"

namespace icts {

class HTreeBuilder
{
 public:
  struct BuildOptions
  {
    std::optional<bool> force_branch_buffer = std::nullopt;
    std::optional<double> min_top_input_slew_ns = std::nullopt;
    std::optional<unsigned> target_depth = std::nullopt;
    std::optional<unsigned> depth_explore_window = std::nullopt;
    std::optional<double> htree_topology_tolerance = std::nullopt;
    bool topology_loads_are_local_buffers = false;
  };

  struct LevelPlan
  {
    int requested_length_dbu = 0;
    double requested_length_um = 0.0;
    unsigned aligned_length_idx = 0;
    double aligned_length_um = 0.0;
    bool is_leaf_level = false;
    bool selected_has_any_buffer = false;
    bool selected_has_terminal_branch_buffer = false;
    std::string selected_leaf_buffer_cell_master;
    std::string selected_terminal_cell_master;
    PatternId segment_pattern_id = PatternId::segment(0);
  };

  struct BuildResult
  {
    bool success = false;
    std::string failure_reason;
    std::optional<unsigned> failure_level = std::nullopt;
    std::optional<unsigned> failure_length_idx = std::nullopt;
    Tree topology;
    std::vector<LevelPlan> levels;
    std::optional<HTreeTopologyChar> best_char = std::nullopt;
    std::optional<HTreeTopologyPattern> best_pattern = std::nullopt;
    double char_wirelength_unit_um = 0.0;
    unsigned char_wirelength_iterations = 0U;
    unsigned char_unique_level_bins = 0U;
    bool char_grid_adapted = false;
    double char_max_slew_ns = 0.0;
    double char_max_cap_pf = 0.0;
    unsigned char_slew_steps = 0U;
    unsigned char_cap_steps = 0U;
    bool force_branch_buffer = false;
    std::optional<unsigned> target_depth = std::nullopt;
    unsigned depth_explore_window = 0U;
    std::optional<unsigned> selected_depth = std::nullopt;
    std::size_t depth_candidate_count = 0U;
    std::size_t selected_final_frontier_count = 0U;
    std::size_t selected_candidate_solution_count = 0U;
    std::size_t selected_candidate_frontier_entry_count = 0U;
    std::size_t selected_feasible_solution_count = 0U;
    std::size_t selected_feasible_frontier_entry_count = 0U;
    std::optional<double> min_top_input_slew_ns = std::nullopt;
    std::optional<unsigned> top_input_slew_covering_idx = std::nullopt;
    std::size_t htree_load_group_count = 0U;
    double htree_load_cap_min_pf = 0.0;
    double htree_load_cap_max_pf = 0.0;
    double htree_load_cap_mean_pf = 0.0;
    double htree_load_cap_median_pf = 0.0;
    std::string selected_root_driver_cell_master;
    bool used_boundary_fallback = false;
    std::optional<double> boundary_fallback_score = std::nullopt;
    std::string boundary_fallback_reason;
    std::size_t pruned_leaf_single_load_buffers = 0U;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;

    Inst* root_inst = nullptr;
    Pin* root_input_pin = nullptr;
    Pin* root_output_pin = nullptr;
    Net* root_net = nullptr;
  };

  HTreeBuilder() = delete;

  static auto build(Net& root_net) -> BuildResult;
  static auto build(Net& root_net, const BuildOptions& options) -> BuildResult;
};

}  // namespace icts
