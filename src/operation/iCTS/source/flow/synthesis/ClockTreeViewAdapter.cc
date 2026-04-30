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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ClockTreeViewAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Clock-tree synthesis-to-view adapter implementation.
 */

#include "synthesis/ClockTreeViewAdapter.hh"

#include <optional>
#include <ranges>

#include "design/Inst.hh"
#include "design/Net.hh"
#include "htree/HTreeBuilder.hh"

namespace icts {
namespace {

auto selectedDepthToInt(const std::optional<unsigned>& selected_depth) -> int
{
  return selected_depth.has_value() ? static_cast<int>(*selected_depth) : -1;
}

auto findInsertedInstLevel(const std::vector<HTreeBuilder::InsertedInstLevel>& levels, const Inst* inst) -> int
{
  if (inst == nullptr) {
    return -1;
  }
  const auto iter
      = std::ranges::find_if(levels, [inst](const HTreeBuilder::InsertedInstLevel& level) -> bool { return level.inst == inst; });
  return iter == levels.end() ? -1 : iter->topology_level;
}

auto findInsertedNetLevel(const std::vector<HTreeBuilder::InsertedNetLevel>& levels, const Net* net) -> std::optional<int>
{
  if (net == nullptr) {
    return std::nullopt;
  }
  const auto iter = std::ranges::find_if(levels, [net](const HTreeBuilder::InsertedNetLevel& level) -> bool { return level.net == net; });
  if (iter == levels.end()) {
    return std::nullopt;
  }
  return iter->topology_level;
}

auto fallbackNetTopologyLevel(CTSNetRole role, ClockTreeSynthesisPhase synthesis_phase, int selected_depth) -> int
{
  if (role == CTSNetRole::kSinkTree) {
    return selected_depth >= 0 ? selected_depth : 0;
  }
  if (role == CTSNetRole::kSourceToRoot && synthesis_phase == ClockTreeSynthesisPhase::kSourceToRootHTree) {
    return selected_depth >= 0 ? selected_depth : 0;
  }
  return 0;
}

auto resolveNetTopologyLevel(const std::vector<HTreeBuilder::InsertedNetLevel>& levels, const Net* net, CTSNetRole role,
                             ClockTreeSynthesisPhase synthesis_phase, int selected_depth) -> int
{
  const auto inserted_level = findInsertedNetLevel(levels, net);
  if (inserted_level.has_value()) {
    return *inserted_level;
  }
  return fallbackNetTopologyLevel(role, synthesis_phase, selected_depth);
}

}  // namespace

auto ClockTreeViewAdapter::makeSinkDomainViewInput(const ClockSynthesis::BuildResult& result) -> ClockSinkDomainViewInput
{
  ClockSinkDomainViewInput input{
      .selected_depth = selectedDepthToInt(result.selected_htree_depth),
      .topology_level_count = static_cast<int>(result.selected_htree_level_count),
      .inserted_insts = {},
      .inserted_nets = {},
  };
  input.inserted_insts.reserve(result.inserted_insts.size());
  for (const auto& inst : result.inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    input.inserted_insts.push_back(ClockTreeViewInstTopology{
        .inst = inst.get(),
        .topology_level = findInsertedInstLevel(result.inserted_inst_levels, inst.get()),
    });
  }
  input.inserted_nets.reserve(result.inserted_nets.size());
  for (const auto& net : result.inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    input.inserted_nets.push_back(ClockTreeViewNetTopology{
        .net = net.get(),
        .topology_level = resolveNetTopologyLevel(result.inserted_net_levels, net.get(), CTSNetRole::kSinkTree,
                                                  ClockTreeSynthesisPhase::kDownstreamHTree, input.selected_depth),
    });
  }
  return input;
}

auto ClockTreeViewAdapter::makeSourceToRootViewInput(const ClockSynthesis::SourceToRootBuildResult& result,
                                                     ClockTreeSynthesisPhase synthesis_phase) -> ClockSourceToRootViewInput
{
  ClockSourceToRootViewInput input{
      .selected_depth = selectedDepthToInt(result.htree_result.selected_depth),
      .topology_level_count = static_cast<int>(result.htree_result.levels.size()),
      .inserted_insts = {},
      .inserted_nets = {},
  };
  input.inserted_insts.reserve(result.inserted_insts.size());
  for (const auto& inst : result.inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    input.inserted_insts.push_back(ClockTreeViewInstTopology{
        .inst = inst.get(),
        .topology_level = findInsertedInstLevel(result.inserted_inst_levels, inst.get()),
    });
  }
  input.inserted_nets.reserve(result.inserted_nets.size());
  for (const auto& net : result.inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    input.inserted_nets.push_back(ClockTreeViewNetTopology{
        .net = net.get(),
        .topology_level
        = resolveNetTopologyLevel(result.inserted_net_levels, net.get(), CTSNetRole::kSourceToRoot, synthesis_phase, input.selected_depth),
    });
  }
  return input;
}

}  // namespace icts
