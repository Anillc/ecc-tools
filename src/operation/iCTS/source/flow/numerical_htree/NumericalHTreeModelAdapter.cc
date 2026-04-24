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
 * @file NumericalHTreeModelAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Converts fitted numerical characterization models into H-tree pattern models.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "numerical_characterization/FitMetrics.hh"
#include "numerical_characterization/NumericalCharLibrary.hh"
#include "numerical_characterization/PatternResponseModel.hh"
#include "numerical_characterization/Polynomial2D.hh"
#include "numerical_htree/NumericalHTreeBuilder.hh"

namespace icts {
namespace {

auto ToNumericalBasis(Polynomial2DBasis basis) -> NumericalHTreeResponseBasis
{
  switch (basis) {
    case Polynomial2DBasis::kConstant:
      return NumericalHTreeResponseBasis::kConstant;
    case Polynomial2DBasis::kAffine:
      return NumericalHTreeResponseBasis::kAffine;
    case Polynomial2DBasis::kQuadratic:
      return NumericalHTreeResponseBasis::kQuadratic;
  }
  return NumericalHTreeResponseBasis::kConstant;
}

auto ToSurface(const Polynomial2D& polynomial) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface::normalized(ToNumericalBasis(polynomial.get_basis()), polynomial.get_coefficients(),
                                                   polynomial.get_slew_center_ns(), polynomial.get_slew_scale_ns(),
                                                   polynomial.get_cap_center_pf(), polynomial.get_cap_scale_pf());
}

auto ToFitMetrics(const FitMetrics& metrics) -> std::optional<NumericalHTreeFitMetrics>
{
  if (!metrics.isUsable()) {
    return std::nullopt;
  }
  return NumericalHTreeFitMetrics{
      .sample_count = metrics.sample_count,
      .rank = metrics.rank,
      .rmse = metrics.rmse,
      .r2 = metrics.r2,
      .max_abs_error = metrics.max_abs_error,
  };
}

auto MakePatternModel(const PatternResponseModel& model) -> NumericalHTreePatternModel
{
  return NumericalHTreePatternModel{
      .pattern_id = model.get_pattern_id(),
      .model_name = "pattern_" + std::to_string(model.get_pattern_id().local_id) + "_length_" + std::to_string(model.get_length_idx()),
      .delay_ns = ToSurface(model.get_delay_fit().polynomial),
      .output_slew_ns = ToSurface(model.get_output_slew_fit().polynomial),
      .driven_cap_pf = ToSurface(model.get_driven_cap_fit().polynomial),
      .power_w = ToSurface(model.get_power_fit().polynomial),
      .source_boundary_switch_power_w = ToSurface(model.get_source_boundary_switch_power_fit().polynomial),
      .delay_metrics = ToFitMetrics(model.get_delay_fit().metrics),
      .output_slew_metrics = ToFitMetrics(model.get_output_slew_fit().metrics),
      .driven_cap_metrics = ToFitMetrics(model.get_driven_cap_fit().metrics),
      .power_metrics = ToFitMetrics(model.get_power_fit().metrics),
      .source_boundary_switch_power_metrics = ToFitMetrics(model.get_source_boundary_switch_power_fit().metrics),
  };
}

}  // namespace

auto NumericalHTreeBuilder::build(const NumericalCharLibrary& library, const std::vector<unsigned>& level_length_indices,
                                  const NumericalHTreeOptions& options) -> NumericalHTreeResult
{
  NumericalHTreeBuildInput input;
  input.options = options;
  input.levels.reserve(level_length_indices.size());

  for (const unsigned length_idx : level_length_indices) {
    NumericalHTreeLevelInput level;
    for (const auto& model : library.get_models()) {
      if (model.get_length_idx() == length_idx && model.isValid()) {
        level.pattern_models.push_back(MakePatternModel(model));
      }
    }
    input.levels.push_back(std::move(level));
  }

  return build(input);
}

}  // namespace icts
