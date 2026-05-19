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
 * @file OptimizationSolver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Solver contracts for CTS post-synthesis optimization.
 */

#pragma once

#include <vector>

#include "FastStaTypes.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts::optimization_internal {

auto SolveClock(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary;
auto SolveClockScalable(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary;
auto ShouldUseScalableSolver(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> bool;

}  // namespace icts::optimization_internal
