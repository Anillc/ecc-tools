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
 * @file LinearClusteringSyntheticOrder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic order-regression support.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Point.hh"
#include "TopologyConfig.hh"
#include "common/data/TestDataGenerator.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/types/TestDataTypes.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/LinearOrderGenerator.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::linear_clustering::synthetic {
namespace {
static_assert(std::is_same_v<icts::LinearClusteringConfig, icts::LinearClusteringConfig>);
static_assert(std::is_same_v<decltype(icts::SegmentCacheKey{}.begin), std::size_t>);

struct OrderRegressionObservation
{
  std::vector<std::size_t> baseline_order;
  std::vector<std::size_t> density_scaled_order;
  std::vector<std::size_t> reference_order;
  std::optional<std::size_t> first_difference;
};

using RegressionObserver
    = OrderRegressionObservation (*)(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& baseline_config,
                                     const icts::LinearClusteringConfig& density_scaled_config);

struct RegressionCaseSpec
{
  std::string_view case_label;
  std::string_view expectation;
  std::vector<icts::Point<int>> points;
  std::string_view pin_name_prefix;
  bool preserves_baseline = false;
};

struct DensityScaledRegressionSpec
{
  std::string_view output_dir_name;
  std::string_view test_name;
  std::string_view mode_description;
  std::string_view baseline_label;
  std::string_view report_title;
  std::string config_line;
};

struct RegressionCaseResult
{
  std::string_view case_label;
  std::string_view expectation;
  OrderRegressionObservation observation;
};

auto BuildOrderRegressionPins(const std::vector<icts::Point<int>>& points, const std::string& name_prefix) -> GeneratedPins
{
  return common::data::BuildPinsFromPoints(points,
                                           CanvasSize{.width = detail::kDensityScaledDiscreteRegressionCanvasExtent,
                                                      .height = detail::kDensityScaledDiscreteRegressionCanvasExtent},
                                           name_prefix);
}

auto BuildDiscreteReferenceConfig(const icts::LinearClusteringConfig& config) -> detail::ReferenceDensityScaledDiscreteConfig
{
  return {
      .discrete_hilbert_encoding = config.discrete_hilbert_encoding,
      .hilbert_transform = config.hilbert_transform,
      .order_bits = config.order_bits,
      .density_grid_size = config.density_grid_size,
      .density_scale_power = config.density_scale_power,
  };
}

auto BuildContinuousReferenceConfig(const icts::LinearClusteringConfig& config) -> detail::ReferenceDensityScaledContinuousConfig
{
  return {
      .density_grid_size = config.density_grid_size,
      .density_scale_power = config.density_scale_power,
  };
}

auto ObserveDiscreteRegressionCase(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& baseline_config,
                                   const icts::LinearClusteringConfig& density_scaled_config) -> OrderRegressionObservation
{
  OrderRegressionObservation observation;
  observation.baseline_order = detail::ExtractOriginalIndices(icts::LinearOrderGenerator::generateOrder(loads, baseline_config));
  observation.density_scaled_order
      = detail::ExtractOriginalIndices(icts::LinearOrderGenerator::generateOrder(loads, density_scaled_config));
  observation.reference_order
      = detail::BuildReferenceDensityScaledDiscreteOrder(loads, BuildDiscreteReferenceConfig(density_scaled_config));
  observation.first_difference = detail::FindFirstOrderDifference(observation.baseline_order, observation.density_scaled_order);
  return observation;
}

auto ObserveContinuousRegressionCase(const std::vector<icts::Pin*>& loads, const icts::LinearClusteringConfig& baseline_config,
                                     const icts::LinearClusteringConfig& density_scaled_config) -> OrderRegressionObservation
{
  OrderRegressionObservation observation;
  observation.baseline_order = detail::ExtractOriginalIndices(icts::LinearOrderGenerator::generateOrder(loads, baseline_config));
  observation.density_scaled_order
      = detail::ExtractOriginalIndices(icts::LinearOrderGenerator::generateOrder(loads, density_scaled_config));
  observation.reference_order
      = detail::BuildReferenceDensityScaledContinuousOrder(loads, BuildContinuousReferenceConfig(density_scaled_config));
  observation.first_difference = detail::FindFirstOrderDifference(observation.baseline_order, observation.density_scaled_order);
  return observation;
}

auto BuildSeparableMarginalRegressionCases() -> std::vector<RegressionCaseSpec>
{
  return {
      RegressionCaseSpec{
          .case_label = "Case 1: balanced joint skew with balanced x/y marginals",
          .expectation = "separable density scaling ignores pure joint skew when x/y marginals remain balanced.",
          .points = detail::BuildBalancedMarginalJointSkewPoints(),
          .pin_name_prefix = "density_scaled_balanced",
          .preserves_baseline = true,
      },
      RegressionCaseSpec{
          .case_label = "Case 2: marginal density skew",
          .expectation = "separable density scaling reacts to true x/y marginal imbalance.",
          .points = detail::BuildMarginalDensitySkewPoints(),
          .pin_name_prefix = "density_scaled_skew",
          .preserves_baseline = false,
      },
  };
}

auto VerifyRegressionCaseObservation(const RegressionCaseSpec& case_spec, const OrderRegressionObservation& observation) -> void
{
  EXPECT_EQ(observation.density_scaled_order, observation.reference_order);
  if (case_spec.preserves_baseline) {
    EXPECT_EQ(observation.density_scaled_order, observation.baseline_order)
        << "Balanced x/y marginals should preserve the non-density-scaled order under separable density scaling.";
    return;
  }

  EXPECT_NE(observation.density_scaled_order, observation.baseline_order)
      << "Marginal density skew should change the density-scaled order under separable scaling.";
  EXPECT_TRUE(observation.first_difference.has_value());
}

auto BuildRegressionCaseResult(const RegressionCaseSpec& case_spec, OrderRegressionObservation&& observation) -> RegressionCaseResult
{
  return {
      .case_label = case_spec.case_label,
      .expectation = case_spec.expectation,
      .observation = std::move(observation),
  };
}

auto EvaluateRegressionCase(const RegressionCaseSpec& case_spec, const icts::LinearClusteringConfig& baseline_config,
                            const icts::LinearClusteringConfig& density_scaled_config, RegressionObserver observer) -> RegressionCaseResult
{
  auto generated = BuildOrderRegressionPins(case_spec.points, std::string(case_spec.pin_name_prefix));
  EXPECT_EQ(generated.loads.size(), case_spec.points.size());

  auto observation = observer(generated.loads, baseline_config, density_scaled_config);
  VerifyRegressionCaseObservation(case_spec, observation);
  return BuildRegressionCaseResult(case_spec, std::move(observation));
}

auto AppendRegressionCaseReport(std::ostringstream& report, std::string_view baseline_label, const RegressionCaseResult& result) -> void
{
  report << result.case_label << "\n";
  report << "  " << baseline_label << " order: " << detail::FormatOrderIndices(result.observation.baseline_order) << "\n";
  report << "  Density-scaled order: " << detail::FormatOrderIndices(result.observation.density_scaled_order) << "\n";
  report << "  Reference order: " << detail::FormatOrderIndices(result.observation.reference_order) << "\n";
  if (result.observation.first_difference.has_value()) {
    report << "  First differing position: " << result.observation.first_difference.value() << "\n";
  } else {
    report << "  First differing position: none\n";
  }
  report << "  Expected behavior: " << result.expectation << "\n";
}

auto RunDensityScaledOrderRegression(const DensityScaledRegressionSpec& regression_spec,
                                     const icts::LinearClusteringConfig& baseline_config,
                                     const icts::LinearClusteringConfig& density_scaled_config, RegressionObserver observer) -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir(std::string(regression_spec.output_dir_name));
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare density-scaled order regression output dir.";

  const auto case_specs = BuildSeparableMarginalRegressionCases();
  std::vector<RegressionCaseResult> case_results;
  case_results.reserve(case_specs.size());
  for (const auto& case_spec : case_specs) {
    case_results.push_back(EvaluateRegressionCase(case_spec, baseline_config, density_scaled_config, observer));
  }

  std::ostringstream report;
  report << "Test: " << regression_spec.test_name << "\n";
  report << "Mode: " << regression_spec.mode_description << "\n";
  report << regression_spec.config_line << "\n";
  for (const auto& case_result : case_results) {
    AppendRegressionCaseReport(report, regression_spec.baseline_label, case_result);
  }
  report << "Artifacts: report.log\n";

  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report.str())) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = std::string(regression_spec.report_title), .content = report.str()});
}
}  // namespace

auto RunContinuousHilbertMatchesSinkReferenceOrder() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("continuous_hilbert_matches_sink_reference_order");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare continuous-order reference output dir.";

  const std::vector<icts::Point<int>> points = {
      {100, 100}, {0, 0}, {100, 0}, {0, 100}, {40, 40}, {60, 60}, {60, 40}, {40, 60}, {50, 50}, {50, 50}, {25, 75}, {75, 25},
  };
  auto generated = common::data::BuildPinsFromPoints(
      points, CanvasSize{.width = detail::kReferenceCanvasExtent, .height = detail::kReferenceCanvasExtent}, "sink_ref");
  ASSERT_EQ(generated.loads.size(), points.size());

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kOrderStrategyCoverageFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;

  const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(generated.loads, config);
  const auto actual_order = detail::ExtractOriginalIndices(ordered_loads);
  const auto reference_order = detail::BuildSinkReferenceContinuousOrder(generated.loads);

  EXPECT_EQ(actual_order, reference_order);
  EXPECT_NE(actual_order, std::vector<std::size_t>({0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U}));

  std::ostringstream report;
  report << "Test: LinearClusteringOrderTest.ContinuousHilbertMatchesSinkReferenceOrder\n";
  report << "Mode: Sink-compatible continuous theta regression\n";
  report << "Point count: " << generated.loads.size() << "\n";
  report << "Actual order: " << detail::FormatOrderIndices(actual_order) << "\n";
  report << "Reference order: " << detail::FormatOrderIndices(reference_order) << "\n";
  report << "Artifacts: report.log\n";

  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report.str())) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = "continuous_hilbert_matches_sink_reference_order", .content = report.str()});
}

auto RunDiscreteHilbertMatchesQuantizedSinkReferenceOrder() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("discrete_hilbert_matches_quantized_sink_reference_order");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare discrete-order reference output dir.";

  const std::vector<icts::Point<int>> points = {
      {100, 100}, {0, 0}, {100, 0}, {0, 100}, {40, 40}, {60, 60}, {60, 40}, {40, 60}, {50, 50}, {50, 50}, {25, 75}, {75, 25},
  };
  auto generated = common::data::BuildPinsFromPoints(
      points, CanvasSize{.width = detail::kReferenceCanvasExtent, .height = detail::kReferenceCanvasExtent}, "discrete_ref");
  ASSERT_EQ(generated.loads.size(), points.size());

  icts::LinearClusteringConfig config{};
  detail::ConfigureLinearDefaults(config, detail::kOrderStrategyCoverageFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(config);
  config.order_strategy = icts::LinearOrderStrategy::kDiscreteHilbert;
  config.order_bits = 4;
  config.density_grid_size = 1;
  config.density_scale_power = 1.0;

  const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(generated.loads, config);
  const auto actual_order = detail::ExtractOriginalIndices(ordered_loads);
  const auto reference_order
      = detail::BuildReferenceDensityScaledDiscreteOrder(generated.loads, detail::ReferenceDensityScaledDiscreteConfig{
                                                                              .discrete_hilbert_encoding = config.discrete_hilbert_encoding,
                                                                              .hilbert_transform = config.hilbert_transform,
                                                                              .order_bits = config.order_bits,
                                                                              .density_grid_size = config.density_grid_size,
                                                                              .density_scale_power = config.density_scale_power,
                                                                          });

  EXPECT_EQ(actual_order, reference_order);

  std::ostringstream report;
  report << "Test: LinearClusteringOrderTest.DiscreteHilbertMatchesQuantizedSinkReferenceOrder\n";
  report << "Mode: quantized Sink-compatible discrete theta regression\n";
  report << "Config: discrete_encoding=" << detail::DiscreteHilbertEncodingName(config.discrete_hilbert_encoding)
         << ", hilbert_transform=" << detail::HilbertTransformName(config.hilbert_transform) << ", order_bits=" << config.order_bits
         << ", density_grid_size=" << config.density_grid_size << ", density_scale_power=" << config.density_scale_power << "\n";
  report << "Point count: " << generated.loads.size() << "\n";
  report << "Actual order: " << detail::FormatOrderIndices(actual_order) << "\n";
  report << "Reference order: " << detail::FormatOrderIndices(reference_order) << "\n";
  report << "Artifacts: report.log\n";

  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report.str())) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = "discrete_hilbert_matches_quantized_sink_reference_order", .content = report.str()});
}

auto RunDiscreteHilbertAlternativeEncodingsMatchReferenceOrder() -> void
{
  const auto output_dir = detail::PrepareSyntheticOutputDir("discrete_hilbert_alternative_encodings_match_reference_order");
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare alternative discrete-order reference output dir.";

  const std::vector<icts::Point<int>> points = {
      {100, 100}, {0, 0}, {100, 0}, {0, 100}, {40, 40}, {60, 60}, {60, 40}, {40, 60}, {50, 50}, {50, 50}, {25, 75}, {75, 25},
  };
  auto generated = common::data::BuildPinsFromPoints(
      points, CanvasSize{.width = detail::kReferenceCanvasExtent, .height = detail::kReferenceCanvasExtent}, "discrete_alt_ref");
  ASSERT_EQ(generated.loads.size(), points.size());

  struct AlternativeDiscreteCase
  {
    icts::DiscreteHilbertEncoding discrete_hilbert_encoding;
    icts::HilbertTransform hilbert_transform;
    int order_bits;
  };

  const std::vector<AlternativeDiscreteCase> cases = {
      {icts::DiscreteHilbertEncoding::kSinkThetaCellTangent, icts::HilbertTransform::kMirrorX, 4},
      {icts::DiscreteHilbertEncoding::kClassicIndex, icts::HilbertTransform::kSwapXY, 4},
      {icts::DiscreteHilbertEncoding::kClassicIndexTangent, icts::HilbertTransform::kSwapMirrorY, 4},
  };

  std::ostringstream report;
  report << "Test: LinearClusteringOrderTest.DiscreteHilbertAlternativeEncodingsMatchReferenceOrder\n";
  report << "Mode: alternative discrete Hilbert encoding regression\n";
  report << "Point count: " << generated.loads.size() << "\n";

  for (const auto& discrete_case : cases) {
    icts::LinearClusteringConfig config{};
    detail::ConfigureLinearDefaults(config, detail::kOrderStrategyCoverageFanout);
    detail::ConfigureSyntheticFallbackCapNeutral(config);
    config.order_strategy = icts::LinearOrderStrategy::kDiscreteHilbert;
    config.discrete_hilbert_encoding = discrete_case.discrete_hilbert_encoding;
    config.hilbert_transform = discrete_case.hilbert_transform;
    config.order_bits = discrete_case.order_bits;
    config.density_grid_size = 1;
    config.density_scale_power = 1.0;

    const auto ordered_loads = icts::LinearOrderGenerator::generateOrder(generated.loads, config);
    const auto actual_order = detail::ExtractOriginalIndices(ordered_loads);
    const auto reference_order = detail::BuildReferenceDensityScaledDiscreteOrder(generated.loads, BuildDiscreteReferenceConfig(config));

    EXPECT_EQ(actual_order, reference_order);

    report << "Case: discrete_encoding=" << detail::DiscreteHilbertEncodingName(config.discrete_hilbert_encoding)
           << ", hilbert_transform=" << detail::HilbertTransformName(config.hilbert_transform) << ", order_bits=" << config.order_bits
           << "\n";
    report << "Actual order: " << detail::FormatOrderIndices(actual_order) << "\n";
    report << "Reference order: " << detail::FormatOrderIndices(reference_order) << "\n";
  }
  report << "Artifacts: report.log\n";

  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(common::io::WriteTextLog(report_path, report.str())) << "Failed to write report: " << report_path.string();
  common::io::EmitInfoReport(InfoReport{.title = "discrete_hilbert_alternative_encodings_match_reference_order", .content = report.str()});
}

auto RunDensityScaledDiscreteHilbertMatchesSeparableMarginalReference() -> void
{
  icts::LinearClusteringConfig discrete_config{};
  detail::ConfigureLinearDefaults(discrete_config, detail::kOrderStrategyCoverageFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(discrete_config);
  discrete_config.order_strategy = icts::LinearOrderStrategy::kDiscreteHilbert;
  discrete_config.order_bits = 4;
  discrete_config.density_grid_size = 4;
  discrete_config.density_scale_power = 1.0;

  auto density_scaled_config = discrete_config;
  density_scaled_config.order_strategy = icts::LinearOrderStrategy::kDensityScaledDiscreteHilbert;

  RunDensityScaledOrderRegression(
      DensityScaledRegressionSpec{
          .output_dir_name = "density_scaled_discrete_hilbert_matches_separable_marginal_reference",
          .test_name = "LinearClusteringOrderTest.DensityScaledDiscreteHilbertMatchesSeparableMarginalReference",
          .mode_description = "discrete separable marginal density-scaling regression",
          .baseline_label = "Discrete",
          .report_title = "density_scaled_discrete_hilbert_matches_separable_marginal_reference",
          .config_line = "Config: order_bits=" + std::to_string(discrete_config.order_bits)
                         + ", density_grid_size=" + std::to_string(discrete_config.density_grid_size)
                         + ", density_scale_power=" + std::to_string(discrete_config.density_scale_power),
      },
      discrete_config, density_scaled_config, &ObserveDiscreteRegressionCase);
}

auto RunDensityScaledContinuousHilbertMatchesSeparableMarginalReference() -> void
{
  icts::LinearClusteringConfig continuous_config{};
  detail::ConfigureLinearDefaults(continuous_config, detail::kOrderStrategyCoverageFanout);
  detail::ConfigureSyntheticFallbackCapNeutral(continuous_config);
  continuous_config.order_strategy = icts::LinearOrderStrategy::kContinuousHilbert;
  continuous_config.density_grid_size = 4;
  continuous_config.density_scale_power = 1.0;

  auto density_scaled_config = continuous_config;
  density_scaled_config.order_strategy = icts::LinearOrderStrategy::kDensityScaledContinuousHilbert;

  RunDensityScaledOrderRegression(
      DensityScaledRegressionSpec{
          .output_dir_name = "density_scaled_continuous_hilbert_matches_separable_marginal_reference",
          .test_name = "LinearClusteringOrderTest.DensityScaledContinuousHilbertMatchesSeparableMarginalReference",
          .mode_description = "continuous separable marginal density-scaling regression",
          .baseline_label = "Continuous",
          .report_title = "density_scaled_continuous_hilbert_matches_separable_marginal_reference",
          .config_line = "Config: density_grid_size=" + std::to_string(continuous_config.density_grid_size)
                         + ", density_scale_power=" + std::to_string(continuous_config.density_scale_power),
      },
      continuous_config, density_scaled_config, &ObserveContinuousRegressionCase);
}

}  // namespace icts_test::linear_clustering::synthetic
