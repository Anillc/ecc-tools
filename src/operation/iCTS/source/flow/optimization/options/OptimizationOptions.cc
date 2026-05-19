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
 * @file OptimizationOptions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Optimizer-owned policy defaults for CTS post-synthesis optimization.
 */

#include "optimization/options/OptimizationOptions.hh"

namespace icts::optimization_internal {

auto DefaultOptimizationOptions() -> const OptimizationOptions&
{
  static const OptimizationOptions options;
  return options;
}

auto ValidateOptimizationOptions(const OptimizationOptions& options) -> bool
{
  return options.max_iterations > 0U && options.max_trials > 0U && options.max_frontier_sinks > 0U && options.max_batch_actions > 0U
         && options.max_batch_trials_per_iteration > 0U && options.trial_progress_interval > 0U && options.initial_detailed_trials > 0U
         && options.slow_trial_log_threshold_s > 0.0 && options.max_scalable_batch_actions > 0U
         && options.max_scalable_exact_trials_per_iteration > 0U && options.target_window_shrink_ratio > 0.0
         && options.target_window_shrink_ratio <= 1.0 && options.min_branch_purity >= 0.0 && options.min_branch_purity <= 1.0
         && options.max_opposite_violation_ratio >= 0.0 && options.route_tree_progress_interval > 0U
         && options.route_tree_initial_detail_net_count > 0U && options.route_tree_slow_net_threshold_s > 0.0;
}

}  // namespace icts::optimization_internal
