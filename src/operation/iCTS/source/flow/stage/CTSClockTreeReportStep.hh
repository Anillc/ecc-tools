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
 * @file CTSClockTreeReportStep.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-tree report step orchestration.
 */

#pragma once

#include <string>

namespace icts {

class ClockTreeView;
struct ClockTreeEvaluationState;

struct CTSClockTreeReportResult
{
  bool report_success = false;
  bool evaluation_ready = false;
};

class CTSClockTreeReportStep
{
 public:
  CTSClockTreeReportStep() = delete;

  static auto run(const std::string& save_dir, bool evaluation_ready, bool refresh_sta_timing, const ClockTreeView& clock_tree_view,
                  ClockTreeEvaluationState& evaluation_state) -> CTSClockTreeReportResult;
};

}  // namespace icts
