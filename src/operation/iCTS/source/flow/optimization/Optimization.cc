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
#include "SteinerTree.hh"
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
constexpr unsigned kTrialProgressInterval = 500U;
constexpr unsigned kInitialDetailedTrials = 3U;
constexpr double kSlowTrialLogThresholdS = 5.0;
constexpr std::size_t kScalableNodeThreshold = 50000U;
constexpr std::size_t kScalableBufferThreshold = 5000U;
constexpr std::size_t kMaxScalableBatchActions = 48U;
constexpr std::size_t kMaxScalableExactTrialsPerIteration = 16U;

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

struct OptimizationRuntimeProfile
{
  double build_route_tree_cache_s = 0.0;
  double build_fast_sta_context_s = 0.0;
  double inject_route_trees_s = 0.0;
  double collect_optimizable_buffers_s = 0.0;
  double collect_cap_baseline_s = 0.0;
  double collect_slew_baseline_s = 0.0;
  double solve_clock_s = 0.0;
  double apply_mutations_s = 0.0;
  double capture_initial_state_s = 0.0;
  double build_topology_index_s = 0.0;
  double generate_batch_candidates_s = 0.0;
  double batch_trial_eval_s = 0.0;
  double apply_accepted_batch_s = 0.0;
  std::size_t node_count = 0U;
  std::size_t net_count = 0U;
  std::size_t sink_count = 0U;
  std::size_t buffer_input_count = 0U;
  std::size_t buffer_output_count = 0U;
  std::size_t optimizable_buffer_count = 0U;
  std::size_t generated_candidate_count = 0U;
};

struct ClockOptimizationSummary
{
  bool valid = false;
  bool target_met = false;
  bool changed = false;
  std::string solve_mode;
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
  OptimizationRuntimeProfile profile;
  std::vector<OptimizationMutation> mutations;
};

struct TopologyIndex
{
  std::vector<FastStaNodeId> parent_by_node;
  std::unordered_map<FastStaNodeId, FastStaNodeId> buffer_input_by_output;
  std::unordered_map<FastStaNodeId, std::size_t> buffer_index_by_output_node;
};

struct FrontierCoverage
{
  std::vector<std::size_t> late_count_by_buffer;
  std::vector<std::size_t> early_count_by_buffer;
};

struct ScoredSizingAction
{
  SizingAction action;
  double score = 0.0;
};

struct ScoredBatchCandidate
{
  std::vector<SizingAction> actions;
  double score = 0.0;
};

enum class FrontierSide
{
  kLate,
  kEarly
};

using RouteTreeCache = std::unordered_map<const Net*, Router::ClockSteinerTreeType>;

auto elapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

auto formatNs(double value) -> std::string
{
  return logformat::FormatWithUnit(value, "ns");
}

auto formatSeconds(double value) -> std::string
{
  return logformat::FormatWithUnit(value, "s");
}

auto captureGraphProfile(const FastStaClockContext& context) -> OptimizationRuntimeProfile
{
  OptimizationRuntimeProfile profile;
  profile.node_count = context.nodes.size();
  profile.net_count = context.nets.size();
  for (const auto& node : context.nodes) {
    switch (node.kind) {
      case FastStaNodeKind::kSink:
        ++profile.sink_count;
        break;
      case FastStaNodeKind::kBufferInput:
        ++profile.buffer_input_count;
        break;
      case FastStaNodeKind::kBufferOutput:
        ++profile.buffer_output_count;
        break;
      case FastStaNodeKind::kSource:
        break;
    }
  }
  return profile;
}

auto copyOuterProfile(OptimizationRuntimeProfile& destination, const OptimizationRuntimeProfile& source) -> void
{
  destination.build_route_tree_cache_s = source.build_route_tree_cache_s;
  destination.build_fast_sta_context_s = source.build_fast_sta_context_s;
  destination.inject_route_trees_s = source.inject_route_trees_s;
  destination.collect_optimizable_buffers_s = source.collect_optimizable_buffers_s;
  destination.collect_cap_baseline_s = source.collect_cap_baseline_s;
  destination.collect_slew_baseline_s = source.collect_slew_baseline_s;
  destination.solve_clock_s = source.solve_clock_s;
  destination.apply_mutations_s = source.apply_mutations_s;
  destination.node_count = source.node_count;
  destination.net_count = source.net_count;
  destination.sink_count = source.sink_count;
  destination.buffer_input_count = source.buffer_input_count;
  destination.buffer_output_count = source.buffer_output_count;
  destination.optimizable_buffer_count = source.optimizable_buffer_count;
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

auto captureStateWithArea(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline,
                          const std::vector<SlewBaseline>& slew_baseline, double area_um2) -> FastState
{
  auto state = captureState(clock_id, cap_baseline, slew_baseline);
  state.power.area_um2 = area_um2;
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

auto changeFastStaMastersTimingOnly(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!FastStaAdapter::changeBufferMastersTimingOnly(clock_id, changes)) {
    return false;
  }
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  return context != nullptr && context->timing_valid;
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

auto actionAreaDelta(const std::vector<SizingAction>& actions) -> double
{
  double area_delta_um2 = 0.0;
  for (const auto& action : actions) {
    area_delta_um2 += action.area_delta_um2;
  }
  return area_delta_um2;
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

auto tryBatchTimingOnly(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions,
                        const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> BatchTrial
{
  BatchTrial trial;
  trial.actions = actions;
  if (trial.actions.empty()) {
    return trial;
  }

  if (!changeFastStaMastersTimingOnly(clock_id, buildMasterChanges(buffers, trial.actions, false))) {
    return trial;
  }
  trial.state = captureStateWithArea(clock_id, cap_baseline, slew_baseline, current.power.area_um2 + actionAreaDelta(trial.actions));
  trial.valid = stateImproves(current, trial.state, target_skew_ns);
  if (!changeFastStaMastersTimingOnly(clock_id, buildMasterChanges(buffers, trial.actions, true))) {
    LOG_FATAL << "Optimization: failed to restore fast STA timing-only batch trial.";
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

auto collectFrontierCoverage(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                             const FastState& current) -> FrontierCoverage
{
  FrontierCoverage coverage;
  coverage.late_count_by_buffer.assign(buffers.size(), 0U);
  coverage.early_count_by_buffer.assign(buffers.size(), 0U);
  for (const auto side : {FrontierSide::kLate, FrontierSide::kEarly}) {
    auto& count_by_buffer = side == FrontierSide::kLate ? coverage.late_count_by_buffer : coverage.early_count_by_buffer;
    const auto frontier_sinks = collectFrontierSinks(clock_id, current, side);
    for (const auto sink_id : frontier_sinks) {
      const auto path = collectPathBufferIndices(topology, sink_id);
      std::unordered_set<std::size_t> visited_buffers;
      for (const auto buffer_index : path) {
        if (buffer_index >= count_by_buffer.size() || visited_buffers.contains(buffer_index)) {
          continue;
        }
        visited_buffers.insert(buffer_index);
        ++count_by_buffer.at(buffer_index);
      }
    }
  }
  return coverage;
}

auto scoreSizingAction(const SizingAction& action, const FrontierCoverage& coverage) -> double
{
  if (action.drive_step == 0 || action.buffer_index >= coverage.late_count_by_buffer.size()
      || action.buffer_index >= coverage.early_count_by_buffer.size()) {
    return 0.0;
  }
  const auto late_count = coverage.late_count_by_buffer.at(action.buffer_index);
  const auto early_count = coverage.early_count_by_buffer.at(action.buffer_index);
  const bool late_action = action.drive_step > 0;
  const auto primary_count = late_action ? late_count : early_count;
  const auto opposite_count = late_action ? early_count : late_count;
  if (primary_count == 0U) {
    return 0.0;
  }

  const double focus = static_cast<double>(primary_count + 1U) / static_cast<double>(opposite_count + 1U);
  const auto coverage_weight = static_cast<double>(primary_count);
  const double drive_weight = 1.0 + 0.25 * static_cast<double>(std::abs(action.drive_step));
  if (late_action) {
    const double area_cost = 1.0 + 0.01 * std::max(0.0, action.area_delta_um2);
    return coverage_weight * focus * drive_weight / area_cost;
  }

  const double area_bonus = 1.0 + 0.02 * std::max(0.0, -action.area_delta_um2);
  return coverage_weight * focus * drive_weight * area_bonus;
}

auto scoreScalableBatch(const std::vector<SizingAction>& actions, const FrontierCoverage& coverage) -> double
{
  double score = 0.0;
  for (const auto& action : actions) {
    score += scoreSizingAction(action, coverage);
  }
  return score;
}

auto batchKey(const std::vector<SizingAction>& actions) -> std::string
{
  auto sorted_actions = actions;
  std::ranges::sort(sorted_actions, [](const SizingAction& lhs, const SizingAction& rhs) -> bool {
    if (lhs.buffer_index != rhs.buffer_index) {
      return lhs.buffer_index < rhs.buffer_index;
    }
    return lhs.to_master < rhs.to_master;
  });

  std::string key;
  for (const auto& action : sorted_actions) {
    key += std::to_string(action.buffer_index);
    key += ':';
    key += action.to_master;
    key += ';';
  }
  return key;
}

auto appendScoredBatch(std::vector<ScoredBatchCandidate>& candidates, std::unordered_set<std::string>& seen,
                       const std::vector<SizingAction>& actions, const FrontierCoverage& coverage,
                       std::size_t max_action_count = kMaxScalableBatchActions) -> void
{
  if (actions.empty()) {
    return;
  }

  std::vector<SizingAction> compact;
  compact.reserve(std::min(actions.size(), max_action_count));
  std::unordered_set<std::size_t> used_buffers;
  for (const auto& action : actions) {
    if (used_buffers.contains(action.buffer_index)) {
      continue;
    }
    used_buffers.insert(action.buffer_index);
    compact.push_back(action);
    if (compact.size() >= max_action_count) {
      break;
    }
  }
  if (compact.empty()) {
    return;
  }

  const auto key = batchKey(compact);
  if (!seen.insert(key).second) {
    return;
  }
  const double score = scoreScalableBatch(compact, coverage);
  if (score <= 0.0) {
    return;
  }
  candidates.push_back(ScoredBatchCandidate{.actions = std::move(compact), .score = score});
}

auto collectScoredActions(const std::vector<OptimizableBuffer>& buffers, const FrontierCoverage& coverage)
    -> std::vector<ScoredSizingAction>
{
  std::vector<ScoredSizingAction> scored_actions;
  scored_actions.reserve(buffers.size() * 4U);
  constexpr std::array<unsigned, 3U> rank_steps = {1U, 2U, 3U};
  for (std::size_t buffer_index = 0U; buffer_index < buffers.size(); ++buffer_index) {
    for (const auto rank_step : rank_steps) {
      for (const auto side : {FrontierSide::kLate, FrontierSide::kEarly}) {
        auto action = makeSizingAction(buffers, buffer_index, side, rank_step);
        if (!action.has_value()) {
          continue;
        }
        const double score = scoreSizingAction(*action, coverage);
        if (score > 0.0) {
          scored_actions.push_back(ScoredSizingAction{.action = std::move(*action), .score = score});
        }
      }
    }
  }

  std::ranges::sort(scored_actions, [](const ScoredSizingAction& lhs, const ScoredSizingAction& rhs) -> bool {
    if (std::abs(lhs.score - rhs.score) > kEpsilon) {
      return lhs.score > rhs.score;
    }
    if (std::abs(lhs.action.area_delta_um2 - rhs.action.area_delta_um2) > kEpsilon) {
      return lhs.action.area_delta_um2 < rhs.action.area_delta_um2;
    }
    if (lhs.action.buffer_index != rhs.action.buffer_index) {
      return lhs.action.buffer_index < rhs.action.buffer_index;
    }
    return lhs.action.to_master < rhs.action.to_master;
  });
  return scored_actions;
}

auto selectTopActions(const std::vector<ScoredSizingAction>& scored_actions, int drive_sign, std::size_t max_action_count)
    -> std::vector<SizingAction>
{
  std::vector<SizingAction> actions;
  actions.reserve(max_action_count);
  std::unordered_set<std::size_t> used_buffers;
  for (const auto& scored_action : scored_actions) {
    if (drive_sign > 0 && scored_action.action.drive_step <= 0) {
      continue;
    }
    if (drive_sign < 0 && scored_action.action.drive_step >= 0) {
      continue;
    }
    if (used_buffers.contains(scored_action.action.buffer_index)) {
      continue;
    }
    used_buffers.insert(scored_action.action.buffer_index);
    actions.push_back(scored_action.action);
    if (actions.size() >= max_action_count) {
      break;
    }
  }
  return actions;
}

auto mergeActionVectors(std::vector<SizingAction> lhs, const std::vector<SizingAction>& rhs) -> std::vector<SizingAction>
{
  lhs.insert(lhs.end(), rhs.begin(), rhs.end());
  return lhs;
}

auto normalizedBatchScore(const ScoredBatchCandidate& candidate) -> double
{
  return candidate.actions.empty() ? 0.0 : candidate.score / std::sqrt(static_cast<double>(candidate.actions.size()));
}

auto generateScalableBatchCandidates(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                     const FastState& current) -> std::vector<ScoredBatchCandidate>
{
  std::vector<ScoredBatchCandidate> candidates;
  std::unordered_set<std::string> seen;
  const auto coverage = collectFrontierCoverage(clock_id, buffers, topology, current);
  const auto scored_actions = collectScoredActions(buffers, coverage);
  constexpr std::array<std::size_t, 7U> batch_sizes = {kMaxScalableBatchActions, 32U, 16U, 8U, 4U, 2U, 1U};
  for (const auto batch_size : batch_sizes) {
    appendScoredBatch(candidates, seen, selectTopActions(scored_actions, 1, batch_size), coverage);
    appendScoredBatch(candidates, seen, selectTopActions(scored_actions, -1, batch_size), coverage);
    const auto late_actions = selectTopActions(scored_actions, 1, batch_size / 2U);
    const auto early_actions = selectTopActions(scored_actions, -1, batch_size - late_actions.size());
    appendScoredBatch(candidates, seen, mergeActionVectors(late_actions, early_actions), coverage);
  }

  const auto conventional_candidates = generateBatchCandidates(clock_id, buffers, topology, current);
  for (const auto& actions : conventional_candidates) {
    appendScoredBatch(candidates, seen, actions, coverage, kMaxBatchActions);
  }

  std::ranges::sort(candidates, [](const ScoredBatchCandidate& lhs, const ScoredBatchCandidate& rhs) -> bool {
    const double lhs_normalized_score = normalizedBatchScore(lhs);
    const double rhs_normalized_score = normalizedBatchScore(rhs);
    if (std::abs(lhs_normalized_score - rhs_normalized_score) > kEpsilon) {
      return lhs_normalized_score > rhs_normalized_score;
    }
    if (std::abs(lhs.score - rhs.score) > kEpsilon) {
      return lhs.score > rhs.score;
    }
    if (lhs.actions.size() != rhs.actions.size()) {
      return lhs.actions.size() < rhs.actions.size();
    }
    return firstActionBufferIndex(lhs.actions) < firstActionBufferIndex(rhs.actions);
  });
  return candidates;
}

auto injectRouteTrees(FastStaClockId clock_id, const Clock& clock, const RouteTreeCache& route_tree_by_net) -> bool
{
  auto* context = FastStaAdapter::mutableClockContext(clock_id);
  const auto* graph = DESIGN_INST.get_clock_dag().graphForClock(&clock);
  if (context == nullptr || graph == nullptr) {
    return false;
  }
  const auto total_start = std::chrono::steady_clock::now();
  auto progress_start = total_start;
  constexpr std::size_t progress_interval = 5000U;
  constexpr std::size_t initial_detail_net_count = 5U;
  constexpr double slow_net_threshold_s = 1.0;
  std::size_t visited_net_count = 0U;
  std::size_t injected_net_count = 0U;
  std::size_t rc_node_count = 0U;
  std::size_t rc_edge_count = 0U;
  LOG_INFO << "Optimization: start route-tree injection for clock \"" << clock.get_clock_name() << "\", dag_nets=" << graph->nets.size()
           << ", cached_route_trees=" << route_tree_by_net.size() << ".";
  for (auto* net : graph->nets) {
    if (net == nullptr) {
      continue;
    }
    ++visited_net_count;
    const auto route_iter = route_tree_by_net.find(net);
    if (route_iter == route_tree_by_net.end()) {
      LOG_ERROR << "Optimization: route tree is unavailable for net \"" << net->get_name() << "\".";
      return false;
    }
    if (visited_net_count <= initial_detail_net_count) {
      LOG_INFO << "Optimization: route-tree injection start net " << visited_net_count << "/" << graph->nets.size() << " \""
               << net->get_name() << "\", loads=" << net->get_loads().size() << ", route_nodes=" << route_iter->second.node_count()
               << ", route_edges=" << route_iter->second.edge_count() << ".";
    }
    const auto net_start = std::chrono::steady_clock::now();
    if (!FastStaBuilder::injectNetRouteTree(*context, *net, route_iter->second)) {
      LOG_ERROR << "Optimization: fast STA route-tree injection failed for net \"" << net->get_name() << "\".";
      return false;
    }
    const double net_runtime_s = elapsedSeconds(net_start);
    ++injected_net_count;
    if (const auto net_iter = context->net_id_by_name.find(net->get_name());
        net_iter != context->net_id_by_name.end() && net_iter->second < context->nets.size()) {
      const auto& parasitic = context->nets.at(net_iter->second).parasitic;
      rc_node_count += parasitic.rc_nodes.size();
      rc_edge_count += parasitic.rc_edges.size();
    }
    if (visited_net_count <= initial_detail_net_count || net_runtime_s >= slow_net_threshold_s) {
      LOG_INFO << "Optimization: route-tree injection finish net " << visited_net_count << "/" << graph->nets.size() << " \""
               << net->get_name() << "\" in " << net_runtime_s << " s.";
    }
    if (injected_net_count % progress_interval == 0U) {
      LOG_INFO << "Optimization: route-tree injection progress for clock \"" << clock.get_clock_name()
               << "\": injected=" << injected_net_count << "/" << graph->nets.size() << ", visited=" << visited_net_count
               << ", elapsed=" << elapsedSeconds(total_start) << " s, interval=" << elapsedSeconds(progress_start) << " s.";
      progress_start = std::chrono::steady_clock::now();
    }
  }
  LOG_INFO << "Optimization: route-tree injection finished for clock \"" << clock.get_clock_name() << "\" in "
           << elapsedSeconds(total_start) << " s, injected_nets=" << injected_net_count << ", rc_nodes=" << rc_node_count
           << ", rc_edges=" << rc_edge_count << ".";

  auto update_start = std::chrono::steady_clock::now();
  LOG_INFO << "Optimization: start post-injection timing update for clock \"" << clock.get_clock_name() << "\".";
  if (!FastStaAdapter::updateTiming(clock_id)) {
    return false;
  }
  LOG_INFO << "Optimization: post-injection timing update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(update_start) << " s.";

  update_start = std::chrono::steady_clock::now();
  LOG_INFO << "Optimization: start post-injection power update for clock \"" << clock.get_clock_name() << "\".";
  if (!FastStaAdapter::updatePower(clock_id)) {
    return false;
  }
  LOG_INFO << "Optimization: post-injection power update for clock \"" << clock.get_clock_name() << "\" finished in "
           << elapsedSeconds(update_start) << " s.";
  return true;
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
      {"solve_mode", summary.solve_mode.empty() ? "n/a" : summary.solve_mode},
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

auto emitClockProfile(const Clock& clock, const OptimizationRuntimeProfile& profile) -> void
{
  schema::EmitKeyValueTable("CTS Optimization Clock Graph Profile",
                            {{"clock", clock.get_clock_name()},
                             {"node_count", std::to_string(profile.node_count)},
                             {"net_count", std::to_string(profile.net_count)},
                             {"sink_count", std::to_string(profile.sink_count)},
                             {"buffer_input_count", std::to_string(profile.buffer_input_count)},
                             {"buffer_output_count", std::to_string(profile.buffer_output_count)},
                             {"optimizable_buffer_count", std::to_string(profile.optimizable_buffer_count)},
                             {"generated_candidate_count", std::to_string(profile.generated_candidate_count)}});

  schema::TableRows rows = {
      {"build_route_tree_cache", formatSeconds(profile.build_route_tree_cache_s)},
      {"build_fast_sta_context", formatSeconds(profile.build_fast_sta_context_s)},
      {"inject_route_trees", formatSeconds(profile.inject_route_trees_s)},
      {"collect_optimizable_buffers", formatSeconds(profile.collect_optimizable_buffers_s)},
      {"collect_cap_baseline", formatSeconds(profile.collect_cap_baseline_s)},
      {"collect_slew_baseline", formatSeconds(profile.collect_slew_baseline_s)},
      {"solve_clock_total", formatSeconds(profile.solve_clock_s)},
      {"capture_initial_state", formatSeconds(profile.capture_initial_state_s)},
      {"build_topology_index", formatSeconds(profile.build_topology_index_s)},
      {"generate_batch_candidates", formatSeconds(profile.generate_batch_candidates_s)},
      {"batch_trial_eval", formatSeconds(profile.batch_trial_eval_s)},
      {"apply_accepted_batch", formatSeconds(profile.apply_accepted_batch_s)},
      {"apply_mutations", formatSeconds(profile.apply_mutations_s)},
  };
  schema::EmitTable("CTS Optimization Clock Runtime Profile", {"Stage", "Runtime"}, rows);
}

auto findBestBatchTrial(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                        const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns, ClockOptimizationSummary& summary)
    -> BatchTrial
{
  BatchTrial best;
  const auto candidate_start = std::chrono::steady_clock::now();
  const auto candidates = generateBatchCandidates(clock_id, buffers, topology, current);
  summary.profile.generate_batch_candidates_s += elapsedSeconds(candidate_start);
  summary.profile.generated_candidate_count += candidates.size();
  LOG_INFO << "Optimization: solve iteration " << (summary.iteration_count + 1U) << " starts with current_skew=" << current.skew.skew_ns
           << " ns, current_area=" << current.power.area_um2 << " um^2, candidates=" << candidates.size()
           << ", total_trials=" << summary.trial_count << ".";
  const auto iteration_start = std::chrono::steady_clock::now();
  for (std::size_t candidate_index = 0U; candidate_index < candidates.size(); ++candidate_index) {
    if (summary.trial_count >= kMaxOptimizationTrials) {
      break;
    }
    const auto& actions = candidates.at(candidate_index);
    ++summary.trial_count;
    ++summary.batch_trial_count;
    if (summary.trial_count <= kInitialDetailedTrials) {
      LOG_INFO << "Optimization: start batch trial " << summary.trial_count << ", candidate=" << (candidate_index + 1U) << "/"
               << candidates.size() << ", action_count=" << actions.size() << ".";
    }
    const auto trial_start = std::chrono::steady_clock::now();
    auto trial = tryBatch(clock_id, buffers, actions, current, cap_baseline, slew_baseline, target_skew_ns);
    const double trial_runtime_s = elapsedSeconds(trial_start);
    summary.profile.batch_trial_eval_s += trial_runtime_s;
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
    if (summary.trial_count <= kInitialDetailedTrials || summary.trial_count % kTrialProgressInterval == 0U
        || trial_runtime_s >= kSlowTrialLogThresholdS) {
      LOG_INFO << "Optimization: batch trial progress, iteration=" << (summary.iteration_count + 1U)
               << ", candidate=" << (candidate_index + 1U) << "/" << candidates.size() << ", total_trials=" << summary.trial_count
               << ", trial_runtime=" << trial_runtime_s << " s, iteration_runtime=" << elapsedSeconds(iteration_start)
               << " s, best_skew=" << (best.valid ? best.state.skew.skew_ns : current.skew.skew_ns) << " ns.";
    }
  }
  return best;
}

auto findBestScalableBatchTrial(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns, ClockOptimizationSummary& summary)
    -> BatchTrial
{
  BatchTrial best;
  const auto candidate_start = std::chrono::steady_clock::now();
  const auto candidates = generateScalableBatchCandidates(clock_id, buffers, topology, current);
  summary.profile.generate_batch_candidates_s += elapsedSeconds(candidate_start);
  summary.profile.generated_candidate_count += candidates.size();
  LOG_INFO << "Optimization: scalable solve iteration " << (summary.iteration_count + 1U)
           << " starts with current_skew=" << current.skew.skew_ns << " ns, current_area=" << current.power.area_um2
           << " um^2, scored_batches=" << candidates.size() << ", exact_trial_limit=" << kMaxScalableExactTrialsPerIteration
           << ", total_trials=" << summary.trial_count << ".";

  const auto iteration_start = std::chrono::steady_clock::now();
  const auto exact_trial_count = std::min(kMaxScalableExactTrialsPerIteration, candidates.size());
  for (std::size_t candidate_index = 0U; candidate_index < exact_trial_count; ++candidate_index) {
    if (summary.trial_count >= kMaxOptimizationTrials) {
      break;
    }
    const auto& candidate = candidates.at(candidate_index);
    ++summary.trial_count;
    ++summary.batch_trial_count;
    LOG_INFO << "Optimization: scalable batch trial " << summary.trial_count << ", candidate=" << (candidate_index + 1U) << "/"
             << candidates.size() << ", action_count=" << candidate.actions.size() << ", score=" << candidate.score << ".";
    const auto trial_start = std::chrono::steady_clock::now();
    auto trial = tryBatchTimingOnly(clock_id, buffers, candidate.actions, current, cap_baseline, slew_baseline, target_skew_ns);
    const double trial_runtime_s = elapsedSeconds(trial_start);
    summary.profile.batch_trial_eval_s += trial_runtime_s;
    if (!trial.state.cap.legal) {
      ++summary.cap_rejected_count;
    }
    if (!trial.state.slew.legal) {
      ++summary.slew_rejected_count;
    }
    if (!trial.valid) {
      ++summary.rejected_candidate_count;
      const char* reject_reason = "no_improvement";
      if (!trial.state.cap.legal) {
        reject_reason = "cap";
      } else if (!trial.state.slew.legal) {
        reject_reason = "slew";
      } else if (!trial.state.skew.valid) {
        reject_reason = "timing";
      }
      LOG_INFO << "Optimization: scalable batch trial rejected, reason=" << reject_reason
               << ", candidate_skew=" << (trial.state.skew.valid ? trial.state.skew.skew_ns : 0.0)
               << " ns, candidate_area=" << trial.state.power.area_um2 << " um^2, trial_runtime=" << trial_runtime_s << " s.";
      continue;
    }
    if (preferTrial(trial, best, current, target_skew_ns)) {
      best = std::move(trial);
    }
    LOG_INFO << "Optimization: scalable batch trial finished, iteration=" << (summary.iteration_count + 1U)
             << ", candidate=" << (candidate_index + 1U) << "/" << candidates.size() << ", total_trials=" << summary.trial_count
             << ", trial_runtime=" << trial_runtime_s << " s, iteration_runtime=" << elapsedSeconds(iteration_start)
             << " s, best_skew=" << (best.valid ? best.state.skew.skew_ns : current.skew.skew_ns) << " ns.";
  }
  return best;
}

auto solveClock(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary
{
  ClockOptimizationSummary summary;
  summary.solve_mode = "exact_full_power_batch";
  auto stage_start = std::chrono::steady_clock::now();
  summary.before = captureState(clock_id, cap_baseline, slew_baseline);
  summary.profile.capture_initial_state_s = elapsedSeconds(stage_start);
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

  stage_start = std::chrono::steady_clock::now();
  const auto topology = buildTopologyIndex(clock_id, buffers);
  summary.profile.build_topology_index_s = elapsedSeconds(stage_start);
  auto current = summary.before;
  while (summary.iteration_count < kMaxOptimizationIterations && summary.trial_count < kMaxOptimizationTrials) {
    auto best = findBestBatchTrial(clock_id, buffers, topology, current, cap_baseline, slew_baseline, target_skew_ns, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= kMaxOptimizationTrials ? "trial_limit" : "no_improving_candidate";
      break;
    }
    stage_start = std::chrono::steady_clock::now();
    if (!changeFastStaMasters(clock_id, buildMasterChanges(buffers, best.actions, false))) {
      summary.stop_reason = "accepted_mutation_apply_failed";
      break;
    }
    current = captureState(clock_id, cap_baseline, slew_baseline);
    summary.profile.apply_accepted_batch_s += elapsedSeconds(stage_start);
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
    LOG_INFO << "Optimization: accepted batch " << summary.accepted_batch_count << ", action_count=" << best.actions.size()
             << ", skew=" << current.skew.skew_ns << " ns, area=" << current.power.area_um2 << " um^2, total_trials=" << summary.trial_count
             << ".";
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

auto solveClockScalable(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary
{
  ClockOptimizationSummary summary;
  summary.solve_mode = "scalable_timing_only_batch";
  auto stage_start = std::chrono::steady_clock::now();
  summary.before = captureState(clock_id, cap_baseline, slew_baseline);
  summary.profile.capture_initial_state_s = elapsedSeconds(stage_start);
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

  stage_start = std::chrono::steady_clock::now();
  const auto topology = buildTopologyIndex(clock_id, buffers);
  summary.profile.build_topology_index_s = elapsedSeconds(stage_start);
  auto current = summary.before;
  while (summary.iteration_count < kMaxOptimizationIterations && summary.trial_count < kMaxOptimizationTrials) {
    auto best = findBestScalableBatchTrial(clock_id, buffers, topology, current, cap_baseline, slew_baseline, target_skew_ns, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= kMaxOptimizationTrials ? "trial_limit" : "no_improving_candidate";
      break;
    }

    stage_start = std::chrono::steady_clock::now();
    if (!changeFastStaMastersTimingOnly(clock_id, buildMasterChanges(buffers, best.actions, false))) {
      summary.stop_reason = "accepted_mutation_apply_failed";
      break;
    }
    current = captureStateWithArea(clock_id, cap_baseline, slew_baseline, current.power.area_um2 + actionAreaDelta(best.actions));
    summary.profile.apply_accepted_batch_s += elapsedSeconds(stage_start);
    if (!current.valid) {
      summary.stop_reason = !current.cap.legal ? "accepted_mutation_cap_violation" : "accepted_mutation_slew_violation";
      break;
    }

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
    LOG_INFO << "Optimization: accepted scalable batch " << summary.accepted_batch_count << ", action_count=" << best.actions.size()
             << ", skew=" << current.skew.skew_ns << " ns, tracked_area=" << current.power.area_um2
             << " um^2, total_trials=" << summary.trial_count << ".";
  }

  if (!FastStaAdapter::updatePower(clock_id)) {
    summary.after = current;
    summary.valid = false;
    if (summary.stop_reason.empty()) {
      summary.stop_reason = "final_power_update_failed";
    }
    return summary;
  }

  summary.after = captureState(clock_id, cap_baseline, slew_baseline);
  summary.valid = summary.before.valid && summary.after.valid;
  summary.changed = !summary.mutations.empty();
  summary.target_met = targetMet(summary.after, target_skew_ns);
  if (summary.stop_reason.empty()) {
    summary.stop_reason = summary.target_met ? "target_met" : "iteration_limit";
  }
  return summary;
}

auto shouldUseScalableSolver(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> bool
{
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return false;
  }
  return context->nodes.size() >= kScalableNodeThreshold || buffers.size() >= kScalableBufferThreshold;
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

  auto stage_start = std::chrono::steady_clock::now();
  const auto route_tree_by_net = buildRouteTreeCache(clocks);
  const double route_tree_cache_runtime_s = elapsedSeconds(stage_start);
  const double target_skew_ns = std::max(0.0, CONFIG_INST.get_skew_bound());
  schema::EmitKeyValueTable("CTS Optimization Setup", {{"timing_source", "cts_fast_sta_incremental"},
                                                       {"target_skew", formatNs(target_skew_ns)},
                                                       {"candidate_master_count", std::to_string(master_infos.size())}});
  schema::EmitKeyValueTable("CTS Optimization Global Profile", {{"build_route_tree_cache", formatSeconds(route_tree_cache_runtime_s)},
                                                                {"cached_route_tree_count", std::to_string(route_tree_by_net.size())}});

  std::string no_op_reason = "no_optimizable_clock";
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    const auto clock_start = std::chrono::steady_clock::now();
    OptimizationRuntimeProfile outer_profile;
    outer_profile.build_route_tree_cache_s = route_tree_cache_runtime_s;
    stage_start = std::chrono::steady_clock::now();
    const auto clock_id = FastStaAdapter::buildClockContext(*clock, clock_layout, clock_index);
    outer_profile.build_fast_sta_context_s = elapsedSeconds(stage_start);

    if (const auto* context = FastStaAdapter::queryClockContext(clock_id); context != nullptr) {
      auto graph_profile = captureGraphProfile(*context);
      graph_profile.build_route_tree_cache_s = outer_profile.build_route_tree_cache_s;
      graph_profile.build_fast_sta_context_s = outer_profile.build_fast_sta_context_s;
      outer_profile = graph_profile;
    }

    stage_start = std::chrono::steady_clock::now();
    if (!injectRouteTrees(clock_id, *clock, route_tree_by_net)) {
      outer_profile.inject_route_trees_s = elapsedSeconds(stage_start);
      emitClockProfile(*clock, outer_profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA context build failed.";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = "fast_sta_context_failed";
      continue;
    }
    outer_profile.inject_route_trees_s = elapsedSeconds(stage_start);

    stage_start = std::chrono::steady_clock::now();
    auto buffers = collectOptimizableBuffers(clock_id, master_infos);
    outer_profile.collect_optimizable_buffers_s = elapsedSeconds(stage_start);
    outer_profile.optimizable_buffer_count = buffers.size();
    if (buffers.empty()) {
      emitClockProfile(*clock, outer_profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because no resizable buffers are available.";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = "no_resizable_buffers";
      continue;
    }

    stage_start = std::chrono::steady_clock::now();
    const auto cap_baseline = collectCapBaseline(clock_id);
    outer_profile.collect_cap_baseline_s = elapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const auto slew_baseline = collectSlewBaseline(clock_id);
    outer_profile.collect_slew_baseline_s = elapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const bool use_scalable_solver = shouldUseScalableSolver(clock_id, buffers);
    LOG_INFO << "Optimization: clock \"" << clock->get_clock_name() << "\" uses "
             << (use_scalable_solver ? "scalable timing-only batch solver" : "exact full-power batch solver") << ".";
    auto summary = use_scalable_solver ? solveClockScalable(clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns)
                                       : solveClock(clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns);
    outer_profile.solve_clock_s = elapsedSeconds(stage_start);
    copyOuterProfile(summary.profile, outer_profile);
    const auto clock_end = std::chrono::steady_clock::now();
    const double clock_runtime_s = std::chrono::duration<double>(clock_end - clock_start).count();
    if (!summary.valid) {
      emitClockProfile(*clock, summary.profile);
      LOG_WARNING << "Optimization: skip clock \"" << clock->get_clock_name() << "\" because fast STA solver failed with reason "
                  << summary.stop_reason << ".";
      (void) FastStaAdapter::eraseClockContext(clock_id);
      no_op_reason = summary.stop_reason.empty() ? "solver_failed" : summary.stop_reason;
      continue;
    }
    stage_start = std::chrono::steady_clock::now();
    if (!summary.mutations.empty() && !applyMutations(summary.mutations, buffers, clock_layout)) {
      result.success = false;
      (void) runtime.failed();
      stage.failed({{"reason", "mutation_apply_failed"}});
      return result;
    }
    summary.profile.apply_mutations_s = elapsedSeconds(stage_start);
    emitClockSummary(*clock, summary, target_skew_ns, clock_runtime_s);
    emitClockProfile(*clock, summary.profile);
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
