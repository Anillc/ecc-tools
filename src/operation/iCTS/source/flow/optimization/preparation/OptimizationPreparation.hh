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
 * @file OptimizationPreparation.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Preparation contracts for CTS post-synthesis optimization.
 */

#pragma once

#include <string_view>
#include <vector>

#include "FastStaTypes.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts {
class Clock;
}  // namespace icts

namespace icts::optimization_internal {

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

}  // namespace icts::optimization_internal
