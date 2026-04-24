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
 * @file NumericalHTreeLevelSearch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree per-level beam expansion and pruning.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto AppendExpandedStatesForLevel(const NumericalHTreeBuildInput& input, std::size_t level_index, double load_cap_pf,
                                  const std::vector<NumericalHTreeEvaluatedState>& states,
                                  std::vector<NumericalHTreeEvaluatedState>& next_states, NumericalHTreeResult& result) -> void
{
  const auto& level = input.levels[level_index];
  for (const auto& state : states) {
    for (const auto& model : level.pattern_models) {
      ++result.evaluated_candidate_count;
      auto level_result = EvaluatePatternModel(model, input.options, static_cast<unsigned>(level_index), state.next_input_slew_ns,
                                               load_cap_pf, state.fanout_multiplier);
      if (!level_result.has_value()) {
        continue;
      }
      if (input.options.require_positive_leaf_power && level_index + 1U == input.levels.size()
          && level_result->power_w <= numerical_htree::kEpsilon) {
        continue;
      }

      NumericalHTreeEvaluatedState next_state = state;
      next_state.levels.push_back(*level_result);
      next_state.pattern_ids.push_back(level_result->segment_pattern_id);
      next_state.next_input_slew_ns = level_result->output_slew_ns;
      next_state.delay_ns += level_result->delay_ns;
      next_state.power_w += level_result->composed_power_contribution_w;
      next_state.fanout_multiplier *= 2.0;
      next_state.score = CalcScore(input.options, next_state);
      if (!numerical_htree::IsFinite(next_state.score) || !numerical_htree::IsFinite(next_state.power_w)
          || !numerical_htree::IsFinite(next_state.delay_ns)) {
        continue;
      }
      next_states.push_back(std::move(next_state));
    }
  }
}

auto PruneLevelStates(std::vector<NumericalHTreeEvaluatedState>& next_states, const NumericalHTreeOptions& options,
                      NumericalHTreeResult& result) -> void
{
  SortNumericalHTreeStates(next_states);
  result.level_candidate_state_counts.push_back(next_states.size());
  if (next_states.size() > options.top_k_per_level) {
    result.pruned_candidate_count += next_states.size() - options.top_k_per_level;
    next_states.resize(options.top_k_per_level);
  }
  result.level_surviving_state_counts.push_back(next_states.size());
}

}  // namespace

auto MakeInitialNumericalHTreeStates(double top_input_slew_ns) -> std::vector<NumericalHTreeEvaluatedState>
{
  std::vector<NumericalHTreeEvaluatedState> states;
  states.push_back(NumericalHTreeEvaluatedState{
      .levels = {},
      .pattern_ids = {},
      .next_input_slew_ns = top_input_slew_ns,
      .delay_ns = 0.0,
      .power_w = 0.0,
      .score = 0.0,
      .fanout_multiplier = 1.0,
  });
  return states;
}

auto SearchNumericalHTreeLevel(const NumericalHTreeBuildInput& input, std::size_t level_index,
                               const std::vector<NumericalHTreeEvaluatedState>& states, NumericalHTreeResult& result)
    -> NumericalHTreeLevelSearchResult
{
  const auto load_cap_pf = ResolveLevelLoadCap(input, level_index);
  if (!load_cap_pf.has_value()) {
    return NumericalHTreeLevelSearchResult{.success = false, .failure_reason = "invalid_level_load_cap", .states = {}};
  }

  const auto& level = input.levels[level_index];
  if (level.pattern_models.empty()) {
    return NumericalHTreeLevelSearchResult{.success = false, .failure_reason = "missing_level_pattern_models", .states = {}};
  }

  std::vector<NumericalHTreeEvaluatedState> next_states;
  next_states.reserve(states.size() * level.pattern_models.size());
  AppendExpandedStatesForLevel(input, level_index, *load_cap_pf, states, next_states, result);

  if (next_states.empty()) {
    return NumericalHTreeLevelSearchResult{.success = false, .failure_reason = "no_valid_level_candidates", .states = {}};
  }

  PruneLevelStates(next_states, input.options, result);
  return NumericalHTreeLevelSearchResult{.success = true, .failure_reason = {}, .states = std::move(next_states)};
}

}  // namespace icts
