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
 * @file NumericalHTreePatternEvaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical H-tree pattern response-surface evaluation helpers.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "PatternId.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {
namespace {

auto EvaluateRequiredSurface(const NumericalHTreeResponseSurface& surface, double input_slew_ns, double load_cap_pf)
    -> std::optional<double>
{
  const auto value = surface.evaluate(input_slew_ns, load_cap_pf);
  if (!value.has_value() || !numerical_htree::IsFinite(*value)) {
    return std::nullopt;
  }
  return value;
}

auto EvaluateOptionalSurface(const NumericalHTreeResponseSurface& surface, double input_slew_ns, double load_cap_pf, double fallback_value)
    -> std::optional<double>
{
  if (!surface.isValid()) {
    return fallback_value;
  }
  const auto value = surface.evaluate(input_slew_ns, load_cap_pf);
  if (!value.has_value() || !numerical_htree::IsFinite(*value)) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

auto ResolveLevelLoadCap(const NumericalHTreeBuildInput& input, std::size_t level_index) -> std::optional<double>
{
  if (level_index >= input.levels.size()) {
    return std::nullopt;
  }

  const double explicit_load_cap_pf = input.levels[level_index].representative_load_cap_pf;
  if (explicit_load_cap_pf > 0.0 && numerical_htree::IsFinite(explicit_load_cap_pf)) {
    return explicit_load_cap_pf;
  }

  const double leaf_load_cap_pf = input.options.leaf_load_cap_pf;
  if (leaf_load_cap_pf <= 0.0 || !numerical_htree::IsFinite(leaf_load_cap_pf)) {
    return std::nullopt;
  }

  double load_cap_pf = leaf_load_cap_pf;
  if (input.options.max_model_load_cap_pf.has_value() && *input.options.max_model_load_cap_pf > 0.0
      && numerical_htree::IsFinite(*input.options.max_model_load_cap_pf)) {
    load_cap_pf = std::min(load_cap_pf, *input.options.max_model_load_cap_pf);
  }
  return load_cap_pf;
}

auto EvaluatePatternModel(const NumericalHTreePatternModel& model, const NumericalHTreeOptions& options, unsigned level_index,
                          double input_slew_ns, double load_cap_pf, double fanout_multiplier) -> std::optional<NumericalHTreeLevelResult>
{
  if (options.max_model_slew_ns.has_value() && *options.max_model_slew_ns > 0.0 && numerical_htree::IsFinite(*options.max_model_slew_ns)
      && input_slew_ns > *options.max_model_slew_ns + numerical_htree::kEpsilon) {
    return std::nullopt;
  }
  if (options.max_model_load_cap_pf.has_value() && *options.max_model_load_cap_pf > 0.0
      && numerical_htree::IsFinite(*options.max_model_load_cap_pf)
      && load_cap_pf > *options.max_model_load_cap_pf + numerical_htree::kEpsilon) {
    return std::nullopt;
  }

  const auto delay_ns = EvaluateRequiredSurface(model.delay_ns, input_slew_ns, load_cap_pf);
  const auto output_slew_ns = EvaluateRequiredSurface(model.output_slew_ns, input_slew_ns, load_cap_pf);
  const auto driven_cap_pf = EvaluateRequiredSurface(model.driven_cap_pf, input_slew_ns, load_cap_pf);
  const auto power_w = EvaluateRequiredSurface(model.power_w, input_slew_ns, load_cap_pf);
  const auto source_boundary_switch_power_w
      = EvaluateOptionalSurface(model.source_boundary_switch_power_w, input_slew_ns, load_cap_pf, 0.0);

  if (!delay_ns.has_value() || !output_slew_ns.has_value() || !driven_cap_pf.has_value() || !power_w.has_value()
      || !source_boundary_switch_power_w.has_value()) {
    return std::nullopt;
  }

  if (*output_slew_ns <= 0.0 || *driven_cap_pf < 0.0) {
    return std::nullopt;
  }
  if (options.max_model_slew_ns.has_value() && *options.max_model_slew_ns > 0.0 && numerical_htree::IsFinite(*options.max_model_slew_ns)
      && *output_slew_ns > *options.max_model_slew_ns + numerical_htree::kEpsilon) {
    return std::nullopt;
  }

  if (options.require_non_negative_qor && (*delay_ns < 0.0 || *power_w < 0.0 || *source_boundary_switch_power_w < 0.0)) {
    return std::nullopt;
  }

  NumericalHTreeLevelResult result;
  result.level_index = level_index;
  result.segment_pattern_id = model.pattern_id;
  result.model_name = model.model_name;
  result.input_slew_ns = input_slew_ns;
  result.load_cap_pf = load_cap_pf;
  result.output_slew_ns = *output_slew_ns;
  result.driven_cap_pf = *driven_cap_pf;
  result.delay_ns = *delay_ns;
  result.power_w = *power_w;
  result.source_boundary_switch_power_w = *source_boundary_switch_power_w;
  result.composed_power_contribution_w = level_index == 0U ? *power_w : fanout_multiplier * (*power_w - *source_boundary_switch_power_w);
  return result;
}

}  // namespace icts
