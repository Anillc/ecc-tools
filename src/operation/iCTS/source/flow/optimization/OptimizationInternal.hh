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
 * @file OptimizationInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Internal function contracts for CTS post-synthesis optimization.
 */

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "optimization/OptimizationOptions.hh"
#include "optimization/OptimizationTypes.hh"

namespace icts {

class Clock;
class ClockLayout;

namespace optimization_internal {

auto ElapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double;
auto FormatNs(double value) -> std::string;
auto FormatSeconds(double value) -> std::string;

auto CaptureGraphProfile(const FastStaClockContext& context) -> OptimizationRuntimeProfile;
auto CopyOuterProfile(OptimizationRuntimeProfile& destination, const OptimizationRuntimeProfile& source) -> void;
auto CollectBufferMasterInfos() -> std::vector<BufferMasterInfo>;
auto BuildRouteTreeCache(const std::vector<Clock*>& clocks) -> RouteTreeCache;
auto FindMasterInfo(const std::vector<BufferMasterInfo>& master_infos, std::string_view cell_master) -> const BufferMasterInfo*;
auto CollectCapBaseline(FastStaClockId clock_id) -> std::vector<CapBaseline>;
auto CollectSlewBaseline(FastStaClockId clock_id) -> std::vector<SlewBaseline>;
auto CollectOptimizableBuffers(FastStaClockId clock_id, const std::vector<BufferMasterInfo>& master_infos)
    -> std::vector<OptimizableBuffer>;
auto InjectRouteTrees(FastStaClockId clock_id, const Clock& clock, const RouteTreeCache& route_tree_by_net) -> bool;

auto BuildTopologyIndex(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> TopologyIndex;
auto MakeSizingAction(const std::vector<OptimizableBuffer>& buffers, std::size_t buffer_index, FrontierSide side, unsigned rank_step)
    -> std::optional<SizingAction>;
auto GenerateBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                             const FastState& current) -> std::vector<std::vector<SizingAction>>;
auto GenerateScalableBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                     const FastState& current, double target_skew_ns) -> std::vector<ScoredBatchCandidate>;
auto FirstActionBufferIndex(const std::vector<SizingAction>& actions) -> std::size_t;

auto SolveClock(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary;
auto SolveClockScalable(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary;
auto ShouldUseScalableSolver(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> bool;

auto ApplyMutations(const std::vector<OptimizationMutation>& mutations, const std::vector<OptimizableBuffer>& buffers,
                    ClockLayout& clock_layout) -> bool;

auto EmitClockSummary(const Clock& clock, const ClockOptimizationSummary& summary, double target_skew_ns, double runtime_s) -> void;
auto EmitClockProfile(const Clock& clock, const OptimizationRuntimeProfile& profile) -> void;

}  // namespace optimization_internal
}  // namespace icts
