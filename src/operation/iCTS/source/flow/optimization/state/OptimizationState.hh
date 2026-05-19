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
 * @file OptimizationState.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Fast STA state capture contracts for CTS optimization.
 */

#pragma once

#include <cstddef>
#include <vector>

#include "FastStaTypes.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts::optimization_internal {

auto FirstActionBufferIndex(const std::vector<SizingAction>& actions) -> std::size_t;
auto CaptureState(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline, const std::vector<SlewBaseline>& slew_baseline)
    -> FastState;
auto CaptureStateWithArea(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline,
                          const std::vector<SlewBaseline>& slew_baseline, double area_um2) -> FastState;
auto TargetMet(const FastState& state, double target_skew_ns) -> bool;
auto StateImproves(const FastState& current, const FastState& candidate, double target_skew_ns) -> bool;

}  // namespace icts::optimization_internal
