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
 * @author OpenAI Codex
 * @date 2026-04-14
 * @brief End-to-end H-tree synthesis flow built from topology and characterization modules.
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Tree.hh"

namespace icts {

class HTreeBuilder
{
 public:
  struct BuildOptions
  {
    std::optional<bool> force_leaf_branch_buffer = std::nullopt;
    std::optional<double> min_top_input_slew_ns = std::nullopt;
    std::optional<double> min_leaf_driven_cap_pf = std::nullopt;
  };

  struct LevelPlan
  {
    int requested_length_dbu = 0;
    double requested_length_um = 0.0;
    unsigned aligned_length_idx = 0;
    double aligned_length_um = 0.0;
    bool is_leaf_level = false;
    bool selected_has_terminal_branch_buffer = false;
    PatternId segment_pattern_id = PatternId::segment(0);
  };

  struct BuildResult
  {
    bool success = false;
    Tree topology;
    std::vector<LevelPlan> levels;
    std::optional<HTreeTopologyChar> best_char = std::nullopt;
    std::optional<HTreeTopologyPattern> best_pattern = std::nullopt;
    std::vector<HTreeTopologyChar> candidate_chars;
    std::vector<HTreeTopologyChar> feasible_chars;
    double char_wire_length_unit_um = 0.0;
    unsigned char_wire_length_iterations = 0U;
    unsigned char_unique_level_bins = 0U;
    bool char_grid_adapted = false;
    double char_max_slew_ns = 0.0;
    double char_max_cap_pf = 0.0;
    unsigned char_slew_steps = 0U;
    unsigned char_cap_steps = 0U;
    bool force_leaf_branch_buffer = false;
    std::optional<double> min_top_input_slew_ns = std::nullopt;
    std::optional<unsigned> top_input_slew_floor_idx = std::nullopt;
    std::optional<double> min_leaf_driven_cap_pf = std::nullopt;
    std::optional<unsigned> leaf_driven_cap_floor_idx = std::nullopt;
    bool used_boundary_fallback = false;
    std::optional<double> boundary_fallback_score = std::nullopt;
    std::string boundary_fallback_reason;

    std::vector<std::unique_ptr<Inst>> inst_storage;
    std::vector<std::unique_ptr<Pin>> pin_storage;
    std::vector<std::unique_ptr<Net>> net_storage;

    std::vector<Inst*> inserted_insts;
    std::vector<Pin*> inserted_pins;
    std::vector<Net*> inserted_nets;

    Inst* root_inst = nullptr;
    Pin* root_input_pin = nullptr;
    Pin* root_output_pin = nullptr;
  };

  HTreeBuilder() = default;
  ~HTreeBuilder() = default;

  static auto build(const std::vector<Pin*>& loads) -> BuildResult;
  static auto build(const std::vector<Pin*>& loads, const BuildOptions& options) -> BuildResult;
};

}  // namespace icts
