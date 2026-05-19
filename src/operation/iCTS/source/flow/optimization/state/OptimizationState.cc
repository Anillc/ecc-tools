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
 * @file OptimizationState.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Fast STA state capture and improvement checks for CTS optimization.
 */

#include "optimization/state/OptimizationState.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "FastSta.hh"
#include "FastStaTypes.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts::optimization_internal {

namespace {

auto CheckCapLegality(FastStaClockId clock_id, const std::vector<CapBaseline>& baseline) -> CapCheckResult
{
  CapCheckResult result;
  const auto* context = FastSTA::queryClockContext(clock_id);
  if (context == nullptr) {
    result.legal = false;
    return result;
  }
  for (FastStaNetId net_id = 0U; net_id < context->nets.size(); ++net_id) {
    const auto cap_status = FastSTA::queryCapStatus(clock_id, net_id);
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
    const bool legal = baseline_violated ? cap_status->load_cap_pf <= baseline_load + kOptimizationEpsilon : !cap_status->violated;
    if (!legal) {
      result.legal = false;
      ++result.violation_count;
    }
  }
  return result;
}

auto ResolveSlewRole(const std::optional<FastStaSlewStatus>& slew_status, FastStaNodeId node_id, const std::vector<SlewBaseline>& baseline)
    -> FastStaSlewRole
{
  if (slew_status.has_value()) {
    return slew_status->role;
  }
  if (node_id < baseline.size()) {
    return baseline.at(node_id).role;
  }
  return FastStaSlewRole::kUnknown;
}

auto CountSlewViolationRole(SlewCheckResult& result, FastStaSlewRole role) -> void
{
  ++result.violation_count;
  switch (role) {
    case FastStaSlewRole::kBufferInput:
      ++result.buffer_violation_count;
      break;
    case FastStaSlewRole::kSink:
      ++result.sink_violation_count;
      break;
    case FastStaSlewRole::kUnknown:
      break;
  }
}

auto CheckSlewLegality(FastStaClockId clock_id, const std::vector<SlewBaseline>& baseline) -> SlewCheckResult
{
  SlewCheckResult result;
  const auto* context = FastSTA::queryClockContext(clock_id);
  if (context == nullptr) {
    result.legal = false;
    return result;
  }
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto slew_status = FastSTA::querySlewStatus(clock_id, node_id);
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
    const auto role = ResolveSlewRole(slew_status, node_id, baseline);
    if (!slew_status.has_value()) {
      result.legal = false;
      CountSlewViolationRole(result, role);
      continue;
    }
    const auto baseline_slew = baseline_available ? baseline.at(node_id).slew_ns : 0.0;
    const auto baseline_violated = baseline_available && baseline.at(node_id).violated;
    const bool legal = baseline_violated ? slew_status->slew_ns <= baseline_slew + kOptimizationEpsilon : !slew_status->violated;
    if (!legal) {
      result.legal = false;
      CountSlewViolationRole(result, role);
    }
  }
  return result;
}

}  // namespace

auto FirstActionBufferIndex(const std::vector<SizingAction>& actions) -> std::size_t
{
  auto first_index = std::numeric_limits<std::size_t>::max();
  for (const auto& action : actions) {
    first_index = std::min(first_index, action.buffer_index);
  }
  return first_index;
}

auto CaptureState(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline, const std::vector<SlewBaseline>& slew_baseline)
    -> FastState
{
  FastState state;
  state.skew = FastSTA::querySkew(clock_id);
  state.power = FastSTA::queryPower(clock_id);
  state.cap = CheckCapLegality(clock_id, cap_baseline);
  state.slew = CheckSlewLegality(clock_id, slew_baseline);
  state.valid = state.skew.valid && state.cap.legal && state.slew.legal;
  return state;
}

auto CaptureStateWithArea(FastStaClockId clock_id, const std::vector<CapBaseline>& cap_baseline,
                          const std::vector<SlewBaseline>& slew_baseline, double area_um2) -> FastState
{
  auto state = CaptureState(clock_id, cap_baseline, slew_baseline);
  state.power.area_um2 = area_um2;
  return state;
}

auto TargetMet(const FastState& state, double target_skew_ns) -> bool
{
  return state.valid && state.skew.skew_ns <= target_skew_ns + kOptimizationEpsilon;
}

auto StateImproves(const FastState& current, const FastState& candidate, double target_skew_ns) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  const bool current_met = TargetMet(current, target_skew_ns);
  const bool candidate_met = TargetMet(candidate, target_skew_ns);
  if (current_met) {
    if (!candidate_met) {
      return false;
    }
    if (candidate.power.area_um2 < current.power.area_um2 - kOptimizationEpsilon) {
      return true;
    }
    return std::abs(candidate.power.area_um2 - current.power.area_um2) <= kOptimizationEpsilon
           && candidate.skew.skew_ns < current.skew.skew_ns - kOptimizationEpsilon;
  }
  if (candidate_met) {
    return true;
  }
  return candidate.skew.skew_ns < current.skew.skew_ns - kOptimizationEpsilon;
}

}  // namespace icts::optimization_internal
