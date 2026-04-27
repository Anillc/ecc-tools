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
 * @file ClockTreeEvaluator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree writeback and evaluation stage.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icts {

struct ClockTreeSummary
{
  struct ClockTiming
  {
    std::string clock_name;
    double setup_tns = 0.0;
    double setup_wns = 0.0;
    double hold_tns = 0.0;
    double hold_wns = 0.0;
    double suggest_freq = 0.0;
  };

  int32_t buffer_num = 0;
  double buffer_area = 0.0;
  int32_t clock_path_min_buffer = 0;
  int32_t clock_path_max_buffer = 0;
  int32_t max_level_of_clock_tree = 0;
  int32_t max_clock_wirelength = 0;
  double total_clock_wirelength = 0.0;
  std::vector<ClockTiming> clocks_timing;
};

class ClockTreeEvaluator
{
 public:
  ClockTreeEvaluator() = delete;

  static auto evaluate() -> void;
  static auto outputSummary() -> ClockTreeSummary;
  static auto resetSummary() -> void;
};

}  // namespace icts
