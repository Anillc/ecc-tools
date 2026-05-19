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
 * @file OptimizationCandidates.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Candidate generation contracts for CTS optimization.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "FastStaTypes.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts::optimization_internal {

auto BuildTopologyIndex(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> TopologyIndex;
auto MakeSizingAction(const std::vector<OptimizableBuffer>& buffers, std::size_t buffer_index, FrontierSide side, unsigned rank_step)
    -> std::optional<SizingAction>;
auto GenerateBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                             const FastState& current) -> std::vector<std::vector<SizingAction>>;
auto GenerateScalableBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                     const FastState& current, double target_skew_ns) -> std::vector<ScoredBatchCandidate>;

}  // namespace icts::optimization_internal
