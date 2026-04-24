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
 * @file NumericalHTreeInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Shared internal helpers for numerical H-tree flow translation units.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "numerical_htree/NumericalHTreeBuilder.hh"

namespace icts::numerical_htree {

constexpr double kEpsilon = 1e-15;

inline auto IsFinite(double value) -> bool
{
  return std::isfinite(value);
}

}  // namespace icts::numerical_htree

namespace icts {

struct NumericalHTreeEvaluatedState
{
  std::vector<NumericalHTreeLevelResult> levels;
  std::vector<PatternId> pattern_ids;
  double next_input_slew_ns = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double score = 0.0;
  double fanout_multiplier = 1.0;
};

struct NumericalHTreeLevelSearchResult
{
  bool success = false;
  std::string failure_reason;
  std::vector<NumericalHTreeEvaluatedState> states;
};

auto CollectModelMetrics(const NumericalHTreeBuildInput& input) -> std::vector<NumericalHTreeModelMetric>;

auto SummarizeModelQuality(const NumericalHTreeBuildInput& input) -> NumericalHTreeModelQualitySummary;

auto ResolveLevelLoadCap(const NumericalHTreeBuildInput& input, std::size_t level_index) -> std::optional<double>;

auto EvaluatePatternModel(const NumericalHTreePatternModel& model, const NumericalHTreeOptions& options, unsigned level_index,
                          double input_slew_ns, double load_cap_pf, double fanout_multiplier) -> std::optional<NumericalHTreeLevelResult>;

auto CalcScore(const NumericalHTreeOptions& options, const NumericalHTreeEvaluatedState& state) -> double;

auto PreferState(const NumericalHTreeEvaluatedState& lhs, const NumericalHTreeEvaluatedState& rhs) -> bool;

auto SortNumericalHTreeStates(std::vector<NumericalHTreeEvaluatedState>& states) -> void;

auto SelectBestFinalState(const std::vector<NumericalHTreeEvaluatedState>& states) -> const NumericalHTreeEvaluatedState*;

auto MakeInitialNumericalHTreeStates(double top_input_slew_ns) -> std::vector<NumericalHTreeEvaluatedState>;

auto SearchNumericalHTreeLevel(const NumericalHTreeBuildInput& input, std::size_t level_index,
                               const std::vector<NumericalHTreeEvaluatedState>& states, NumericalHTreeResult& result)
    -> NumericalHTreeLevelSearchResult;

}  // namespace icts
