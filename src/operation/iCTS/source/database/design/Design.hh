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
#include <string>
#include <vector>

namespace icts {

class Clock;

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
  auto reset() -> void;

  // Getter
  auto get_clocks() const -> std::vector<Clock*>;

  // Setter
  auto set_clocks(std::vector<std::unique_ptr<Clock>> clocks) -> void;

  // Adder
  auto add_clock(std::unique_ptr<Clock> clock) -> Clock*;
  auto emitClockDistributionSummary(const std::string& title = "Clock Distribution Summary") const -> void;

 private:
  Design();
  ~Design();

  std::vector<std::unique_ptr<Clock>> _clocks;
};

}  // namespace icts
