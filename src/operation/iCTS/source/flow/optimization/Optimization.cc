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
#include <stdlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastStaAdapter.hh"
#include "FastStaBuilder.hh"
#include "FastStaTypes.hh"
#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "router/Router.hh"

namespace icts {
namespace {

constexpr double kEpsilon = 1e-12;
constexpr unsigned kMaxOptimizationIterations = 64U;
constexpr unsigned kMaxOptimizationTrials = 20000U;
constexpr std::size_t kMaxFrontierSinks = 64U;
constexpr std::size_t kMaxBatchActions = 16U;
constexpr std::size_t kMaxBatchTrialsPerIteration = 1600U;

struct BufferMasterInfo
{
  std::string cell_master;
  double input_cap_pf = 0.0;
  double output_cap_limit_pf = 0.0;
  double area_um2 = 0.0;
  unsigned drive_rank = 0U;
};

struct OptimizableBuffer
{
  FastStaNodeId node_id = kInvalidFastStaNodeId;
  Inst* inst = nullptr;
  std::string inst_name;
  std::string current_master;
  std::vector<BufferMasterInfo> candidates;
};

struct CapBaseline
{
  double load_cap_pf = 0.0;
  double max_cap_pf = 0.0;
  bool violated = false;
};

struct CapCheckResult
{
  bool legal = true;
  std::size_t violation_count = 0U;
};

struct SlewBaseline
{
  double slew_ns = 0.0;
  double max_slew_ns = 0.0;
  bool available = false;
  bool violated = false;
};

struct SlewCheckResult
{
  bool legal = true;
  std::size_t violation_count = 0U;
};

struct FastState
{
  bool valid = false;
  FastStaSkewSummary skew;
  FastStaPowerSummary power;
  CapCheckResult cap;
  SlewCheckResult slew;
};

struct SizingAction
{
  std::size_t buffer_index = 0U;
  std::string from_master;
  std::string to_master;
  int drive_step = 0;
  double area_delta_um2 = 0.0;
};

struct BatchTrial
{
  bool valid = false;
  std::vector<SizingAction> actions;
  FastState state;
};

struct OptimizationMutation
{
  std::string inst_name;
  std::string from_master;
  std::string to_master;
  double area_delta_um2 = 0.0;
};

struct ClockOptimizationSummary
{
  bool valid = false;
  bool target_met = false;
  bool changed = false;
  std::string stop_reason;
  FastState before;
  FastState after;
  unsigned iteration_count = 0U;
  unsigned trial_count = 0U;
  unsigned batch_trial_count = 0U;
  unsigned accepted_mutation_count = 0U;
  unsigned accepted_batch_count = 0U;
  unsigned rejected_candidate_count = 0U;
  unsigned cap_rejected_count = 0U;
  unsigned slew_rejected_count = 0U;
  std::vector<OptimizationMutation> mutations;
};

struct TopologyIndex
{
  std::vector<FastStaNodeId> parent_by_node;
  std::unordered_map<FastStaNodeId, FastStaNodeId> buffer_input_by_output;
  std::unordered_map<FastStaNodeId, std::size_t> buffer_index_by_output_node;
};

enum class FrontierSide
{
  kLate,
  kEarly
};

using RouteTreeCache = std::unordered_map<const Net*, Router::ClockSteinerTreeType>;

auto formatNs(double value) -> std::string
{
  return logformat::FormatWithUnit(value, "ns");
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
    if (std::abs(lhs.output_cap_limit_pf - rhs.output_cap_limit_pf) > kEpsilon) {
      return lhs.output_cap_limit_pf < rhs.output_cap_limit_pf;
    }
    if (std::abs(lhs.area_um2 - rhs.area_um2) > kEpsilon) {
      return lhs.area_um2 < rhs.area_um2;
    }
    return lhs.cell_master < rhs.cell_master;
  });
  for (std::size_t index = 0U; index < infos.size(); ++index) {
    infos.at(index).drive_rank = static_cast<unsigned>(index);
  }
  return infos;
}

auto buildRouteTreeCache(const std::vector<Clock*>& clocks) -> RouteTreeCache
{
  RouteTreeCache route_tree_by_net;
  const auto& clock_dag = DESIGN_INST.get_clock_dag();
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    const auto* graph = clock_dag.graphForClock(clock);
    if (graph == nullptr) {
      continue;
    }
    for (auto* net : graph->nets) {
      if (net == nullptr || route_tree_by_net.contains(net)) {
        continue;
      }
      route_tree_by_net.emplace(net, Router::buildClockNetTree(*net));
    }
  }
  return route_tree_by_net;
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

auto driveStep(const BufferMasterInfo& from, const BufferMasterInfo& to) -> int
{
  return static_cast<int>(to.drive_rank) - static_cast<int>(from.drive_rank);
}

auto findMasterInfo(const std::vector<BufferMasterInfo>& master_infos, std::string_view cell_master) -> const BufferMasterInfo*
{
  const auto iter
      = std::ranges::find_if(master_infos, [cell_master](const BufferMasterInfo& info) -> bool { return info.cell_master == cell_master; });
  return iter == master_infos.end() ? nullptr : &(*iter);
}

auto collectCapBaseline(FastStaClockId clock_id) -> std::vector<CapBaseline>
{
  std::vector<CapBaseline> baseline;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return baseline;
  }
  baseline.reserve(context->nets.size());
  for (FastStaNetId net_id = 0U; net_id < context->nets.size(); ++net_id) {
    const auto cap_status = FastStaAdapter::queryCapStatus(clock_id, net_id);
    baseline.push_back(CapBaseline{.load_cap_pf = cap_status.has_value() ? cap_status->load_cap_pf : 0.0,
                                   .max_cap_pf = cap_status.has_value() ? cap_status->max_cap_pf : 0.0,
                                   .violated = cap_status.has_value() && cap_status->violated});
  }
  return baseline;
}

auto collectSlewBaseline(FastStaClockId clock_id) -> std::vector<SlewBaseline>
{
  std::vector<SlewBaseline> baseline;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return baseline;
  }
  baseline.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto slew_status = FastStaAdapter::querySlewStatus(clock_id, node_id);
    baseline.push_back(SlewBaseline{.slew_ns = slew_status.has_value() ? slew_status->slew_ns : 0.0,
                                    .max_slew_ns = slew_status.has_value() ? slew_status->max_slew_ns : 0.0,
                                    .available = slew_status.has_value(),
                                    .violated = slew_status.has_value() && slew_status->violated});
  }
  return baseline;
}

auto checkCapLegality(FastStaClockId clock_id, const std::vector<CapBaseline>& baseline) -> CapCheckResult
{
  CapCheckResult result;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    result.legal = false;
    return result;
  }
  for (FastStaNetId net_id = 0U; net_id < context->nets.size(); ++net_id) {
    const auto cap_status = FastStaAdapter::queryCapStatus(clock_id, net_id);
    if (!cap_status.has_value()) {
      result.legal = false;
      ++result.violation_count;
      continue;
    }
    if (cap_status->max_cap_pf <= 0.0) {
      continue;
    }
    const auto baseline_load = net_id < baseline.size() ? baseline.at(net_id).load_cap_pf : 0.0;
    const auto baseline_violated = net_id < baseline.size() && baseline.at(net_id).violated;
    const bool legal = baseline_violated ? cap_status->load_cap_pf <= baseline_load + kEpsilon : !cap_status->violated;
    if (!legal) {
      result.legal = false;
      ++result.violation_count;
    }
  }
  return result;
}

auto checkSlewLegality(FastStaClockId clock_id, const std::vector<SlewBaseline>& baseline) -> SlewCheckResult
{
  SlewCheckResult result;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    result.legal = false;
    return result;
  }
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto slew_status = FastStaAdapter::querySlewStatus(clock_id, node_id);
    const auto baseline_available = node_id < baseline.size() && baseline.at(node_id).available;
    auto max_slew_ns = 0.0;
    if (slew_status.has_value()) {
      max_slew_ns = slew_status->max_slew_ns;
    } else if (baseline_available) {
      max_slew_ns = baseline.at(node_id).max_slew_ns;
    }
    if (max_slew_ns <= 0.0) {
      continue;
    }
    if (!slew_status.has_value()) {
      result.legal = false;
      ++result.violation_count;
      continue;
    }
    const auto baseline_slew = baseline_available ? baseline.at(node_id).slew_ns : 0.0;
    const auto baseline_violated = baseline_available && baseline.at(node_id).violated;
    const bool legal = baseline_violated ? slew_status->slew_ns <= baseline_slew + kEpsilon : !slew_status->violated;
    if (!legal) {
      result.legal = false;
      ++result.violation_count;
    }
  }
  return result;
}

auto captureState(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline, const std::vector<SlewBaseline>& slew_baseline)
    -> FastState
{
  FastState state;
  state.skew = FastStaAdapter::querySkew(clock_id);
  state.power = FastStaAdapter::queryPower(clock_id);
  state.cap = checkCapLegality(clock_id, cap_baseline);
  state.slew = checkSlewLegality(clock_id, slew_baseline);
  state.valid = state.skew.valid && state.cap.legal && state.slew.legal;
  return state;
}

auto targetMet(const FastState& state, double target_skew_ns) -> bool
{
  return state.valid && state.skew.skew_ns <= target_skew_ns + kEpsilon;
}

auto stateImproves(const FastState& current, const FastState& candidate, double target_skew_ns) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  const bool current_met = targetMet(current, target_skew_ns);
  const bool candidate_met = targetMet(candidate, target_skew_ns);
  if (current_met) {
    if (!candidate_met) {
      return false;
    }
    if (candidate.power.area_um2 < current.power.area_um2 - kEpsilon) {
      return true;
    }
    return std::abs(candidate.power.area_um2 - current.power.area_um2) <= kEpsilon
           && candidate.skew.skew_ns < current.skew.skew_ns - kEpsilon;
  }
  if (candidate_met) {
    return true;
  }
  return candidate.skew.skew_ns < current.skew.skew_ns - kEpsilon;
}

auto actionDriveMagnitude(const std::vector<SizingAction>& actions) -> int
{
  int magnitude = 0;
  for (const auto& action : actions) {
    magnitude += std::abs(action.drive_step);
  }
  return magnitude;
}

auto firstActionBufferIndex(const std::vector<SizingAction>& actions) -> std::size_t
{
  auto first_index = std::numeric_limits<std::size_t>::max();
  for (const auto& action : actions) {
    first_index = std::min(first_index, action.buffer_index);
  }
  return first_index;
}

auto preferTrial(const BatchTrial& candidate, const BatchTrial& incumbent, const FastState& current, double target_skew_ns) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  if (!incumbent.valid) {
    return true;
  }

  const bool current_met = targetMet(current, target_skew_ns);
  const bool candidate_met = targetMet(candidate.state, target_skew_ns);
  const bool incumbent_met = targetMet(incumbent.state, target_skew_ns);

  if (!current_met && candidate_met != incumbent_met) {
    return candidate_met;
  }
  if (current_met || candidate_met) {
    if (candidate.state.power.area_um2 < incumbent.state.power.area_um2 - kEpsilon) {
      return true;
    }
    if (std::abs(candidate.state.power.area_um2 - incumbent.state.power.area_um2) > kEpsilon) {
      return false;
    }
  }

  if (candidate.state.skew.skew_ns < incumbent.state.skew.skew_ns - kEpsilon) {
    return true;
  }
  if (std::abs(candidate.state.skew.skew_ns - incumbent.state.skew.skew_ns) > kEpsilon) {
    return false;
  }
  if (candidate.state.power.area_um2 < incumbent.state.power.area_um2 - kEpsilon) {
    return true;
  }
  if (std::abs(candidate.state.power.area_um2 - incumbent.state.power.area_um2) > kEpsilon) {
    return false;
  }
  if (candidate.actions.size() != incumbent.actions.size()) {
    return candidate.actions.size() < incumbent.actions.size();
  }
  const auto candidate_drive_magnitude = actionDriveMagnitude(candidate.actions);
  const auto incumbent_drive_magnitude = actionDriveMagnitude(incumbent.actions);
  if (candidate_drive_magnitude != incumbent_drive_magnitude) {
    return candidate_drive_magnitude < incumbent_drive_magnitude;
  }
  return firstActionBufferIndex(candidate.actions) < firstActionBufferIndex(incumbent.actions);
}

auto changeFastStaMasters(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!FastStaAdapter::changeBufferMasters(clock_id, changes)) {
    return false;
  }
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  return context != nullptr && context->timing_valid && context->power_valid;
}

auto buildMasterChanges(const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions, bool restore)
    -> std::vector<FastStaBufferMasterChange>
{
  std::vector<FastStaBufferMasterChange> changes;
  changes.reserve(actions.size());
  for (const auto& action : actions) {
    if (action.buffer_index >= buffers.size()) {
      continue;
    }
    changes.push_back(FastStaBufferMasterChange{
        .node_id = buffers.at(action.buffer_index).node_id,
        .cell_master = restore ? action.from_master : action.to_master,
    });
  }
  return changes;
}

auto tryBatch(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions,
              const FastState& current, const std::vector<CapBaseline>& cap_baseline, const std::vector<SlewBaseline>& slew_baseline,
              double target_skew_ns) -> BatchTrial
{
  BatchTrial trial;
  trial.actions = actions;
  if (trial.actions.empty()) {
    return trial;
  }

  if (!changeFastStaMasters(clock_id, buildMasterChanges(buffers, trial.actions, false))) {
    return trial;
  }
  trial.state = captureState(clock_id, cap_baseline, slew_baseline);
  trial.valid = stateImproves(current, trial.state, target_skew_ns);
  if (!changeFastStaMasters(clock_id, buildMasterChanges(buffers, trial.actions, true))) {
    LOG_FATAL << "Optimization: failed to restore fast STA batch trial.";
  }
  return trial;
}

auto collectOptimizableBuffers(FastStaClockId clock_id, const std::vector<BufferMasterInfo>& master_infos) -> std::vector<OptimizableBuffer>
{
  std::vector<OptimizableBuffer> buffers;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr || master_infos.empty()) {
    return buffers;
  }
  buffers.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kBufferOutput || node.inst_name.empty() || node.cell_master.empty()) {
      continue;
    }
    auto* inst = DESIGN_INST.findInst(node.inst_name);
    if (inst == nullptr || !inst->is_buffer() || findMasterInfo(master_infos, node.cell_master) == nullptr) {
      continue;
    }
    buffers.push_back(OptimizableBuffer{
        .node_id = node_id, .inst = inst, .inst_name = node.inst_name, .current_master = node.cell_master, .candidates = master_infos});
  }
  return buffers;
}

auto buildTopologyIndex(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> TopologyIndex
{
  TopologyIndex topology;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return topology;
  }

  topology.parent_by_node.assign(context->nodes.size(), kInvalidFastStaNodeId);
  std::unordered_map<std::string, FastStaNodeId> input_by_inst;
  input_by_inst.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kBufferInput && !node.inst_name.empty()) {
      input_by_inst[node.inst_name] = node_id;
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind == FastStaNodeKind::kBufferOutput) {
      const auto input_iter = input_by_inst.find(node.inst_name);
      if (input_iter != input_by_inst.end()) {
        topology.parent_by_node.at(node_id) = input_iter->second;
        topology.buffer_input_by_output[node_id] = input_iter->second;
      }
      continue;
    }
    if (node.incoming_net_id < context->nets.size()) {
      topology.parent_by_node.at(node_id) = context->nets.at(node.incoming_net_id).driver_node_id;
    }
  }
  for (std::size_t buffer_index = 0U; buffer_index < buffers.size(); ++buffer_index) {
    topology.buffer_index_by_output_node[buffers.at(buffer_index).node_id] = buffer_index;
  }
  return topology;
}

auto collectFrontierSinks(FastStaClockId clock_id, const FastState& current, FrontierSide side) -> std::vector<FastStaNodeId>
{
  std::vector<std::pair<FastStaNodeId, double>> sinks;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr || !current.skew.valid) {
    return {};
  }

  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kSink || !node.timing.valid) {
      continue;
    }
    sinks.emplace_back(node_id, node.timing.arrival_ns);
  }
  if (sinks.empty()) {
    const auto fallback_id = side == FrontierSide::kLate ? current.skew.max_sink_node_id : current.skew.min_sink_node_id;
    if (fallback_id < context->nodes.size()) {
      sinks.emplace_back(fallback_id, context->nodes.at(fallback_id).timing.arrival_ns);
    }
  }

  std::ranges::sort(sinks, [context, side](const auto& lhs, const auto& rhs) -> bool {
    if (std::abs(lhs.second - rhs.second) > kEpsilon) {
      return side == FrontierSide::kLate ? lhs.second > rhs.second : lhs.second < rhs.second;
    }
    return context->nodes.at(lhs.first).name < context->nodes.at(rhs.first).name;
  });
  if (sinks.size() > kMaxFrontierSinks) {
    sinks.resize(kMaxFrontierSinks);
  }

  std::vector<FastStaNodeId> sink_ids;
  sink_ids.reserve(sinks.size());
  for (const auto& [node_id, _] : sinks) {
    sink_ids.push_back(node_id);
  }
  return sink_ids;
}

auto collectPathBufferIndices(const TopologyIndex& topology, FastStaNodeId sink_node_id) -> std::vector<std::size_t>
{
  std::vector<std::size_t> path;
  FastStaNodeId node_id = sink_node_id;
  std::unordered_set<FastStaNodeId> visited;
  while (node_id < topology.parent_by_node.size() && !visited.contains(node_id)) {
    visited.insert(node_id);
    const auto buffer_iter = topology.buffer_index_by_output_node.find(node_id);
    if (buffer_iter != topology.buffer_index_by_output_node.end()) {
      path.push_back(buffer_iter->second);
    }
    node_id = topology.parent_by_node.at(node_id);
  }
  std::ranges::reverse(path);
  return path;
}

auto makeSizingAction(const std::vector<OptimizableBuffer>& buffers, std::size_t buffer_index, FrontierSide side, unsigned rank_step)
    -> std::optional<SizingAction>
{
  if (buffer_index >= buffers.size() || rank_step == 0U) {
    return std::nullopt;
  }
  const auto& buffer = buffers.at(buffer_index);
  const auto* from = findMasterInfo(buffer.candidates, buffer.current_master);
  if (from == nullptr) {
    return std::nullopt;
  }
  const auto from_rank = static_cast<int>(from->drive_rank);
  const auto target_rank = side == FrontierSide::kLate ? from_rank + static_cast<int>(rank_step) : from_rank - static_cast<int>(rank_step);
  if (target_rank < 0 || static_cast<std::size_t>(target_rank) >= buffer.candidates.size()) {
    return std::nullopt;
  }
  const auto& to = buffer.candidates.at(static_cast<std::size_t>(target_rank));
  if (to.cell_master == from->cell_master) {
    return std::nullopt;
  }
  return SizingAction{.buffer_index = buffer_index,
                      .from_master = from->cell_master,
                      .to_master = to.cell_master,
                      .drive_step = driveStep(*from, to),
                      .area_delta_um2 = to.area_um2 - from->area_um2};
}

auto appendBatchCandidate(std::vector<std::vector<SizingAction>>& candidates, std::unordered_set<std::string>& seen,
                          std::vector<SizingAction> actions) -> void
{
  if (actions.empty() || candidates.size() >= kMaxBatchTrialsPerIteration) {
    return;
  }
  std::ranges::sort(actions, [](const SizingAction& lhs, const SizingAction& rhs) -> bool {
    if (lhs.buffer_index != rhs.buffer_index) {
      return lhs.buffer_index < rhs.buffer_index;
    }
    return lhs.to_master < rhs.to_master;
  });

  std::vector<SizingAction> compact;
  compact.reserve(std::min(actions.size(), kMaxBatchActions));
  std::unordered_set<std::size_t> used_buffers;
  for (const auto& action : actions) {
    if (used_buffers.contains(action.buffer_index)) {
      continue;
    }
    used_buffers.insert(action.buffer_index);
    compact.push_back(action);
    if (compact.size() >= kMaxBatchActions) {
      break;
    }
  }
  if (compact.empty()) {
    return;
  }

  std::string key;
  for (const auto& action : compact) {
    key += std::to_string(action.buffer_index);
    key += ':';
    key += action.to_master;
    key += ';';
  }
  if (!seen.insert(key).second) {
    return;
  }
  candidates.push_back(std::move(compact));
}

auto generatePathSegmentBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::size_t>& path, FrontierSide side,
                                unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                std::unordered_set<std::string>& seen) -> void
{
  constexpr std::array<std::size_t, 6U> segment_lengths = {1U, 2U, 4U, 6U, 8U, 12U};
  for (std::size_t start = 0U; start < path.size() && candidates.size() < kMaxBatchTrialsPerIteration; ++start) {
    for (const auto length : segment_lengths) {
      std::vector<SizingAction> actions;
      for (std::size_t offset = 0U; offset < length && start + offset < path.size(); ++offset) {
        auto action = makeSizingAction(buffers, path.at(start + offset), side, rank_step);
        if (action.has_value()) {
          actions.push_back(std::move(*action));
        }
      }
      appendBatchCandidate(candidates, seen, std::move(actions));
      if (candidates.size() >= kMaxBatchTrialsPerIteration) {
        break;
      }
    }
  }
}

auto generateFrontierLevelBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::vector<std::size_t>>& paths,
                                  FrontierSide side, unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                  std::unordered_set<std::string>& seen) -> void
{
  std::size_t max_path_size = 0U;
  for (const auto& path : paths) {
    max_path_size = std::max(max_path_size, path.size());
  }
  for (std::size_t depth_index = 0U; depth_index < max_path_size && candidates.size() < kMaxBatchTrialsPerIteration; ++depth_index) {
    std::vector<SizingAction> actions;
    for (const auto& path : paths) {
      if (depth_index >= path.size()) {
        continue;
      }
      auto action = makeSizingAction(buffers, path.at(depth_index), side, rank_step);
      if (action.has_value()) {
        actions.push_back(std::move(*action));
      }
    }
    appendBatchCandidate(candidates, seen, std::move(actions));
  }
}

auto generateFrontierPrefixBatches(const std::vector<OptimizableBuffer>& buffers, const std::vector<std::vector<std::size_t>>& paths,
                                   FrontierSide side, unsigned rank_step, std::vector<std::vector<SizingAction>>& candidates,
                                   std::unordered_set<std::string>& seen) -> void
{
  constexpr std::array<std::size_t, 5U> path_counts = {4U, 8U, 16U, 32U, 64U};
  constexpr std::array<std::size_t, 6U> prefix_lengths = {1U, 2U, 3U, 4U, 6U, 8U};
  for (const auto path_count : path_counts) {
    if (candidates.size() >= kMaxBatchTrialsPerIteration) {
      break;
    }
    const auto selected_path_count = std::min(path_count, paths.size());
    if (selected_path_count == 0U) {
      continue;
    }
    for (const auto prefix_length : prefix_lengths) {
      std::vector<SizingAction> actions;
      for (std::size_t path_index = 0U; path_index < selected_path_count && actions.size() < kMaxBatchActions; ++path_index) {
        const auto& path = paths.at(path_index);
        for (std::size_t depth_index = 0U; depth_index < prefix_length && depth_index < path.size(); ++depth_index) {
          auto action = makeSizingAction(buffers, path.at(depth_index), side, rank_step);
          if (action.has_value()) {
            actions.push_back(std::move(*action));
          }
        }
      }
      appendBatchCandidate(candidates, seen, std::move(actions));
      if (candidates.size() >= kMaxBatchTrialsPerIteration) {
        break;
      }
    }
  }
}

auto generateBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                             const FastState& current) -> std::vector<std::vector<SizingAction>>
{
  std::vector<std::vector<SizingAction>> candidates;
  std::unordered_set<std::string> seen;
  constexpr std::array<unsigned, 3U> rank_steps = {1U, 2U, 3U};
  for (const auto side : {FrontierSide::kLate, FrontierSide::kEarly}) {
    const auto frontier_sinks = collectFrontierSinks(clock_id, current, side);
    std::vector<std::vector<std::size_t>> paths;
    paths.reserve(frontier_sinks.size());
    for (const auto sink_id : frontier_sinks) {
      auto path = collectPathBufferIndices(topology, sink_id);
      if (!path.empty()) {
        paths.push_back(std::move(path));
      }
    }
    for (const auto rank_step : rank_steps) {
      generateFrontierPrefixBatches(buffers, paths, side, rank_step, candidates, seen);
      generateFrontierLevelBatches(buffers, paths, side, rank_step, candidates, seen);
      for (const auto& path : paths) {
        generatePathSegmentBatches(buffers, path, side, rank_step, candidates, seen);
        if (candidates.size() >= kMaxBatchTrialsPerIteration) {
          break;
        }
      }
      if (candidates.size() >= kMaxBatchTrialsPerIteration) {
        break;
      }
    }
  }
  return candidates;
}

auto injectRouteTrees(FastStaClockId clock_id, const Clock& clock, const RouteTreeCache& route_tree_by_net) -> bool
{
  auto* context = FastStaAdapter::mutableClockContext(clock_id);
  const auto* graph = DESIGN_INST.get_clock_dag().graphForClock(&clock);
  if (context == nullptr || graph == nullptr) {
    return false;
  }
  for (auto* net : graph->nets) {
    if (net == nullptr) {
      continue;
    }
    const auto route_iter = route_tree_by_net.find(net);
    if (route_iter == route_tree_by_net.end()) {
      LOG_ERROR << "Optimization: route tree is unavailable for net \"" << net->get_name() << "\".";
      return false;
    }
    if (!FastStaBuilder::injectNetRouteTree(*context, *net, route_iter->second)) {
      LOG_ERROR << "Optimization: fast STA route-tree injection failed for net \"" << net->get_name() << "\".";
      return false;
    }
  }
  return FastStaAdapter::updateTiming(clock_id) && FastStaAdapter::updatePower(clock_id);
}

auto applyMutations(const std::vector<OptimizationMutation>& mutations, const std::vector<OptimizableBuffer>& buffers,
                    ClockLayout& clock_layout) -> bool
{
  std::map<std::string, std::string> final_master_by_inst;
  std::map<std::string, std::string> expected_master_by_inst;
  for (const auto& buffer : buffers) {
    if (buffer.inst != nullptr) {
      expected_master_by_inst[buffer.inst_name] = buffer.inst->get_cell_master();
    }
  }
  for (const auto& mutation : mutations) {
    auto expected_iter = expected_master_by_inst.find(mutation.inst_name);
    if (expected_iter == expected_master_by_inst.end()) {
      LOG_ERROR << "Optimization: cannot apply mutation for unresolved inst \"" << mutation.inst_name << "\".";
      return false;
    }
    if (expected_iter->second != mutation.from_master) {
      LOG_ERROR << "Optimization: cannot apply mutation for inst \"" << mutation.inst_name << "\" because current master is \""
                << expected_iter->second << "\" but solver expected \"" << mutation.from_master << "\".";
      return false;
    }
    expected_iter->second = mutation.to_master;
    final_master_by_inst[mutation.inst_name] = mutation.to_master;
  }

  for (const auto& [inst_name, final_master] : final_master_by_inst) {
    auto* inst = DESIGN_INST.findInst(inst_name);
    auto* input_pin = findSingleBufferInputPin(inst);
    auto* output_pin = inst == nullptr ? nullptr : inst->findDriverPin();
    const auto ports = resolveBufferPorts(final_master);
    if (inst == nullptr || input_pin == nullptr || output_pin == nullptr || !ports.has_value() || !canRenamePin(input_pin, ports->first)
        || !canRenamePin(output_pin, ports->second)) {
      LOG_ERROR << "Optimization: cannot apply final master \"" << final_master << "\" to buffer inst \"" << inst_name
                << "\" because its pin pair cannot be updated.";
      return false;
    }
  }

  for (const auto& [inst_name, final_master] : final_master_by_inst) {
    auto* inst = DESIGN_INST.findInst(inst_name);
    auto* input_pin = findSingleBufferInputPin(inst);
    auto* output_pin = inst->findDriverPin();
    const auto ports = resolveBufferPorts(final_master);
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
    inst->set_cell_master(final_master);
    inst->set_type(InstType::kBuffer);
    input_pin->set_type(PinType::kIn);
    output_pin->set_type(PinType::kOut);
    inst->insertDriverPin(output_pin);
    updateClockLayoutInstMaster(clock_layout, inst->get_name(), final_master);
  }
  return true;
}

auto summarizeTransitions(const std::vector<OptimizationMutation>& mutations) -> std::map<std::string, std::size_t>
{
  std::map<std::string, std::size_t> counts;
  for (const auto& mutation : mutations) {
    ++counts[mutation.from_master + " -> " + mutation.to_master];
  }
  return counts;
}

auto emitClockSummary(const Clock& clock, const ClockOptimizationSummary& summary, double target_skew_ns, double runtime_s) -> void
{
  schema::KeyValueFields fields = {
      {"clock", clock.get_clock_name()},
      {"runtime", logformat::FormatWithUnit(runtime_s, "s")},
      {"target_skew", formatNs(target_skew_ns)},
      {"initial_skew", formatNs(summary.before.skew.skew_ns)},
      {"optimized_skew", formatNs(summary.after.skew.skew_ns)},
      {"improvement", formatNs(summary.before.skew.skew_ns - summary.after.skew.skew_ns)},
      {"initial_area", logformat::FormatWithUnit(summary.before.power.area_um2, "um^2")},
      {"optimized_area", logformat::FormatWithUnit(summary.after.power.area_um2, "um^2")},
      {"area_delta", logformat::FormatWithUnit(summary.after.power.area_um2 - summary.before.power.area_um2, "um^2")},
      {"iteration_count", std::to_string(summary.iteration_count)},
      {"trial_count", std::to_string(summary.trial_count)},
      {"batch_trial_count", std::to_string(summary.batch_trial_count)},
      {"accepted_batch_count", std::to_string(summary.accepted_batch_count)},
      {"accepted_mutation_count", std::to_string(summary.accepted_mutation_count)},
      {"rejected_candidate_count", std::to_string(summary.rejected_candidate_count)},
      {"cap_rejected_count", std::to_string(summary.cap_rejected_count)},
      {"slew_rejected_count", std::to_string(summary.slew_rejected_count)},
      {"cap_legal", summary.after.cap.legal ? "true" : "false"},
      {"slew_legal", summary.after.slew.legal ? "true" : "false"},
      {"target_met", summary.target_met ? "true" : "false"},
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

auto findBestBatchTrial(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                        const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns, ClockOptimizationSummary& summary)
    -> BatchTrial
{
  BatchTrial best;
  const auto candidates = generateBatchCandidates(clock_id, buffers, topology, current);
  for (const auto& actions : candidates) {
    if (summary.trial_count >= kMaxOptimizationTrials) {
      break;
    }
    ++summary.trial_count;
    ++summary.batch_trial_count;
    auto trial = tryBatch(clock_id, buffers, actions, current, cap_baseline, slew_baseline, target_skew_ns);
    if (!trial.state.cap.legal) {
      ++summary.cap_rejected_count;
    }
    if (!trial.state.slew.legal) {
      ++summary.slew_rejected_count;
    }
    if (!trial.valid) {
      ++summary.rejected_candidate_count;
      continue;
    }
    if (preferTrial(trial, best, current, target_skew_ns)) {
      best = std::move(trial);
    }
  }
  return best;
}

auto solveClock(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary
{
  ClockOptimizationSummary summary;
  summary.before = captureState(clock_id, cap_baseline, slew_baseline);
  if (!summary.before.valid) {
    if (!summary.before.skew.valid) {
      summary.stop_reason = "initial_skew_unavailable";
    } else if (!summary.before.cap.legal) {
      summary.stop_reason = "initial_cap_worse_than_baseline";
    } else {
      summary.stop_reason = "initial_slew_worse_than_baseline";
    }
    summary.after = summary.before;
    return summary;
  }

  const auto topology = buildTopologyIndex(clock_id, buffers);
  auto current = summary.before;
  while (summary.iteration_count < kMaxOptimizationIterations && summary.trial_count < kMaxOptimizationTrials) {
    auto best = findBestBatchTrial(clock_id, buffers, topology, current, cap_baseline, slew_baseline, target_skew_ns, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= kMaxOptimizationTrials ? "trial_limit" : "no_improving_candidate";
      break;
    }
    if (!changeFastStaMasters(clock_id, buildMasterChanges(buffers, best.actions, false))) {
      summary.stop_reason = "accepted_mutation_apply_failed";
      break;
    }
    current = captureState(clock_id, cap_baseline, slew_baseline);
    for (const auto& action : best.actions) {
      if (action.buffer_index >= buffers.size()) {
        continue;
      }
      auto& buffer = buffers.at(action.buffer_index);
      summary.mutations.push_back(OptimizationMutation{.inst_name = buffer.inst_name,
                                                       .from_master = action.from_master,
                                                       .to_master = action.to_master,
                                                       .area_delta_um2 = action.area_delta_um2});
      buffer.current_master = action.to_master;
    }
    summary.accepted_mutation_count += static_cast<unsigned>(best.actions.size());
    ++summary.accepted_batch_count;
    ++summary.iteration_count;
  }

  summary.after = current;
  summary.valid = summary.before.valid && summary.after.valid;
  summary.changed = !summary.mutations.empty();
  summary.target_met = targetMet(summary.after, target_skew_ns);
  if (summary.stop_reason.empty()) {
    summary.stop_reason = summary.target_met ? "target_met" : "iteration_limit";
  }
  return summary;
}

}  // namespace

auto Optimization::run(ClockLayout& clock_layout, CharacterizationLibrary& characterization_library) -> OptimizationResult
{
  (void) characterization_library;
  OptimizationResult result;
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("optimization");
  auto stage = SCHEMA_WRITER_INST.beginStage("Optimization", "Optimize synthesized CTS buffers with CTS fast STA", {},
                                             schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Optimization Overview");

  const auto start_time = std::chrono::steady_clock::now();
  const auto clocks = DESIGN_INST.get_clocks();
  result.clock_count = clocks.size();
  const auto master_infos = collectBufferMasterInfos();
  if (master_infos.empty()) {
    LOG_WARNING << "Optimization: skip because no legal buffer sizing candidates are available.";
    (void) runtime.finish("skipped");
    stage.skip({{"reason", "no_sizing_candidates"}});
    return result;
  }

  const auto route_tree_by_net = buildRouteTreeCache(clocks);
  const double target_skew_ns = std::max(0.0, CONFIG_INST.get_skew_bound());
  schema::EmitKeyValueTable("CTS Optimization Setup", {{"timing_source", "cts_fast_sta_incremental"},
                                                       {"target_skew", formatNs(target_skew_ns)},
                                                       {"candidate_master_count", std::to_string(master_infos.size())}});

  std::string no_op_reason = "no_optimizable_clock";
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    const auto clock_start = std::chrono::steady_clock::now();
    const auto clock_id = FastStaAdapter::buildClockContext(*clock, clock_layout, clock_index);
    if (!injectRouteTrees(clock_id, *clock, route_tree_by_net)) {
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA context build failed.";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = "fast_sta_context_failed";
      continue;
    }

    auto buffers = collectOptimizableBuffers(clock_id, master_infos);
    if (buffers.empty()) {
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because no resizable buffers are available.";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = "no_resizable_buffers";
      continue;
    }

    const auto cap_baseline = collectCapBaseline(clock_id);
    const auto slew_baseline = collectSlewBaseline(clock_id);
    auto summary = solveClock(clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns);
    const auto clock_end = std::chrono::steady_clock::now();
    const double clock_runtime_s = std::chrono::duration<double>(clock_end - clock_start).count();
    if (!summary.valid) {
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA solver failed with reason "
                  << summary.stop_reason << ".";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = summary.stop_reason.empty() ? "solver_failed" : summary.stop_reason;
      continue;
    }
    if (!summary.mutations.empty() && !applyMutations(summary.mutations, buffers, clock_layout)) {
      result.success = false;
      (void) runtime.failed();
      stage.failed({{"reason", "mutation_apply_failed"}});
      return result;
    }
    emitClockSummary(*clock, summary, target_skew_ns, clock_runtime_s);
    if (summary.mutations.empty() && !summary.stop_reason.empty()
        && (no_op_reason == "no_optimizable_clock" || no_op_reason == "target_met")) {
      no_op_reason = summary.stop_reason;
    }
    result.optimized = result.optimized || !summary.mutations.empty();
    result.optimized_clock_count += summary.mutations.empty() ? 0U : 1U;
    result.accepted_mutation_count += summary.accepted_mutation_count;
    (void) FastStaAdapter::eraseClockContext(clock_id);
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
    stage.skip({{"reason", no_op_reason}});
  }
  return result;
}

}  // namespace icts
