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
 * @file Polynomial2D.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Normalized affine and quadratic two-dimensional polynomial fitting.
 */

#include "Polynomial2D.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace icts {
namespace {

constexpr double kScaleEpsilon = 1e-12;

struct Normalization
{
  double slew_center_ns = 0.0;
  double slew_scale_ns = 1.0;
  double cap_center_pf = 0.0;
  double cap_scale_pf = 1.0;
};

struct LinearSolveResult
{
  std::vector<double> solution;
  unsigned rank = 0U;
  bool full_rank = false;
};

auto makeNormalization(const std::vector<Polynomial2DPoint>& points) -> Normalization
{
  double min_slew = std::numeric_limits<double>::max();
  double max_slew = std::numeric_limits<double>::lowest();
  double min_cap = std::numeric_limits<double>::max();
  double max_cap = std::numeric_limits<double>::lowest();

  for (const auto& point : points) {
    min_slew = std::min(min_slew, point.slew_in_ns);
    max_slew = std::max(max_slew, point.slew_in_ns);
    min_cap = std::min(min_cap, point.cap_load_pf);
    max_cap = std::max(max_cap, point.cap_load_pf);
  }

  Normalization normalization;
  normalization.slew_center_ns = 0.5 * (min_slew + max_slew);
  normalization.cap_center_pf = 0.5 * (min_cap + max_cap);
  normalization.slew_scale_ns = 0.5 * (max_slew - min_slew);
  normalization.cap_scale_pf = 0.5 * (max_cap - min_cap);

  if (normalization.slew_scale_ns <= kScaleEpsilon) {
    normalization.slew_scale_ns = 1.0;
  }
  if (normalization.cap_scale_pf <= kScaleEpsilon) {
    normalization.cap_scale_pf = 1.0;
  }
  return normalization;
}

auto makeTerms(Polynomial2DBasis basis, double normalized_slew, double normalized_cap) -> std::array<double, 6>
{
  std::array<double, 6> terms{1.0,
                              normalized_slew,
                              normalized_cap,
                              normalized_slew * normalized_cap,
                              normalized_slew * normalized_slew,
                              normalized_cap * normalized_cap};

  if (basis == Polynomial2DBasis::kConstant) {
    terms[1] = 0.0;
    terms[2] = 0.0;
    terms[3] = 0.0;
    terms[4] = 0.0;
    terms[5] = 0.0;
  } else if (basis == Polynomial2DBasis::kAffine) {
    terms[3] = 0.0;
    terms[4] = 0.0;
    terms[5] = 0.0;
  }
  return terms;
}

auto normalizeSlew(double slew_in_ns, const Normalization& normalization) -> double
{
  return (slew_in_ns - normalization.slew_center_ns) / normalization.slew_scale_ns;
}

auto normalizeCap(double cap_load_pf, const Normalization& normalization) -> double
{
  return (cap_load_pf - normalization.cap_center_pf) / normalization.cap_scale_pf;
}

auto buildNormalEquations(const std::vector<Polynomial2DPoint>& points, Polynomial2DBasis basis, const Normalization& normalization)
    -> std::pair<std::vector<std::vector<double>>, std::vector<double>>
{
  const unsigned term_count = Polynomial2D::termCount(basis);
  std::vector<std::vector<double>> normal_matrix(term_count, std::vector<double>(term_count, 0.0));
  std::vector<double> rhs(term_count, 0.0);

  for (const auto& point : points) {
    const auto terms = makeTerms(basis, normalizeSlew(point.slew_in_ns, normalization), normalizeCap(point.cap_load_pf, normalization));
    for (unsigned row = 0U; row < term_count; ++row) {
      rhs[row] += terms[row] * point.value;
      for (unsigned col = 0U; col < term_count; ++col) {
        normal_matrix[row][col] += terms[row] * terms[col];
      }
    }
  }

  return {std::move(normal_matrix), std::move(rhs)};
}

auto solveLinearSystem(std::vector<std::vector<double>> matrix, std::vector<double> rhs, double rank_tolerance) -> LinearSolveResult
{
  const auto dimension = static_cast<unsigned>(rhs.size());
  LinearSolveResult result;
  result.solution.assign(dimension, 0.0);

  double largest_entry = 0.0;
  for (unsigned row = 0U; row < dimension; ++row) {
    for (unsigned col = 0U; col < dimension; ++col) {
      largest_entry = std::max(largest_entry, std::fabs(matrix[row][col]));
    }
  }
  const double pivot_tolerance = std::max(rank_tolerance, largest_entry * rank_tolerance);

  unsigned rank = 0U;
  for (unsigned col = 0U; col < dimension; ++col) {
    unsigned pivot_row = rank;
    double pivot_value = 0.0;
    for (unsigned row = rank; row < dimension; ++row) {
      const double candidate = std::fabs(matrix[row][col]);
      if (candidate > pivot_value) {
        pivot_value = candidate;
        pivot_row = row;
      }
    }

    if (pivot_value <= pivot_tolerance) {
      continue;
    }

    if (pivot_row != rank) {
      std::swap(matrix[pivot_row], matrix[rank]);
      std::swap(rhs[pivot_row], rhs[rank]);
    }

    const double pivot = matrix[rank][col];
    for (unsigned entry = col; entry < dimension; ++entry) {
      matrix[rank][entry] /= pivot;
    }
    rhs[rank] /= pivot;

    for (unsigned row = 0U; row < dimension; ++row) {
      if (row == rank) {
        continue;
      }

      const double factor = matrix[row][col];
      if (std::fabs(factor) <= pivot_tolerance) {
        continue;
      }
      for (unsigned entry = col; entry < dimension; ++entry) {
        matrix[row][entry] -= factor * matrix[rank][entry];
      }
      rhs[row] -= factor * rhs[rank];
    }

    ++rank;
  }

  result.rank = rank;
  result.full_rank = rank == dimension;
  if (!result.full_rank) {
    return result;
  }

  for (unsigned row = 0U; row < dimension; ++row) {
    result.solution[row] = rhs[row];
  }
  return result;
}

auto computeMetrics(const std::vector<Polynomial2DPoint>& points, const Polynomial2D& polynomial, unsigned rank, unsigned term_count,
                    FitStatus status) -> FitMetrics
{
  const double mean_value = std::accumulate(points.begin(), points.end(), 0.0,
                                            [](double sum, const Polynomial2DPoint& point) -> double { return sum + point.value; })
                            / static_cast<double>(points.size());

  double residual_sum_square = 0.0;
  double total_sum_square = 0.0;
  double max_abs_error = 0.0;

  for (const auto& point : points) {
    const double estimate = polynomial.evaluate(point.slew_in_ns, point.cap_load_pf);
    const double residual = point.value - estimate;
    residual_sum_square += residual * residual;
    max_abs_error = std::max(max_abs_error, std::fabs(residual));

    const double centered = point.value - mean_value;
    total_sum_square += centered * centered;
  }

  FitMetrics metrics;
  metrics.sample_count = static_cast<unsigned>(points.size());
  metrics.rank = rank;
  metrics.basis_term_count = term_count;
  metrics.rmse = std::sqrt(residual_sum_square / static_cast<double>(points.size()));
  metrics.max_abs_error = max_abs_error;
  metrics.status = status;

  if (total_sum_square <= kScaleEpsilon) {
    metrics.r2 = residual_sum_square <= kScaleEpsilon ? 1.0 : 0.0;
  } else {
    metrics.r2 = 1.0 - residual_sum_square / total_sum_square;
  }
  return metrics;
}

auto fitCandidate(const std::vector<Polynomial2DPoint>& points, Polynomial2DBasis basis, FitStatus success_status,
                  const PolynomialFitOptions& options) -> Polynomial2DFitResult
{
  Polynomial2DFitResult fit_result;
  fit_result.metrics.sample_count = static_cast<unsigned>(points.size());
  fit_result.metrics.basis_term_count = Polynomial2D::termCount(basis);

  if (points.empty()) {
    fit_result.metrics.status = FitStatus::kNoSamples;
    return fit_result;
  }

  const Normalization normalization = makeNormalization(points);
  auto equations = buildNormalEquations(points, basis, normalization);
  const auto solve_result = solveLinearSystem(std::move(equations.first), std::move(equations.second), options.rank_tolerance);
  fit_result.metrics.rank = solve_result.rank;
  if (!solve_result.full_rank) {
    fit_result.metrics.status
        = points.size() < fit_result.metrics.basis_term_count ? FitStatus::kUnderdetermined : FitStatus::kRankDeficient;
    return fit_result;
  }

  fit_result.polynomial = Polynomial2D(basis, solve_result.solution, normalization.slew_center_ns, normalization.slew_scale_ns,
                                       normalization.cap_center_pf, normalization.cap_scale_pf);
  fit_result.metrics
      = computeMetrics(points, fit_result.polynomial, solve_result.rank, fit_result.metrics.basis_term_count, success_status);
  return fit_result;
}

auto basisFallbackOrder(Polynomial2DBasis preferred_basis, bool allow_lower_order) -> std::vector<std::pair<Polynomial2DBasis, FitStatus>>
{
  if (!allow_lower_order) {
    return {{preferred_basis, FitStatus::kOk}};
  }

  if (preferred_basis == Polynomial2DBasis::kQuadratic) {
    return {{Polynomial2DBasis::kQuadratic, FitStatus::kOk},
            {Polynomial2DBasis::kAffine, FitStatus::kFallbackAffine},
            {Polynomial2DBasis::kConstant, FitStatus::kFallbackConstant}};
  }
  if (preferred_basis == Polynomial2DBasis::kAffine) {
    return {{Polynomial2DBasis::kAffine, FitStatus::kOk}, {Polynomial2DBasis::kConstant, FitStatus::kFallbackConstant}};
  }
  return {{Polynomial2DBasis::kConstant, FitStatus::kOk}};
}

}  // namespace

Polynomial2D::Polynomial2D(Polynomial2DBasis basis, std::vector<double> coefficients, double slew_center_ns, double slew_scale_ns,
                           double cap_center_pf, double cap_scale_pf)
    : _basis(basis),
      _coefficients(std::move(coefficients)),
      _slew_center_ns(slew_center_ns),
      _slew_scale_ns(slew_scale_ns > kScaleEpsilon ? slew_scale_ns : 1.0),
      _cap_center_pf(cap_center_pf),
      _cap_scale_pf(cap_scale_pf > kScaleEpsilon ? cap_scale_pf : 1.0)
{
}

auto Polynomial2D::termCount(Polynomial2DBasis basis) -> unsigned
{
  if (basis == Polynomial2DBasis::kConstant) {
    return 1U;
  }
  if (basis == Polynomial2DBasis::kAffine) {
    return 3U;
  }
  return 6U;
}

auto Polynomial2D::evaluate(double slew_in_ns, double cap_load_pf) const -> double
{
  if (_coefficients.empty()) {
    return 0.0;
  }

  const auto terms = makeTerms(_basis, normalizeSlew(slew_in_ns), normalizeCap(cap_load_pf));
  double value = 0.0;
  const unsigned term_count = std::min(static_cast<unsigned>(_coefficients.size()), termCount(_basis));
  for (unsigned term_index = 0U; term_index < term_count; ++term_index) {
    value += _coefficients[term_index] * terms[term_index];
  }
  return value;
}

auto Polynomial2D::normalizeSlew(double slew_in_ns) const -> double
{
  return (slew_in_ns - _slew_center_ns) / _slew_scale_ns;
}

auto Polynomial2D::normalizeCap(double cap_load_pf) const -> double
{
  return (cap_load_pf - _cap_center_pf) / _cap_scale_pf;
}

auto FitPolynomial2D(const std::vector<Polynomial2DPoint>& points, const PolynomialFitOptions& options) -> Polynomial2DFitResult
{
  Polynomial2DFitResult last_failure;
  for (const auto& [basis, success_status] : basisFallbackOrder(options.preferred_basis, options.allow_lower_order)) {
    auto candidate = fitCandidate(points, basis, success_status, options);
    if (candidate.metrics.isUsable()) {
      return candidate;
    }
    last_failure = std::move(candidate);
  }
  return last_failure;
}

}  // namespace icts
