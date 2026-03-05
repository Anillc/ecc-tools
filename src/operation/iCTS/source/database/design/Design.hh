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
 * @file Design.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief Aggregated design database for iCTS
 */

#pragma once
#include <vector>

#include "Clock.hh"

namespace icts {

#define CTSDesignInst (icts::Design::getInst())

class Design
{
 public:
  static Design& getInst()
  {
    static Design inst;
    return inst;
  }

  // Delete copy and move constructors
  Design(const Design& rhs) = delete;
  Design(Design&& rhs) = delete;
  Design& operator=(const Design& rhs) = delete;
  Design& operator=(Design&& rhs) = delete;

  // Reset design data
  void reset()
  {
    for (auto* clock : _clocks) {
      delete clock;
    }
    _clocks.clear();
  }

  // Getter
  const std::vector<Clock*>& get_clocks() const { return _clocks; }

  // Setter
  void set_clocks(const std::vector<Clock*>& clocks) { _clocks = clocks; }

  // Adder
  void add_clock(Clock* clock) { _clocks.push_back(clock); }

 private:
  Design() = default;
  ~Design() = default;

  std::vector<Clock*> _clocks;
};

}  // namespace icts