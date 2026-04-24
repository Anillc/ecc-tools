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
 * @file NumericalCharacterizationTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Numerical characterization fitting and sample extraction tests.
 */

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "characterization/CharCore.hh"
#include "characterization/PatternId.hh"
#include "characterization/SegmentChar.hh"
#include "characterization/ValueLattice.hh"
#include "numerical_characterization/FitMetrics.hh"
#include "numerical_characterization/NumericalCharLibrary.hh"
#include "numerical_characterization/NumericalSample.hh"
#include "numerical_characterization/PatternResponseModel.hh"
#include "numerical_characterization/Polynomial2D.hh"

namespace icts_test {
namespace {

constexpr double kTolerance = 1e-9;

auto affineValue(double slew_in_ns, double cap_load_pf) -> double
{
  return 0.7 + 1.6 * slew_in_ns - 2.5 * cap_load_pf;
}

auto quadraticValue(double slew_in_ns, double cap_load_pf) -> double
{
  return 1.1 + 0.8 * slew_in_ns + 3.5 * cap_load_pf + 0.45 * slew_in_ns * cap_load_pf + 1.75 * slew_in_ns * slew_in_ns
         - 2.25 * cap_load_pf * cap_load_pf;
}

auto makePoint(double slew_in_ns, double cap_load_pf, double value) -> icts::Polynomial2DPoint
{
  return icts::Polynomial2DPoint{.slew_in_ns = slew_in_ns, .cap_load_pf = cap_load_pf, .value = value};
}

auto makeAffineSamples() -> std::vector<icts::NumericalSample>
{
  const icts::PatternId pattern_id = icts::PatternId::segment(17U);
  std::vector<icts::NumericalSample> samples;
  for (const double slew_in_ns : std::array<double, 3>{0.05, 0.15, 0.35}) {
    for (const double cap_load_pf : std::array<double, 3>{0.02, 0.08, 0.20}) {
      samples.push_back(icts::NumericalSample{
          .pattern_id = pattern_id,
          .length_idx = 4U,
          .length_um = 40.0,
          .slew_in_ns = slew_in_ns,
          .cap_load_pf = cap_load_pf,
          .delay_ns = affineValue(slew_in_ns, cap_load_pf),
          .slew_out_ns = 0.2 + 0.4 * slew_in_ns + 0.7 * cap_load_pf,
          .power_w = 1.5 + 0.2 * slew_in_ns + 0.9 * cap_load_pf,
          .driven_cap_pf = 0.12 + 0.1 * cap_load_pf,
          .source_boundary_net_switch_power_w = 0.03 + 0.02 * cap_load_pf,
      });
    }
  }
  return samples;
}

TEST(NumericalCharacterizationTest, FitsExactAffinePatternLibrary)
{
  icts::PolynomialFitOptions options;
  options.preferred_basis = icts::Polynomial2DBasis::kAffine;

  const auto library = icts::NumericalCharLibrary::buildFromSamples(makeAffineSamples(), options);
  ASSERT_EQ(library.size(), 1U);

  const auto* model = library.findModel(icts::PatternId::segment(17U), 4U);
  ASSERT_NE(model, nullptr);
  ASSERT_TRUE(model->isValid());
  EXPECT_EQ(model->get_delay_fit().metrics.status, icts::FitStatus::kOk);
  EXPECT_EQ(model->get_delay_fit().metrics.rank, 3U);
  EXPECT_NEAR(model->get_delay_fit().metrics.rmse, 0.0, kTolerance);
  EXPECT_NEAR(model->get_delay_fit().metrics.r2, 1.0, kTolerance);
  EXPECT_NEAR(model->get_delay_fit().metrics.max_abs_error, 0.0, kTolerance);

  const auto response = model->evaluate(0.27, 0.11);
  EXPECT_NEAR(response.delay_ns, affineValue(0.27, 0.11), kTolerance);
  EXPECT_NEAR(response.slew_out_ns, 0.2 + 0.4 * 0.27 + 0.7 * 0.11, kTolerance);
  EXPECT_NEAR(response.power_w, 1.5 + 0.2 * 0.27 + 0.9 * 0.11, kTolerance);
}

TEST(NumericalCharacterizationTest, FitsExactQuadraticSurface)
{
  std::vector<icts::Polynomial2DPoint> points;
  for (const double slew_in_ns : std::array<double, 3>{0.04, 0.16, 0.40}) {
    for (const double cap_load_pf : std::array<double, 3>{0.01, 0.09, 0.22}) {
      points.push_back(makePoint(slew_in_ns, cap_load_pf, quadraticValue(slew_in_ns, cap_load_pf)));
    }
  }

  icts::PolynomialFitOptions options;
  options.preferred_basis = icts::Polynomial2DBasis::kQuadratic;

  const auto fit = icts::FitPolynomial2D(points, options);
  ASSERT_EQ(fit.metrics.status, icts::FitStatus::kOk);
  EXPECT_EQ(fit.metrics.sample_count, 9U);
  EXPECT_EQ(fit.metrics.rank, 6U);
  EXPECT_NEAR(fit.metrics.rmse, 0.0, kTolerance);
  EXPECT_NEAR(fit.metrics.r2, 1.0, kTolerance);
  EXPECT_NEAR(fit.polynomial.evaluate(0.23, 0.13), quadraticValue(0.23, 0.13), 1e-8);
}

TEST(NumericalCharacterizationTest, FitsNoisyQuadraticWithHighR2)
{
  std::vector<icts::Polynomial2DPoint> points;
  for (unsigned slew_index = 0U; slew_index < 5U; ++slew_index) {
    for (unsigned cap_index = 0U; cap_index < 5U; ++cap_index) {
      const double slew_in_ns = 0.05 + 0.07 * static_cast<double>(slew_index);
      const double cap_load_pf = 0.02 + 0.04 * static_cast<double>(cap_index);
      const int signed_noise_bucket = static_cast<int>((slew_index + 2U * cap_index) % 3U) - 1;
      const double noise = 0.0005 * static_cast<double>(signed_noise_bucket);
      points.push_back(makePoint(slew_in_ns, cap_load_pf, quadraticValue(slew_in_ns, cap_load_pf) + noise));
    }
  }

  const auto fit = icts::FitPolynomial2D(points);
  ASSERT_EQ(fit.metrics.status, icts::FitStatus::kOk);
  EXPECT_EQ(fit.metrics.sample_count, 25U);
  EXPECT_EQ(fit.metrics.rank, 6U);
  EXPECT_GT(fit.metrics.rmse, 0.0);
  EXPECT_GT(fit.metrics.r2, 0.999);
  EXPECT_LT(fit.metrics.max_abs_error, 0.001);
}

TEST(NumericalCharacterizationTest, FallsBackToAffineWhenQuadraticIsUnderdetermined)
{
  const std::vector<icts::Polynomial2DPoint> points{
      makePoint(0.05, 0.02, affineValue(0.05, 0.02)),
      makePoint(0.20, 0.05, affineValue(0.20, 0.05)),
      makePoint(0.15, 0.18, affineValue(0.15, 0.18)),
  };

  icts::PolynomialFitOptions options;
  options.preferred_basis = icts::Polynomial2DBasis::kQuadratic;

  const auto fit = icts::FitPolynomial2D(points, options);
  ASSERT_EQ(fit.metrics.status, icts::FitStatus::kFallbackAffine);
  EXPECT_EQ(fit.metrics.rank, 3U);
  EXPECT_EQ(fit.metrics.basis_term_count, 3U);
  EXPECT_NEAR(fit.metrics.rmse, 0.0, kTolerance);
  EXPECT_NEAR(fit.polynomial.evaluate(0.31, 0.09), affineValue(0.31, 0.09), 1e-8);
}

TEST(NumericalCharacterizationTest, ExtractsPhysicalSamplesFromSegmentCharLattices)
{
  const icts::CharCore core(2U, 3U, 4U, 5U, 0.42, 1.75, icts::PatternId::segment(9U), 0.125);
  const icts::SegmentChar segment_char(core, 6U);

  const icts::NumericalSampleLattices lattices{
      .slew_lattice = icts::UniformValueLattice(0.05, 10U),
      .load_cap_lattice = icts::UniformValueLattice(0.02, 10U),
      .output_slew_lattice = icts::UniformValueLattice(0.04, 10U),
      .driven_cap_lattice = icts::UniformValueLattice(0.03, 10U),
      .length_lattice = icts::UniformValueLattice(12.5, 10U),
  };

  const auto sample = icts::MakeNumericalSample(segment_char, lattices);
  EXPECT_EQ(sample.pattern_id, icts::PatternId::segment(9U));
  EXPECT_EQ(sample.length_idx, 6U);
  EXPECT_DOUBLE_EQ(sample.length_um, 75.0);
  EXPECT_DOUBLE_EQ(sample.slew_in_ns, 0.10);
  EXPECT_DOUBLE_EQ(sample.cap_load_pf, 0.10);
  EXPECT_DOUBLE_EQ(sample.delay_ns, 0.42);
  EXPECT_DOUBLE_EQ(sample.slew_out_ns, 0.12);
  EXPECT_DOUBLE_EQ(sample.power_w, 1.75);
  EXPECT_DOUBLE_EQ(sample.driven_cap_pf, 0.12);
  EXPECT_DOUBLE_EQ(sample.source_boundary_net_switch_power_w, 0.125);
}

}  // namespace
}  // namespace icts_test
