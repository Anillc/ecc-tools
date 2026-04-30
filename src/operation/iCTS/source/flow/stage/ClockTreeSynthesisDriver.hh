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
 * @file ClockTreeSynthesisDriver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Per-clock CTS sink-domain synthesis coordinator.
 */

#pragma once

#include <cstddef>

#include "logger/Schema.hh"
#include "stage/CTSClockTreeRunSummary.hh"

namespace icts {

class Clock;
class ClockTreeView;

struct ClockTreeSynthesisResult
{
  bool success = false;
  bool skipped = false;
};

class ClockTreeSynthesisDriver
{
 public:
  ClockTreeSynthesisDriver() = delete;

  static auto run(Clock& clock, std::size_t clock_index, ClockTreeView& clock_tree_view, CTSClockTreeRunSummary& summary,
                  schema::TableRows& rows, std::size_t& total_sink_domains, std::size_t& hard_macro_sink_count,
                  std::size_t& regular_sink_count) -> ClockTreeSynthesisResult;
};

}  // namespace icts
