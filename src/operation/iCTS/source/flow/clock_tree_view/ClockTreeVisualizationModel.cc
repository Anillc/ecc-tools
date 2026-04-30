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
 * @file ClockTreeVisualizationModel.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Normalized CTS clock-tree visualization model implementation.
 */

#include "clock_tree_view/ClockTreeVisualizationModel.hh"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "router/Router.hh"

namespace icts {
namespace {

auto isValidLocation(const Point<int>& point) -> bool
{
  return point.get_x() >= 0 && point.get_y() >= 0;
}

auto makeSegment(const ClockTreeViewClock& clock, const ClockTreeViewSegment& segment, ClockTreeViewMode view)
    -> ClockTreeVisualizationSegment
{
  return ClockTreeVisualizationSegment{
      .clock_name = clock.clock_name,
      .net_name = segment.net_name,
      .begin = segment.begin,
      .end = segment.end,
      .view = view,
      .net_role = segment.net_role,
      .sink_domain = segment.sink_domain,
      .synthesis_phase = segment.synthesis_phase,
      .clock_index = clock.clock_index,
      .topology_depth = segment.topology_depth,
      .topology_level = segment.topology_level,
      .routed = segment.routed,
      .fallback = segment.fallback,
  };
}

auto appendViewSegments(const ClockTreeView& clock_tree_view, ClockTreeVisualizationModel& model) -> void
{
  for (const auto& clock : clock_tree_view.get_clocks()) {
    for (const auto& net : clock.nets) {
      for (const auto& segment : net.routed_segments) {
        if (isValidLocation(segment.begin) && isValidLocation(segment.end)) {
          model.design_segments.push_back(makeSegment(clock, segment, ClockTreeViewMode::kDesign));
        }
      }
      for (const auto& segment : net.flyline_segments) {
        if (isValidLocation(segment.begin) && isValidLocation(segment.end)) {
          model.flyline_segments.push_back(makeSegment(clock, segment, ClockTreeViewMode::kFlyline));
        }
      }
    }
  }
}

auto appendViewInsts(const ClockTreeView& clock_tree_view, ClockTreeVisualizationModel& model) -> void
{
  for (const auto& clock : clock_tree_view.get_clocks()) {
    for (const auto& inst : clock.insts) {
      model.insts.push_back(ClockTreeVisualizationInst{
          .clock_name = clock.clock_name,
          .inst_name = inst.inst_name,
          .cell_master = inst.cell_master,
          .origin = inst.origin,
          .width_dbu = inst.width_dbu,
          .height_dbu = inst.height_dbu,
          .role = inst.role,
          .sink_domain = inst.sink_domain,
          .synthesis_phase = inst.synthesis_phase,
          .clock_index = clock.clock_index,
          .topology_depth = inst.topology_depth,
          .topology_level = inst.topology_level,
      });
    }
  }
}

auto appendFallbackRouteTreeSegments(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role,
                                     std::vector<ClockTreeVisualizationSegment>& segments) -> bool
{
  auto route_tree = Router::buildClockNetTree(net);
  if (route_tree.node_count() == 0U || route_tree.edge_count() == 0U) {
    return false;
  }

  bool appended = false;
  for (const auto& edge : route_tree.get_edges()) {
    const auto* source = route_tree.get_node(edge.source_node_id);
    const auto* target = route_tree.get_node(edge.target_node_id);
    if (source == nullptr || target == nullptr || !isValidLocation(source->location) || !isValidLocation(target->location)) {
      continue;
    }
    segments.push_back(ClockTreeVisualizationSegment{
        .clock_name = clock.get_clock_name(),
        .net_name = net.get_name(),
        .begin = source->location,
        .end = target->location,
        .view = ClockTreeViewMode::kDesign,
        .net_role = role,
        .sink_domain = role == CTSNetRole::kSourceToRoot ? CTSSinkDomain::kSourceToRoot : CTSSinkDomain::kUnknown,
        .synthesis_phase = ClockTreeSynthesisPhase::kUnknown,
        .clock_index = clock_index,
        .topology_depth = -1,
        .topology_level = 0,
        .routed = true,
        .fallback = false,
    });
    appended = true;
  }
  return appended;
}

auto appendFallbackPinSegments(const Clock& clock, std::size_t clock_index, const Net& net, CTSNetRole role, ClockTreeViewMode view,
                               std::vector<ClockTreeVisualizationSegment>& segments) -> bool
{
  auto* driver = net.get_driver();
  if (driver == nullptr || !isValidLocation(driver->get_location())) {
    return false;
  }

  bool appended = false;
  const auto driver_location = driver->get_location();
  for (auto* load : net.get_loads()) {
    if (load == nullptr || !isValidLocation(load->get_location())) {
      continue;
    }
    segments.push_back(ClockTreeVisualizationSegment{
        .clock_name = clock.get_clock_name(),
        .net_name = net.get_name(),
        .begin = driver_location,
        .end = load->get_location(),
        .view = view,
        .net_role = role,
        .sink_domain = role == CTSNetRole::kSourceToRoot ? CTSSinkDomain::kSourceToRoot : CTSSinkDomain::kUnknown,
        .synthesis_phase = ClockTreeSynthesisPhase::kUnknown,
        .clock_index = clock_index,
        .topology_depth = -1,
        .topology_level = 0,
        .routed = false,
        .fallback = view == ClockTreeViewMode::kDesign,
    });
    appended = true;
  }
  return appended;
}

auto collectClockNets(const Clock& clock) -> std::vector<Net*>
{
  std::vector<Net*> nets;
  std::unordered_set<Net*> seen_nets;
  auto append_net = [&nets, &seen_nets](Net* net) -> void {
    if (net == nullptr || seen_nets.contains(net)) {
      return;
    }
    seen_nets.insert(net);
    nets.push_back(net);
  };

  append_net(clock.get_clock_source_net());
  for (auto* net : clock.get_nets()) {
    append_net(net);
  }
  return nets;
}

auto appendFallbackSegments(ClockTreeVisualizationModel& model) -> void
{
  const auto clocks = DESIGN_INST.get_clocks();
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    const auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    for (const auto* net : collectClockNets(*clock)) {
      if (net == nullptr) {
        continue;
      }
      const auto role = net == clock->get_clock_source_net() ? CTSNetRole::kSourceToRoot : CTSNetRole::kSinkTree;
      if (!appendFallbackRouteTreeSegments(*clock, clock_index, *net, role, model.design_segments)) {
        const bool appended = appendFallbackPinSegments(*clock, clock_index, *net, role, ClockTreeViewMode::kDesign, model.design_segments);
        if (appended) {
          LOG_WARNING << "CTS visualization model: design view falls back to driver-to-load segments for net " << net->get_name();
        }
      }
      (void) appendFallbackPinSegments(*clock, clock_index, *net, role, ClockTreeViewMode::kFlyline, model.flyline_segments);
    }
  }
}

auto appendPinMarkers(ClockTreeVisualizationModel& model) -> void
{
  std::unordered_set<const Pin*> seen_pins;
  for (const auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    for (const auto* net : collectClockNets(*clock)) {
      if (net == nullptr) {
        continue;
      }
      if (const auto* driver = net->get_driver();
          driver != nullptr && isValidLocation(driver->get_location()) && seen_pins.insert(driver).second) {
        model.pins.push_back(ClockTreeVisualizationPin{.location = driver->get_location(), .driver = true});
      }
      for (const auto* load : net->get_loads()) {
        if (load != nullptr && isValidLocation(load->get_location()) && seen_pins.insert(load).second) {
          model.pins.push_back(ClockTreeVisualizationPin{.location = load->get_location(), .driver = false});
        }
      }
    }
  }
}

auto appendFallbackInsts(ClockTreeVisualizationModel& model) -> void
{
  if (!model.insts.empty()) {
    return;
  }

  std::unordered_set<const Inst*> seen_insts;
  const auto clocks = DESIGN_INST.get_clocks();
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    const auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    for (const auto* inst : clock->get_insts()) {
      if (inst == nullptr || !seen_insts.insert(inst).second) {
        continue;
      }
      model.insts.push_back(ClockTreeVisualizationInst{
          .clock_name = clock->get_clock_name(),
          .inst_name = inst->get_name(),
          .cell_master = inst->get_cell_master(),
          .origin = inst->get_location(),
          .width_dbu = 0,
          .height_dbu = 0,
          .role = CTSInstRole::kHTreeBuffer,
          .sink_domain = CTSSinkDomain::kUnknown,
          .synthesis_phase = ClockTreeSynthesisPhase::kUnknown,
          .clock_index = clock_index,
          .topology_depth = -1,
          .topology_level = -1,
      });
    }
  }
}

auto ctsInstNames(const ClockTreeVisualizationModel& model) -> std::unordered_set<std::string>
{
  std::unordered_set<std::string> names;
  for (const auto& inst : model.insts) {
    if (!inst.inst_name.empty()) {
      names.insert(inst.inst_name);
    }
  }
  return names;
}

auto appendLogicCells(ClockTreeVisualizationModel& model) -> void
{
  const auto cts_names = ctsInstNames(model);
  for (const auto& geometry : WRAPPER_INST.collectLogicCellGeometries()) {
    if (cts_names.contains(geometry.name) || !isValidLocation(geometry.origin)) {
      continue;
    }
    model.logic_cells.push_back(ClockTreeVisualizationLogicCell{
        .inst_name = geometry.name,
        .origin = geometry.origin,
        .width_dbu = geometry.width_dbu,
        .height_dbu = geometry.height_dbu,
    });
  }
}

auto fillInstGeometry(ClockTreeVisualizationModel& model) -> void
{
  const int32_t dbu_per_um = std::max(model.dbu_per_um, int32_t{1});
  for (auto& inst : model.insts) {
    const auto geometry = WRAPPER_INST.queryInstGeometry(inst.inst_name);
    if (geometry.has_value()) {
      if (isValidLocation(geometry->origin)) {
        inst.origin = geometry->origin;
      }
      inst.width_dbu = geometry->width_dbu > 0 ? geometry->width_dbu : inst.width_dbu;
      inst.height_dbu = geometry->height_dbu > 0 ? geometry->height_dbu : inst.height_dbu;
    }
    if (inst.width_dbu <= 0) {
      inst.width_dbu = std::max(dbu_per_um / 5, int32_t{1});
    }
    if (inst.height_dbu <= 0) {
      inst.height_dbu = std::max(dbu_per_um / 5, int32_t{1});
    }
  }
}

}  // namespace

auto ClockTreeVisualizationModelBuilder::build(const ClockTreeView& clock_tree_view) -> ClockTreeVisualizationModel
{
  ClockTreeVisualizationModel model;
  model.has_clocks = !clock_tree_view.get_clocks().empty() || !DESIGN_INST.get_clocks().empty();
  model.dbu_per_um = std::max(clock_tree_view.get_design_dbu_per_um(), int32_t{1});
  appendViewSegments(clock_tree_view, model);
  appendViewInsts(clock_tree_view, model);
  if (model.design_segments.empty() || model.flyline_segments.empty()) {
    appendFallbackSegments(model);
  }
  appendFallbackInsts(model);
  fillInstGeometry(model);
  appendLogicCells(model);
  appendPinMarkers(model);
  return model;
}

}  // namespace icts
