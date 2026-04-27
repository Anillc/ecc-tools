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
 * @file ClockTreeEvaluator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree writeback and evaluation stage implementation.
 */

#include "evaluation/ClockTreeEvaluator.hh"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Point.hh"
#include "RoutingTerminal.hh"
#include "SteinerTree.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "routing/Router.hh"
#include "timing/TimingEngine.hh"

namespace icts {
namespace {

auto latestSummary() -> ClockTreeSummary&
{
  static ClockTreeSummary summary;
  return summary;
}

auto clearSummary(ClockTreeSummary& summary) -> void
{
  summary.buffer_num = 0;
  summary.buffer_area = 0.0;
  summary.clock_path_min_buffer = 0;
  summary.clock_path_max_buffer = 0;
  summary.max_level_of_clock_tree = 0;
  summary.max_clock_wirelength = 0;
  summary.total_clock_wirelength = 0.0;
  summary.clocks_timing.clear();
}

auto collectBufferMembershipMetrics(Clock& clock, int& min_buffer_count, int& max_buffer_count, int& max_level) -> bool
{
  int buffer_count = 0;
  for (auto* inst : clock.get_insts()) {
    if (inst != nullptr && inst->is_buffer()) {
      ++buffer_count;
    }
  }

  if (buffer_count == 0) {
    return false;
  }

  min_buffer_count = std::min(min_buffer_count, buffer_count);
  max_buffer_count = std::max(max_buffer_count, buffer_count);
  max_level = std::max(max_level, buffer_count);
  return true;
}

auto makeTerminal(Pin* pin) -> Router::ClockTerminal
{
  Router::ClockTerminal terminal;
  if (pin == nullptr) {
    return terminal;
  }
  auto* inst = pin->get_inst();
  terminal.name = inst != nullptr ? (inst->get_name() + "/" + pin->get_name()) : pin->get_name();
  terminal.location = pin->get_location();
  terminal.pin_cap = 0.0;
  terminal.insertion_delay = 0.0;
  return terminal;
}

auto buildRouteTree(Net* net) -> Router::ClockSteinerTreeType
{
  if (net == nullptr || net->get_driver() == nullptr || net->get_loads().empty()) {
    return {};
  }

  std::vector<Router::ClockTerminal> load_terminals;
  load_terminals.reserve(net->get_loads().size());
  for (auto* load : net->get_loads()) {
    if (load == nullptr) {
      continue;
    }
    load_terminals.push_back(makeTerminal(load));
  }
  if (load_terminals.empty()) {
    return {};
  }
  if (load_terminals.size() == 1U) {
    Router::ClockSteinerTreeType route_tree;
    const auto driver_terminal = makeTerminal(net->get_driver());
    const auto root_id = route_tree.addNode(driver_terminal.name, driver_terminal.location, true, driver_terminal.pin_cap,
                                            driver_terminal.insertion_delay);
    if (root_id == Router::ClockSteinerTreeType::kInvalidId) {
      return {};
    }
    route_tree.setRoot(root_id);
    const auto& load_terminal = load_terminals.front();
    const auto load_id
        = route_tree.addNode(load_terminal.name, load_terminal.location, true, load_terminal.pin_cap, load_terminal.insertion_delay);
    if (load_id == Router::ClockSteinerTreeType::kInvalidId) {
      return {};
    }
    const auto distance = geometry::Manhattan(driver_terminal.location, load_terminal.location);
    const auto edge_id = route_tree.addEdge(root_id, load_id, distance, distance);
    if (edge_id == Router::ClockSteinerTreeType::kInvalidId || !route_tree.validate()) {
      return {};
    }
    return route_tree;
  }
  return Router::buildFluteTree(makeTerminal(net->get_driver()), load_terminals);
}

auto calcRouteWirelength(const Router::ClockSteinerTreeType& route_tree) -> int64_t
{
  int64_t wirelength = 0;
  for (const auto& edge : route_tree.get_edges()) {
    wirelength += std::max(edge.distance, edge.routed_distance);
  }
  return wirelength;
}

auto buildRcOptionsFromRuntimeConfig() -> Router::RCTreeBuildOptions
{
  Router::RCTreeBuildOptions options;
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (!routing_layers.empty()) {
    options.routing_layer = static_cast<int>(routing_layers.front());
  }
  if (CONFIG_INST.get_wire_width() > 0.0) {
    options.wire_width = CONFIG_INST.get_wire_width();
  }
  return options;
}

auto evaluateClockNet(Net* net, bool install_sta_rc_tree, ClockTreeSummary& summary) -> void
{
  auto route_tree = buildRouteTree(net);
  if (route_tree.node_count() == 0 || route_tree.edge_count() == 0) {
    return;
  }

  const auto wirelength = calcRouteWirelength(route_tree);
  summary.total_clock_wirelength += static_cast<double>(wirelength);
  summary.max_clock_wirelength = std::max(summary.max_clock_wirelength, static_cast<int32_t>(wirelength));

  if (WRAPPER_INST.is_design_ready()) {
    auto rc_tree = Router::buildRCTree(route_tree, buildRcOptionsFromRuntimeConfig());
    auto timing_metrics = TimingEngine::update(rc_tree);
    (void) timing_metrics;
  }

  if (install_sta_rc_tree && net != nullptr) {
    (void) STA_ADAPTER_INST.installClockNetRcTree(*net, route_tree);
  }
}

auto appendClockTiming(const Clock& clock, bool query_sta_timing, ClockTreeSummary& summary) -> void
{
  if (query_sta_timing) {
    auto timing = STA_ADAPTER_INST.queryClockTiming(clock.get_clock_name());
    if (timing.has_value()) {
      summary.clocks_timing.push_back(ClockTreeSummary::ClockTiming{
          .clock_name = clock.get_clock_name(),
          .setup_tns = timing->setup_tns,
          .setup_wns = timing->setup_wns,
          .hold_tns = timing->hold_tns,
          .hold_wns = timing->hold_wns,
          .suggest_freq = timing->suggest_freq,
      });
      return;
    }
  }

  summary.clocks_timing.push_back(ClockTreeSummary::ClockTiming{
      .clock_name = clock.get_clock_name(),
      .setup_tns = 0.0,
      .setup_wns = 0.0,
      .hold_tns = 0.0,
      .hold_wns = 0.0,
      .suggest_freq = 0.0,
  });
}

auto emitEvaluationSummary(const ClockTreeSummary& summary, bool wrote_idb, bool refreshed_sta) -> void
{
  schema::EmitKeyValueTable("CTS Evaluation Summary", {
                                                          {"idb_writeback", wrote_idb ? "true" : "false"},
                                                          {"sta_refresh", refreshed_sta ? "true" : "false"},
                                                          {"buffer_num", std::to_string(summary.buffer_num)},
                                                          {"buffer_area", std::to_string(summary.buffer_area)},
                                                          {"clock_path_min_buffer", std::to_string(summary.clock_path_min_buffer)},
                                                          {"clock_path_max_buffer", std::to_string(summary.clock_path_max_buffer)},
                                                          {"max_level_of_clock_tree", std::to_string(summary.max_level_of_clock_tree)},
                                                          {"max_clock_wirelength", std::to_string(summary.max_clock_wirelength)},
                                                          {"total_clock_wirelength", std::to_string(summary.total_clock_wirelength)},
                                                      });
}

}  // namespace

auto ClockTreeEvaluator::evaluate() -> void
{
  auto& summary = latestSummary();
  clearSummary(summary);

  auto clocks = DESIGN_INST.get_clocks();
  const bool wrote_idb = WRAPPER_INST.writeClocks(clocks);
  const bool should_refresh_sta = WRAPPER_INST.is_design_ready() && wrote_idb;
  if (should_refresh_sta) {
    STA_ADAPTER_INST.refreshFullDesignTimingContext();
  }

  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }

    for (auto* inst : clock->get_insts()) {
      if (inst == nullptr || !inst->is_buffer()) {
        continue;
      }
      ++summary.buffer_num;
      if (WRAPPER_INST.is_layout_ready()) {
        summary.buffer_area += STA_ADAPTER_INST.queryCellAreaUm2(inst->get_cell_master());
      }
    }

    int min_buffer_count = std::numeric_limits<int>::max();
    int max_buffer_count = 0;
    int max_level = 0;
    if (collectBufferMembershipMetrics(*clock, min_buffer_count, max_buffer_count, max_level)) {
      if (summary.clock_path_min_buffer == 0 && summary.clock_path_max_buffer == 0 && summary.max_level_of_clock_tree == 0) {
        summary.clock_path_min_buffer = min_buffer_count;
      } else {
        summary.clock_path_min_buffer = std::min(summary.clock_path_min_buffer, min_buffer_count);
      }
      summary.clock_path_max_buffer = std::max(summary.clock_path_max_buffer, max_buffer_count);
      summary.max_level_of_clock_tree = std::max(summary.max_level_of_clock_tree, max_level);
    }

    evaluateClockNet(clock->get_clock_source_net(), should_refresh_sta, summary);
    for (auto* net : clock->get_nets()) {
      if (net == clock->get_clock_source_net()) {
        continue;
      }
      evaluateClockNet(net, should_refresh_sta, summary);
    }
  }

  if (should_refresh_sta) {
    STA_ADAPTER_INST.updateTiming();
  }
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    appendClockTiming(*clock, should_refresh_sta, summary);
  }
  emitEvaluationSummary(summary, wrote_idb, should_refresh_sta);
}

auto ClockTreeEvaluator::outputSummary() -> ClockTreeSummary
{
  return latestSummary();
}

auto ClockTreeEvaluator::resetSummary() -> void
{
  clearSummary(latestSummary());
}

}  // namespace icts
