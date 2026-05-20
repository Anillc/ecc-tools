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
 * @file OptimizationReport.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Report and runtime formatting contracts for CTS optimization.
 */

#pragma once

#include <chrono>
#include <string>

namespace icts {
class Clock;
}  // namespace icts

namespace icts::clock_sizing_optimization {

struct ClockSizingSummary;
struct ClockSizingRuntimeProfile;

auto ElapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double;
auto FormatNs(double value) -> std::string;
auto FormatSeconds(double value) -> std::string;
auto EmitClockSummary(const Clock& clock, const ClockSizingSummary& summary, double target_skew_ns, double runtime_s) -> void;
auto EmitClockProfile(const Clock& clock, const ClockSizingRuntimeProfile& profile) -> void;

}  // namespace icts::clock_sizing_optimization
