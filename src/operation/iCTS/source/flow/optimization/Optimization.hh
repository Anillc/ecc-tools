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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file Optimization.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief CTS post-synthesis optimization flow facade.
 */

#pragma once

#include <cstddef>

namespace icts {

class CharacterizationLibrary;
class ClockLayout;

struct OptimizationResult
{
  bool success = true;
  bool optimized = false;
  std::size_t clock_count = 0U;
  std::size_t optimized_clock_count = 0U;
  std::size_t accepted_mutation_count = 0U;
};

class Optimization
{
 public:
  Optimization() = delete;

  static auto run(ClockLayout& clock_layout, CharacterizationLibrary& char_library) -> OptimizationResult;
};

}  // namespace icts
