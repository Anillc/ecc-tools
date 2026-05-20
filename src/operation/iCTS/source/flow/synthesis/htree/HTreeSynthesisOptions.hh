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
 * @file HTreeSynthesisOptions.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief H-tree topology synthesis option and logging context contracts.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "spatial/Point.hh"

namespace icts {

class CharacterizationLibrary;

struct HTreeSynthesisLogContext
{
  std::string clock_name;
  std::string clock_net_name;
  std::string sink_domain;
  std::string stage;
  std::string object_name_prefix;
};

struct HTreeSynthesisOptions
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
  HTreeSynthesisLogContext log_context;
  std::string object_name_prefix;
  bool enable_analytical_solver = false;
};

}  // namespace icts
