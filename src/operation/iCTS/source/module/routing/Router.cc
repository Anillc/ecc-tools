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
 * @file Router.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Unified routing dispatch facade implementation
 */

#include "Router.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Inst.hh"
#include "Log.hh"
#include "Pin.hh"
#include "PinLocationHelper.hh"
#include "Point.hh"
#include "adapter/sta/STAAdapter.hh"
#include "bound_skew_tree/BSTRouter.hh"
#include "concurrent_bst_salt/CBSRouter.hh"
#include "flute/FLUTERouter.hh"
#include "io/Wrapper.hh"
#include "local_legalization/LocalLegalization.hh"
#include "salt/SALTRouter.hh"

namespace icts {
namespace {

constexpr double kMilliOhmPerOhm = 1000.0;

auto ResolveRoutingLayer(const Router::RCTreeBuildOptions& options) -> int
{
  if (options.routing_layer.has_value() && options.routing_layer.value() > 0) {
    return options.routing_layer.value();
  }

  return 1;
}

auto ResolveWireWidth(const Router::RCTreeBuildOptions& options) -> std::optional<double>
{
  if (options.wire_width.has_value() && options.wire_width.value() > 0.0) {
    return options.wire_width;
  }

  return std::nullopt;
}

auto QueryArcParasitics(int wire_distance_dbu, const Router::RCTreeBuildOptions& options) -> std::pair<double, double>
{
  const auto db_unit = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  const auto wire_length = static_cast<double>(std::max(wire_distance_dbu, 0)) / db_unit;
  if (wire_length <= 0.0) {
    return {0.0, 0.0};
  }

  const auto routing_layer = ResolveRoutingLayer(options);
  const auto wire_width = ResolveWireWidth(options);
  const auto resistance = STA_ADAPTER_INST.queryWireResistance(routing_layer, wire_length, wire_width) / kMilliOhmPerOhm;
  const auto capacitance = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, wire_length, wire_width);
  return {resistance, capacitance};
}

auto ResolveVertexName(const Router::ClockSteinerTreeType::NodeType& node, const Router::RCTreeType& rc_tree) -> std::string
{
  if (!node.name.empty() && !rc_tree.hasVertex(node.name)) {
    return node.name;
  }

  const auto base_name = node.is_terminal ? std::string("terminal_") : std::string("steiner_");
  auto name = base_name + std::to_string(node.id);
  std::size_t suffix = 0;
  while (rc_tree.hasVertex(name)) {
    ++suffix;
    name = base_name + std::to_string(node.id) + "_" + std::to_string(suffix);
  }
  return name;
}

auto GetWireDistance(const Router::ClockSteinerTreeType::EdgeType& edge) -> int
{
  return std::max(edge.distance, edge.routed_distance);
}

auto WriteBackPinLocations(const std::vector<Pin*>& pins, const std::vector<Point<int>>& points) -> void
{
  const auto count = std::min(pins.size(), points.size());
  for (std::size_t i = 0; i < count; ++i) {
    auto* pin = pins.at(i);
    if (pin == nullptr) {
      continue;
    }
    pin->set_location(points.at(i));
    auto* inst = pin->get_inst();
    if (inst != nullptr) {
      inst->set_location(points.at(i));
    }
  }
}

auto BuildClockRCTree(const Router::ClockSteinerTreeType& tree, const Router::RCTreeBuildOptions& options) -> Router::RCTreeType
{
  Router::RCTreeType rc_tree;
  if (tree.node_count() == 0) {
    return rc_tree;
  }

  LOG_FATAL_IF(!tree.validate()) << "Routing tree is invalid before RCTree conversion.";

  rc_tree.reserveVertices(tree.node_count());
  rc_tree.reserveArcs(tree.edge_count());

  std::vector<std::size_t> node_to_vertex_id(tree.node_count(), Router::RCTreeType::kInvalidId);
  for (const auto& node : tree.get_nodes()) {
    auto vertex_name = ResolveVertexName(node, rc_tree);
    auto vertex_id = rc_tree.addVertex(vertex_name, node.is_terminal, node.pin_cap);
    LOG_FATAL_IF(vertex_id == Router::RCTreeType::kInvalidId) << "Failed to add RCTree vertex for routing node: " << vertex_name;
    node_to_vertex_id.at(node.id) = vertex_id;
  }

  rc_tree.setRoot(node_to_vertex_id.at(tree.get_root()));

  for (const auto& edge : tree.get_edges()) {
    auto source_vertex_id = node_to_vertex_id.at(edge.source_node_id);
    auto sink_vertex_id = node_to_vertex_id.at(edge.target_node_id);
    LOG_FATAL_IF(source_vertex_id == Router::RCTreeType::kInvalidId || sink_vertex_id == Router::RCTreeType::kInvalidId)
        << "Routing edge endpoint is missing during RCTree conversion.";

    const auto [arc_resistance, arc_capacitance] = QueryArcParasitics(GetWireDistance(edge), options);
    auto arc_id = rc_tree.addArc(source_vertex_id, sink_vertex_id, arc_resistance, arc_capacitance);
    LOG_FATAL_IF(arc_id == Router::RCTreeType::kInvalidId) << "Failed to add RCTree arc when converting routing tree edge " << edge.id;
  }

  LOG_FATAL_IF(!rc_tree.validate()) << "Constructed RCTree is invalid after routing conversion.";
  return rc_tree;
}

}  // namespace

auto Router::buildFluteTree(const ClockTerminal& driver_terminal, const std::vector<ClockTerminal>& load_terminals)
    -> Router::ClockSteinerTreeType
{
  return FLUTERouter::buildTree(driver_terminal, load_terminals);
}

auto Router::buildSaltTree(const ClockTerminal& driver_terminal, const std::vector<ClockTerminal>& load_terminals)
    -> Router::ClockSteinerTreeType
{
  return SALTRouter::buildTree(driver_terminal, load_terminals);
}

auto Router::buildBstTree(const std::vector<ClockTerminal>& load_terminals, const BSTParameters& parameters) -> Router::ClockSteinerTreeType
{
  return BSTRouter::buildTree(load_terminals, parameters);
}

auto Router::buildCbsTree(const std::vector<ClockTerminal>& load_terminals, const BSTParameters& parameters) -> Router::ClockSteinerTreeType
{
  return CBSRouter::buildTree(load_terminals, parameters);
}

auto Router::legalizePins(std::vector<Pin*>& movable_pins, const std::vector<Pin*>& fixed_pins, const LegalizationRegion& feasible_region,
                          const LegalizationRegion& block_region) -> Router::LegalizationResult
{
  return legalizePins(movable_pins, fixed_pins, feasible_region, block_region, LegalizationOptions{});
}

auto Router::legalizePins(std::vector<Pin*>& movable_pins, const std::vector<Pin*>& fixed_pins, const LegalizationRegion& feasible_region,
                          const LegalizationRegion& block_region, const LegalizationOptions& options) -> Router::LegalizationResult
{
  auto movable_points = CollectPinLocations(movable_pins);
  const auto fixed_points = CollectPinLocations(fixed_pins);
  auto result = LocalLegalization::legalize(movable_points, fixed_points, feasible_region, block_region, options);
  if (!result.success) {
    LOG_WARNING << "Router::legalizePins did not produce a successful legalization result.";
  }
  if (result.success || options.failure_policy == LocalLegalization::FailurePolicy::kKeepOriginal) {
    WriteBackPinLocations(movable_pins, result.legalized_points);
  }
  return result;
}

auto Router::buildRCTree(const ClockSteinerTreeType& clock_tree) -> Router::RCTreeType
{
  return buildRCTree(clock_tree, RCTreeBuildOptions{});
}

auto Router::buildRCTree(const ClockSteinerTreeType& clock_tree, const RCTreeBuildOptions& options) -> Router::RCTreeType
{
  return BuildClockRCTree(clock_tree, options);
}

}  // namespace icts
