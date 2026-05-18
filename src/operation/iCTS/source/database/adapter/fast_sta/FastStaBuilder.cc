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
 * @file FastStaBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Initialization bridge from committed CTS state to fast STA context.
 */

#include "FastStaBuilder.hh"

#include <glog/logging.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastStaClockTree.hh"
#include "FastStaLiberty.hh"
#include "FastStaParasitics.hh"
#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "io/Wrapper.hh"

namespace icts {
namespace {

auto elapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

auto logBuilderStage(const Clock& clock, std::string_view stage_name, double runtime_s, const FastStaClockContext& context) -> void
{
  LOG_INFO << "FastStaBuilder: " << stage_name << " for clock \"" << clock.get_clock_name() << "\" finished in " << runtime_s
           << " s, nodes=" << context.nodes.size() << ", nets=" << context.nets.size()
           << ", liberty_cells=" << context.liberty_cell_by_master.size() << ".";
}

auto resolveRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (!routing_layers.empty() && routing_layers.front() > 0U) {
    return static_cast<int>(routing_layers.front());
  }
  LOG_FATAL << "FastStaBuilder: routing layer is not configured.";
  return 0;
}

auto resolveWireWidth() -> std::optional<double>
{
  return CONFIG_INST.get_wire_width() > 0.0 ? std::optional<double>{CONFIG_INST.get_wire_width()} : std::nullopt;
}

auto applyRuntimeOptions(FastStaClockContext& context) -> void
{
  const auto dbu_per_um = WRAPPER_INST.queryDbUnit();
  LOG_FATAL_IF(dbu_per_um <= 0) << "FastStaBuilder: DBU-per-micron is unavailable.";
  context.dbu_per_um = dbu_per_um;
  context.routing_layer = resolveRoutingLayer();
  context.wire_width_um = resolveWireWidth();
}

auto snapshotClockData(FastStaClockContext& context) -> void
{
  for (auto& node : context.nodes) {
    if (node.cell_master.empty()) {
      continue;
    }
    if (!context.liberty_cell_by_master.contains(node.cell_master)) {
      context.liberty_cell_by_master[node.cell_master] = FastStaLiberty::snapshotBufferCell(node.cell_master);
    }
    const auto& liberty_cell = context.liberty_cell_by_master.at(node.cell_master);
    if (node.kind == FastStaNodeKind::kBufferInput) {
      node.input_cap_pf = liberty_cell.input_cap_pf;
      node.max_slew_ns = liberty_cell.input_slew_limit_ns;
    }
  }
  for (auto& net : context.nets) {
    if (CONFIG_INST.has_max_cap() && CONFIG_INST.get_max_cap() > 0.0) {
      net.max_cap_pf = CONFIG_INST.get_max_cap();
    } else if (net.driver_node_id != kInvalidFastStaNodeId) {
      const auto& driver = context.nodes.at(net.driver_node_id);
      if (const auto iter = context.liberty_cell_by_master.find(driver.cell_master); iter != context.liberty_cell_by_master.end()) {
        net.max_cap_pf = iter->second.output_cap_limit_pf;
      }
    }
  }
  FastStaParasitics::updateNetLoads(context);
}

auto snapshotSinkPinCaps(const Clock& clock, FastStaClockContext& context) -> void
{
  for (auto* pin : clock.get_loads()) {
    if (pin == nullptr) {
      continue;
    }
    const auto node_iter = context.node_id_by_name.find(Design::getPinFullName(pin));
    if (node_iter == context.node_id_by_name.end() || node_iter->second >= context.nodes.size()) {
      continue;
    }
    context.nodes.at(node_iter->second).input_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(pin);
  }
}

}  // namespace

auto FastStaBuilder::buildClockContext(const Clock& clock) -> FastStaClockContext
{
  const auto total_start = std::chrono::steady_clock::now();
  auto stage_start = std::chrono::steady_clock::now();
  auto context = FastStaClockTree::buildFromClock(clock);
  logBuilderStage(clock, "build committed clock-tree graph", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  applyRuntimeOptions(context);
  logBuilderStage(clock, "apply runtime options", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  snapshotSinkPinCaps(clock, context);
  logBuilderStage(clock, "snapshot sink pin caps", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  snapshotClockData(context);
  logBuilderStage(clock, "snapshot liberty and clock data", elapsedSeconds(stage_start), context);
  LOG_INFO << "FastStaBuilder: build clock context for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(total_start) << " s.";
  return context;
}

auto FastStaBuilder::buildClockContext(const Clock& clock, const ClockLayout& clock_layout, std::size_t clock_index) -> FastStaClockContext
{
  const auto total_start = std::chrono::steady_clock::now();
  auto stage_start = std::chrono::steady_clock::now();
  auto context = FastStaClockTree::buildFromClockLayout(clock, clock_layout, clock_index);
  logBuilderStage(clock, "build layout clock-tree graph", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  applyRuntimeOptions(context);
  logBuilderStage(clock, "apply runtime options", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  FastStaClockTree::applyLayoutParasitics(context, clock_layout, clock_index);
  logBuilderStage(clock, "apply layout parasitics", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  snapshotSinkPinCaps(clock, context);
  logBuilderStage(clock, "snapshot sink pin caps", elapsedSeconds(stage_start), context);

  stage_start = std::chrono::steady_clock::now();
  snapshotClockData(context);
  logBuilderStage(clock, "snapshot liberty and clock data", elapsedSeconds(stage_start), context);
  LOG_INFO << "FastStaBuilder: build layout clock context for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(total_start) << " s.";
  return context;
}

auto FastStaBuilder::injectNetRouteTree(FastStaClockContext& context, const Net& net, const ClockSteinerTree<int>& route_tree) -> bool
{
  const auto net_iter = context.net_id_by_name.find(net.get_name());
  if (net_iter == context.net_id_by_name.end() || net_iter->second >= context.nets.size()) {
    return false;
  }
  return FastStaParasitics::buildNetParasiticFromRouteTree(context, net_iter->second, net, route_tree);
}

}  // namespace icts
