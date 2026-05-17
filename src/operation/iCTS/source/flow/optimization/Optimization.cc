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
 * @file Optimization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief CTS post-synthesis optimization flow facade implementation.
 */

#include "optimization/Optimization.hh"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "buffer_sizing/BufferSizingTypes.hh"
#include "buffer_sizing/CharTimingLookup.hh"
#include "buffer_sizing/TreeBufferSizing.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "io/Wrapper.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "router/Router.hh"
#include "routing/SteinerTree.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"

namespace icts {
namespace {

using buffer_sizing::BufferCandidate;
using buffer_sizing::BufferMutation;
using buffer_sizing::CharTimingLookup;
using buffer_sizing::TreeBuffer;
using buffer_sizing::TreeNet;
using buffer_sizing::TreeNode;
using buffer_sizing::TreeNodeKind;
using buffer_sizing::TreeSizingProblem;
using buffer_sizing::TreeSizingSummary;

struct ProblemBuildResult
{
  bool success = false;
  std::string failure_reason;
  TreeSizingProblem problem;
  std::vector<Inst*> inst_by_buffer_id;
};

struct BufferMasterInfo
{
  std::string cell_master;
  double input_cap_pf = 0.0;
  double output_cap_limit_pf = 0.0;
  double area_um2 = 0.0;
  unsigned drive_rank = 0U;
};

struct RoutedNetLengthInfo
{
  bool available = false;
  double total_length_um = 0.0;
  std::unordered_map<const Pin*, double> path_length_by_load_pin;
};

auto resolveRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  return routing_layers.empty() ? 1 : static_cast<int>(routing_layers.front());
}

auto resolveWireWidth() -> std::optional<double>
{
  return CONFIG_INST.get_wire_width() > 0.0 ? std::optional<double>{CONFIG_INST.get_wire_width()} : std::nullopt;
}

auto pointDistanceUm(const Pin& from, const Pin& to, int32_t dbu_per_um) -> double
{
  const int distance_dbu = geometry::Manhattan(from.get_location(), to.get_location());
  return static_cast<double>(std::max(distance_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
}

auto routedEdgeLengthUm(const Router::ClockSteinerTreeType::EdgeType& edge, int32_t dbu_per_um) -> double
{
  const int distance_dbu = std::max(edge.distance, edge.routed_distance);
  return static_cast<double>(std::max(distance_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
}

auto buildRoutedNetLengthInfo(const Net& net, int32_t dbu_per_um) -> RoutedNetLengthInfo
{
  RoutedNetLengthInfo info;
  auto route_tree = Router::buildClockNetTree(net);
  if (!route_tree.validate() || route_tree.node_count() == 0U) {
    return info;
  }

  std::vector<double> path_length_by_tree_node(route_tree.node_count(), 0.0);
  std::deque<std::size_t> pending;
  if (route_tree.get_root() < route_tree.node_count()) {
    pending.push_back(route_tree.get_root());
  }
  while (!pending.empty()) {
    const auto node_id = pending.front();
    pending.pop_front();
    const auto* node = route_tree.get_node(node_id);
    if (node == nullptr) {
      continue;
    }
    for (const auto edge_id : node->child_edge_ids) {
      const auto* edge = route_tree.get_edge(edge_id);
      if (edge == nullptr || edge->target_node_id >= route_tree.node_count()) {
        continue;
      }
      const double edge_length_um = routedEdgeLengthUm(*edge, dbu_per_um);
      info.total_length_um += edge_length_um;
      path_length_by_tree_node.at(edge->target_node_id) = path_length_by_tree_node.at(node_id) + edge_length_um;
      pending.push_back(edge->target_node_id);
    }
  }

  for (auto* load : net.get_loads()) {
    if (load == nullptr) {
      continue;
    }
    const auto* route_node = route_tree.findNode(Design::getPinFullName(load));
    if (route_node == nullptr || route_node->id >= path_length_by_tree_node.size()) {
      continue;
    }
    info.path_length_by_load_pin[load] = path_length_by_tree_node.at(route_node->id);
  }
  info.available = info.total_length_um > 0.0 && !info.path_length_by_load_pin.empty();
  return info;
}

auto findSingleBufferInputPin(Inst* inst) -> Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }
  const auto* driver_pin = inst->findDriverPin();
  for (auto* pin : inst->get_pins()) {
    if (pin == nullptr || pin == driver_pin) {
      continue;
    }
    if (pin->get_type() == PinType::kIn || pin->get_type() == PinType::kClock) {
      return pin;
    }
  }
  for (auto* pin : inst->get_pins()) {
    if (pin != nullptr && pin != driver_pin) {
      return pin;
    }
  }
  return nullptr;
}

auto resolveOutputCapLimit(const std::string& cell_master) -> double
{
  double max_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (max_cap_pf <= 0.0) {
    max_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return max_cap_pf;
}

auto collectBufferMasterInfos() -> std::vector<BufferMasterInfo>
{
  std::vector<BufferMasterInfo> infos;
  infos.reserve(CONFIG_INST.get_buffer_types().size());
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (cell_master.empty()) {
      continue;
    }
    const double output_cap_limit_pf = resolveOutputCapLimit(cell_master);
    const double input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(cell_master);
    if (output_cap_limit_pf <= 0.0 || input_cap_pf <= 0.0) {
      continue;
    }
    infos.push_back(BufferMasterInfo{.cell_master = cell_master,
                                     .input_cap_pf = input_cap_pf,
                                     .output_cap_limit_pf = output_cap_limit_pf,
                                     .area_um2 = std::max(0.0, STA_ADAPTER_INST.queryCellAreaUm2(cell_master)),
                                     .drive_rank = 0U});
  }
  std::ranges::sort(infos, [](const BufferMasterInfo& lhs, const BufferMasterInfo& rhs) -> bool {
    if (std::abs(lhs.output_cap_limit_pf - rhs.output_cap_limit_pf) > 1e-12) {
      return lhs.output_cap_limit_pf < rhs.output_cap_limit_pf;
    }
    return lhs.cell_master < rhs.cell_master;
  });
  for (std::size_t index = 0U; index < infos.size(); ++index) {
    infos.at(index).drive_rank = static_cast<unsigned>(index);
  }
  return infos;
}

auto buildCandidates(const std::string& current_master, const std::vector<BufferMasterInfo>& master_infos,
                     std::size_t& current_candidate_index) -> std::vector<BufferCandidate>
{
  std::vector<BufferCandidate> candidates;
  current_candidate_index = buffer_sizing::kInvalidIndex;
  const auto current_iter = std::ranges::find_if(
      master_infos, [&current_master](const BufferMasterInfo& info) -> bool { return info.cell_master == current_master; });
  if (current_iter == master_infos.end()) {
    return candidates;
  }
  const auto current_rank = current_iter->drive_rank;
  for (const auto& info : master_infos) {
    if (info.drive_rank < current_rank) {
      continue;
    }
    if (info.cell_master == current_master) {
      current_candidate_index = candidates.size();
    }
    candidates.push_back(BufferCandidate{.cell_master = info.cell_master,
                                         .input_cap_pf = info.input_cap_pf,
                                         .output_cap_limit_pf = info.output_cap_limit_pf,
                                         .area_um2 = info.area_um2,
                                         .drive_rank = info.drive_rank});
  }
  return candidates;
}

auto appendNodeIfMissing(TreeSizingProblem& problem, std::unordered_map<const Pin*, std::size_t>& node_by_pin, Pin* pin, TreeNodeKind kind)
    -> std::size_t
{
  if (pin == nullptr) {
    return buffer_sizing::kInvalidIndex;
  }
  if (const auto iter = node_by_pin.find(pin); iter != node_by_pin.end()) {
    return iter->second;
  }
  const auto node_id = problem.nodes.size();
  problem.nodes.push_back(TreeNode{.kind = kind, .name = Design::getPinFullName(pin)});
  node_by_pin[pin] = node_id;
  return node_id;
}

auto updateClockLayoutInstMaster(ClockLayout& clock_layout, const std::string& inst_name, const std::string& cell_master) -> void
{
  for (auto& layout_clock : clock_layout.get_clocks()) {
    for (auto& layout_inst : layout_clock.insts) {
      if (layout_inst.inst_name == inst_name) {
        layout_inst.cell_master = cell_master;
      }
    }
  }
}

auto canRenamePin(Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  const auto* inst = pin->get_inst();
  const std::string full_name = inst == nullptr ? local_name : inst->get_name() + "/" + local_name;
  auto* existing_pin = DESIGN_INST.findPin(full_name);
  return existing_pin == nullptr || existing_pin == pin;
}

auto renamePin(Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  return DESIGN_INST.renamePin(pin, local_name);
}

auto resolveBufferPorts(const std::string& cell_master) -> std::optional<std::pair<std::string, std::string>>
{
  auto [input_pin_name, output_pin_name] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin_name.empty() || output_pin_name.empty() || input_pin_name == output_pin_name) {
    return std::nullopt;
  }
  return std::make_pair(std::move(input_pin_name), std::move(output_pin_name));
}

auto formatNs(double value) -> std::string
{
  return logformat::FormatWithUnit(value, "ns");
}

auto summarizeTransitions(const std::vector<BufferMutation>& mutations) -> std::map<std::string, std::size_t>
{
  std::map<std::string, std::size_t> counts;
  for (const auto& mutation : mutations) {
    ++counts[mutation.from_master + "->" + mutation.to_master];
  }
  return counts;
}

auto emitClockSummary(const TreeSizingProblem& problem, const TreeSizingSummary& summary, double runtime_s) -> void
{
  schema::KeyValueFields fields = {
      {"clock", problem.clock_name},
      {"runtime", logformat::FormatWithUnit(runtime_s, "s")},
      {"target_skew", formatNs(problem.target_skew_ns)},
      {"before_skew", formatNs(summary.before.skew_ns)},
      {"optimized_skew", formatNs(summary.after.skew_ns)},
      {"improvement", formatNs(summary.before.skew_ns - summary.after.skew_ns)},
      {"iteration_count", std::to_string(summary.iteration_count)},
      {"accepted_mutation_count", std::to_string(summary.accepted_mutation_count)},
      {"rejected_candidate_count", std::to_string(summary.rejected_candidate_count)},
      {"cap_rejected_count", std::to_string(summary.cap_rejected_count)},
      {"total_area_delta", logformat::FormatWithUnit(summary.total_area_delta_um2, "um^2")},
      {"stop_reason", summary.stop_reason.empty() ? "n/a" : summary.stop_reason},
  };
  schema::EmitKeyValueTable("CTS Optimization Clock Summary", fields);

  const auto transition_counts = summarizeTransitions(summary.mutations);
  schema::TableRows rows;
  rows.reserve(transition_counts.size());
  for (const auto& [transition, count] : transition_counts) {
    rows.push_back({transition, std::to_string(count)});
  }
  if (!rows.empty()) {
    schema::EmitTable("CTS Optimization Master Transitions", {"Transition", "Count"}, rows);
  }
}

auto applyMutations(const TreeSizingSummary& summary, const std::vector<Inst*>& inst_by_buffer_id, ClockLayout& clock_layout) -> bool
{
  struct ApplyRecord
  {
    std::size_t buffer_id = buffer_sizing::kInvalidIndex;
    std::string final_master;
  };

  std::vector<std::string> expected_master_by_buffer(inst_by_buffer_id.size());
  for (std::size_t buffer_id = 0U; buffer_id < inst_by_buffer_id.size(); ++buffer_id) {
    expected_master_by_buffer.at(buffer_id)
        = inst_by_buffer_id.at(buffer_id) == nullptr ? std::string{} : inst_by_buffer_id.at(buffer_id)->get_cell_master();
  }
  std::map<std::size_t, ApplyRecord> record_by_buffer;
  for (const auto& mutation : summary.mutations) {
    if (mutation.buffer_id >= inst_by_buffer_id.size() || inst_by_buffer_id.at(mutation.buffer_id) == nullptr) {
      LOG_ERROR << "Optimization: cannot apply mutation for unresolved buffer id " << mutation.buffer_id << ".";
      return false;
    }
    auto* inst = inst_by_buffer_id.at(mutation.buffer_id);
    if (expected_master_by_buffer.at(mutation.buffer_id) != mutation.from_master) {
      LOG_ERROR << "Optimization: cannot apply mutation for inst \"" << inst->get_name() << "\" because current master is \""
                << expected_master_by_buffer.at(mutation.buffer_id) << "\" but solver expected \"" << mutation.from_master << "\".";
      return false;
    }
    expected_master_by_buffer.at(mutation.buffer_id) = mutation.to_master;
    record_by_buffer[mutation.buffer_id] = ApplyRecord{.buffer_id = mutation.buffer_id, .final_master = mutation.to_master};
  }

  for (const auto& [buffer_id, record] : record_by_buffer) {
    auto* inst = inst_by_buffer_id.at(buffer_id);
    auto* input_pin = findSingleBufferInputPin(inst);
    auto* output_pin = inst == nullptr ? nullptr : inst->findDriverPin();
    const auto ports = resolveBufferPorts(record.final_master);
    if (inst == nullptr || input_pin == nullptr || output_pin == nullptr || !ports.has_value() || !canRenamePin(input_pin, ports->first)
        || !canRenamePin(output_pin, ports->second)) {
      LOG_ERROR << "Optimization: cannot apply final master \"" << record.final_master << "\" to buffer inst \""
                << (inst == nullptr ? std::string{"<null>"} : inst->get_name()) << "\" because its pin pair cannot be updated.";
      return false;
    }
  }

  for (const auto& [buffer_id, record] : record_by_buffer) {
    auto* inst = inst_by_buffer_id.at(buffer_id);
    auto* input_pin = findSingleBufferInputPin(inst);
    auto* output_pin = inst->findDriverPin();
    const auto ports = resolveBufferPorts(record.final_master);
    if (!ports.has_value()) {
      return false;
    }
    const std::string old_input_name = input_pin->get_name();
    if (!renamePin(input_pin, ports->first)) {
      return false;
    }
    if (!renamePin(output_pin, ports->second)) {
      LOG_FATAL_IF(!renamePin(input_pin, old_input_name)) << "Optimization: failed to roll back buffer input-pin rename.";
      return false;
    }
    inst->set_cell_master(record.final_master);
    inst->set_type(InstType::kBuffer);
    input_pin->set_type(PinType::kIn);
    output_pin->set_type(PinType::kOut);
    inst->insertDriverPin(output_pin);
    updateClockLayoutInstMaster(clock_layout, inst->get_name(), record.final_master);
  }
  return true;
}

auto buildProblem(const Clock& clock, const std::vector<BufferMasterInfo>& master_infos) -> ProblemBuildResult
{
  ProblemBuildResult result;
  if (master_infos.empty()) {
    result.failure_reason = "no_sizing_candidates";
    return result;
  }
  const auto* graph = DESIGN_INST.get_clock_dag().graphForClock(&clock);
  if (graph == nullptr) {
    result.failure_reason = "clock_dag_unavailable";
    return result;
  }
  auto* source = clock.get_clock_source();
  if (source == nullptr) {
    result.failure_reason = "clock_source_missing";
    return result;
  }

  auto& problem = result.problem;
  problem.clock_name = clock.get_clock_name();
  problem.root_node_id = 0U;
  problem.source_input_slew_ns = std::max(0.0, CONFIG_INST.get_root_input_slew());
  problem.target_skew_ns = std::max(0.0, CONFIG_INST.get_skew_bound());
  problem.nodes.reserve(graph->pins.size());
  problem.nets.reserve(graph->nets.size());
  problem.buffers.reserve(clock.get_insts().size());

  std::unordered_map<const Pin*, std::size_t> node_by_pin;
  std::unordered_map<const Net*, std::size_t> net_by_cts_net;
  std::unordered_map<const Net*, RoutedNetLengthInfo> routed_length_by_net;
  const auto root_node_id = appendNodeIfMissing(problem, node_by_pin, source, TreeNodeKind::kSource);
  problem.root_node_id = root_node_id;

  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  const int routing_layer = resolveRoutingLayer();
  const auto wire_width = resolveWireWidth();

  for (auto* pin : graph->topological_pins) {
    if (pin == nullptr) {
      continue;
    }
    const auto* inst = pin->get_inst();
    TreeNodeKind kind = TreeNodeKind::kSink;
    if (pin == source) {
      kind = TreeNodeKind::kSource;
    } else if (inst != nullptr && inst->is_buffer() && pin == inst->findDriverPin()) {
      kind = TreeNodeKind::kBuffer;
    } else if (inst != nullptr && inst->is_buffer()) {
      continue;
    }
    const auto node_id = appendNodeIfMissing(problem, node_by_pin, pin, kind);
    problem.nodes.at(node_id).kind = kind;
    if (kind == TreeNodeKind::kSink) {
      problem.nodes.at(node_id).sink_pin_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(pin);
    }
  }

  for (auto* pin : graph->topological_pins) {
    if (pin == nullptr || pin->get_inst() == nullptr || !pin->get_inst()->is_buffer() || pin != pin->get_inst()->findDriverPin()) {
      continue;
    }
    auto* input_pin = findSingleBufferInputPin(pin->get_inst());
    if (input_pin == nullptr) {
      result.failure_reason = "buffer_input_missing";
      return result;
    }
    const auto node_iter = node_by_pin.find(pin);
    if (node_iter == node_by_pin.end()) {
      continue;
    }
    const auto buffer_id = problem.buffers.size();
    std::size_t current_candidate_index = buffer_sizing::kInvalidIndex;
    auto candidates = buildCandidates(pin->get_inst()->get_cell_master(), master_infos, current_candidate_index);
    if (candidates.empty() || current_candidate_index == buffer_sizing::kInvalidIndex) {
      continue;
    }
    problem.nodes.at(node_iter->second).buffer_id = buffer_id;
    problem.buffers.push_back(TreeBuffer{.node_id = node_iter->second,
                                         .inst_name = pin->get_inst()->get_name(),
                                         .current_master = pin->get_inst()->get_cell_master(),
                                         .candidates = std::move(candidates),
                                         .current_candidate_index = current_candidate_index});
    result.inst_by_buffer_id.push_back(pin->get_inst());
  }

  for (const auto& [from_pin, arcs] : graph->outgoing_arcs) {
    if (from_pin == nullptr || arcs.empty()) {
      continue;
    }
    const auto driver_node_iter = node_by_pin.find(from_pin);
    if (driver_node_iter == node_by_pin.end()) {
      continue;
    }
    for (const auto& arc : arcs) {
      if (arc.net == nullptr || arc.to == nullptr) {
        continue;
      }
      std::size_t net_id = buffer_sizing::kInvalidIndex;
      const auto net_iter = net_by_cts_net.find(arc.net);
      if (net_iter == net_by_cts_net.end()) {
        net_id = problem.nets.size();
        net_by_cts_net.emplace(arc.net, net_id);
        problem.nets.push_back(TreeNet{.name = arc.net->get_name(),
                                       .driver_node_id = driver_node_iter->second,
                                       .arcs = {},
                                       .wire_cap_pf = 0.0,
                                       .fixed_load_cap_pf = 0.0,
                                       .baseline_load_cap_pf = 0.0,
                                       .max_cap_pf = 0.0});
      } else {
        net_id = net_iter->second;
      }
      auto& net = problem.nets.at(net_id);
      net.driver_node_id = driver_node_iter->second;
      problem.nodes.at(driver_node_iter->second).output_net_id = net_id;

      auto routed_length_iter = routed_length_by_net.find(arc.net);
      if (routed_length_iter == routed_length_by_net.end()) {
        routed_length_iter = routed_length_by_net.emplace(arc.net, buildRoutedNetLengthInfo(*arc.net, dbu_per_um)).first;
      }
      const auto& routed_length_info = routed_length_iter->second;
      double arc_length_um = pointDistanceUm(*from_pin, *arc.to, dbu_per_um);
      if (routed_length_info.available) {
        const auto path_iter = routed_length_info.path_length_by_load_pin.find(arc.to);
        if (path_iter != routed_length_info.path_length_by_load_pin.end() && path_iter->second > 0.0) {
          arc_length_um = path_iter->second;
        }
      }

      auto child_kind = TreeNodeKind::kSink;
      if (auto* child_inst = arc.to->get_inst(); child_inst != nullptr && child_inst->is_buffer()) {
        if (auto* output_pin = child_inst->findDriverPin(); output_pin != nullptr) {
          const auto child_node_id = appendNodeIfMissing(problem, node_by_pin, output_pin, TreeNodeKind::kBuffer);
          problem.nodes.at(child_node_id).parent_id = driver_node_iter->second;
          problem.nodes.at(child_node_id).incoming_net_id = net_id;
          net.arcs.push_back(buffer_sizing::TreeArc{.child_node_id = child_node_id, .length_um = arc_length_um});
          child_kind = TreeNodeKind::kBuffer;
        }
      }
      if (child_kind != TreeNodeKind::kBuffer) {
        const auto child_node_id = appendNodeIfMissing(problem, node_by_pin, arc.to, TreeNodeKind::kSink);
        problem.nodes.at(child_node_id).parent_id = driver_node_iter->second;
        problem.nodes.at(child_node_id).incoming_net_id = net_id;
        problem.nodes.at(child_node_id).sink_pin_cap_pf = STA_ADAPTER_INST.queryPinCapacitance(arc.to);
        net.arcs.push_back(buffer_sizing::TreeArc{.child_node_id = child_node_id, .length_um = arc_length_um});
      }
    }
  }

  for (auto& net : problem.nets) {
    double wire_length_um = 0.0;
    for (const auto& arc : net.arcs) {
      wire_length_um += std::max(0.0, arc.length_um);
    }
    auto* cts_net = DESIGN_INST.findNet(net.name);
    if (cts_net != nullptr) {
      const auto routed_length_iter = routed_length_by_net.find(cts_net);
      if (routed_length_iter != routed_length_by_net.end() && routed_length_iter->second.available
          && routed_length_iter->second.total_length_um > 0.0) {
        wire_length_um = routed_length_iter->second.total_length_um;
      }
    }
    net.wire_cap_pf = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, wire_length_um, wire_width);
    net.baseline_load_cap_pf = net.wire_cap_pf;
    for (const auto& arc : net.arcs) {
      const auto& child = problem.nodes.at(arc.child_node_id);
      if (child.kind == TreeNodeKind::kBuffer && child.buffer_id < problem.buffers.size()) {
        const auto& buffer = problem.buffers.at(child.buffer_id);
        net.baseline_load_cap_pf += buffer.candidates.at(buffer.current_candidate_index).input_cap_pf;
      } else if (child.kind == TreeNodeKind::kSink) {
        net.baseline_load_cap_pf += child.sink_pin_cap_pf;
      }
    }
    net.max_cap_pf = CONFIG_INST.has_max_cap() && CONFIG_INST.get_max_cap() > 0.0 ? CONFIG_INST.get_max_cap() : 0.0;
  }

  if (problem.buffers.empty()) {
    result.failure_reason = "no_resizable_buffers";
    return result;
  }
  result.success = true;
  return result;
}

}  // namespace

auto Optimization::run(ClockLayout& clock_layout, CharacterizationLibrary& char_library) -> OptimizationResult
{
  OptimizationResult result;
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("optimization");
  auto stage = SCHEMA_WRITER_INST.beginStage("Optimization", "Optimize synthesized CTS buffers", {},
                                             schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Optimization Overview");

  const auto start_time = std::chrono::steady_clock::now();
  auto char_lookup = char_library.isReady() ? CharTimingLookup::buildFromCharBuilder(char_library.getCharBuilder()) : CharTimingLookup{};
  if (!char_lookup.isReady()) {
    const auto runtime_options = CharacterizationLibrary::buildRuntimeOptions();
    const auto ensure_result = char_library.ensure(runtime_options);
    char_lookup = char_library.isReady() ? CharTimingLookup::buildFromCharBuilder(char_library.getCharBuilder()) : CharTimingLookup{};
    if (!ensure_result.success || !char_lookup.isReady()) {
      LOG_WARNING << "Optimization: skip because characterization lookup is unavailable.";
      schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "Optimization",
                             "post-synthesis buffer sizing skipped because characterization lookup is unavailable.",
                             {{"reason", ensure_result.failure_reason.empty() ? "char_lookup_not_ready" : ensure_result.failure_reason}});
      (void) runtime.finish("skipped");
      stage.skip({{"reason", "char_lookup_not_ready"}});
      return result;
    }
  }

  const auto master_infos = collectBufferMasterInfos();
  const auto clocks = DESIGN_INST.get_clocks();
  result.clock_count = clocks.size();
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    const auto clock_start = std::chrono::steady_clock::now();
    auto problem_result = buildProblem(*clock, master_infos);
    if (!problem_result.success) {
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because " << problem_result.failure_reason << ".";
      continue;
    }
    auto summary = buffer_sizing::TreeBufferSizing::solve(problem_result.problem, char_lookup);
    const auto clock_end = std::chrono::steady_clock::now();
    const double clock_runtime_s = std::chrono::duration<double>(clock_end - clock_start).count();
    if (!summary.valid) {
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because solver failed with reason "
                  << summary.stop_reason << ".";
      continue;
    }
    if (!summary.mutations.empty() && !applyMutations(summary, problem_result.inst_by_buffer_id, clock_layout)) {
      result.success = false;
      (void) runtime.failed();
      stage.failed({{"reason", "mutation_apply_failed"}});
      return result;
    }
    emitClockSummary(problem_result.problem, summary, clock_runtime_s);
    result.optimized = result.optimized || !summary.mutations.empty();
    result.optimized_clock_count += summary.mutations.empty() ? 0U : 1U;
    result.accepted_mutation_count += summary.accepted_mutation_count;
  }

  const auto end_time = std::chrono::steady_clock::now();
  const double total_runtime_s = std::chrono::duration<double>(end_time - start_time).count();
  schema::EmitKeyValueTable("CTS Optimization Summary", {
                                                            {"runtime", logformat::FormatWithUnit(total_runtime_s, "s")},
                                                            {"clock_count", std::to_string(result.clock_count)},
                                                            {"optimized_clock_count", std::to_string(result.optimized_clock_count)},
                                                            {"accepted_mutation_count", std::to_string(result.accepted_mutation_count)},
                                                            {"status", result.optimized ? "optimized" : "no_op"},
                                                        });
  LOG_INFO << "CTS optimization finished with " << result.accepted_mutation_count << " accepted sizing mutations across "
           << result.optimized_clock_count << " clocks.";
  if (result.optimized) {
    (void) runtime.finished();
    stage.finished({{"accepted_mutation_count", std::to_string(result.accepted_mutation_count)}});
  } else {
    (void) runtime.finish("no_op");
    stage.skip({{"reason", "no_improving_candidate"}});
  }
  return result;
}

}  // namespace icts
