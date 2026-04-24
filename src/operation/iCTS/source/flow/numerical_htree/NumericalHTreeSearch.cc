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
 * @file NumericalHTreeSearch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Level-by-level numerical H-tree pattern candidate search.
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <ratio>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto BuildFailureResult(const NumericalHTreeBuildInput& input, const std::string& failure_reason, std::optional<unsigned> failure_level,
                        const std::chrono::steady_clock::time_point& start_time) -> NumericalHTreeResult
{
  NumericalHTreeResult result;
  result.failure_reason = failure_reason;
  result.failure_level = failure_level;
  result.model_quality_summary = SummarizeModelQuality(input);
  result.model_metrics = CollectModelMetrics(input);
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

}  // namespace

auto NumericalHTreeBuilder::build(const NumericalHTreeBuildInput& input) -> NumericalHTreeResult
{
  const auto start_time = std::chrono::steady_clock::now();

  if (input.options.top_k_per_level == 0U) {
    return BuildFailureResult(input, "top_k_per_level_must_be_positive", std::nullopt, start_time);
  }
  if (input.levels.empty()) {
    return BuildFailureResult(input, "missing_htree_levels", std::nullopt, start_time);
  }
  if (input.options.top_input_slew_ns <= 0.0 || !numerical_htree::IsFinite(input.options.top_input_slew_ns)) {
    return BuildFailureResult(input, "invalid_top_input_slew", std::nullopt, start_time);
  }

  auto states = MakeInitialNumericalHTreeStates(input.options.top_input_slew_ns);

  NumericalHTreeResult result;
  result.level_candidate_state_counts.reserve(input.levels.size());
  result.level_surviving_state_counts.reserve(input.levels.size());

  for (std::size_t level_index = 0U; level_index < input.levels.size(); ++level_index) {
    auto level_search = SearchNumericalHTreeLevel(input, level_index, states, result);
    if (!level_search.success) {
      return BuildFailureResult(input, level_search.failure_reason, static_cast<unsigned>(level_index), start_time);
    }
    states = std::move(level_search.states);
  }

  SortNumericalHTreeStates(states);
  const auto* best_state = SelectBestFinalState(states);
  if (best_state == nullptr) {
    return BuildFailureResult(input, "no_final_state_selected", std::nullopt, start_time);
  }

  result.success = true;
  result.selected_depth = static_cast<unsigned>(best_state->levels.size());
  result.selected_levels = best_state->levels.size();
  result.delay_ns = best_state->delay_ns;
  result.power_w = best_state->power_w;
  result.selected_delay_ns = best_state->delay_ns;
  result.selected_power_w = best_state->power_w;
  result.selected_score = best_state->score;
  result.selected_segment_pattern_ids = best_state->pattern_ids;
  result.levels = best_state->levels;
  result.level_results = best_state->levels;
  result.model_metrics = CollectModelMetrics(input);
  result.model_quality_summary = SummarizeModelQuality(input);
  const auto end_time = std::chrono::steady_clock::now();
  result.runtime_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  return result;
}

}  // namespace icts
