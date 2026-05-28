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
 * @file STAAdapterRcTree.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief iCTS STA adapter exact RC-tree installation.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "IdbLayout.h"
#include "IdbUnits.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "api/TimingEngine.hh"
#include "config/Config.hh"
#include "design/Net.hh"
#include "idm.h"
#include "netlist/DesignObject.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "routing/ClockRouteSegmentRc.hh"
#include "routing/SteinerTree.hh"
#include "timing_query/STAAdapterTimingQuery.hh"

namespace ista {
class RctNode;
}  // namespace ista

namespace icts {

namespace {

constexpr double kMilliOhmPerOhm = 1000.0;

struct ClockNetRcTreeInstallPolicy
{
  int32_t dbu_per_um = 0;
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
};

auto normalizePinName(std::string name) -> std::string
{
  std::ranges::replace(name, ':', '/');
  return name;
}

auto isQualifiedPinName(const std::string& name) -> bool
{
  return name.find('/') != std::string::npos || name.find(':') != std::string::npos;
}

auto isSameTerminal(const std::string& cts_pin_name, ista::DesignObject* sta_pin_or_port) -> bool
{
  if (sta_pin_or_port == nullptr) {
    return false;
  }

  const auto sta_full_name = sta_pin_or_port->getFullName();
  if (cts_pin_name == sta_full_name || cts_pin_name == normalizePinName(sta_full_name)) {
    return true;
  }

  const auto sta_leaf_name = std::string(sta_pin_or_port->get_name());
  if (isQualifiedPinName(cts_pin_name)) {
    return false;
  }
  if (cts_pin_name == sta_leaf_name) {
    return true;
  }

  return cts_pin_name == normalizePinName(sta_leaf_name);
}

auto findStaTerminal(ista::Net* sta_net, const std::string& cts_pin_name) -> ista::DesignObject*
{
  if (sta_net == nullptr) {
    return nullptr;
  }

  auto* driver = sta_net->getDriver();
  if (isSameTerminal(cts_pin_name, driver)) {
    return driver;
  }
  for (auto* load : sta_net->getLoads()) {
    if (isSameTerminal(cts_pin_name, load)) {
      return load;
    }
  }
  return nullptr;
}

auto resolveDbUnit() -> int32_t
{
  auto* idb_layout = dmInst->get_idb_layout();
  LOG_FATAL_IF(idb_layout == nullptr || idb_layout->get_units() == nullptr)
      << "STAAdapter: iDB units are unavailable for RC-tree installation.";
  const auto dbu_per_um = idb_layout->get_units()->get_micron_dbu();
  LOG_FATAL_IF(dbu_per_um <= 0) << "STAAdapter: iDB DBU-per-micron is invalid for RC-tree installation.";
  return dbu_per_um;
}

auto resolveRoutingLayer(const Config& config) -> int
{
  const auto& routing_layers = config.get_routing_layers();
  LOG_FATAL_IF(routing_layers.empty() || routing_layers.front() == 0U)
      << "STAAdapter: routing layer is not configured for RC-tree installation.";
  return static_cast<int>(routing_layers.front());
}

auto resolveWireWidth(const Config& config) -> std::optional<double>
{
  const auto wire_width = config.get_wire_width();
  return wire_width > 0.0 ? std::optional<double>{wire_width} : std::nullopt;
}

auto getWireDistance(const ClockSteinerTree<int>::EdgeType& edge) -> int
{
  return std::max(edge.distance, edge.routed_distance);
}

auto buildClockNetRcTreeInstallPolicy(const Config& config) -> ClockNetRcTreeInstallPolicy
{
  return ClockNetRcTreeInstallPolicy{
      .dbu_per_um = resolveDbUnit(),
      .routing_layer = resolveRoutingLayer(config),
      .wire_width_um = resolveWireWidth(config),
  };
}

auto queryWireResistanceOhm(STAAdapter& sta_adapter, const ClockNetRcTreeInstallPolicy& policy, int wire_distance_dbu) -> double
{
  const auto wirelength_um = static_cast<double>(std::max(wire_distance_dbu, 0)) / static_cast<double>(policy.dbu_per_um);
  if (wirelength_um <= 0.0) {
    return 0.0;
  }
  return sta_adapter.queryRequiredWireResistance(policy.routing_layer, wirelength_um, policy.wire_width_um) / kMilliOhmPerOhm;
}

auto queryWireCapacitancePf(STAAdapter& sta_adapter, const ClockNetRcTreeInstallPolicy& policy, int wire_distance_dbu) -> double
{
  const auto wirelength_um = static_cast<double>(std::max(wire_distance_dbu, 0)) / static_cast<double>(policy.dbu_per_um);
  if (wirelength_um <= 0.0) {
    return 0.0;
  }
  return sta_adapter.queryRequiredWireCapacitance(policy.routing_layer, wirelength_um, policy.wire_width_um);
}

}  // namespace

auto STAAdapter::queryConfiguredClockRouteSegmentRc(const Config& config) -> ClockRouteSegmentRc
{
  const auto dbu_per_um = resolveDbUnit();
  const auto routing_layer = resolveRoutingLayer(config);
  const auto wire_width = resolveWireWidth(config);
  return ClockRouteSegmentRc{
      .dbu_per_um = dbu_per_um,
      .resistance_per_um_ohm = queryRequiredWireResistance(routing_layer, 1.0, wire_width) / kMilliOhmPerOhm,
      .capacitance_per_um_pf = queryRequiredWireCapacitance(routing_layer, 1.0, wire_width),
  };
}

auto STAAdapter::installClockNetRcTree(const Config& config, const Net& cts_net, const ClockSteinerTree<int>& clock_tree) -> bool
{
  if (clock_tree.node_count() == 0 || clock_tree.edge_count() == 0) {
    return true;
  }
  if (!clock_tree.validate()) {
    LOG_WARNING << "Skip exact RC tree for CTS net \"" << cts_net.get_name() << "\": routing tree is invalid.";
    return false;
  }

  auto* timing_engine = sta_adapter_timing_query::GetStaEngine();
  auto* sta_netlist = timing_engine->get_netlist();
  if (sta_netlist == nullptr) {
    LOG_WARNING << "Skip exact RC tree for CTS net \"" << cts_net.get_name() << "\": STA netlist is null.";
    return false;
  }

  auto* sta_net = sta_netlist->findNet(cts_net.get_name().c_str());
  if (sta_net == nullptr) {
    LOG_WARNING << "Skip exact RC tree for CTS net \"" << cts_net.get_name() << "\": net is not found in STA.";
    return false;
  }

  timing_engine->resetRcTree(sta_net);
  timing_engine->initRcTree(sta_net);
  const auto install_policy = buildClockNetRcTreeInstallPolicy(config);

  std::unordered_map<std::size_t, ista::RctNode*> rc_node_by_route_node;
  rc_node_by_route_node.reserve(clock_tree.node_count());
  for (const auto& route_node : clock_tree.get_nodes()) {
    if (route_node.is_terminal) {
      auto* sta_terminal = findStaTerminal(sta_net, route_node.name);
      if (sta_terminal == nullptr) {
        LOG_WARNING << "Skip exact RC tree for CTS net \"" << cts_net.get_name() << "\": terminal " << route_node.name
                    << " is not found in STA.";
        return false;
      }
      rc_node_by_route_node[route_node.id] = timing_engine->makeOrFindRCTreeNode(sta_terminal);
    } else {
      // route_node.id is the per-net route-tree node id and is used as the STA net-inner RC node id.
      rc_node_by_route_node[route_node.id] = timing_engine->makeOrFindRCTreeNode(sta_net, static_cast<int64_t>(route_node.id));
    }
  }

  for (const auto& route_edge : clock_tree.get_edges()) {
    auto* from_node = rc_node_by_route_node[route_edge.source_node_id];
    auto* to_node = rc_node_by_route_node[route_edge.target_node_id];
    if (from_node == nullptr || to_node == nullptr) {
      LOG_WARNING << "Skip incomplete RC edge for CTS net \"" << cts_net.get_name() << "\".";
      continue;
    }

    const auto wire_distance = getWireDistance(route_edge);
    const auto resistance = queryWireResistanceOhm(*this, install_policy, wire_distance);
    const auto capacitance = queryWireCapacitancePf(*this, install_policy, wire_distance);
    timing_engine->makeResistor(sta_net, from_node, to_node, resistance);
    timing_engine->incrCap(from_node, capacitance / 2.0, true);
    timing_engine->incrCap(to_node, capacitance / 2.0, true);
  }

  timing_engine->updateRCTreeInfo(sta_net);
  return true;
}

}  // namespace icts
