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
 * @file PatternResponseModel.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Per-pattern fitted response models for numerical characterization.
 */

#include "PatternResponseModel.hh"

#include "numerical_characterization/FitMetrics.hh"
#include "numerical_characterization/NumericalSample.hh"

namespace icts {
namespace {

auto metricValue(const NumericalSample& sample, NumericalResponseMetric metric) -> double
{
  switch (metric) {
    case NumericalResponseMetric::kDelay:
      return sample.delay_ns;
    case NumericalResponseMetric::kOutputSlew:
      return sample.slew_out_ns;
    case NumericalResponseMetric::kPower:
      return sample.power_w;
    case NumericalResponseMetric::kDrivenCap:
      return sample.driven_cap_pf;
    case NumericalResponseMetric::kSourceBoundarySwitchPower:
      return sample.source_boundary_net_switch_power_w;
  }
  return 0.0;
}

auto collectPoints(const std::vector<NumericalSample>& samples, PatternId pattern_id, unsigned length_idx, NumericalResponseMetric metric)
    -> std::vector<Polynomial2DPoint>
{
  std::vector<Polynomial2DPoint> points;
  points.reserve(samples.size());
  for (const auto& sample : samples) {
    if (sample.pattern_id != pattern_id || sample.length_idx != length_idx) {
      continue;
    }
    points.push_back(Polynomial2DPoint{
        .slew_in_ns = sample.slew_in_ns,
        .cap_load_pf = sample.cap_load_pf,
        .value = metricValue(sample, metric),
    });
  }
  return points;
}

}  // namespace

auto PatternResponseModel::fit(PatternId pattern_id, unsigned length_idx, const std::vector<NumericalSample>& samples,
                               const PolynomialFitOptions& options) -> PatternResponseModel
{
  PatternResponseModel model;
  model._pattern_id = pattern_id;
  model._length_idx = length_idx;

  for (const auto& sample : samples) {
    if (sample.pattern_id == pattern_id && sample.length_idx == length_idx) {
      model._length_um = sample.length_um;
      ++model._sample_count;
    }
  }

  model._delay_fit = FitPolynomial2D(collectPoints(samples, pattern_id, length_idx, NumericalResponseMetric::kDelay), options);
  model._output_slew_fit = FitPolynomial2D(collectPoints(samples, pattern_id, length_idx, NumericalResponseMetric::kOutputSlew), options);
  model._power_fit = FitPolynomial2D(collectPoints(samples, pattern_id, length_idx, NumericalResponseMetric::kPower), options);
  model._driven_cap_fit = FitPolynomial2D(collectPoints(samples, pattern_id, length_idx, NumericalResponseMetric::kDrivenCap), options);
  model._source_boundary_switch_power_fit
      = FitPolynomial2D(collectPoints(samples, pattern_id, length_idx, NumericalResponseMetric::kSourceBoundarySwitchPower), options);
  return model;
}

auto PatternResponseModel::evaluate(double slew_in_ns, double cap_load_pf) const -> NumericalResponse
{
  return NumericalResponse{
      .delay_ns = _delay_fit.polynomial.evaluate(slew_in_ns, cap_load_pf),
      .slew_out_ns = _output_slew_fit.polynomial.evaluate(slew_in_ns, cap_load_pf),
      .power_w = _power_fit.polynomial.evaluate(slew_in_ns, cap_load_pf),
      .driven_cap_pf = _driven_cap_fit.polynomial.evaluate(slew_in_ns, cap_load_pf),
      .source_boundary_net_switch_power_w = _source_boundary_switch_power_fit.polynomial.evaluate(slew_in_ns, cap_load_pf),
  };
}

auto PatternResponseModel::isValid() const -> bool
{
  return _delay_fit.metrics.isUsable() && _output_slew_fit.metrics.isUsable() && _power_fit.metrics.isUsable()
         && _driven_cap_fit.metrics.isUsable() && _source_boundary_switch_power_fit.metrics.isUsable();
}

}  // namespace icts
