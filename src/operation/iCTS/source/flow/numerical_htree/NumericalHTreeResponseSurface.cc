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
 * @file NumericalHTreeResponseSurface.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Response-surface evaluation used by numerical H-tree pattern models.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "numerical_htree/NumericalHTreeBuilder.hh"
#include "numerical_htree/NumericalHTreeInternal.hh"

namespace icts {

NumericalHTreeResponseSurface::NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients)
    : NumericalHTreeResponseSurface(basis, std::move(coefficients), 0.0, 1.0, 0.0, 1.0, false)
{
}

NumericalHTreeResponseSurface::NumericalHTreeResponseSurface(NumericalHTreeResponseBasis basis, std::vector<double> coefficients,
                                                             double slew_center_ns, double slew_scale_ns, double cap_center_pf,
                                                             double cap_scale_pf, bool normalize_inputs)
    : _basis(basis),
      _coefficients(std::move(coefficients)),
      _slew_center_ns(slew_center_ns),
      _slew_scale_ns(slew_scale_ns),
      _cap_center_pf(cap_center_pf),
      _cap_scale_pf(cap_scale_pf),
      _normalize_inputs(normalize_inputs)
{
}

auto NumericalHTreeResponseSurface::constant(double value) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kConstant, {value});
}

auto NumericalHTreeResponseSurface::affine(double intercept, double slew_coeff, double cap_coeff) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kAffine, {intercept, slew_coeff, cap_coeff});
}

auto NumericalHTreeResponseSurface::quadratic(double intercept, double slew_coeff, double cap_coeff, double slew_cap_coeff,
                                              double slew_sq_coeff, double cap_sq_coeff) -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(NumericalHTreeResponseBasis::kQuadratic,
                                       {intercept, slew_coeff, cap_coeff, slew_cap_coeff, slew_sq_coeff, cap_sq_coeff});
}

auto NumericalHTreeResponseSurface::normalized(NumericalHTreeResponseBasis basis, std::vector<double> coefficients, double slew_center_ns,
                                               double slew_scale_ns, double cap_center_pf, double cap_scale_pf)
    -> NumericalHTreeResponseSurface
{
  return NumericalHTreeResponseSurface(basis, std::move(coefficients), slew_center_ns, slew_scale_ns, cap_center_pf, cap_scale_pf, true);
}

auto NumericalHTreeResponseSurface::evaluate(double input_slew_ns, double load_cap_pf) const -> std::optional<double>
{
  if (!isValid() || !numerical_htree::IsFinite(input_slew_ns) || !numerical_htree::IsFinite(load_cap_pf)) {
    return std::nullopt;
  }

  double slew_value = input_slew_ns;
  double cap_value = load_cap_pf;
  if (_normalize_inputs) {
    if (_slew_scale_ns == 0.0 || _cap_scale_pf == 0.0) {
      return std::nullopt;
    }
    slew_value = (input_slew_ns - _slew_center_ns) / _slew_scale_ns;
    cap_value = (load_cap_pf - _cap_center_pf) / _cap_scale_pf;
  }

  switch (_basis) {
    case NumericalHTreeResponseBasis::kConstant:
      return _coefficients[0U];
    case NumericalHTreeResponseBasis::kAffine:
      return _coefficients[0U] + _coefficients[1U] * slew_value + _coefficients[2U] * cap_value;
    case NumericalHTreeResponseBasis::kQuadratic:
      return _coefficients[0U] + _coefficients[1U] * slew_value + _coefficients[2U] * cap_value + _coefficients[3U] * slew_value * cap_value
             + _coefficients[4U] * slew_value * slew_value + _coefficients[5U] * cap_value * cap_value;
  }

  return std::nullopt;
}

auto NumericalHTreeResponseSurface::isValid() const -> bool
{
  std::size_t expected_size = 0U;
  switch (_basis) {
    case NumericalHTreeResponseBasis::kConstant:
      expected_size = 1U;
      break;
    case NumericalHTreeResponseBasis::kAffine:
      expected_size = 3U;
      break;
    case NumericalHTreeResponseBasis::kQuadratic:
      expected_size = 6U;
      break;
  }
  if (_coefficients.size() != expected_size) {
    return false;
  }
  return std::ranges::all_of(_coefficients, [](double coefficient) -> bool { return numerical_htree::IsFinite(coefficient); })
         && numerical_htree::IsFinite(_slew_center_ns) && numerical_htree::IsFinite(_slew_scale_ns)
         && numerical_htree::IsFinite(_cap_center_pf) && numerical_htree::IsFinite(_cap_scale_pf)
         && (!_normalize_inputs
             || (std::abs(_slew_scale_ns) > numerical_htree::kEpsilon && std::abs(_cap_scale_pf) > numerical_htree::kEpsilon));
}

}  // namespace icts
