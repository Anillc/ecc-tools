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
 * @file TimingEngine.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Pure RCTree timing propagation engine for the timing module.
 */

#pragma once

#include <cstddef>

#include "RCTree.hh"

namespace icts {

class TimingEngine
{
 public:
  struct Metrics
  {
    double skew = 0.0;
    double min_delay = 0.0;
    double max_delay = 0.0;
    double max_slew = 0.0;
    double total_cap = 0.0;
  };

  TimingEngine() = delete;
  ~TimingEngine() = default;

  static Metrics update(RCTree& rc_tree);

  static void updateDownstreamCap(RCTree& rc_tree);
  static void updateIncreaseDelay(RCTree& rc_tree);
  static void updateArrival(RCTree& rc_tree);
  static void updateSlew(RCTree& rc_tree);
  static void updateDownstreamDelay(RCTree& rc_tree);

  static Metrics evaluate(const RCTree& rc_tree);
  static double calcSkew(const RCTree& rc_tree);

  static double calcArcDelay(double downstream_cap, double resistance, double capacitance);
  static double calcIdealSlew(double arc_delay);
};

}  // namespace icts
