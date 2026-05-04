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
 * @file QorEvaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree evaluation stage implementation.
 */

#include "evaluation/qor/QorEvaluation.hh"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Point.hh"
#include "Qor.hh"
#include "SteinerTree.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "routing/router/Router.hh"
#include "timing/TimingEngine.hh"

namespace icts {
namespace {

enum class ClockNetRole
{
  kSourceToRoot,
  kTrunk,
  kLeaf
};

struct ClockNetMeasurement
{
  ClockNetRole role = ClockNetRole::kTrunk;
  int64_t wirelength_dbu = 0;
  int64_t hpwl_dbu = 0;
};

struct RootArrivalStats
{
  std::size_t count = 0U;
  double min_ns = 0.0;
  double max_ns = 0.0;
  double mean_ns = 0.0;
  double median_ns = 0.0;
};

struct RootDriverProbe
{
  const Clock* clock = nullptr;
  Inst* root_inst = nullptr;
  Pin* root_input_pin = nullptr;
  Pin* root_output_pin = nullptr;
  Net* root_output_net = nullptr;
  std::vector<Pin*> leaf_output_pins;
  std::string leaf_output_collection_rule;
  std::string leaf_output_role_summary;
  double root_arrival_ns = 0.0;
  bool has_root_arrival = false;
  RootArrivalStats arrival_stats;
};

struct LeafOutputCollection
{
  std::vector<Pin*> output_pins;
  std::string rule;
  std::string role_summary;
};

struct HTreeBufferRoleIndex
{
  bool metadata_available = false;
  std::unordered_set<const Inst*> insts;
  std::size_t layout_record_count = 0U;
  std::size_t design_inst_count = 0U;
  std::size_t missing_design_inst_count = 0U;
  std::size_t non_buffer_design_inst_count = 0U;
};

auto clearStatistics(Qor& statistics) -> void
{
  statistics = Qor{};
}

auto clearSummary(QorSummary& summary) -> void
{
  summary.has_evaluation_result = false;
  summary.sta_clocks_propagated = false;
  summary.propagated_clock_count = 0U;
  summary.final_clock_buffer_count = 0;
  summary.final_buffer_area_um2 = 0.0;
  summary.clock_member_buffer_count = 0;
  summary.max_clock_net_wirelength_um = 0.0;
  summary.total_clock_network_wirelength_um = 0.0;
  summary.max_clock_net_wirelength_dbu = 0;
  summary.total_clock_network_wirelength_dbu = 0.0;
  summary.design_dbu_per_um = 0;
  summary.buffer_num = 0;
  summary.buffer_area = 0.0;
  summary.clock_path_min_buffer = 0;
  summary.clock_path_max_buffer = 0;
  summary.feature_max_clock_network_level = 0;
  summary.max_clock_wirelength = 0;
  summary.total_clock_wirelength = 0.0;
  summary.clocks_timing.clear();
  summary.clocks_latency_skew.clear();
}

auto syncCompatibilityAliases(QorSummary& summary) -> void
{
  summary.buffer_num = summary.final_clock_buffer_count;
  summary.buffer_area = summary.final_buffer_area_um2;
  summary.clock_path_min_buffer = summary.clock_member_buffer_count;
  summary.clock_path_max_buffer = summary.clock_member_buffer_count;
  summary.feature_max_clock_network_level = summary.clock_member_buffer_count;
  summary.max_clock_wirelength = summary.max_clock_net_wirelength_dbu;
  summary.total_clock_wirelength = summary.total_clock_network_wirelength_dbu;
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

auto dbuToUm(int64_t dbu, const QorSummary& summary) -> double
{
  const double dbu_per_um = static_cast<double>(std::max(summary.design_dbu_per_um, int32_t{1}));
  return static_cast<double>(dbu) / dbu_per_um;
}

auto hasValidLocation(const Point<int>& point) -> bool
{
  return point.get_x() >= 0 && point.get_y() >= 0;
}

auto calcHpwlDbu(const Net* net) -> int64_t
{
  if (net == nullptr || net->get_driver() == nullptr) {
    return 0;
  }

  std::vector<Point<int>> locations;
  if (hasValidLocation(net->get_driver()->get_location())) {
    locations.push_back(net->get_driver()->get_location());
  }
  for (const auto* load : net->get_loads()) {
    if (load != nullptr && hasValidLocation(load->get_location())) {
      locations.push_back(load->get_location());
    }
  }
  if (locations.size() < 2U) {
    return 0;
  }

  int min_x = locations.front().get_x();
  int max_x = locations.front().get_x();
  int min_y = locations.front().get_y();
  int max_y = locations.front().get_y();
  for (const auto& location : locations) {
    min_x = std::min(min_x, location.get_x());
    max_x = std::max(max_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_y = std::max(max_y, location.get_y());
  }
  return static_cast<int64_t>(max_x - min_x) + static_cast<int64_t>(max_y - min_y);
}

auto containsClockLoad(const Clock& clock, const Pin* pin) -> bool
{
  const auto& clock_loads = clock.get_loads();
  return std::ranges::find(clock_loads, pin) != clock_loads.end();
}

auto classifyClockNet(const Clock& clock, const Net* net) -> ClockNetRole
{
  if (net == nullptr) {
    return ClockNetRole::kTrunk;
  }
  if (net == clock.get_clock_source_net()) {
    return ClockNetRole::kSourceToRoot;
  }

  for (const auto* load : net->get_loads()) {
    if (load == nullptr) {
      continue;
    }
    const auto* inst = load->get_inst();
    if (containsClockLoad(clock, load) || inst == nullptr || !inst->is_buffer()) {
      return ClockNetRole::kLeaf;
    }
  }
  return ClockNetRole::kTrunk;
}

auto calcStats(std::vector<double> values) -> RootArrivalStats
{
  RootArrivalStats stats;
  if (values.empty()) {
    return stats;
  }

  std::ranges::sort(values);
  stats.count = values.size();
  stats.min_ns = values.front();
  stats.max_ns = values.back();
  double sum = 0.0;
  for (const double value : values) {
    sum += value;
  }
  stats.mean_ns = sum / static_cast<double>(values.size());
  const std::size_t middle = values.size() / 2U;
  if (values.size() % 2U == 0U) {
    stats.median_ns = (values.at(middle - 1U) + values.at(middle)) / 2.0;
  } else {
    stats.median_ns = values.at(middle);
  }
  return stats;
}

auto formatOptionalNs(bool valid, double value) -> std::string
{
  return valid ? logformat::FormatFixed(value, 4) : "n/a";
}

auto formatExamplePinNames(const std::vector<Pin*>& pins) -> std::string
{
  if (pins.empty()) {
    return "n/a";
  }

  std::string result;
  const std::size_t sample_count = std::min<std::size_t>(pins.size(), 3U);
  for (std::size_t index = 0; index < sample_count; ++index) {
    if (!result.empty()) {
      result += ", ";
    }
    result += Design::getPinFullName(pins.at(index));
  }
  if (pins.size() > sample_count) {
    result += ", ...";
  }
  return result;
}

auto isBufferInputPin(const Pin* pin) -> bool
{
  auto* inst = pin != nullptr ? pin->get_inst() : nullptr;
  return inst != nullptr && inst->is_buffer() && inst->findDriverPin() != pin;
}

auto isClockMemberNet(const Clock& clock, const Net* net) -> bool
{
  const auto& nets = clock.get_nets();
  return std::ranges::find(nets, net) != nets.end();
}

auto findLayoutClock(const ClockLayout* clock_layout, const Clock& clock) -> const ClockLayoutClock*
{
  if (clock_layout == nullptr) {
    return nullptr;
  }

  const auto& clock_name = clock.get_clock_name();
  const auto& clock_net_name = clock.get_clock_net_name();
  for (const auto& layout_clock : clock_layout->get_clocks()) {
    if (layout_clock.clock_name == clock_name && layout_clock.clock_net_name == clock_net_name) {
      return &layout_clock;
    }
  }
  for (const auto& layout_clock : clock_layout->get_clocks()) {
    if (layout_clock.clock_name == clock_name && (layout_clock.clock_net_name.empty() || clock_net_name.empty())) {
      return &layout_clock;
    }
  }
  for (const auto& layout_clock : clock_layout->get_clocks()) {
    if (!clock_net_name.empty() && layout_clock.clock_name.empty() && layout_clock.clock_net_name == clock_net_name) {
      return &layout_clock;
    }
  }
  return nullptr;
}

auto isStructuredHTreeBufferLayoutInst(const ClockLayoutInst& layout_inst) -> bool
{
  return layout_inst.role == LayoutInstRole::kHTreeBuffer && layout_inst.synthesis_phase == ClockLayoutPhase::kDownstreamHTree
         && layout_inst.topology_level >= 0 && !layout_inst.inst_name.empty();
}

auto makeHTreeBufferRoleIndex(const Clock& clock, const ClockLayout* clock_layout) -> HTreeBufferRoleIndex
{
  HTreeBufferRoleIndex index;
  const auto* layout_clock = findLayoutClock(clock_layout, clock);
  if (layout_clock == nullptr) {
    return index;
  }

  index.metadata_available = true;
  for (const auto& layout_inst : layout_clock->insts) {
    if (!isStructuredHTreeBufferLayoutInst(layout_inst)) {
      continue;
    }

    ++index.layout_record_count;
    auto* design_inst = DESIGN_INST.findInst(layout_inst.inst_name);
    if (design_inst == nullptr) {
      ++index.missing_design_inst_count;
      continue;
    }
    if (!design_inst->is_buffer()) {
      ++index.non_buffer_design_inst_count;
      continue;
    }
    if (index.insts.insert(design_inst).second) {
      ++index.design_inst_count;
    }
  }
  return index;
}

auto formatHTreeBufferRoleIndexSummary(const HTreeBufferRoleIndex& index) -> std::string
{
  if (!index.metadata_available) {
    return "structured ClockLayout metadata unavailable for this clock";
  }

  return "ClockLayout downstream_htree htree_buffer records=" + std::to_string(index.layout_record_count) + ", design_insts="
         + std::to_string(index.design_inst_count) + ", missing_design_insts=" + std::to_string(index.missing_design_inst_count)
         + ", non_buffer_design_insts=" + std::to_string(index.non_buffer_design_inst_count);
}

auto isStructuredHTreeBufferInst(const Inst* inst, const HTreeBufferRoleIndex& index) -> bool
{
  return inst != nullptr && inst->is_buffer() && index.insts.contains(inst);
}

auto isStructuredHTreeBufferInputPin(const Pin* pin, const HTreeBufferRoleIndex& index) -> bool
{
  auto* inst = pin != nullptr ? pin->get_inst() : nullptr;
  auto* driver_pin = inst != nullptr ? inst->findDriverPin() : nullptr;
  return isStructuredHTreeBufferInst(inst, index) && driver_pin != nullptr && driver_pin != pin;
}

auto isStructuredHTreeBufferOutputPin(const Pin* pin, const HTreeBufferRoleIndex& index) -> bool
{
  auto* inst = pin != nullptr ? pin->get_inst() : nullptr;
  return isStructuredHTreeBufferInst(inst, index) && inst->findDriverPin() == pin;
}

auto hasDownstreamStructuredHTreeBufferInputLoad(const Net* net, const HTreeBufferRoleIndex& index) -> bool
{
  if (net == nullptr) {
    return false;
  }
  for (auto* load : net->get_loads()) {
    if (isStructuredHTreeBufferInputPin(load, index)) {
      return true;
    }
  }
  return false;
}

auto collectLeafBufferOutputs(Pin* root_output_pin, const HTreeBufferRoleIndex& role_index) -> LeafOutputCollection
{
  LeafOutputCollection collection;
  if (!role_index.metadata_available) {
    collection.rule = "structured_role_metadata_unavailable";
    collection.role_summary = "ClockLayout metadata unavailable; H-tree leaf buffer outputs are not sampled";
    return collection;
  }
  if (role_index.insts.empty()) {
    collection.rule = "structured_role_metadata_empty";
    collection.role_summary
        = "no downstream H-tree buffer insts found in structured role metadata; " + formatHTreeBufferRoleIndexSummary(role_index);
    return collection;
  }
  if (root_output_pin == nullptr || root_output_pin->get_net() == nullptr) {
    collection.rule = "none";
    collection.role_summary = "no H-tree leaf buffer output discovered from structured role metadata";
    return collection;
  }

  std::deque<Net*> pending_nets;
  std::unordered_set<const Net*> visited_nets;
  std::unordered_set<const Pin*> visited_leaf_outputs;
  pending_nets.push_back(root_output_pin->get_net());

  while (!pending_nets.empty()) {
    auto* net = pending_nets.front();
    pending_nets.pop_front();
    if (net == nullptr || !visited_nets.insert(net).second) {
      continue;
    }

    auto* driver = net->get_driver();
    const bool driver_is_htree_leaf_buffer_output = isStructuredHTreeBufferOutputPin(driver, role_index) && driver != root_output_pin;
    if (driver_is_htree_leaf_buffer_output && !hasDownstreamStructuredHTreeBufferInputLoad(net, role_index)) {
      if (visited_leaf_outputs.insert(driver).second) {
        collection.output_pins.push_back(driver);
      }
      continue;
    }

    for (auto* load : net->get_loads()) {
      if (!isStructuredHTreeBufferInputPin(load, role_index)) {
        continue;
      }
      auto* inst = load->get_inst();
      auto* output_pin = inst != nullptr ? inst->findDriverPin() : nullptr;
      auto* output_net = output_pin != nullptr ? output_pin->get_net() : nullptr;
      if (output_net != nullptr) {
        pending_nets.push_back(output_net);
      }
    }
  }

  if (!collection.output_pins.empty()) {
    collection.rule = "structured_downstream_htree_buffer_role";
    collection.role_summary = "last downstream H-tree buffer output before no downstream structured H-tree buffer input; "
                              + formatHTreeBufferRoleIndexSummary(role_index);
  } else {
    collection.rule = "none";
    collection.role_summary
        = "no H-tree leaf buffer output discovered from structured role metadata; " + formatHTreeBufferRoleIndexSummary(role_index);
  }
  return collection;
}

auto singleBufferInputLoad(Net* net) -> Pin*
{
  if (net == nullptr || net->get_loads().size() != 1U) {
    return nullptr;
  }
  auto* load = net->get_loads().front();
  return isBufferInputPin(load) ? load : nullptr;
}

auto makeRootDriverProbe(const Clock& clock, Pin* root_input, const HTreeBufferRoleIndex& role_index) -> std::optional<RootDriverProbe>
{
  auto* root_inst = root_input != nullptr ? root_input->get_inst() : nullptr;
  auto* root_output = root_inst != nullptr ? root_inst->findDriverPin() : nullptr;
  auto* root_output_net = root_output != nullptr ? root_output->get_net() : nullptr;
  if (root_inst == nullptr || root_output == nullptr || root_output_net == nullptr) {
    return std::nullopt;
  }
  if (root_output_net == clock.get_clock_source_net()) {
    return std::nullopt;
  }
  if (!isClockMemberNet(clock, root_output_net)) {
    return std::nullopt;
  }

  RootDriverProbe probe;
  probe.clock = &clock;
  probe.root_inst = root_inst;
  probe.root_input_pin = root_input;
  probe.root_output_pin = root_output;
  probe.root_output_net = root_output_net;
  const auto leaf_outputs = collectLeafBufferOutputs(root_output, role_index);
  probe.leaf_output_pins = leaf_outputs.output_pins;
  probe.leaf_output_collection_rule = leaf_outputs.rule;
  probe.leaf_output_role_summary = leaf_outputs.role_summary;
  return probe;
}

auto collectRootDriverProbes(const Clock& clock, const HTreeBufferRoleIndex& role_index) -> std::vector<RootDriverProbe>
{
  std::vector<RootDriverProbe> probes;
  auto* source_net = clock.get_clock_source_net();
  if (source_net == nullptr) {
    return probes;
  }

  std::unordered_set<const Pin*> visited_inputs;
  for (auto* source_load : source_net->get_loads()) {
    if (!isBufferInputPin(source_load)) {
      continue;
    }

    auto* root_input = source_load;
    while (root_input != nullptr && visited_inputs.insert(root_input).second) {
      auto* root_inst = root_input->get_inst();
      auto* root_output = root_inst != nullptr ? root_inst->findDriverPin() : nullptr;
      auto* root_output_net = root_output != nullptr ? root_output->get_net() : nullptr;
      auto* next_input = singleBufferInputLoad(root_output_net);
      if (next_input == nullptr) {
        break;
      }
      root_input = next_input;
    }

    if (auto probe = makeRootDriverProbe(clock, root_input, role_index); probe.has_value()) {
      probes.push_back(std::move(*probe));
    }
  }
  return probes;
}

auto evaluateRootInputToLeafOutputProbe(RootDriverProbe& probe, bool query_sta_timing) -> void
{
  if (probe.clock == nullptr || probe.root_inst == nullptr || probe.root_input_pin == nullptr || probe.root_output_pin == nullptr
      || !query_sta_timing) {
    return;
  }

  const auto root_arrival = STA_ADAPTER_INST.queryPinClockArrival(probe.root_input_pin, probe.clock->get_clock_name());
  if (root_arrival.has_value()) {
    probe.root_arrival_ns = *root_arrival;
    probe.has_root_arrival = true;
  }

  std::vector<double> leaf_arrival_deltas_ns;
  leaf_arrival_deltas_ns.reserve(probe.leaf_output_pins.size());
  if (root_arrival.has_value()) {
    for (auto* leaf_output : probe.leaf_output_pins) {
      const auto leaf_arrival = STA_ADAPTER_INST.queryPinClockArrival(leaf_output, probe.clock->get_clock_name());
      if (leaf_arrival.has_value() && *leaf_arrival >= *root_arrival) {
        leaf_arrival_deltas_ns.push_back(*leaf_arrival - *root_arrival);
      }
    }
  }
  probe.arrival_stats = calcStats(std::move(leaf_arrival_deltas_ns));
}

auto emitRootInputToLeafOutputProbeTable(const std::vector<RootDriverProbe>& probes, bool query_sta_timing) -> void
{
  if (probes.empty()) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "CTS HTree Evaluation",
                           "no H-tree root driver buffers were discovered from clock source nets.",
                           {{"discovery_rule", "clock source net load buffer input"}});
    return;
  }

  schema::TableRows arrival_rows;
  arrival_rows.reserve(probes.size());
  std::size_t leaf_output_pin_count = 0U;
  std::size_t arrival_sample_count = 0U;

  for (const auto& probe : probes) {
    const auto root_input_name = Design::getPinFullName(probe.root_input_pin);
    const auto root_output_name = Design::getPinFullName(probe.root_output_pin);
    const auto clock_name = probe.clock != nullptr ? probe.clock->get_clock_name() : "";
    leaf_output_pin_count += probe.leaf_output_pins.size();
    arrival_sample_count += probe.arrival_stats.count;

    arrival_rows.push_back({
        clock_name,
        root_input_name,
        root_output_name,
        probe.leaf_output_collection_rule.empty() ? "n/a" : probe.leaf_output_collection_rule,
        probe.leaf_output_role_summary.empty() ? "n/a" : probe.leaf_output_role_summary,
        std::to_string(probe.leaf_output_pins.size()),
        formatExamplePinNames(probe.leaf_output_pins),
        std::to_string(probe.arrival_stats.count),
        formatOptionalNs(probe.has_root_arrival, probe.root_arrival_ns),
        probe.arrival_stats.count > 0U ? logformat::FormatFixed(probe.arrival_stats.min_ns, 4) : "n/a",
        probe.arrival_stats.count > 0U ? logformat::FormatFixed(probe.arrival_stats.max_ns, 4) : "n/a",
        probe.arrival_stats.count > 0U ? logformat::FormatFixed(probe.arrival_stats.mean_ns, 4) : "n/a",
        probe.arrival_stats.count > 0U ? logformat::FormatFixed(probe.arrival_stats.median_ns, 4) : "n/a",
    });
  }

  schema::EmitTable("CTS Root Input To HTree Leaf Buffer Output Evaluation",
                    {"Clock", "Root Input", "Root Output", "Leaf Sample Role", "Leaf Role Detail", "HTree Leaf Buffer Output Pin Count",
                     "Leaf Pin Examples", "Arrival Samples", "Root AT (ns)", "Min (ns)", "Max (ns)", "Mean (ns)", "Median (ns)"},
                    arrival_rows);

  schema::EmitKeyValueTable("CTS Root Input To HTree Leaf Buffer Output Summary",
                            {
                                {"sta_timing_available", query_sta_timing ? "true" : "false"},
                                {"root_driver_count", std::to_string(probes.size())},
                                {"leaf_buffer_output_pin_count", std::to_string(leaf_output_pin_count)},
                                {"arrival_sample_count", std::to_string(arrival_sample_count)},
                                {"leaf_output_detection", "ClockLayout downstream H-tree buffer role metadata with topology_level >= 0"},
                                {"probe_scope", "STA h-tree root input pin to h-tree leaf buffer output pin propagation"},
                            });
}

auto addWirelengthByRole(Qor& statistics, ClockNetRole role, double wirelength_um, double hpwl_um) -> void
{
  switch (role) {
    case ClockNetRole::kSourceToRoot:
      statistics.top_wirelength_um += wirelength_um;
      statistics.hpwl_top_wirelength_um += hpwl_um;
      break;
    case ClockNetRole::kTrunk:
      statistics.trunk_wirelength_um += wirelength_um;
      statistics.hpwl_trunk_wirelength_um += hpwl_um;
      break;
    case ClockNetRole::kLeaf:
      statistics.leaf_wirelength_um += wirelength_um;
      statistics.hpwl_leaf_wirelength_um += hpwl_um;
      break;
  }
  statistics.total_wirelength_um += wirelength_um;
  statistics.hpwl_total_wirelength_um += hpwl_um;
  statistics.max_net_wirelength_um = std::max(statistics.max_net_wirelength_um, wirelength_um);
  statistics.hpwl_max_net_wirelength_um = std::max(statistics.hpwl_max_net_wirelength_um, hpwl_um);
}

auto instTypeName(const Inst& inst) -> std::string
{
  if (inst.is_buffer()) {
    return "Buffer";
  }
  if (inst.is_inverter()) {
    return "Inverter";
  }
  if (inst.is_clock_gate()) {
    return "ICG";
  }
  if (inst.is_mux()) {
    return "Mux";
  }
  if (inst.is_macro_block()) {
    return "Macro";
  }
  if (inst.is_flipflop()) {
    return "FlipFlop";
  }
  return "Others";
}

auto calcInstInputPinCapPf(const Inst& inst) -> double
{
  double total_cap_pf = 0.0;
  for (const auto* pin : inst.get_pins()) {
    if (pin == nullptr || pin->get_inst() != &inst) {
      continue;
    }
    total_cap_pf += STA_ADAPTER_INST.queryPinCapacitance(pin);
  }
  return total_cap_pf;
}

auto accumulateInstStatistics(const Inst& inst, Qor& statistics) -> void
{
  if (!inst.is_buffer()) {
    return;
  }

  const auto& cell_master = inst.get_cell_master();
  if (cell_master.empty()) {
    return;
  }

  const std::string cell_type = instTypeName(inst);
  const double area_um2 = STA_ADAPTER_INST.queryCellAreaUm2(cell_master);
  const double cap_pf = calcInstInputPinCapPf(inst);

  auto& cell_stat = statistics.cell_stats[cell_type];
  ++cell_stat.count;
  cell_stat.total_area_um2 += area_um2;
  cell_stat.total_cap_pf += cap_pf;

  auto& lib_dist = statistics.lib_cell_dist[cell_master];
  lib_dist.cell_type = cell_type;
  ++lib_dist.count;
  lib_dist.total_area_um2 += area_um2;
}

auto installClockNetRcTreeAndMeasure(Net* net, ClockNetRole role, bool install_sta_rc_tree) -> std::optional<ClockNetMeasurement>
{
  auto route_tree = net == nullptr ? Router::ClockSteinerTreeType{} : Router::buildClockNetTree(*net);
  if (route_tree.node_count() == 0 || route_tree.edge_count() == 0) {
    return std::nullopt;
  }

  const auto wirelength = calcRouteWirelength(route_tree);
  const auto hpwl = calcHpwlDbu(net);

  if (install_sta_rc_tree && net != nullptr) {
    (void) STA_ADAPTER_INST.installClockNetRcTree(*net, route_tree);
  }

  if (WRAPPER_INST.is_design_ready()) {
    auto rc_tree = Router::buildRCTree(route_tree, buildRcOptionsFromRuntimeConfig());
    auto timing_metrics = TimingEngine::update(rc_tree);
    (void) timing_metrics;
  }

  return ClockNetMeasurement{
      .role = role,
      .wirelength_dbu = wirelength,
      .hpwl_dbu = hpwl,
  };
}

auto appendClockNetStatistics(const std::vector<ClockNetMeasurement>& measurements, QorSummary& summary, Qor& statistics) -> void
{
  for (const auto& measurement : measurements) {
    const double wirelength_um = dbuToUm(measurement.wirelength_dbu, summary);
    const double hpwl_um = dbuToUm(measurement.hpwl_dbu, summary);
    const auto wirelength = measurement.wirelength_dbu;

    summary.total_clock_network_wirelength_um += wirelength_um;
    summary.max_clock_net_wirelength_um = std::max(summary.max_clock_net_wirelength_um, wirelength_um);
    summary.total_clock_network_wirelength_dbu += static_cast<double>(wirelength);
    summary.max_clock_net_wirelength_dbu = std::max(summary.max_clock_net_wirelength_dbu, static_cast<int32_t>(wirelength));
    addWirelengthByRole(statistics, measurement.role, wirelength_um, hpwl_um);
  }
}

auto appendClockTimings(bool query_sta_timing, QorSummary& summary) -> void
{
  if (!query_sta_timing) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "CTS Evaluation",
                           "clock timing metrics were not queried because STA timing context is unavailable.", {{"timing_source", "STA"}});
    return;
  }

  const auto timing_records = STA_ADAPTER_INST.queryClockTimings();
  if (timing_records.empty()) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "CTS Evaluation",
                           "clock timing metrics are unavailable from STA; no fallback values are reported.", {{"timing_source", "STA"}});
    return;
  }
  summary.clocks_timing.reserve(summary.clocks_timing.size() + timing_records.size());
  for (const auto& timing_record : timing_records) {
    summary.clocks_timing.push_back(QorSummary::ClockTiming{
        .clock_name = timing_record.clock_name,
        .setup_tns = timing_record.metrics.setup_tns,
        .setup_wns = timing_record.metrics.setup_wns,
        .hold_tns = timing_record.metrics.hold_tns,
        .hold_wns = timing_record.metrics.hold_wns,
        .suggest_freq = timing_record.metrics.suggest_freq,
    });
  }
}

auto appendClockLatencySkew(QorSummary& summary) -> void
{
  auto latency_skew_metrics = STA_ADAPTER_INST.queryClockLatencySkew();
  summary.clocks_latency_skew.reserve(summary.clocks_latency_skew.size() + latency_skew_metrics.size());
  for (const auto& metric : latency_skew_metrics) {
    summary.clocks_latency_skew.push_back(QorSummary::ClockLatencySkew{
        .clock_name = metric.clock_name,
        .analysis_mode = metric.analysis_mode,
        .launch_pin = metric.launch_pin,
        .capture_pin = metric.capture_pin,
        .launch_latency_ns = metric.launch_latency_ns,
        .capture_latency_ns = metric.capture_latency_ns,
        .worst_skew_ns = metric.worst_skew_ns,
        .average_worst_skew_ns = metric.average_worst_skew_ns,
        .path_count = metric.path_count,
        .average_sample_count = metric.average_sample_count,
    });
  }
}

auto emitClockTimingTables(const QorSummary& summary) -> void
{
  if (!summary.clocks_timing.empty()) {
    schema::TableRows rows;
    rows.reserve(summary.clocks_timing.size());
    for (const auto& timing : summary.clocks_timing) {
      rows.push_back({
          timing.clock_name,
          logformat::FormatFixed(timing.setup_tns, 3),
          logformat::FormatFixed(timing.setup_wns, 3),
          logformat::FormatFixed(timing.hold_tns, 3),
          logformat::FormatFixed(timing.hold_wns, 3),
          logformat::FormatFixed(timing.suggest_freq, 3),
      });
    }
    schema::EmitTable("CTS Clock Timing Overview",
                      {"Clock", "Setup TNS (ns)", "Setup WNS (ns)", "Hold TNS (ns)", "Hold WNS (ns)", "Suggested Frequency (MHz)"}, rows);
  }

  if (!summary.clocks_latency_skew.empty()) {
    schema::TableRows rows;
    rows.reserve(summary.clocks_latency_skew.size());
    for (const auto& metric : summary.clocks_latency_skew) {
      rows.push_back({
          metric.clock_name,
          metric.analysis_mode,
          metric.launch_pin,
          metric.capture_pin,
          logformat::FormatFixed(metric.launch_latency_ns, 3),
          logformat::FormatFixed(metric.capture_latency_ns, 3),
          logformat::FormatFixed(metric.worst_skew_ns, 3),
          logformat::FormatFixed(metric.average_worst_skew_ns, 3),
          std::to_string(metric.path_count),
          std::to_string(metric.average_sample_count),
      });
    }
    schema::EmitTable("CTS Clock Latency Skew Overview",
                      {"Clock", "Mode", "Launch Pin", "Capture Pin", "Launch Latency (ns)", "Capture Latency (ns)", "Worst Skew (ns)",
                       "Average Worst Skew (ns)", "Path Count", "Average Sample Count"},
                      rows);
  }
}

auto emitEvaluationSummary(const QorSummary& summary, bool refreshed_sta) -> void
{
  schema::EmitKeyValueTable("CTS Evaluation Overview", {
                                                           {"sta_timing_refreshed", refreshed_sta ? "true" : "false"},
                                                           {"sdc_clocks_propagated", summary.sta_clocks_propagated ? "true" : "false"},
                                                           {"propagated_clock_count", std::to_string(summary.propagated_clock_count)},
                                                           {"final_metrics_source", "CTS Key Results"},
                                                           {"clock_member_buffer_count", std::to_string(summary.clock_member_buffer_count)},
                                                           {"path_depth_metric_status", "not_reported_no_source_to_sink_traversal"},
                                                           {"design_units", std::to_string(summary.design_dbu_per_um) + " DBU/um"},
                                                           {"statistics_reports", "wirelength.rpt, cell_stats.rpt, lib_cell_dist.rpt"},
                                                       });
  emitClockTimingTables(summary);
}

}  // namespace

auto QorEvaluation::evaluate(EvaluationState& state) -> void
{
  evaluate(state, EvaluationOptions{});
}

auto QorEvaluation::evaluate(EvaluationState& state, const EvaluationOptions& options) -> void
{
  auto& summary = state.summary;
  auto& statistics = state.statistics;
  clearSummary(summary);
  clearStatistics(statistics);

  auto clocks = DESIGN_INST.get_clocks();
  summary.design_dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  const bool should_refresh_sta = WRAPPER_INST.is_design_ready() && options.refresh_sta_timing;
  if (should_refresh_sta) {
    STA_ADAPTER_INST.refreshFullDesignTimingContext();
    summary.propagated_clock_count = STA_ADAPTER_INST.setPropagatedClocks();
    summary.sta_clocks_propagated = summary.propagated_clock_count > 0U;
  }

  std::unordered_set<const Inst*> counted_buffer_insts;
  std::vector<ClockNetMeasurement> clock_net_measurements;
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }

    int32_t clock_member_buffer_count = 0;
    for (auto* inst : clock->get_insts()) {
      if (inst == nullptr || !inst->is_buffer()) {
        continue;
      }
      ++clock_member_buffer_count;
      const bool is_new_buffer_inst = counted_buffer_insts.insert(inst).second;
      if (is_new_buffer_inst) {
        ++summary.final_clock_buffer_count;
        accumulateInstStatistics(*inst, statistics);
      }
      if (WRAPPER_INST.is_layout_ready() && is_new_buffer_inst) {
        summary.final_buffer_area_um2 += STA_ADAPTER_INST.queryCellAreaUm2(inst->get_cell_master());
      }
    }
    summary.clock_member_buffer_count += clock_member_buffer_count;

    if (auto measurement = installClockNetRcTreeAndMeasure(clock->get_clock_source_net(),
                                                           classifyClockNet(*clock, clock->get_clock_source_net()), should_refresh_sta);
        measurement.has_value()) {
      clock_net_measurements.push_back(*measurement);
    }
    for (auto* net : clock->get_nets()) {
      if (net == clock->get_clock_source_net()) {
        continue;
      }
      if (auto measurement = installClockNetRcTreeAndMeasure(net, classifyClockNet(*clock, net), should_refresh_sta);
          measurement.has_value()) {
        clock_net_measurements.push_back(*measurement);
      }
    }
  }

  bool timing_updated = false;
  if (should_refresh_sta) {
    STA_ADAPTER_INST.updateTiming();
    timing_updated = true;
    (void) STA_ADAPTER_INST.reportTiming();
    appendClockLatencySkew(summary);
  }
  appendClockTimings(timing_updated, summary);
  if (timing_updated) {
    std::vector<RootDriverProbe> root_driver_probes;
    for (const auto* clock : clocks) {
      if (clock == nullptr) {
        continue;
      }
      const auto role_index = makeHTreeBufferRoleIndex(*clock, options.clock_layout);
      auto clock_probes = collectRootDriverProbes(*clock, role_index);
      root_driver_probes.insert(root_driver_probes.end(), std::make_move_iterator(clock_probes.begin()),
                                std::make_move_iterator(clock_probes.end()));
    }
    for (auto& probe : root_driver_probes) {
      evaluateRootInputToLeafOutputProbe(probe, timing_updated);
    }
    emitRootInputToLeafOutputProbeTable(root_driver_probes, timing_updated);
  }
  appendClockNetStatistics(clock_net_measurements, summary, statistics);
  syncCompatibilityAliases(summary);
  statistics.valid = true;
  summary.has_evaluation_result = true;
  emitEvaluationSummary(summary, timing_updated);
}

auto QorEvaluation::outputSummary(const EvaluationState& state) -> QorSummary
{
  return state.summary;
}

auto QorEvaluation::hasEvaluationResult(const EvaluationState& state) -> bool
{
  return state.summary.has_evaluation_result && state.statistics.valid;
}

auto QorEvaluation::reset(EvaluationState& state) -> void
{
  clearSummary(state.summary);
  clearStatistics(state.statistics);
}

}  // namespace icts
