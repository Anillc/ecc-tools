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
 * @file ClockTreeViewBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS clock-tree view construction helper implementation.
 */

#include "clock_tree_view/ClockTreeViewBuilder.hh"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "router/Router.hh"

namespace icts {
namespace {

enum class ClockTreeViewSegmentSource
{
  kRoutedTree,
  kFlylinePins,
  kFallbackPins
};

auto isValidLocation(const Point<int>& point) -> bool
{
  return point.get_x() >= 0 && point.get_y() >= 0;
}

auto makeSegment(const Clock& clock, std::size_t clock_index, const Net& net, const Point<int>& begin, const Point<int>& end,
                 CTSNetRole role, CTSSinkDomain sink_domain, ClockTreeSynthesisPhase synthesis_phase, int selected_depth,
                 int topology_level, bool routed, bool fallback) -> ClockTreeViewSegment
{
  return ClockTreeViewSegment{
      .clock_name = clock.get_clock_name(),
      .net_name = net.get_name(),
      .begin = begin,
      .end = end,
      .net_role = role,
      .sink_domain = sink_domain,
      .synthesis_phase = synthesis_phase,
      .clock_index = clock_index,
      .topology_depth = selected_depth,
      .topology_level = topology_level,
      .routed = routed,
      .fallback = fallback,
  };
}

auto appendPinToPinSegments(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, CTSSinkDomain sink_domain,
                            ClockTreeSynthesisPhase synthesis_phase, ClockTreeViewNet& view_net, ClockTreeViewSegmentSource segment_source)
    -> void
{
  auto* driver = net.get_driver();
  if (driver == nullptr || !isValidLocation(driver->get_location())) {
    return;
  }

  const bool fallback = segment_source == ClockTreeViewSegmentSource::kFallbackPins;
  const auto driver_location = driver->get_location();
  for (auto* load : net.get_loads()) {
    if (load == nullptr || !isValidLocation(load->get_location())) {
      continue;
    }
    auto segment = makeSegment(clock, clock_index, net, driver_location, load->get_location(), role, sink_domain, synthesis_phase,
                               view_net.selected_depth, view_net.topology_level, false, fallback);
    if (segment_source == ClockTreeViewSegmentSource::kFlylinePins) {
      view_net.flyline_segments.push_back(std::move(segment));
    } else {
      view_net.routed_segments.push_back(std::move(segment));
    }
  }
}

auto appendClockTreeSegments(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, CTSSinkDomain sink_domain,
                             ClockTreeSynthesisPhase synthesis_phase, ClockTreeViewNet& view_net, ClockTreeViewSegmentSource segment_source)
    -> void
{
  if (segment_source != ClockTreeViewSegmentSource::kRoutedTree) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, view_net, segment_source);
    return;
  }

  auto route_tree = Router::buildClockNetTree(net);
  if (route_tree.node_count() == 0U || route_tree.edge_count() == 0U) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, view_net,
                           ClockTreeViewSegmentSource::kFallbackPins);
    return;
  }

  bool appended_routed_segment = false;
  for (const auto& edge : route_tree.get_edges()) {
    const auto* source = route_tree.get_node(edge.source_node_id);
    const auto* target = route_tree.get_node(edge.target_node_id);
    if (source == nullptr || target == nullptr || !isValidLocation(source->location) || !isValidLocation(target->location)) {
      continue;
    }
    view_net.routed_segments.push_back(makeSegment(clock, clock_index, net, source->location, target->location, role, sink_domain,
                                                   synthesis_phase, view_net.selected_depth, view_net.topology_level, true, false));
    appended_routed_segment = true;
  }

  if (!appended_routed_segment) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, view_net,
                           ClockTreeViewSegmentSource::kFallbackPins);
  }
}

auto makeViewNet(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, CTSSinkDomain sink_domain,
                 ClockTreeSynthesisPhase synthesis_phase, int selected_depth, int topology_level_count, int topology_level)
    -> ClockTreeViewNet
{
  ClockTreeViewNet view_net{
      .clock_name = clock.get_clock_name(),
      .net_name = net.get_name(),
      .role = role,
      .sink_domain = sink_domain,
      .synthesis_phase = synthesis_phase,
      .clock_index = clock_index,
      .selected_depth = selected_depth,
      .topology_level = topology_level,
      .topology_level_count = topology_level_count,
      .routed_segments = {},
      .flyline_segments = {},
  };
  appendClockTreeSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, view_net, ClockTreeViewSegmentSource::kRoutedTree);
  appendClockTreeSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, view_net, ClockTreeViewSegmentSource::kFlylinePins);
  return view_net;
}

auto makeViewInst(const Clock& clock, std::size_t clock_index, const Inst& inst, CTSInstRole role, CTSSinkDomain sink_domain,
                  ClockTreeSynthesisPhase synthesis_phase, int selected_depth, int topology_level) -> ClockTreeViewInst
{
  return ClockTreeViewInst{
      .clock_name = clock.get_clock_name(),
      .inst_name = inst.get_name(),
      .cell_master = inst.get_cell_master(),
      .origin = inst.get_location(),
      .width_dbu = 0,
      .height_dbu = 0,
      .role = role,
      .sink_domain = sink_domain,
      .synthesis_phase = synthesis_phase,
      .clock_index = clock_index,
      .topology_depth = selected_depth,
      .topology_level = topology_level,
  };
}

auto makeSinkViewInst(const Clock& clock, std::size_t clock_index, const Pin& sink, CTSSinkDomain sink_domain)
    -> std::optional<ClockTreeViewInst>
{
  auto* inst = sink.get_inst();
  if (inst == nullptr) {
    return std::nullopt;
  }

  auto view_inst
      = makeViewInst(clock, clock_index, *inst, CTSInstRole::kClockLoad, sink_domain, ClockTreeSynthesisPhase::kReadData, -1, -1);
  if (!isValidLocation(view_inst.origin) && isValidLocation(sink.get_location())) {
    view_inst.origin = sink.get_location();
  }
  return view_inst;
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

}  // namespace

auto ClockTreeViewBuilder::appendSinkInsts(ClockTreeView& clock_tree_view, const Clock& clock, std::size_t clock_index,
                                           const std::vector<Pin*>& sinks, CTSSinkDomain sink_domain) -> void
{
  std::unordered_set<const Inst*> seen_insts;
  for (const auto* sink : sinks) {
    if (sink == nullptr || sink->get_inst() == nullptr || seen_insts.contains(sink->get_inst())) {
      continue;
    }
    auto sink_view_inst = makeSinkViewInst(clock, clock_index, *sink, sink_domain);
    if (!sink_view_inst.has_value()) {
      continue;
    }
    seen_insts.insert(sink->get_inst());
    clock_tree_view.addInst(*sink_view_inst);
  }
}

auto ClockTreeViewBuilder::appendDirectSinkDomain(ClockTreeView& clock_tree_view, const Clock& clock, std::size_t clock_index,
                                                  const ClockSinkDomainViewTopology& sink_domain_topology) -> void
{
  if (sink_domain_topology.root_buffer != nullptr) {
    clock_tree_view.addInst(makeViewInst(clock, clock_index, *sink_domain_topology.root_buffer, CTSInstRole::kRootBuffer,
                                         sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree, -1, -1));
  }
  if (sink_domain_topology.downstream_net != nullptr) {
    clock_tree_view.addNet(makeViewNet(clock, clock_index, *sink_domain_topology.downstream_net, CTSNetRole::kDownstream,
                                       sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree, -1, 0,
                                       fallbackNetTopologyLevel(CTSNetRole::kDownstream, ClockTreeSynthesisPhase::kDownstreamHTree, -1)));
  }
}

auto ClockTreeViewBuilder::makeSinkDomainView(const Clock& clock, std::size_t clock_index,
                                              const ClockSinkDomainViewTopology& sink_domain_topology,
                                              const ClockSinkDomainViewInput& view_input) -> ClockTreeView
{
  ClockTreeView clock_tree_view;
  if (sink_domain_topology.root_buffer != nullptr) {
    clock_tree_view.addInst(makeViewInst(clock, clock_index, *sink_domain_topology.root_buffer, CTSInstRole::kRootBuffer,
                                         sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree,
                                         view_input.selected_depth, -1));
  }
  if (sink_domain_topology.downstream_net != nullptr) {
    clock_tree_view.addNet(makeViewNet(
        clock, clock_index, *sink_domain_topology.downstream_net, CTSNetRole::kDownstream, sink_domain_topology.sink_domain,
        ClockTreeSynthesisPhase::kDownstreamHTree, view_input.selected_depth, view_input.topology_level_count,
        fallbackNetTopologyLevel(CTSNetRole::kDownstream, ClockTreeSynthesisPhase::kDownstreamHTree, view_input.selected_depth)));
  }
  for (const auto& inst : view_input.inserted_insts) {
    if (inst.inst != nullptr) {
      clock_tree_view.addInst(makeViewInst(clock, clock_index, *inst.inst, CTSInstRole::kHTreeBuffer, sink_domain_topology.sink_domain,
                                           ClockTreeSynthesisPhase::kDownstreamHTree, view_input.selected_depth, inst.topology_level));
    }
  }
  for (const auto& net : view_input.inserted_nets) {
    if (net.net != nullptr) {
      clock_tree_view.addNet(makeViewNet(clock, clock_index, *net.net, CTSNetRole::kSinkTree, sink_domain_topology.sink_domain,
                                         ClockTreeSynthesisPhase::kDownstreamHTree, view_input.selected_depth,
                                         view_input.topology_level_count, net.topology_level));
    }
  }
  return clock_tree_view;
}

auto ClockTreeViewBuilder::makeSourceToRootView(const Clock& clock, std::size_t clock_index, const Net& source_net,
                                                const ClockSourceToRootViewInput& view_input, ClockTreeSynthesisPhase synthesis_phase)
    -> ClockTreeView
{
  ClockTreeView clock_tree_view;
  clock_tree_view.addNet(makeViewNet(clock, clock_index, source_net, CTSNetRole::kSourceToRoot, CTSSinkDomain::kSourceToRoot,
                                     synthesis_phase, view_input.selected_depth, view_input.topology_level_count, 0));
  for (const auto& inst : view_input.inserted_insts) {
    if (inst.inst != nullptr) {
      clock_tree_view.addInst(makeViewInst(clock, clock_index, *inst.inst, CTSInstRole::kSourceRootBuffer, CTSSinkDomain::kSourceToRoot,
                                           synthesis_phase, view_input.selected_depth, inst.topology_level));
    }
  }
  for (const auto& net : view_input.inserted_nets) {
    if (net.net != nullptr) {
      clock_tree_view.addNet(makeViewNet(clock, clock_index, *net.net, CTSNetRole::kSourceToRoot, CTSSinkDomain::kSourceToRoot,
                                         synthesis_phase, view_input.selected_depth, view_input.topology_level_count, net.topology_level));
    }
  }
  return clock_tree_view;
}

auto ClockTreeViewBuilder::merge(ClockTreeView& target, const ClockTreeView& source) -> void
{
  for (const auto& source_clock : source.get_clocks()) {
    auto& target_clock = target.ensureClock(source_clock.clock_name, source_clock.clock_net_name, source_clock.clock_index);
    target_clock.nets.insert(target_clock.nets.end(), source_clock.nets.begin(), source_clock.nets.end());
    target_clock.insts.insert(target_clock.insts.end(), source_clock.insts.begin(), source_clock.insts.end());
  }
}

}  // namespace icts
