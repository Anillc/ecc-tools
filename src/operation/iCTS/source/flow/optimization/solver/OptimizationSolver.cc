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
 * @file OptimizationSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Solver loops and trial evaluation for CTS post-synthesis optimization.
 */

#include "optimization/solver/OptimizationSolver.hh"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "FastSta.hh"
#include "FastStaTypes.hh"
#include "Log.hh"
#include "optimization/candidate/OptimizationCandidates.hh"
#include "optimization/model/OptimizationTypes.hh"
#include "optimization/options/OptimizationOptions.hh"
#include "optimization/report/OptimizationReport.hh"
#include "optimization/state/OptimizationState.hh"

namespace icts::optimization_internal {

namespace {

auto ActionDriveMagnitude(const std::vector<SizingAction>& actions) -> int
{
  int magnitude = 0;
  for (const auto& action : actions) {
    magnitude += std::abs(action.drive_step);
  }
  return magnitude;
}

auto PreferTrial(const BatchTrial& candidate, const BatchTrial& incumbent, const FastState& current, double target_skew_ns) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  if (!incumbent.valid) {
    return true;
  }

  const bool current_met = TargetMet(current, target_skew_ns);
  const bool candidate_met = TargetMet(candidate.state, target_skew_ns);
  const bool incumbent_met = TargetMet(incumbent.state, target_skew_ns);

  if (!current_met && candidate_met != incumbent_met) {
    return candidate_met;
  }
  if (current_met || candidate_met) {
    if (candidate.state.power.area_um2 < incumbent.state.power.area_um2 - kOptimizationEpsilon) {
      return true;
    }
    if (std::abs(candidate.state.power.area_um2 - incumbent.state.power.area_um2) > kOptimizationEpsilon) {
      return false;
    }
  }

  if (candidate.state.skew.skew_ns < incumbent.state.skew.skew_ns - kOptimizationEpsilon) {
    return true;
  }
  if (std::abs(candidate.state.skew.skew_ns - incumbent.state.skew.skew_ns) > kOptimizationEpsilon) {
    return false;
  }
  if (candidate.state.power.area_um2 < incumbent.state.power.area_um2 - kOptimizationEpsilon) {
    return true;
  }
  if (std::abs(candidate.state.power.area_um2 - incumbent.state.power.area_um2) > kOptimizationEpsilon) {
    return false;
  }
  if (candidate.actions.size() != incumbent.actions.size()) {
    return candidate.actions.size() < incumbent.actions.size();
  }
  const auto candidate_drive_magnitude = ActionDriveMagnitude(candidate.actions);
  const auto incumbent_drive_magnitude = ActionDriveMagnitude(incumbent.actions);
  if (candidate_drive_magnitude != incumbent_drive_magnitude) {
    return candidate_drive_magnitude < incumbent_drive_magnitude;
  }
  return FirstActionBufferIndex(candidate.actions) < FirstActionBufferIndex(incumbent.actions);
}

auto ChangeFastStaMasters(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!FastSTA::changeBufferMasters(clock_id, changes)) {
    return false;
  }
  const auto* context = FastSTA::queryClockContext(clock_id);
  return context != nullptr && context->timing_valid && context->power_valid;
}

auto ChangeFastStaMastersTimingOnly(FastStaClockId clock_id, const std::vector<FastStaBufferMasterChange>& changes) -> bool
{
  if (!FastSTA::changeBufferMastersTimingOnly(clock_id, changes)) {
    return false;
  }
  const auto* context = FastSTA::queryClockContext(clock_id);
  return context != nullptr && context->timing_valid;
}

auto BuildMasterChanges(const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions, bool restore)
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

auto ActionAreaDelta(const std::vector<SizingAction>& actions) -> double
{
  double area_delta_um2 = 0.0;
  for (const auto& action : actions) {
    area_delta_um2 += action.area_delta_um2;
  }
  return area_delta_um2;
}

auto TryBatch(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions,
              const FastState& current, const std::vector<CapBaseline>& cap_baseline, const std::vector<SlewBaseline>& slew_baseline,
              double target_skew_ns) -> BatchTrial
{
  BatchTrial trial;
  trial.actions = actions;
  if (trial.actions.empty()) {
    return trial;
  }

  if (!ChangeFastStaMasters(clock_id, BuildMasterChanges(buffers, trial.actions, false))) {
    return trial;
  }
  trial.state = CaptureState(clock_id, cap_baseline, slew_baseline);
  trial.valid = StateImproves(current, trial.state, target_skew_ns);
  if (!ChangeFastStaMasters(clock_id, BuildMasterChanges(buffers, trial.actions, true))) {
    LOG_FATAL << "Optimization: failed to restore fast STA batch trial.";
  }
  return trial;
}

auto TryBatchTimingOnly(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const std::vector<SizingAction>& actions,
                        const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> BatchTrial
{
  BatchTrial trial;
  trial.actions = actions;
  if (trial.actions.empty()) {
    return trial;
  }

  if (!ChangeFastStaMastersTimingOnly(clock_id, BuildMasterChanges(buffers, trial.actions, false))) {
    return trial;
  }
  trial.state = CaptureStateWithArea(clock_id, cap_baseline, slew_baseline, current.power.area_um2 + ActionAreaDelta(trial.actions));
  trial.valid = StateImproves(current, trial.state, target_skew_ns);
  if (!ChangeFastStaMastersTimingOnly(clock_id, BuildMasterChanges(buffers, trial.actions, true))) {
    LOG_FATAL << "Optimization: failed to restore fast STA timing-only batch trial.";
  }
  return trial;
}

auto FindBestBatchTrial(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                        const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns, ClockOptimizationSummary& summary)
    -> BatchTrial
{
  BatchTrial best;
  const auto candidate_start = std::chrono::steady_clock::now();
  const auto candidates = GenerateBatchCandidates(clock_id, buffers, topology, current);
  summary.profile.generate_batch_candidates_s += ElapsedSeconds(candidate_start);
  summary.profile.generated_candidate_count += candidates.size();
  LOG_INFO << "Optimization: solve iteration " << (summary.iteration_count + 1U) << " starts with current_skew=" << current.skew.skew_ns
           << " ns, current_area=" << current.power.area_um2 << " um^2, candidates=" << candidates.size()
           << ", total_trials=" << summary.trial_count << ".";
  const auto iteration_start = std::chrono::steady_clock::now();
  for (std::size_t candidate_index = 0U; candidate_index < candidates.size(); ++candidate_index) {
    if (summary.trial_count >= DefaultOptimizationOptions().max_trials) {
      break;
    }
    const auto& actions = candidates.at(candidate_index);
    ++summary.trial_count;
    ++summary.batch_trial_count;
    if (summary.trial_count <= DefaultOptimizationOptions().initial_detailed_trials) {
      LOG_INFO << "Optimization: start batch trial " << summary.trial_count << ", candidate=" << (candidate_index + 1U) << "/"
               << candidates.size() << ", action_count=" << actions.size() << ".";
    }
    const auto trial_start = std::chrono::steady_clock::now();
    auto trial = TryBatch(clock_id, buffers, actions, current, cap_baseline, slew_baseline, target_skew_ns);
    const double trial_runtime_s = ElapsedSeconds(trial_start);
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
    if (PreferTrial(trial, best, current, target_skew_ns)) {
      best = std::move(trial);
    }
    if (summary.trial_count <= DefaultOptimizationOptions().initial_detailed_trials
        || summary.trial_count % DefaultOptimizationOptions().trial_progress_interval == 0U
        || trial_runtime_s >= DefaultOptimizationOptions().slow_trial_log_threshold_s) {
      LOG_INFO << "Optimization: batch trial progress, iteration=" << (summary.iteration_count + 1U)
               << ", candidate=" << (candidate_index + 1U) << "/" << candidates.size() << ", total_trials=" << summary.trial_count
               << ", trial_runtime=" << trial_runtime_s << " s, iteration_runtime=" << ElapsedSeconds(iteration_start)
               << " s, best_skew=" << (best.valid ? best.state.skew.skew_ns : current.skew.skew_ns) << " ns.";
    }
  }
  return best;
}

auto FindBestScalableBatchTrial(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers, const TopologyIndex& topology,
                                const FastState& current, const std::vector<CapBaseline>& cap_baseline,
                                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns, ClockOptimizationSummary& summary)
    -> BatchTrial
{
  BatchTrial best;
  const auto candidate_start = std::chrono::steady_clock::now();
  const auto candidates = GenerateScalableBatchCandidates(clock_id, buffers, topology, current, target_skew_ns);
  summary.profile.generate_batch_candidates_s += ElapsedSeconds(candidate_start);
  summary.profile.generated_candidate_count += candidates.size();
  LOG_INFO << "Optimization: scalable solve iteration " << (summary.iteration_count + 1U)
           << " starts with current_skew=" << current.skew.skew_ns << " ns, current_area=" << current.power.area_um2
           << " um^2, scored_batches=" << candidates.size()
           << ", exact_trial_limit=" << DefaultOptimizationOptions().max_scalable_exact_trials_per_iteration
           << ", total_trials=" << summary.trial_count << ".";

  const auto iteration_start = std::chrono::steady_clock::now();
  const auto exact_trial_count = std::min(DefaultOptimizationOptions().max_scalable_exact_trials_per_iteration, candidates.size());
  for (std::size_t candidate_index = 0U; candidate_index < exact_trial_count; ++candidate_index) {
    if (summary.trial_count >= DefaultOptimizationOptions().max_trials) {
      break;
    }
    const auto& candidate = candidates.at(candidate_index);
    ++summary.trial_count;
    ++summary.batch_trial_count;
    LOG_INFO << "Optimization: scalable batch trial " << summary.trial_count << ", candidate=" << (candidate_index + 1U) << "/"
             << candidates.size() << ", action_count=" << candidate.actions.size() << ", score=" << candidate.score << ".";
    const auto trial_start = std::chrono::steady_clock::now();
    auto trial = TryBatchTimingOnly(clock_id, buffers, candidate.actions, current, cap_baseline, slew_baseline, target_skew_ns);
    const double trial_runtime_s = ElapsedSeconds(trial_start);
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
    if (PreferTrial(trial, best, current, target_skew_ns)) {
      best = std::move(trial);
    }
    LOG_INFO << "Optimization: scalable batch trial finished, iteration=" << (summary.iteration_count + 1U)
             << ", candidate=" << (candidate_index + 1U) << "/" << candidates.size() << ", total_trials=" << summary.trial_count
             << ", trial_runtime=" << trial_runtime_s << " s, iteration_runtime=" << ElapsedSeconds(iteration_start)
             << " s, best_skew=" << (best.valid ? best.state.skew.skew_ns : current.skew.skew_ns) << " ns.";
  }
  return best;
}

}  // namespace

auto SolveClock(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary
{
  ClockOptimizationSummary summary;
  summary.solve_mode = "exact_full_power_batch";
  auto stage_start = std::chrono::steady_clock::now();
  summary.before = CaptureState(clock_id, cap_baseline, slew_baseline);
  summary.profile.capture_initial_state_s = ElapsedSeconds(stage_start);
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
  const auto topology = BuildTopologyIndex(clock_id, buffers);
  summary.profile.build_topology_index_s = ElapsedSeconds(stage_start);
  auto current = summary.before;
  while (summary.iteration_count < DefaultOptimizationOptions().max_iterations
         && summary.trial_count < DefaultOptimizationOptions().max_trials) {
    auto best = FindBestBatchTrial(clock_id, buffers, topology, current, cap_baseline, slew_baseline, target_skew_ns, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= DefaultOptimizationOptions().max_trials ? "trial_limit" : "no_improving_candidate";
      break;
    }
    stage_start = std::chrono::steady_clock::now();
    if (!ChangeFastStaMasters(clock_id, BuildMasterChanges(buffers, best.actions, false))) {
      summary.stop_reason = "accepted_mutation_apply_failed";
      break;
    }
    current = CaptureState(clock_id, cap_baseline, slew_baseline);
    summary.profile.apply_accepted_batch_s += ElapsedSeconds(stage_start);
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
  summary.target_met = TargetMet(summary.after, target_skew_ns);
  if (summary.stop_reason.empty()) {
    summary.stop_reason = summary.target_met ? "target_met" : "iteration_limit";
  }
  return summary;
}

auto SolveClockScalable(FastStaClockId clock_id, std::vector<OptimizableBuffer>& buffers, const std::vector<CapBaseline>& cap_baseline,
                        const std::vector<SlewBaseline>& slew_baseline, double target_skew_ns) -> ClockOptimizationSummary
{
  ClockOptimizationSummary summary;
  summary.solve_mode = "scalable_timing_only_batch";
  auto stage_start = std::chrono::steady_clock::now();
  summary.before = CaptureState(clock_id, cap_baseline, slew_baseline);
  summary.profile.capture_initial_state_s = ElapsedSeconds(stage_start);
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
  const auto topology = BuildTopologyIndex(clock_id, buffers);
  summary.profile.build_topology_index_s = ElapsedSeconds(stage_start);
  auto current = summary.before;
  while (summary.iteration_count < DefaultOptimizationOptions().max_iterations
         && summary.trial_count < DefaultOptimizationOptions().max_trials) {
    auto best = FindBestScalableBatchTrial(clock_id, buffers, topology, current, cap_baseline, slew_baseline, target_skew_ns, summary);
    if (!best.valid) {
      summary.stop_reason = summary.trial_count >= DefaultOptimizationOptions().max_trials ? "trial_limit" : "no_improving_candidate";
      break;
    }

    stage_start = std::chrono::steady_clock::now();
    if (!ChangeFastStaMastersTimingOnly(clock_id, BuildMasterChanges(buffers, best.actions, false))) {
      summary.stop_reason = "accepted_mutation_apply_failed";
      break;
    }
    current = CaptureStateWithArea(clock_id, cap_baseline, slew_baseline, current.power.area_um2 + ActionAreaDelta(best.actions));
    summary.profile.apply_accepted_batch_s += ElapsedSeconds(stage_start);
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

  if (!FastSTA::updatePower(clock_id)) {
    summary.after = current;
    summary.valid = false;
    if (summary.stop_reason.empty()) {
      summary.stop_reason = "final_power_update_failed";
    }
    return summary;
  }

  summary.after = CaptureState(clock_id, cap_baseline, slew_baseline);
  summary.valid = summary.before.valid && summary.after.valid;
  summary.changed = !summary.mutations.empty();
  summary.target_met = TargetMet(summary.after, target_skew_ns);
  if (summary.stop_reason.empty()) {
    summary.stop_reason = summary.target_met ? "target_met" : "iteration_limit";
  }
  return summary;
}

auto ShouldUseScalableSolver(FastStaClockId clock_id, const std::vector<OptimizableBuffer>& buffers) -> bool
{
  const auto* context = FastSTA::queryClockContext(clock_id);
  if (context == nullptr) {
    return false;
  }
  return context->nodes.size() >= DefaultOptimizationOptions().scalable_node_threshold
         || buffers.size() >= DefaultOptimizationOptions().scalable_buffer_threshold;
}

}  // namespace icts::optimization_internal
