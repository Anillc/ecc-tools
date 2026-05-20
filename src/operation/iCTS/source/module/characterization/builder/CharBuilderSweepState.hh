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
 * @file CharBuilderSweepState.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Private sweep topology and sampling state for CTS segment characterization.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "characterization/builder/CharBuilder.hh"

namespace icts {

struct CharBuilder::BuildProgress
{
  double wirelength_um = 0.0;
  std::size_t estimated_patterns = 0;
  std::size_t estimated_sta_samples = 0;
  std::size_t evaluated_patterns = 0;
  std::size_t feasible_patterns = 0;
  std::size_t skipped_patterns_infeasible = 0;
  std::size_t skipped_load_points = 0;
  std::size_t skipped_sta_samples = 0;
  std::size_t executed_sta_samples = 0;
  std::size_t output_slew_overflow_samples = 0;
  std::size_t driven_cap_overflow_samples = 0;
  std::size_t driven_cap_overflow_load_points = 0;
  double max_observed_output_slew_ns = 0.0;
  unsigned max_observed_output_slew_idx = 0;
  double max_observed_driven_cap_pf = 0.0;
  unsigned max_observed_driven_cap_idx = 0;
};

struct CharBuilder::TopologyBits
{
  std::uint64_t value = 0U;
};

struct CharBuilder::TopologyDesc
{
  std::vector<double> wire_segments_um;
  std::vector<std::size_t> buffer_positions;
  bool has_terminal_branch_buffer = false;
};

struct CharBuilder::StoredSampleIndices
{
  unsigned input_slew_idx = 0U;
  unsigned output_slew_idx = 0U;
  unsigned driven_cap_idx = 0U;
  unsigned load_cap_idx = 0U;
};

struct CharBuilder::PatternFeasibility
{
  bool is_pattern_feasible = false;
  double max_load_pf = 0.0;
};

}  // namespace icts
