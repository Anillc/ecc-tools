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
 * @file LevelSizingOperator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "LevelSizingOperator.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>

#include "TimingPropagator.hh"
#include "log/Log.hh"

namespace icts {

namespace {
constexpr double kMetricEpsilon = 1e-12;
constexpr double kConstraintEpsilon = 1e-9;

std::string formatSizingAssignmentSummary(const std::vector<size_t>& level_lib_indices,
                                          const std::vector<std::vector<Inst*>>& buffers_by_depth, const std::vector<CtsCellLib*>& libs)
{
  std::ostringstream oss;
  bool first = true;
  for (size_t depth = 0; depth < level_lib_indices.size(); ++depth) {
    if (depth >= buffers_by_depth.size() || buffers_by_depth[depth].empty()) {
      continue;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << "d" << depth << "=" << libs[level_lib_indices[depth]]->get_cell_master() << "x" << buffers_by_depth[depth].size();
  }
  return first ? "none" : oss.str();
}
}  // namespace

void LevelSizingOperator::run(SolverPipelineState& state) const
{
  if (state.buffers_by_depth.empty()) {
    return;
  }

  std::vector<SolverSizingCandidate> feasible_candidates;
  std::vector<SolverSizingCandidate> all_candidates;
  SolverSizingSearchStats stats;
  std::vector<size_t> current_assignment(state.buffers_by_depth.size(), 0);
  const auto& libs = TimingPropagator::getDelayLibs();
  LOG_FATAL_IF(libs.empty()) << "Net [" << state.net_name << "] has no delay libraries for sizing.";
  const size_t max_lib_index = libs.size() - 1;
  enumerateLevelSizing(state, 0, max_lib_index, current_assignment, feasible_candidates, all_candidates, stats);
  LOG_FATAL_IF(all_candidates.empty()) << "Net [" << state.net_name << "] failed to enumerate any sizing candidates.";

  auto build_search_summary = [&](size_t feasible_count, size_t pareto_count, const std::string& mode_suffix) {
    std::ostringstream sizing_search_summary;
    sizing_search_summary << "Net [" << state.net_name << "] level sizing search summary => depth count: " << state.buffers_by_depth.size()
                          << ", library choices: " << libs.size() << ", evaluated: " << stats.evaluated << ", feasible: " << feasible_count
                          << ", pareto front: " << pareto_count
                          << ", rejected(skew/buf_slew/sink_slew/cap/length/fanout): " << stats.rejected_skew << "/"
                          << stats.rejected_buffer_slew << "/" << stats.rejected_sink_slew << "/" << stats.rejected_cap << "/"
                          << stats.rejected_length << "/" << stats.rejected_fanout << mode_suffix;
    return sizing_search_summary.str();
  };

  SolverSizingCandidate best;
  size_t pareto_count = 0;
  if (feasible_candidates.empty()) {
    auto fallback_candidates = all_candidates;
    normalizeCandidates(fallback_candidates);
    pareto_count = countParetoCandidates(fallback_candidates);
    best = fallback_candidates[selectBalancedCandidate(fallback_candidates)];

    const auto summary = build_search_summary(
        0, pareto_count, ", fallback: global delay/area/power Pareto candidate selected because no feasible sizing exists");
    LOG_WARNING << summary;
    _runtime.saveToLog(summary);

    std::ostringstream fallback_summary;
    fallback_summary << std::fixed << std::setprecision(6);
    fallback_summary << "Net [" << state.net_name << "] fallback sizing trigger => selected from global delay/area/power Pareto front"
                     << ", violation score: " << best.violation_score << ", violated constraints: " << best.violated_constraints
                     << ", details: " << formatFeasibilitySummary(best.feasibility);
    LOG_WARNING << fallback_summary.str();
    _runtime.saveToLog(fallback_summary.str());
  } else {
    normalizeCandidates(feasible_candidates);
    pareto_count = countParetoCandidates(feasible_candidates);
    best = feasible_candidates[selectBalancedCandidate(feasible_candidates)];

    const auto summary = build_search_summary(stats.feasible, pareto_count, "");
    LOG_INFO << summary;
    _runtime.saveToLog(summary);
  }

  applyLevelSizing(state, best.level_lib_indices);
  reevaluateTree(state);
  logSizingSummary(state, best);
}

void LevelSizingOperator::enumerateLevelSizing(SolverPipelineState& state, size_t depth, size_t max_lib_index,
                                               std::vector<size_t>& current_assignment,
                                               std::vector<SolverSizingCandidate>& feasible_candidates,
                                               std::vector<SolverSizingCandidate>& all_candidates, SolverSizingSearchStats& stats) const
{
  if (depth == current_assignment.size()) {
    auto candidate = evaluateSizing(state, current_assignment, stats);
    all_candidates.push_back(candidate);
    if (candidate.feasible) {
      feasible_candidates.push_back(candidate);
    }
    return;
  }

  for (size_t lib_index = 0; lib_index <= max_lib_index; ++lib_index) {
    current_assignment[depth] = lib_index;
    enumerateLevelSizing(state, depth + 1, lib_index, current_assignment, feasible_candidates, all_candidates, stats);
  }
}

SolverSizingCandidate LevelSizingOperator::evaluateSizing(SolverPipelineState& state, const std::vector<size_t>& level_lib_indices,
                                                          SolverSizingSearchStats& stats) const
{
  ++stats.evaluated;
  applyLevelSizing(state, level_lib_indices);
  reevaluateTree(state);
  auto feasibility = checkSizingFeasibility(state);
  if (!feasibility.feasible) {
    stats.accumulate(feasibility);
  }

  SolverSizingCandidate candidate;
  candidate.level_lib_indices = level_lib_indices;
  candidate.delay = state.driver->get_max_delay();
  candidate.skew = state.driver->get_max_delay() - state.driver->get_min_delay();
  candidate.area = totalBufferArea(state);
  candidate.power = totalBufferPower(state);
  candidate.feasible = feasibility.feasible;
  candidate.violated_constraints = feasibility.violationCount();
  candidate.violation_score = feasibility.totalViolation();
  candidate.feasibility = feasibility;
  if (candidate.feasible) {
    ++stats.feasible;
  }
  return candidate;
}

SolverFeasibilityResult LevelSizingOperator::checkSizingFeasibility(const SolverPipelineState& state) const
{
  SolverFeasibilityResult result;
  const auto normalized_overflow = [](double actual, double limit) {
    if (actual <= limit + kConstraintEpsilon) {
      return 0.0;
    }
    return (actual - limit) / std::max(std::abs(limit), kConstraintEpsilon);
  };

  if (!TimingPropagator::skewFeasible(state.driver)) {
    result.feasible = false;
    result.skew = true;
    result.skew_over = normalized_overflow(state.driver->get_max_delay() - state.driver->get_min_delay(), TimingPropagator::getSkewBound());
  }

  const double max_cap = TimingPropagator::getMaxCap();
  const double max_length = TimingPropagator::getMaxLength();
  const int max_fanout = TimingPropagator::getMaxFanout();
  const double max_buf_tran = TimingPropagator::getMaxBufTran();
  const double max_sink_tran = TimingPropagator::getMaxSinkTran();

  std::ranges::for_each(state.net_records, [&](const SolverNetRecord& record) {
    auto* net = record.net;
    if (net == nullptr) {
      result.feasible = false;
      result.fanout = true;
      return;
    }

    auto* driver_pin = net->get_driver_pin();
    if (driver_pin == nullptr) {
      result.feasible = false;
      result.fanout = true;
      return;
    }

    if (net->getFanout() > max_fanout) {
      result.feasible = false;
      result.fanout = true;
      result.fanout_over = std::max(result.fanout_over, 1.0 * (net->getFanout() - max_fanout) / std::max(max_fanout, 1));
    }
    if (driver_pin->get_cap_load() > max_cap + kConstraintEpsilon) {
      result.feasible = false;
      result.cap = true;
      result.cap_over = std::max(result.cap_over, normalized_overflow(driver_pin->get_cap_load(), max_cap));
    }
    if (driver_pin->get_sub_len() > max_length + kConstraintEpsilon) {
      result.feasible = false;
      result.length = true;
      result.length_over = std::max(result.length_over, normalized_overflow(driver_pin->get_sub_len(), max_length));
    }

    driver_pin->preOrder([&](Node* node) {
      if (!node->isPin()) {
        return;
      }
      auto* pin = dynamic_cast<Pin*>(node);
      if (pin == nullptr) {
        return;
      }
      if (pin->isSinkPin()) {
        if (pin->get_slew_in() > max_sink_tran + kConstraintEpsilon) {
          result.feasible = false;
          result.sink_slew = true;
          result.sink_slew_over = std::max(result.sink_slew_over, normalized_overflow(pin->get_slew_in(), max_sink_tran));
        }
        return;
      }
      if (pin->get_slew_in() > max_buf_tran + kConstraintEpsilon) {
        result.feasible = false;
        result.buffer_slew = true;
        result.buffer_slew_over = std::max(result.buffer_slew_over, normalized_overflow(pin->get_slew_in(), max_buf_tran));
      }
    });
  });

  return result;
}

void LevelSizingOperator::applyLevelSizing(SolverPipelineState& state, const std::vector<size_t>& level_lib_indices) const
{
  const auto& libs = TimingPropagator::getDelayLibs();
  LOG_FATAL_IF(level_lib_indices.size() != state.buffers_by_depth.size()) << "Net [" << state.net_name << "] sizing depth mismatch.";

  for (size_t depth = 0; depth < state.buffers_by_depth.size(); ++depth) {
    LOG_FATAL_IF(level_lib_indices[depth] >= libs.size())
        << "Net [" << state.net_name << "] invalid library index " << level_lib_indices[depth] << " at depth " << depth;
    const auto& cell_master = libs[level_lib_indices[depth]]->get_cell_master();
    std::ranges::for_each(state.buffers_by_depth[depth], [&](Inst* buffer) { buffer->set_cell_master(cell_master); });
  }
}

void LevelSizingOperator::reevaluateTree(SolverPipelineState& state) const
{
  refreshNetEvaluationOrder(state);
  std::ranges::sort(state.net_records, [](const SolverNetRecord& lhs, const SolverNetRecord& rhs) {
    if (lhs.evaluation_order != rhs.evaluation_order) {
      return lhs.evaluation_order < rhs.evaluation_order;
    }
    return lhs.net->get_name() < rhs.net->get_name();
  });

  std::ranges::for_each(state.net_records, [](const SolverNetRecord& record) { TimingPropagator::update(record.net); });
}

void LevelSizingOperator::refreshNetEvaluationOrder(SolverPipelineState& state) const
{
  std::unordered_map<Net*, int> order_cache;
  std::ranges::for_each(state.net_records, [&](SolverNetRecord& record) {
    record.evaluation_order = computeNetEvaluationOrder(state, record.net, order_cache);
  });
}

int LevelSizingOperator::computeNetEvaluationOrder(const SolverPipelineState& state, Net* net, std::unordered_map<Net*, int>& cache) const
{
  LOG_FATAL_IF(net == nullptr) << "Net [" << state.net_name << "] encountered a null solver net during timing-order refresh.";

  auto cache_it = cache.find(net);
  if (cache_it != cache.end()) {
    return cache_it->second;
  }

  int max_child_order = -1;
  std::ranges::for_each(net->get_load_pins(), [&](Pin* load) {
    if (load == nullptr) {
      return;
    }
    auto* child_inst = load->get_inst();
    if (child_inst == nullptr || child_inst->isSink()) {
      return;
    }

    auto* child_driver = child_inst->get_driver_pin();
    auto* child_net = child_driver == nullptr ? nullptr : child_driver->get_net();
    LOG_FATAL_IF(child_net == nullptr) << "Net [" << state.net_name << "] buffer child " << child_inst->get_name()
                                       << " is missing a downstream solver net.";
    max_child_order = std::max(max_child_order, computeNetEvaluationOrder(state, child_net, cache));
  });

  const int order = max_child_order + 1;
  cache[net] = order;
  return order;
}

void LevelSizingOperator::normalizeCandidates(std::vector<SolverSizingCandidate>& candidates) const
{
  if (candidates.empty()) {
    return;
  }

  auto [delay_min_it, delay_max_it] = std::ranges::minmax_element(candidates, {}, &SolverSizingCandidate::delay);
  auto [area_min_it, area_max_it] = std::ranges::minmax_element(candidates, {}, &SolverSizingCandidate::area);
  auto [power_min_it, power_max_it] = std::ranges::minmax_element(candidates, {}, &SolverSizingCandidate::power);

  const double delay_min = delay_min_it->delay;
  const double delay_max = delay_max_it->delay;
  const double area_min = area_min_it->area;
  const double area_max = area_max_it->area;
  const double power_min = power_min_it->power;
  const double power_max = power_max_it->power;

  auto normalize = [](double value, double min_value, double max_value) {
    auto delta = max_value - min_value;
    return delta < kMetricEpsilon ? 0.0 : (value - min_value) / delta;
  };

  std::ranges::for_each(candidates, [&](SolverSizingCandidate& candidate) {
    candidate.delay_norm = normalize(candidate.delay, delay_min, delay_max);
    candidate.area_norm = normalize(candidate.area, area_min, area_max);
    candidate.power_norm = normalize(candidate.power, power_min, power_max);
    candidate.distance_to_ideal = std::sqrt(candidate.delay_norm * candidate.delay_norm + candidate.area_norm * candidate.area_norm
                                            + candidate.power_norm * candidate.power_norm);
  });
}

bool LevelSizingOperator::dominates(const SolverSizingCandidate& lhs, const SolverSizingCandidate& rhs)
{
  const bool no_worse = lhs.delay_norm <= rhs.delay_norm + kMetricEpsilon && lhs.area_norm <= rhs.area_norm + kMetricEpsilon
                        && lhs.power_norm <= rhs.power_norm + kMetricEpsilon;
  const bool strictly_better = lhs.delay_norm + kMetricEpsilon < rhs.delay_norm || lhs.area_norm + kMetricEpsilon < rhs.area_norm
                               || lhs.power_norm + kMetricEpsilon < rhs.power_norm;
  return no_worse && strictly_better;
}

size_t LevelSizingOperator::selectBalancedCandidate(const std::vector<SolverSizingCandidate>& candidates) const
{
  std::vector<size_t> pareto_indices;
  for (size_t i = 0; i < candidates.size(); ++i) {
    bool dominated = false;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j) {
        continue;
      }
      if (dominates(candidates[j], candidates[i])) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_indices.push_back(i);
    }
  }

  LOG_FATAL_IF(pareto_indices.empty()) << "Failed to compute a Pareto front for level sizing candidates.";

  auto better = [&](size_t lhs_idx, size_t rhs_idx) {
    const auto& lhs = candidates[lhs_idx];
    const auto& rhs = candidates[rhs_idx];
    if (std::abs(lhs.distance_to_ideal - rhs.distance_to_ideal) > kMetricEpsilon) {
      return lhs.distance_to_ideal < rhs.distance_to_ideal;
    }
    const double lhs_worst = std::max({lhs.delay_norm, lhs.area_norm, lhs.power_norm});
    const double rhs_worst = std::max({rhs.delay_norm, rhs.area_norm, rhs.power_norm});
    if (std::abs(lhs_worst - rhs_worst) > kMetricEpsilon) {
      return lhs_worst < rhs_worst;
    }
    const double lhs_sum = lhs.delay_norm + lhs.area_norm + lhs.power_norm;
    const double rhs_sum = rhs.delay_norm + rhs.area_norm + rhs.power_norm;
    if (std::abs(lhs_sum - rhs_sum) > kMetricEpsilon) {
      return lhs_sum < rhs_sum;
    }
    return lhs.level_lib_indices < rhs.level_lib_indices;
  };

  size_t best_index = pareto_indices.front();
  for (size_t idx : pareto_indices) {
    if (better(idx, best_index)) {
      best_index = idx;
    }
  }
  return best_index;
}

size_t LevelSizingOperator::countParetoCandidates(const std::vector<SolverSizingCandidate>& candidates) const
{
  size_t pareto_count = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    bool dominated = false;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j) {
        continue;
      }
      if (dominates(candidates[j], candidates[i])) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      ++pareto_count;
    }
  }
  return pareto_count;
}

std::string LevelSizingOperator::formatFeasibilitySummary(const SolverFeasibilityResult& result) const
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  bool first = true;
  auto append = [&](const char* label, bool violated, double overflow) {
    if (!violated) {
      return;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << label << "=" << overflow;
  };
  append("skew", result.skew, result.skew_over);
  append("buf_slew", result.buffer_slew, result.buffer_slew_over);
  append("sink_slew", result.sink_slew, result.sink_slew_over);
  append("cap", result.cap, result.cap_over);
  append("length", result.length, result.length_over);
  append("fanout", result.fanout, result.fanout_over);
  return first ? "none" : oss.str();
}

double LevelSizingOperator::totalBufferArea(const SolverPipelineState& state) const
{
  double total_area = 0.0;
  std::ranges::for_each(state.buffer_depths, [&](const auto& item) { total_area += _runtime.cellArea(item.first->get_cell_master()); });
  return total_area;
}

double LevelSizingOperator::totalBufferPower(const SolverPipelineState& state) const
{
  double total_power = 0.0;
  std::ranges::for_each(state.buffer_depths,
                        [&](const auto& item) { total_power += _runtime.cellLeakagePower(item.first->get_cell_master()); });
  return total_power;
}

void LevelSizingOperator::logSizingSummary(const SolverPipelineState& state, const SolverSizingCandidate& candidate) const
{
  const auto& libs = TimingPropagator::getDelayLibs();
  const auto assignment_summary = formatSizingAssignmentSummary(candidate.level_lib_indices, state.buffers_by_depth, libs);

  std::ostringstream sizing_summary;
  sizing_summary << std::fixed << std::setprecision(6);
  sizing_summary << "Net [" << state.net_name << "] selected sizing summary => assignment: [" << assignment_summary
                 << "], delay: " << candidate.delay << ", area: " << candidate.area << ", power: " << candidate.power
                 << ", skew: " << candidate.skew << ", normalized: (" << candidate.delay_norm << ", " << candidate.area_norm << ", "
                 << candidate.power_norm << ")";
  LOG_INFO << sizing_summary.str();
  _runtime.saveToLog(sizing_summary.str());
}

}  // namespace icts
