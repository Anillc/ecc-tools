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
 * @file QorEvaluationRootProbe.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief CTS QoR H-tree root input to leaf output probe helpers.
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "adapter/sta/STAAdapter.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "evaluation/qor/QorEvaluationInternal.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace icts::qor_evaluation {
namespace {

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

auto collectLeafBufferOutputs(const Clock& clock, Pin* root_output_pin, const HTreeBufferRoleIndex& role_index) -> LeafOutputCollection
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

  std::unordered_set<const Pin*> visited_leaf_outputs;
  const auto reachable_pins = DESIGN_INST.get_clock_dag().reachablePinsFrom(&clock, root_output_pin);
  for (auto* pin : reachable_pins) {
    if (pin == nullptr || pin == root_output_pin || !isStructuredHTreeBufferOutputPin(pin, role_index)) {
      continue;
    }
    auto* net = pin->get_net();
    if (net != nullptr && !hasDownstreamStructuredHTreeBufferInputLoad(net, role_index) && visited_leaf_outputs.insert(pin).second) {
      collection.output_pins.push_back(pin);
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
  const auto leaf_outputs = collectLeafBufferOutputs(clock, root_output, role_index);
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

}  // namespace

auto EmitRootInputToLeafOutputProbeReport(const std::vector<Clock*>& clocks, const ClockLayout* clock_layout, bool query_sta_timing) -> void
{
  std::vector<RootDriverProbe> root_driver_probes;
  for (const auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    const auto role_index = makeHTreeBufferRoleIndex(*clock, clock_layout);
    auto clock_probes = collectRootDriverProbes(*clock, role_index);
    root_driver_probes.insert(root_driver_probes.end(), std::make_move_iterator(clock_probes.begin()),
                              std::make_move_iterator(clock_probes.end()));
  }
  for (auto& probe : root_driver_probes) {
    evaluateRootInputToLeafOutputProbe(probe, query_sta_timing);
  }
  emitRootInputToLeafOutputProbeTable(root_driver_probes, query_sta_timing);
}

}  // namespace icts::qor_evaluation
