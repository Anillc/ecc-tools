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
 * @file HTree.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief H-tree topology-family synthesis entry.
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
#include "spatial/Point.hh"
#include "spatial/Tree.hh"

namespace icts {

class CharacterizationLibrary;

class HTree
{
 public:
  struct LogContext
  {
    std::string clock_name;
    std::string clock_net_name;
    std::string sink_domain;
    std::string stage;
    std::string object_name_prefix;
  };

  struct BuildOptions
  {
    std::optional<bool> force_branch_buffer = std::nullopt;
    std::optional<double> min_top_input_slew_ns = std::nullopt;
    std::optional<unsigned> target_depth = std::nullopt;
    std::optional<unsigned> depth_explore_window = std::nullopt;
    std::optional<double> htree_topology_tolerance = std::nullopt;
    std::optional<Point<int>> fixed_topology_root_location = std::nullopt;
    CharacterizationLibrary* characterization_library = nullptr;
    std::vector<double> additional_characterization_lengths_um;
    bool enable_root_driver_sizing = true;
    bool allow_boundary_relaxation = false;
    bool topology_loads_are_local_buffers = false;
    double clock_period_ns = 0.0;
    std::string clock_period_source;
    LogContext log_context;
    std::string object_name_prefix;
    bool enable_analytical_solver = false;
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
    std::size_t selected_buffer_count = 0U;
    double selected_buffer_area_um2 = 0.0;
    std::size_t selected_weighted_buffer_count = 0U;
    double selected_weighted_buffer_area_um2 = 0.0;
    PatternId segment_pattern_id = PatternId::segment(0);
  };

  struct InsertedInstLevel
  {
    Inst* inst = nullptr;
    int topology_level = -1;
    std::size_t index_in_level = 0U;
  };

  struct InsertedNetLevel
  {
    Net* net = nullptr;
    int topology_level = -1;
    std::size_t index_in_level = 0U;
  };

  struct RootDriverCompensationReport
  {
    bool enabled = false;
    bool valid = false;
    std::string method;
    std::string cell_master;
    std::string load_source;
    std::string route_estimator;
    double input_slew_ns = 0.0;
    unsigned load_bucket_idx = 0U;
    double load_cap_pf = 0.0;
    unsigned source_boundary_bucket_idx = 0U;
    double source_boundary_load_cap_pf = 0.0;
    std::size_t source_boundary_branch_count = 0U;
    double terminal_pin_cap_pf = 0.0;
    double wire_cap_pf = 0.0;
    double routed_wirelength_um = 0.0;
    std::size_t terminal_count = 0U;
    double clock_period_ns = 0.0;
    std::string clock_period_source;
    double output_slew_ns = 0.0;
    unsigned output_slew_bucket_idx = 0U;
    double cell_delay_ns = 0.0;
    double internal_power_w = 0.0;
    double leakage_power_w = 0.0;
    double cell_power_w = 0.0;
    double raw_delay_ns = 0.0;
    double raw_power_w = 0.0;
    double compensated_delay_ns = 0.0;
    double compensated_power_w = 0.0;
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
    bool root_driver_sizing_enabled = true;
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
    RootDriverCompensationReport root_driver_compensation;
    bool used_boundary_relaxation = false;
    std::optional<double> boundary_relaxation_score = std::nullopt;
    std::string boundary_relaxation_reason;
    std::size_t pruned_leaf_single_load_buffers = 0U;
    bool analytical_mode_enabled = false;
    bool analytical_mode_selected = false;
    std::string analytical_failure_reason;
    std::size_t analytical_model_set_count = 0U;
    std::size_t analytical_rejected_fit_count = 0U;
    std::size_t analytical_structural_cap_operator_count = 0U;
    std::size_t analytical_evaluated_segment_count = 0U;
    std::size_t analytical_generated_candidate_count = 0U;
    std::size_t analytical_validated_candidate_count = 0U;
    std::size_t analytical_validated_pareto_count = 0U;
    std::size_t analytical_selected_pareto_power_rank = 0U;
    double analytical_validated_delay_min_ns = 0.0;
    double analytical_validated_delay_median_ns = 0.0;
    double analytical_validated_delay_max_ns = 0.0;
    double analytical_validated_power_min_w = 0.0;
    double analytical_validated_power_median_w = 0.0;
    double analytical_validated_power_max_w = 0.0;
    LogContext log_context;
    std::string object_name_prefix;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;
    std::vector<InsertedInstLevel> inserted_inst_levels;
    std::vector<InsertedNetLevel> inserted_net_levels;

    Inst* root_inst = nullptr;
    Pin* root_input_pin = nullptr;
    Pin* root_output_pin = nullptr;
    Net* root_net = nullptr;
  };

  HTree() = delete;

  static auto build(Net& root_net) -> BuildResult;
  static auto build(Net& root_net, const BuildOptions& options) -> BuildResult;
};

}  // namespace icts
