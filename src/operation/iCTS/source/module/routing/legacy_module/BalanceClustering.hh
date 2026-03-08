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
 * @file BalanceClustering.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Minimal balance clustering compatibility helpers for routing migration
 */

#pragma once

#include <vector>

#include "Pin.hh"

namespace icts {

class BalanceClustering
{
 public:
  BalanceClustering() = delete;
  ~BalanceClustering() = default;

  static Point calcCentroid(const std::vector<Pin*>& load_pins)
  {
    if (load_pins.empty()) {
      return Point(0, 0);
    }

    long long sum_x = 0;
    long long sum_y = 0;
    for (auto* pin : load_pins) {
      auto loc = pin->get_location();
      sum_x += loc.x();
      sum_y += loc.y();
    }

    auto size = static_cast<long long>(load_pins.size());
    return Point(static_cast<int>(sum_x / size), static_cast<int>(sum_y / size));
  }
};

}  // namespace icts
