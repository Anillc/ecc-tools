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
 * @file ClockLayoutAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Clock-tree synthesis-to-layout adapter implementation.
 */

#include "synthesis/trace/layout/ClockLayoutAdapter.hh"

#include <optional>
#include <ranges>

#include "design/Inst.hh"
#include "design/Net.hh"
#include "synthesis/htree/HTree.hh"

namespace icts {
namespace {

auto selectedDepthToInt(const std::optional<unsigned>& selected_depth) -> int
{
  return selected_depth.has_value() ? static_cast<int>(*selected_depth) : -1;
}

auto findInsertedInstLevel(const std::vector<HTree::InsertedInstLevel>& levels, const Inst* inst) -> int
{
  if (inst == nullptr) {
    return -1;
  }
  const auto iter = std::ranges::find_if(levels, [inst](const HTree::InsertedInstLevel& level) -> bool { return level.inst == inst; });
  return iter == levels.end() ? -1 : iter->topology_level;
}

auto findInsertedNetLevel(const std::vector<HTree::InsertedNetLevel>& levels, const Net* net) -> std::optional<int>
{
  if (net == nullptr) {
    return std::nullopt;
  }
  const auto iter = std::ranges::find_if(levels, [net](const HTree::InsertedNetLevel& level) -> bool { return level.net == net; });
  if (iter == levels.end()) {
    return std::nullopt;
  }
  return iter->topology_level;
}

auto fallbackNetTopologyLevel(LayoutNetRole role, ClockLayoutPhase synthesis_phase, int selected_depth) -> int
{
  if (role == LayoutNetRole::kSinkTree) {
    return selected_depth >= 0 ? selected_depth : 0;
  }
  if (role == LayoutNetRole::kSourceToRoot && synthesis_phase == ClockLayoutPhase::kSourceToRootHTree) {
    return selected_depth >= 0 ? selected_depth : 0;
  }
  return 0;
}

auto resolveNetTopologyLevel(const std::vector<HTree::InsertedNetLevel>& levels, const Net* net, LayoutNetRole role,
                             ClockLayoutPhase synthesis_phase, int selected_depth) -> int
{
  const auto inserted_level = findInsertedNetLevel(levels, net);
  if (inserted_level.has_value()) {
    return *inserted_level;
  }
  return fallbackNetTopologyLevel(role, synthesis_phase, selected_depth);
}

}  // namespace

auto ClockLayoutAdapter::makeSinkDomainLayoutInput(const Topology::BuildResult& result) -> SinkDomainLayoutInput
{
  SinkDomainLayoutInput input{
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
    input.inserted_insts.push_back(ClockLayoutInstTopology{
        .inst = inst.get(),
        .topology_level = findInsertedInstLevel(result.inserted_inst_levels, inst.get()),
    });
  }
  input.inserted_nets.reserve(result.inserted_nets.size());
  for (const auto& net : result.inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    input.inserted_nets.push_back(ClockLayoutNetTopology{
        .net = net.get(),
        .topology_level = resolveNetTopologyLevel(result.inserted_net_levels, net.get(), LayoutNetRole::kSinkTree,
                                                  ClockLayoutPhase::kDownstreamHTree, input.selected_depth),
    });
  }
  return input;
}

auto ClockLayoutAdapter::makeSourceTrunkLayoutInput(const Topology::SourceTrunkBuildResult& result, ClockLayoutPhase synthesis_phase)
    -> SourceToRootLayoutInput
{
  SourceToRootLayoutInput input{
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
    input.inserted_insts.push_back(ClockLayoutInstTopology{
        .inst = inst.get(),
        .topology_level = findInsertedInstLevel(result.inserted_inst_levels, inst.get()),
    });
  }
  input.inserted_nets.reserve(result.inserted_nets.size());
  for (const auto& net : result.inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    input.inserted_nets.push_back(ClockLayoutNetTopology{
        .net = net.get(),
        .topology_level = resolveNetTopologyLevel(result.inserted_net_levels, net.get(), LayoutNetRole::kSourceToRoot, synthesis_phase,
                                                  input.selected_depth),
    });
  }
  return input;
}

}  // namespace icts
