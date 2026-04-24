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
 * @file Polynomial2D.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Normalized affine and quadratic two-dimensional polynomial fitting.
 */

#pragma once

#include <vector>

#include "numerical_characterization/FitMetrics.hh"

namespace icts {

enum class Polynomial2DBasis
{
  kConstant,
  kAffine,
  kQuadratic
};

struct PolynomialFitOptions
{
  Polynomial2DBasis preferred_basis = Polynomial2DBasis::kQuadratic;
  bool allow_lower_order = true;
  double rank_tolerance = 1e-10;
};

struct Polynomial2DPoint
{
  double slew_in_ns = 0.0;
  double cap_load_pf = 0.0;
  double value = 0.0;
};

/**
 * @brief A fitted 2D response surface evaluated with physical input units.
 */
class Polynomial2D
{
 public:
  Polynomial2D() = default;

  Polynomial2D(Polynomial2DBasis basis, std::vector<double> coefficients, double slew_center_ns, double slew_scale_ns, double cap_center_pf,
               double cap_scale_pf);

  static auto termCount(Polynomial2DBasis basis) -> unsigned;

  auto evaluate(double slew_in_ns, double cap_load_pf) const -> double;

  auto isValid() const -> bool { return !_coefficients.empty(); }
  auto get_basis() const -> Polynomial2DBasis { return _basis; }
  auto get_coefficients() const -> const std::vector<double>& { return _coefficients; }
  auto get_slew_center_ns() const -> double { return _slew_center_ns; }
  auto get_slew_scale_ns() const -> double { return _slew_scale_ns; }
  auto get_cap_center_pf() const -> double { return _cap_center_pf; }
  auto get_cap_scale_pf() const -> double { return _cap_scale_pf; }

 private:
  auto normalizeSlew(double slew_in_ns) const -> double;
  auto normalizeCap(double cap_load_pf) const -> double;

  Polynomial2DBasis _basis = Polynomial2DBasis::kConstant;
  std::vector<double> _coefficients;
  double _slew_center_ns = 0.0;
  double _slew_scale_ns = 1.0;
  double _cap_center_pf = 0.0;
  double _cap_scale_pf = 1.0;
};

struct Polynomial2DFitResult
{
  Polynomial2D polynomial;
  FitMetrics metrics;
};

auto FitPolynomial2D(const std::vector<Polynomial2DPoint>& points, const PolynomialFitOptions& options = {}) -> Polynomial2DFitResult;

}  // namespace icts
