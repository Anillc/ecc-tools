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
 * @file ClockTreeReportDataBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS clock-tree report-data construction helper implementation.
 */

#include "report_data/ClockTreeReportDataBuilder.hh"

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

enum class ClockTreeReportSegmentSource
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
                 int topology_level, bool routed, bool fallback) -> ClockTreeReportSegment
{
  return ClockTreeReportSegment{
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
                            ClockTreeSynthesisPhase synthesis_phase, ClockTreeReportNet& report_net,
                            ClockTreeReportSegmentSource segment_source) -> void
{
  auto* driver = net.get_driver();
  if (driver == nullptr || !isValidLocation(driver->get_location())) {
    return;
  }

  const bool fallback = segment_source == ClockTreeReportSegmentSource::kFallbackPins;
  const auto driver_location = driver->get_location();
  for (auto* load : net.get_loads()) {
    if (load == nullptr || !isValidLocation(load->get_location())) {
      continue;
    }
    auto segment = makeSegment(clock, clock_index, net, driver_location, load->get_location(), role, sink_domain, synthesis_phase,
                               report_net.selected_depth, report_net.topology_level, false, fallback);
    if (segment_source == ClockTreeReportSegmentSource::kFlylinePins) {
      report_net.flyline_segments.push_back(std::move(segment));
    } else {
      report_net.routed_segments.push_back(std::move(segment));
    }
  }
}

auto appendClockTreeSegments(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, CTSSinkDomain sink_domain,
                             ClockTreeSynthesisPhase synthesis_phase, ClockTreeReportNet& report_net,
                             ClockTreeReportSegmentSource segment_source) -> void
{
  if (segment_source != ClockTreeReportSegmentSource::kRoutedTree) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, report_net, segment_source);
    return;
  }

  auto route_tree = Router::buildClockNetTree(net);
  if (route_tree.node_count() == 0U || route_tree.edge_count() == 0U) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, report_net,
                           ClockTreeReportSegmentSource::kFallbackPins);
    return;
  }

  bool appended_routed_segment = false;
  for (const auto& edge : route_tree.get_edges()) {
    const auto* source = route_tree.get_node(edge.source_node_id);
    const auto* target = route_tree.get_node(edge.target_node_id);
    if (source == nullptr || target == nullptr || !isValidLocation(source->location) || !isValidLocation(target->location)) {
      continue;
    }
    report_net.routed_segments.push_back(makeSegment(clock, clock_index, net, source->location, target->location, role, sink_domain,
                                                     synthesis_phase, report_net.selected_depth, report_net.topology_level, true, false));
    appended_routed_segment = true;
  }

  if (!appended_routed_segment) {
    appendPinToPinSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, report_net,
                           ClockTreeReportSegmentSource::kFallbackPins);
  }
}

auto makeReportNet(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, CTSSinkDomain sink_domain,
                   ClockTreeSynthesisPhase synthesis_phase, int selected_depth, int topology_level_count, int topology_level)
    -> ClockTreeReportNet
{
  ClockTreeReportNet report_net{
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
  appendClockTreeSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, report_net,
                          ClockTreeReportSegmentSource::kRoutedTree);
  appendClockTreeSegments(clock, clock_index, net, role, sink_domain, synthesis_phase, report_net,
                          ClockTreeReportSegmentSource::kFlylinePins);
  return report_net;
}

auto makeReportInst(const Clock& clock, std::size_t clock_index, const Inst& inst, CTSInstRole role, CTSSinkDomain sink_domain,
                    ClockTreeSynthesisPhase synthesis_phase, int selected_depth, int topology_level) -> ClockTreeReportInst
{
  return ClockTreeReportInst{
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

auto makeSinkReportInst(const Clock& clock, std::size_t clock_index, const Pin& sink, CTSSinkDomain sink_domain)
    -> std::optional<ClockTreeReportInst>
{
  auto* inst = sink.get_inst();
  if (inst == nullptr) {
    return std::nullopt;
  }

  auto report_inst
      = makeReportInst(clock, clock_index, *inst, CTSInstRole::kClockLoad, sink_domain, ClockTreeSynthesisPhase::kReadData, -1, -1);
  if (!isValidLocation(report_inst.origin) && isValidLocation(sink.get_location())) {
    report_inst.origin = sink.get_location();
  }
  return report_inst;
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

auto ClockTreeReportDataBuilder::appendSinkInsts(ClockTreeReportData& report_data, const Clock& clock, std::size_t clock_index,
                                                 const std::vector<Pin*>& sinks, CTSSinkDomain sink_domain) -> void
{
  std::unordered_set<const Inst*> seen_insts;
  for (const auto* sink : sinks) {
    if (sink == nullptr || sink->get_inst() == nullptr || seen_insts.contains(sink->get_inst())) {
      continue;
    }
    auto sink_report_inst = makeSinkReportInst(clock, clock_index, *sink, sink_domain);
    if (!sink_report_inst.has_value()) {
      continue;
    }
    seen_insts.insert(sink->get_inst());
    report_data.addInst(*sink_report_inst);
  }
}

auto ClockTreeReportDataBuilder::appendDirectSinkDomain(ClockTreeReportData& report_data, const Clock& clock, std::size_t clock_index,
                                                        const ClockSinkDomainReportTopology& sink_domain_topology) -> void
{
  if (sink_domain_topology.root_buffer != nullptr) {
    report_data.addInst(makeReportInst(clock, clock_index, *sink_domain_topology.root_buffer, CTSInstRole::kRootBuffer,
                                       sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree, -1, -1));
  }
  if (sink_domain_topology.downstream_net != nullptr) {
    report_data.addNet(makeReportNet(clock, clock_index, *sink_domain_topology.downstream_net, CTSNetRole::kDownstream,
                                     sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree, -1, 0,
                                     fallbackNetTopologyLevel(CTSNetRole::kDownstream, ClockTreeSynthesisPhase::kDownstreamHTree, -1)));
  }
}

auto ClockTreeReportDataBuilder::makeSinkDomainReportData(const Clock& clock, std::size_t clock_index,
                                                          const ClockSinkDomainReportTopology& sink_domain_topology,
                                                          const ClockSinkDomainReportInput& report_input) -> ClockTreeReportData
{
  ClockTreeReportData report_data;
  if (sink_domain_topology.root_buffer != nullptr) {
    report_data.addInst(makeReportInst(clock, clock_index, *sink_domain_topology.root_buffer, CTSInstRole::kRootBuffer,
                                       sink_domain_topology.sink_domain, ClockTreeSynthesisPhase::kDownstreamHTree,
                                       report_input.selected_depth, -1));
  }
  if (sink_domain_topology.downstream_net != nullptr) {
    report_data.addNet(makeReportNet(
        clock, clock_index, *sink_domain_topology.downstream_net, CTSNetRole::kDownstream, sink_domain_topology.sink_domain,
        ClockTreeSynthesisPhase::kDownstreamHTree, report_input.selected_depth, report_input.topology_level_count,
        fallbackNetTopologyLevel(CTSNetRole::kDownstream, ClockTreeSynthesisPhase::kDownstreamHTree, report_input.selected_depth)));
  }
  for (const auto& inst : report_input.inserted_insts) {
    if (inst.inst != nullptr) {
      report_data.addInst(makeReportInst(clock, clock_index, *inst.inst, CTSInstRole::kHTreeBuffer, sink_domain_topology.sink_domain,
                                         ClockTreeSynthesisPhase::kDownstreamHTree, report_input.selected_depth, inst.topology_level));
    }
  }
  for (const auto& net : report_input.inserted_nets) {
    if (net.net != nullptr) {
      report_data.addNet(makeReportNet(clock, clock_index, *net.net, CTSNetRole::kSinkTree, sink_domain_topology.sink_domain,
                                       ClockTreeSynthesisPhase::kDownstreamHTree, report_input.selected_depth,
                                       report_input.topology_level_count, net.topology_level));
    }
  }
  return report_data;
}

auto ClockTreeReportDataBuilder::makeSourceToRootReportData(const Clock& clock, std::size_t clock_index, const Net& source_net,
                                                            const ClockSourceToRootReportInput& report_input,
                                                            ClockTreeSynthesisPhase synthesis_phase) -> ClockTreeReportData
{
  ClockTreeReportData report_data;
  report_data.addNet(makeReportNet(clock, clock_index, source_net, CTSNetRole::kSourceToRoot, CTSSinkDomain::kSourceToRoot, synthesis_phase,
                                   report_input.selected_depth, report_input.topology_level_count, 0));
  for (const auto& inst : report_input.inserted_insts) {
    if (inst.inst != nullptr) {
      report_data.addInst(makeReportInst(clock, clock_index, *inst.inst, CTSInstRole::kSourceRootBuffer, CTSSinkDomain::kSourceToRoot,
                                         synthesis_phase, report_input.selected_depth, inst.topology_level));
    }
  }
  for (const auto& net : report_input.inserted_nets) {
    if (net.net != nullptr) {
      report_data.addNet(makeReportNet(clock, clock_index, *net.net, CTSNetRole::kSourceToRoot, CTSSinkDomain::kSourceToRoot,
                                       synthesis_phase, report_input.selected_depth, report_input.topology_level_count,
                                       net.topology_level));
    }
  }
  return report_data;
}

auto ClockTreeReportDataBuilder::merge(ClockTreeReportData& target, const ClockTreeReportData& source) -> void
{
  for (const auto& source_clock : source.get_clocks()) {
    auto& target_clock = target.ensureClock(source_clock.clock_name, source_clock.clock_net_name, source_clock.clock_index);
    target_clock.nets.insert(target_clock.nets.end(), source_clock.nets.begin(), source_clock.nets.end());
    target_clock.insts.insert(target_clock.insts.end(), source_clock.insts.begin(), source_clock.insts.end());
  }
}

}  // namespace icts
