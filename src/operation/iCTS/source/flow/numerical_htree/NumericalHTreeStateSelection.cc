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
 * @file NumericalHTreeStateSelection.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree candidate-state scoring and final selection helpers.
 */

#include <algorithm>
#include <cstddef>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto PatternIdLexLess(const std::vector<PatternId>& lhs, const std::vector<PatternId>& rhs) -> bool
{
  const std::size_t common_size = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0U; index < common_size; ++index) {
    const unsigned lhs_pack = lhs[index].pack();
    const unsigned rhs_pack = rhs[index].pack();
    if (lhs_pack != rhs_pack) {
      return lhs_pack < rhs_pack;
    }
  }
  return lhs.size() < rhs.size();
}

auto DelayPowerDominates(const NumericalHTreeEvaluatedState& lhs, const NumericalHTreeEvaluatedState& rhs) -> bool
{
  const bool not_worse = lhs.delay_ns <= rhs.delay_ns && lhs.power_w <= rhs.power_w;
  const bool strictly_better = lhs.delay_ns < rhs.delay_ns || lhs.power_w < rhs.power_w;
  return not_worse && strictly_better;
}

auto PreferPowerMedianOrder(const NumericalHTreeEvaluatedState& lhs, const NumericalHTreeEvaluatedState& rhs) -> bool
{
  if (lhs.power_w != rhs.power_w) {
    return lhs.power_w < rhs.power_w;
  }
  if (lhs.delay_ns != rhs.delay_ns) {
    return lhs.delay_ns < rhs.delay_ns;
  }
  if (lhs.levels.empty() || rhs.levels.empty()) {
    return lhs.levels.size() < rhs.levels.size();
  }
  const auto& lhs_leaf = lhs.levels.back();
  const auto& rhs_leaf = rhs.levels.back();
  if (lhs_leaf.driven_cap_pf != rhs_leaf.driven_cap_pf) {
    return lhs_leaf.driven_cap_pf < rhs_leaf.driven_cap_pf;
  }
  if (lhs_leaf.output_slew_ns != rhs_leaf.output_slew_ns) {
    return lhs_leaf.output_slew_ns < rhs_leaf.output_slew_ns;
  }
  return PatternIdLexLess(lhs.pattern_ids, rhs.pattern_ids);
}

}  // namespace

auto CalcScore(const NumericalHTreeOptions& options, const NumericalHTreeEvaluatedState& state) -> double
{
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  if (!state.levels.empty()) {
    output_slew_ns = state.levels.back().output_slew_ns;
    driven_cap_pf = state.levels.back().driven_cap_pf;
  }
  return options.delay_weight * state.delay_ns + options.power_weight * state.power_w + options.output_slew_weight * output_slew_ns
         + options.driven_cap_weight * driven_cap_pf;
}

auto PreferState(const NumericalHTreeEvaluatedState& lhs, const NumericalHTreeEvaluatedState& rhs) -> bool
{
  if (lhs.score != rhs.score) {
    return lhs.score < rhs.score;
  }
  if (lhs.delay_ns != rhs.delay_ns) {
    return lhs.delay_ns < rhs.delay_ns;
  }
  if (lhs.power_w != rhs.power_w) {
    return lhs.power_w < rhs.power_w;
  }
  if (lhs.next_input_slew_ns != rhs.next_input_slew_ns) {
    return lhs.next_input_slew_ns < rhs.next_input_slew_ns;
  }
  return PatternIdLexLess(lhs.pattern_ids, rhs.pattern_ids);
}

auto SortNumericalHTreeStates(std::vector<NumericalHTreeEvaluatedState>& states) -> void
{
  std::ranges::sort(states, PreferState);
}

auto SelectBestFinalState(const std::vector<NumericalHTreeEvaluatedState>& states) -> const NumericalHTreeEvaluatedState*
{
  if (states.empty()) {
    return nullptr;
  }

  std::vector<const NumericalHTreeEvaluatedState*> pareto_front;
  pareto_front.reserve(states.size());
  for (std::size_t state_index = 0U; state_index < states.size(); ++state_index) {
    bool dominated = false;
    for (std::size_t other_index = 0U; other_index < states.size(); ++other_index) {
      if (state_index == other_index) {
        continue;
      }
      if (DelayPowerDominates(states.at(other_index), states.at(state_index))) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_front.push_back(&states.at(state_index));
    }
  }

  if (pareto_front.empty()) {
    return &states.front();
  }
  std::ranges::sort(pareto_front, [](const NumericalHTreeEvaluatedState* lhs, const NumericalHTreeEvaluatedState* rhs) -> bool {
    if (lhs == nullptr || rhs == nullptr) {
      return lhs != nullptr;
    }
    return PreferPowerMedianOrder(*lhs, *rhs);
  });
  return pareto_front.at((pareto_front.size() - 1U) / 2U);
}

}  // namespace icts
