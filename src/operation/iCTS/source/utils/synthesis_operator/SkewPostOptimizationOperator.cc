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
 * @file SkewPostOptimizationOperator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "SkewPostOptimizationOperator.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>
#include <unordered_set>

#include "TimingPropagator.hh"
#include "log/Log.hh"

namespace icts {

namespace {
constexpr double kMetricEpsilon = 1e-12;
constexpr double kConstraintEpsilon = 1e-9;

struct DelayExtrema
{
  Pin* min_sink = nullptr;
  Pin* max_sink = nullptr;
  double min_delay = std::numeric_limits<double>::infinity();
  double max_delay = -std::numeric_limits<double>::infinity();
};

Node* nextConceptualAncestor(Node* node)
{
  if (node == nullptr) {
    return nullptr;
  }

  if (node->isPin()) {
    auto* pin = dynamic_cast<Pin*>(node);
    if (pin != nullptr) {
      if (auto* parent = pin->get_parent(); parent != nullptr) {
        return parent;
      }
      auto* inst = pin->get_inst();
      if (inst != nullptr && pin->isDriver() && inst->get_load_pin() != pin) {
        return inst->get_load_pin();
      }
    }
  }

  return node->get_parent();
}

Pin* traceExtremaSink(Node* node, bool use_min_delay)
{
  if (node == nullptr) {
    return nullptr;
  }

  if (node->isPin()) {
    auto* pin = dynamic_cast<Pin*>(node);
    if (pin == nullptr) {
      return nullptr;
    }
    if (pin->isSinkPin()) {
      return pin;
    }
    if (pin->isLoad()) {
      auto* inst = pin->get_inst();
      if (inst != nullptr && !inst->isSink()) {
        return traceExtremaSink(inst->get_driver_pin(), use_min_delay);
      }
    }
  }

  Node* best_child = nullptr;
  double best_metric = use_min_delay ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
  for (Node* child : node->get_children()) {
    const double edge_delay = TimingPropagator::calcElmoreDelay(node, child);
    const double candidate_metric = edge_delay + (use_min_delay ? child->get_min_delay() : child->get_max_delay());
    const bool better
        = use_min_delay ? (candidate_metric + kMetricEpsilon < best_metric) : (candidate_metric > best_metric + kMetricEpsilon);
    if (better || best_child == nullptr) {
      best_metric = candidate_metric;
      best_child = child;
    }
  }

  if (best_child == nullptr) {
    return nullptr;
  }
  return traceExtremaSink(best_child, use_min_delay);
}

double normalizedOverflow(double actual, double limit)
{
  if (actual <= limit + kConstraintEpsilon) {
    return 0.0;
  }
  return (actual - limit) / std::max(std::abs(limit), kConstraintEpsilon);
}
}  // namespace

void SkewPostOptimizationOperator::run(SolverPipelineState& state) const
{
  if (state.driver == nullptr || state.net_records.empty()) {
    return;
  }
  if (state.buffer_depths.empty()) {
    return;
  }

  const auto& libs = TimingPropagator::getDelayLibs();
  LOG_FATAL_IF(libs.empty()) << "Net [" << state.net_name << "] has no delay libraries for skew post optimization.";

  std::unordered_map<std::string, size_t> lib_index_lookup;
  lib_index_lookup.reserve(libs.size());
  for (size_t i = 0; i < libs.size(); ++i) {
    const auto& master = libs[i]->get_cell_master();
    if (!lib_index_lookup.contains(master)) {
      lib_index_lookup.emplace(master, i);
    }
  }

  reevaluateTree(state);
  const double initial_skew = currentGlobalSkew(state);
  const size_t max_iterations = std::max<size_t>(1, state.buffer_depths.size() * std::max<size_t>(1, libs.size() - 1));

  std::ostringstream start_summary;
  start_summary << std::fixed << std::setprecision(6);
  start_summary << "Net [" << state.net_name << "] skew post optimization start => initial skew: " << initial_skew
                << ", skew bound: " << TimingPropagator::getSkewBound() << ", buffers: " << state.buffer_depths.size()
                << ", delay libs: " << libs.size() << ", max iterations: " << max_iterations;
  LOG_INFO << start_summary.str();
  _runtime.saveToLog(start_summary.str());

  SearchStats stats;
  for (size_t iteration = 0; iteration < max_iterations; ++iteration) {
    ++stats.iterations;
    reevaluateTree(state);
    const double baseline_skew = currentGlobalSkew(state);
    if (baseline_skew <= TimingPropagator::getSkewBound() + kConstraintEpsilon) {
      std::ostringstream converged;
      converged << std::fixed << std::setprecision(6);
      converged << "Net [" << state.net_name << "] skew post optimization converged at iter " << (iteration + 1)
                << " with skew: " << baseline_skew;
      LOG_INFO << converged.str();
      _runtime.saveToLog(converged.str());
      break;
    }

    const auto worst_pair = extractWorstPair(state.driver);
    if (!worst_pair.valid()) {
      std::ostringstream invalid_worst_pair;
      invalid_worst_pair << "Net [" << state.net_name << "] skew post optimization stopped at iter " << (iteration + 1)
                         << " because worst sink pair extraction failed.";
      LOG_WARNING << invalid_worst_pair.str();
      _runtime.saveToLog(invalid_worst_pair.str());
      break;
    }

    auto* lca = computeLca(worst_pair.fast_sink, worst_pair.slow_sink);
    LOG_FATAL_IF(lca == nullptr) << "Net [" << state.net_name << "] skew post optimization failed to compute LCA for "
                                 << worst_pair.fast_sink->get_name() << " and " << worst_pair.slow_sink->get_name();

    auto candidates = collectCandidateBuffers(state, worst_pair.slow_sink, lca);
    if (candidates.empty()) {
      std::ostringstream no_candidate;
      no_candidate << "Net [" << state.net_name << "] skew post optimization stopped at iter " << (iteration + 1)
                   << " because no buffer candidates were found on worst-pair path.";
      LOG_WARNING << no_candidate.str();
      _runtime.saveToLog(no_candidate.str());
      break;
    }

    std::ostringstream iter_summary;
    iter_summary << std::fixed << std::setprecision(6);
    iter_summary << "Net [" << state.net_name << "] skew post optimization iter " << (iteration + 1) << " => skew: " << baseline_skew
                 << ", worst pair: (" << worst_pair.fast_sink->get_name() << ", " << worst_pair.slow_sink->get_name() << "), delays: ("
                 << worst_pair.fast_delay << ", " << worst_pair.slow_delay << "), LCA: " << lca->get_name()
                 << ", candidate buffers: " << candidates.size();
    LOG_INFO << iter_summary.str();
    _runtime.saveToLog(iter_summary.str());

    const double baseline_area = totalBufferArea(state);
    const double baseline_power = totalBufferPower(state);
    auto best_move = findBestMove(state, candidates, baseline_skew, lib_index_lookup, stats);
    if (!best_move.has_value()) {
      std::ostringstream no_move;
      no_move << std::fixed << std::setprecision(6);
      no_move << "Net [" << state.net_name << "] skew post optimization stopped at iter " << (iteration + 1)
              << " because no parent-cap-safe non-worsening move was found. baseline skew: " << baseline_skew;
      LOG_INFO << no_move.str();
      _runtime.saveToLog(no_move.str());
      break;
    }

    auto* target_inst = best_move->inst;
    LOG_FATAL_IF(target_inst == nullptr) << "Net [" << state.net_name << "] skew post optimization selected null move target.";
    target_inst->set_cell_master(libs[best_move->target_lib_index]->get_cell_master());
    reevaluateTree(state);

    const double applied_skew = currentGlobalSkew(state);
    const auto applied_parent_cap = measureParentCapStatus(state, target_inst);
    const bool parent_cap_non_worsening
        = applied_parent_cap.has_value() && parentCapNonWorsening(best_move->baseline_parent_cap, applied_parent_cap.value());
    const bool skew_non_worsening = applied_skew <= baseline_skew + kConstraintEpsilon;
    if (!parent_cap_non_worsening || !skew_non_worsening) {
      target_inst->set_cell_master(best_move->from_master);
      reevaluateTree(state);

      std::ostringstream rollback_summary;
      rollback_summary << std::fixed << std::setprecision(6);
      rollback_summary << "Net [" << state.net_name << "] skew post optimization rollback at iter " << (iteration + 1);
      if (!parent_cap_non_worsening) {
        rollback_summary << " because parent-net cap worsened (baseline: " << formatParentCapStatus(best_move->baseline_parent_cap)
                         << ", applied: "
                         << (applied_parent_cap.has_value() ? formatParentCapStatus(applied_parent_cap.value()) : "unavailable") << ").";
      } else {
        rollback_summary << " because skew worsened (baseline=" << baseline_skew << ", applied=" << applied_skew << ").";
      }
      LOG_WARNING << rollback_summary.str();
      _runtime.saveToLog(rollback_summary.str());
      break;
    }

    ++stats.accepted_moves;
    const double updated_skew = applied_skew;
    const double updated_area = totalBufferArea(state);
    const double updated_power = totalBufferPower(state);

    std::ostringstream accepted_summary;
    accepted_summary << std::fixed << std::setprecision(6);
    accepted_summary << "Net [" << state.net_name << "] skew post optimization accepted iter " << (iteration + 1)
                     << " => inst: " << target_inst->get_name() << ", lib: " << best_move->from_master << " -> "
                     << libs[best_move->target_lib_index]->get_cell_master() << ", skew: " << baseline_skew << " -> " << updated_skew
                     << ", parent-cap: " << formatParentCapStatus(best_move->baseline_parent_cap) << " -> "
                     << (applied_parent_cap.has_value() ? formatParentCapStatus(applied_parent_cap.value()) : "unavailable")
                     << ", area: " << baseline_area << " -> " << updated_area << ", power: " << baseline_power << " -> " << updated_power;
    LOG_INFO << accepted_summary.str();
    _runtime.saveToLog(accepted_summary.str());
  }

  reevaluateTree(state);
  logSummary(state, stats, initial_skew);
}

void SkewPostOptimizationOperator::reevaluateTree(SolverPipelineState& state) const
{
  refreshNetEvaluationOrder(state);
  std::ranges::sort(state.net_records, [](const SolverNetRecord& lhs, const SolverNetRecord& rhs) {
    if (lhs.evaluation_order != rhs.evaluation_order) {
      return lhs.evaluation_order < rhs.evaluation_order;
    }
    if (lhs.net == nullptr || rhs.net == nullptr) {
      return lhs.net < rhs.net;
    }
    return lhs.net->get_name() < rhs.net->get_name();
  });

  std::ranges::for_each(state.net_records, [](const SolverNetRecord& record) {
    if (record.net != nullptr) {
      TimingPropagator::update(record.net);
    }
  });
}

void SkewPostOptimizationOperator::refreshNetEvaluationOrder(SolverPipelineState& state) const
{
  std::unordered_map<Net*, int> order_cache;
  std::ranges::for_each(state.net_records, [&](SolverNetRecord& record) {
    if (record.net == nullptr) {
      record.evaluation_order = -1;
      return;
    }
    record.evaluation_order = computeNetEvaluationOrder(state, record.net, order_cache);
  });
}

int SkewPostOptimizationOperator::computeNetEvaluationOrder(const SolverPipelineState& state, Net* net,
                                                            std::unordered_map<Net*, int>& cache) const
{
  LOG_FATAL_IF(net == nullptr) << "Net [" << state.net_name << "] encountered null solver net during skew post optimization.";

  auto cache_it = cache.find(net);
  if (cache_it != cache.end()) {
    return cache_it->second;
  }

  int max_child_order = -1;
  for (Pin* load : net->get_load_pins()) {
    if (load == nullptr) {
      continue;
    }
    auto* child_inst = load->get_inst();
    if (child_inst == nullptr || child_inst->isSink()) {
      continue;
    }
    auto* child_driver = child_inst->get_driver_pin();
    auto* child_net = child_driver == nullptr ? nullptr : child_driver->get_net();
    LOG_FATAL_IF(child_net == nullptr) << "Net [" << state.net_name << "] buffer child " << child_inst->get_name()
                                       << " is missing downstream solver net in skew post optimization.";
    max_child_order = std::max(max_child_order, computeNetEvaluationOrder(state, child_net, cache));
  }

  const int order = max_child_order + 1;
  cache[net] = order;
  return order;
}

std::optional<SkewPostOptimizationOperator::ParentCapStatus> SkewPostOptimizationOperator::measureParentCapStatus(
    const SolverPipelineState& state, const Inst* inst) const
{
  if (inst == nullptr) {
    return std::nullopt;
  }

  auto* load_pin = inst->get_load_pin();
  if (load_pin == nullptr) {
    LOG_WARNING << "Net [" << state.net_name << "] candidate " << inst->get_name() << " has no load pin during skew post optimization.";
    return std::nullopt;
  }

  auto* parent_net = load_pin->get_net();
  if (parent_net == nullptr) {
    LOG_WARNING << "Net [" << state.net_name << "] candidate " << inst->get_name() << " has no parent net during skew post optimization.";
    return std::nullopt;
  }

  auto* parent_driver_pin = parent_net->get_driver_pin();
  if (parent_driver_pin == nullptr) {
    LOG_WARNING << "Net [" << state.net_name << "] candidate " << inst->get_name()
                << " has a parent net without driver during skew post optimization.";
    return std::nullopt;
  }

  ParentCapStatus status;
  status.net_name = parent_net->get_name();
  status.cap_load = parent_driver_pin->get_cap_load();
  status.limit = TimingPropagator::getMaxCap();
  status.overflow = status.limit > kConstraintEpsilon ? normalizedOverflow(status.cap_load, status.limit) : 0.0;
  return status;
}

bool SkewPostOptimizationOperator::parentCapNonWorsening(const ParentCapStatus& baseline, const ParentCapStatus& updated) const
{
  return updated.overflow <= baseline.overflow + kConstraintEpsilon;
}

SkewPostOptimizationOperator::WorstSinkPair SkewPostOptimizationOperator::extractWorstPair(Pin* root) const
{
  WorstSinkPair pair;
  if (root == nullptr || !std::isfinite(root->get_min_delay()) || !std::isfinite(root->get_max_delay())) {
    return pair;
  }

  pair.fast_sink = traceExtremaSink(root, true);
  pair.slow_sink = traceExtremaSink(root, false);
  pair.fast_delay = root->get_min_delay();
  pair.slow_delay = root->get_max_delay();
  return pair;
}

Node* SkewPostOptimizationOperator::computeLca(Node* lhs, Node* rhs) const
{
  if (lhs == nullptr || rhs == nullptr) {
    return nullptr;
  }

  std::unordered_set<Node*> ancestors;
  for (Node* node = lhs; node != nullptr; node = nextConceptualAncestor(node)) {
    ancestors.insert(node);
  }
  for (Node* node = rhs; node != nullptr; node = nextConceptualAncestor(node)) {
    if (ancestors.contains(node)) {
      return node;
    }
  }
  return nullptr;
}

std::vector<Inst*> SkewPostOptimizationOperator::collectCandidateBuffers(const SolverPipelineState& state, Pin* slow_sink, Node* lca) const
{
  std::vector<Inst*> candidates;
  if (slow_sink == nullptr || lca == nullptr) {
    return candidates;
  }

  std::unordered_set<Inst*> visited;
  auto append_candidate = [&](Node* node) {
    if (node == nullptr || !node->isPin()) {
      return;
    }
    auto* pin = dynamic_cast<Pin*>(node);
    if (pin == nullptr || !pin->isLoad()) {
      return;
    }
    auto* inst = pin->get_inst();
    if (inst == nullptr || !inst->isBuffer() || !state.buffer_depths.contains(inst)) {
      return;
    }
    if (visited.insert(inst).second) {
      candidates.push_back(inst);
    }
  };

  for (Node* node = slow_sink; node != nullptr && node != lca; node = nextConceptualAncestor(node)) {
    append_candidate(node);
  }

  return candidates;
}

std::optional<SkewPostOptimizationOperator::MoveCandidate> SkewPostOptimizationOperator::findBestMove(
    SolverPipelineState& state, const std::vector<Inst*>& candidates, double baseline_skew,
    const std::unordered_map<std::string, size_t>& lib_index_lookup, SearchStats& stats) const
{
  const auto& libs = TimingPropagator::getDelayLibs();
  std::optional<MoveCandidate> best_move;

  for (Inst* inst : candidates) {
    if (inst == nullptr || !inst->isBuffer()) {
      continue;
    }

    const auto from_master = inst->get_cell_master();
    auto lib_it = lib_index_lookup.find(from_master);
    if (lib_it == lib_index_lookup.end()) {
      std::ostringstream missing_lib;
      missing_lib << "Net [" << state.net_name << "] skip skew post candidate " << inst->get_name()
                  << " because current master has no delay-lib index: " << from_master;
      LOG_WARNING << missing_lib.str();
      _runtime.saveToLog(missing_lib.str());
      continue;
    }
    const size_t current_lib_index = lib_it->second;
    if (current_lib_index + 1 >= libs.size()) {
      continue;
    }
    const auto baseline_parent_cap = measureParentCapStatus(state, inst);
    if (!baseline_parent_cap.has_value()) {
      std::ostringstream missing_parent_net;
      missing_parent_net << "Net [" << state.net_name << "] skip skew post candidate " << inst->get_name()
                         << " because parent-net cap status is unavailable.";
      LOG_WARNING << missing_parent_net.str();
      _runtime.saveToLog(missing_parent_net.str());
      continue;
    }

    for (size_t target_index = current_lib_index + 1; target_index < libs.size(); ++target_index) {
      ++stats.evaluated_moves;
      inst->set_cell_master(libs[target_index]->get_cell_master());
      reevaluateTree(state);

      const auto candidate_parent_cap = measureParentCapStatus(state, inst);
      const double skew = currentGlobalSkew(state);
      const bool parent_cap_non_worsening
          = candidate_parent_cap.has_value() && parentCapNonWorsening(baseline_parent_cap.value(), candidate_parent_cap.value());
      if (!parent_cap_non_worsening) {
        ++stats.parent_cap_rejected_moves;
      } else if (skew > baseline_skew + kConstraintEpsilon) {
        ++stats.worsening_moves;
      } else {
        MoveCandidate move;
        move.inst = inst;
        move.from_master = from_master;
        move.target_lib_index = target_index;
        move.skew = skew;
        move.area = totalBufferArea(state);
        move.power = totalBufferPower(state);
        move.baseline_parent_cap = baseline_parent_cap.value();
        move.updated_parent_cap = candidate_parent_cap.value();

        const bool skew_better = !best_move.has_value() || move.skew + kMetricEpsilon < best_move->skew;
        const bool area_better = best_move.has_value() && std::abs(move.skew - best_move->skew) <= kMetricEpsilon
                                 && move.area + kMetricEpsilon < best_move->area;
        const bool power_better = best_move.has_value() && std::abs(move.skew - best_move->skew) <= kMetricEpsilon
                                  && std::abs(move.area - best_move->area) <= kMetricEpsilon
                                  && move.power + kMetricEpsilon < best_move->power;
        if (skew_better || area_better || power_better) {
          best_move = move;
        }
      }

      inst->set_cell_master(from_master);
      reevaluateTree(state);
    }
  }

  return best_move;
}

std::string SkewPostOptimizationOperator::formatParentCapStatus(const ParentCapStatus& status) const
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  oss << status.net_name << "(cap=" << status.cap_load << ", overflow=" << status.overflow << ", limit=" << status.limit << ")";
  return oss.str();
}

double SkewPostOptimizationOperator::currentGlobalSkew(const SolverPipelineState& state) const
{
  if (state.driver == nullptr) {
    return 0.0;
  }
  return state.driver->get_max_delay() - state.driver->get_min_delay();
}

double SkewPostOptimizationOperator::totalBufferArea(const SolverPipelineState& state) const
{
  double total_area = 0.0;
  std::ranges::for_each(state.buffer_depths, [&](const auto& item) { total_area += _runtime.cellArea(item.first->get_cell_master()); });
  return total_area;
}

double SkewPostOptimizationOperator::totalBufferPower(const SolverPipelineState& state) const
{
  double total_power = 0.0;
  std::ranges::for_each(state.buffer_depths,
                        [&](const auto& item) { total_power += _runtime.cellLeakagePower(item.first->get_cell_master()); });
  return total_power;
}

void SkewPostOptimizationOperator::logSummary(const SolverPipelineState& state, const SearchStats& stats, double initial_skew) const
{
  const double final_skew = currentGlobalSkew(state);

  std::ostringstream summary;
  summary << std::fixed << std::setprecision(6);
  summary << "Net [" << state.net_name << "] skew post optimization summary => initial skew: " << initial_skew
          << ", final skew: " << final_skew << ", delta: " << (initial_skew - final_skew) << ", iterations: " << stats.iterations
          << ", evaluated moves: " << stats.evaluated_moves << ", accepted moves: " << stats.accepted_moves
          << ", parent-cap rejected moves: " << stats.parent_cap_rejected_moves << ", worsening moves: " << stats.worsening_moves;
  LOG_INFO << summary.str();
  _runtime.saveToLog(summary.str());
}

}  // namespace icts
