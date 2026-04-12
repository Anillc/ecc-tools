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
#include <memory>
#include <vector>

#include "Clock.hh"

namespace icts {

#define DESIGN_INST (icts::Design::getInst())

class Design
{
 public:
  static auto getInst() -> Design&
  {
    static Design inst;
    return inst;
  }

  // Delete copy and move constructors
  Design(const Design& rhs) = delete;
  Design(Design&& rhs) = delete;
  auto operator=(const Design& rhs) -> Design& = delete;
  auto operator=(Design&& rhs) -> Design& = delete;

  // Reset design data
  auto reset() -> void { _clocks.clear(); }

  // Getter
  auto get_clocks() const -> std::vector<Clock*>
  {
    std::vector<Clock*> clocks;
    clocks.reserve(_clocks.size());
    for (const auto& clock : _clocks) {
      clocks.push_back(clock.get());
    }
    return clocks;
  }

  // Setter
  auto set_clocks(std::vector<std::unique_ptr<Clock>> clocks) -> void { _clocks = std::move(clocks); }

  // Adder
  auto add_clock(std::unique_ptr<Clock> clock) -> Clock*
  {
    if (clock == nullptr) {
      return nullptr;
    }
    auto* raw = clock.get();
    _clocks.push_back(std::move(clock));
    return raw;
  }

 private:
  Design() = default;
  ~Design() = default;

  std::vector<std::unique_ptr<Clock>> _clocks;
};

}  // namespace icts