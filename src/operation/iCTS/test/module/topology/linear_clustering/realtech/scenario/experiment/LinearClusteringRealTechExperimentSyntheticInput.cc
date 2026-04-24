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
 * @file LinearClusteringRealTechExperimentSyntheticInput.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Source-cloud and representative synthetic input generation for real-tech linear clustering experiments.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/linear_clustering/realtech/scenario/experiment/LinearClusteringRealTechExperimentInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech::experiment {

auto BuildSourcePointCloud(const RealClockLoads& real_clock_loads) -> SourcePointCloud
{
  SourcePointCloud source_cloud;
  if (!real_clock_loads.available || real_clock_loads.loads.empty()) {
    return source_cloud;
  }

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
  for (const auto* pin : real_clock_loads.loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_x = std::max(max_x, location.get_x());
    max_y = std::max(max_y, location.get_y());
  }
  if (min_x > max_x || min_y > max_y) {
    return source_cloud;
  }

  source_cloud.min_x = min_x;
  source_cloud.min_y = min_y;
  source_cloud.max_x = max_x;
  source_cloud.max_y = max_y;
  source_cloud.source_width = std::max(1, max_x - min_x);
  source_cloud.source_height = std::max(1, max_y - min_y);
  source_cloud.points.reserve(real_clock_loads.loads.size());
  for (const auto* pin : real_clock_loads.loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto location = pin->get_location();
    source_cloud.points.push_back(SourcePointTemplate{
        .pin = pin,
        .normalized_x = source_cloud.source_width <= 1.0 ? 0.5 : static_cast<double>(location.get_x() - min_x) / source_cloud.source_width,
        .normalized_y
        = source_cloud.source_height <= 1.0 ? 0.5 : static_cast<double>(location.get_y() - min_y) / source_cloud.source_height,
    });
  }
  return source_cloud;
}

auto ResolveSyntheticLoadCount(SyntheticLoadScale load_scale) -> std::size_t
{
  switch (load_scale) {
    case SyntheticLoadScale::kLoads500:
      return 500U;
    case SyntheticLoadScale::kLoads1000:
      return 1000U;
    case SyntheticLoadScale::kLoads2000:
      return 2000U;
    case SyntheticLoadScale::kLoads5000:
      return 5000U;
  }
  return 1000U;
}

auto SampleSyntheticDieScale(std::mt19937& generator, std::size_t real_load_count, SyntheticDistributionFamily family,
                             std::size_t synthetic_load_count) -> std::pair<double, double>
{
  const double load_ratio = static_cast<double>(std::max<std::size_t>(1U, synthetic_load_count))
                            / static_cast<double>(std::max<std::size_t>(1U, real_load_count));
  const double base_linear_scale = std::sqrt(std::max(load_ratio, 1.0e-6));

  std::pair<double, double> density_range = {0.94, 1.06};
  std::pair<double, double> aspect_range = {0.88, 1.14};
  switch (family) {
    case SyntheticDistributionFamily::kArm9Subset:
      density_range = {0.96, 1.04};
      aspect_range = {0.92, 1.10};
      break;
    case SyntheticDistributionFamily::kGaussianHotspots:
      density_range = {0.88, 1.00};
      aspect_range = {0.84, 1.18};
      break;
    case SyntheticDistributionFamily::kAxisBands:
      density_range = {0.92, 1.06};
      aspect_range = {0.68, 1.48};
      break;
    case SyntheticDistributionFamily::kDiagonalBand:
      density_range = {0.93, 1.05};
      aspect_range = {0.74, 1.34};
      break;
    case SyntheticDistributionFamily::kUniformSpread:
      density_range = {1.00, 1.14};
      aspect_range = {0.90, 1.12};
      break;
  }

  std::uniform_real_distribution<double> density_distribution(density_range.first, density_range.second);
  std::uniform_real_distribution<double> aspect_distribution(aspect_range.first, aspect_range.second);
  const double density_scale = density_distribution(generator);
  const double aspect_scale = aspect_distribution(generator);
  const double final_linear_scale = base_linear_scale * density_scale;
  return {
      std::clamp(final_linear_scale * aspect_scale, 0.55, 2.80),
      std::clamp(final_linear_scale / aspect_scale, 0.55, 2.80),
  };
}

auto SampleJitterRatio(std::mt19937& generator, SyntheticDistributionFamily family, std::size_t synthetic_load_count) -> double
{
  std::pair<double, double> jitter_range = {0.002, 0.010};
  switch (family) {
    case SyntheticDistributionFamily::kArm9Subset:
      jitter_range = {0.002, 0.010};
      break;
    case SyntheticDistributionFamily::kGaussianHotspots:
      jitter_range = {0.001, 0.006};
      break;
    case SyntheticDistributionFamily::kAxisBands:
    case SyntheticDistributionFamily::kDiagonalBand:
      jitter_range = {0.001, 0.007};
      break;
    case SyntheticDistributionFamily::kUniformSpread:
      jitter_range = {0.001, 0.004};
      break;
  }

  double scale_bonus = 0.0;
  if (synthetic_load_count >= 5000U) {
    scale_bonus = 0.0020;
  } else if (synthetic_load_count >= 2000U) {
    scale_bonus = 0.0010;
  }
  std::uniform_real_distribution<double> jitter_distribution(jitter_range.first, jitter_range.second + scale_bonus);
  return jitter_distribution(generator);
}

auto BuildRepresentativeCaseSpecs(std::size_t real_load_count) -> std::vector<SyntheticCaseSpec>
{
  std::vector<SyntheticCaseSpec> specs;
  specs.reserve(kSyntheticDistributionFamilies.size() * kSyntheticLoadScales.size() * kRepresentativeCasesPerFamilyScale);
  std::seed_seq seed_sequence{kRepresentativeBenchmarkSeedBase};
  std::mt19937 generator(seed_sequence);

  for (const auto family : kSyntheticDistributionFamilies) {
    for (const auto load_scale : kSyntheticLoadScales) {
      const auto load_count = ResolveSyntheticLoadCount(load_scale);
      for (std::size_t family_instance_index = 0; family_instance_index < kRepresentativeCasesPerFamilyScale; ++family_instance_index) {
        const auto [die_scale_x, die_scale_y] = SampleSyntheticDieScale(generator, real_load_count, family, load_count);
        specs.push_back(SyntheticCaseSpec{
            .family = family,
            .load_scale = load_scale,
            .family_instance_index = family_instance_index,
            .load_count = load_count,
            .die_scale_x = die_scale_x,
            .die_scale_y = die_scale_y,
            .local_jitter_ratio = SampleJitterRatio(generator, family, load_count),
        });
      }
    }
  }

  return specs;
}

auto GenerateArm9SubsetPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (source_cloud.points.empty() || count == 0U) {
    return points;
  }

  std::vector<std::size_t> indices(source_cloud.points.size());
  std::iota(indices.begin(), indices.end(), 0U);
  std::shuffle(indices.begin(), indices.end(), generator);

  points.reserve(count);
  const auto direct_count = std::min(count, indices.size());
  for (std::size_t index = 0; index < direct_count; ++index) {
    const auto& source_point = source_cloud.points.at(indices.at(index));
    points.emplace_back(source_point.normalized_x, source_point.normalized_y);
  }

  if (count > direct_count) {
    std::uniform_int_distribution<std::size_t> source_index_distribution(0U, source_cloud.points.size() - 1U);
    while (points.size() < count) {
      const auto& source_point = source_cloud.points.at(source_index_distribution(generator));
      points.emplace_back(source_point.normalized_x, source_point.normalized_y);
    }
  }
  return points;
}

auto GenerateGaussianHotspotPoints(const SourcePointCloud& source_cloud, std::size_t count, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (source_cloud.points.empty() || count == 0U) {
    return points;
  }

  std::uniform_int_distribution<int> hotspot_count_distribution(2, 4);
  const auto hotspot_count = static_cast<std::size_t>(hotspot_count_distribution(generator));
  std::vector<std::size_t> indices(source_cloud.points.size());
  std::iota(indices.begin(), indices.end(), 0U);
  std::shuffle(indices.begin(), indices.end(), generator);

  std::vector<std::pair<double, double>> centers;
  std::vector<double> weights;
  centers.reserve(hotspot_count);
  weights.reserve(hotspot_count);
  std::uniform_real_distribution<double> weight_distribution(0.2, 1.0);
  for (std::size_t hotspot_index = 0; hotspot_index < hotspot_count; ++hotspot_index) {
    const auto& source_point = source_cloud.points.at(indices.at(hotspot_index % indices.size()));
    centers.emplace_back(source_point.normalized_x, source_point.normalized_y);
    weights.push_back(weight_distribution(generator));
  }

  std::discrete_distribution<std::size_t> hotspot_distribution(weights.begin(), weights.end());
  std::uniform_real_distribution<double> sigma_distribution(0.035, 0.10);
  const double sigma_x = sigma_distribution(generator);
  const double sigma_y = sigma_distribution(generator);

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const auto& center = centers.at(hotspot_distribution(generator));
    std::normal_distribution<double> x_distribution(center.first, sigma_x);
    std::normal_distribution<double> y_distribution(center.second, sigma_y);
    points.emplace_back(ClampUnit(x_distribution(generator)), ClampUnit(y_distribution(generator)));
  }
  return points;
}

auto GenerateAxisBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  std::bernoulli_distribution vertical_distribution(0.5);
  const bool vertical_bands = vertical_distribution(generator);
  std::uniform_int_distribution<int> band_count_distribution(2, 4);
  const auto band_count = static_cast<std::size_t>(band_count_distribution(generator));
  std::uniform_real_distribution<double> center_distribution(0.12, 0.88);
  std::uniform_real_distribution<double> width_distribution(0.025, 0.08);

  std::vector<double> band_centers;
  band_centers.reserve(band_count);
  for (std::size_t band_index = 0; band_index < band_count; ++band_index) {
    band_centers.push_back(center_distribution(generator));
  }

  std::vector<double> band_weights(band_count, 1.0);
  std::discrete_distribution<std::size_t> band_distribution(band_weights.begin(), band_weights.end());
  const double band_width = width_distribution(generator);
  std::uniform_real_distribution<double> secondary_distribution(0.0, 1.0);

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const auto band_center = band_centers.at(band_distribution(generator));
    std::normal_distribution<double> primary_distribution(band_center, band_width);
    const auto primary = ClampUnit(primary_distribution(generator));
    const auto secondary = ClampUnit(secondary_distribution(generator));
    points.emplace_back(vertical_bands ? primary : secondary, vertical_bands ? secondary : primary);
  }
  return points;
}

auto GenerateDiagonalBandPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  std::bernoulli_distribution anti_diagonal_distribution(0.5);
  const bool anti_diagonal = anti_diagonal_distribution(generator);
  std::uniform_real_distribution<double> position_distribution(0.0, 1.0);
  std::normal_distribution<double> parallel_distribution(0.0, 0.02);
  std::uniform_real_distribution<double> width_distribution(0.035, 0.10);
  std::normal_distribution<double> cross_distribution(0.0, width_distribution(generator));

  points.reserve(count);
  for (std::size_t point_index = 0; point_index < count; ++point_index) {
    const double u = position_distribution(generator) + parallel_distribution(generator);
    const double v = cross_distribution(generator);
    if (anti_diagonal) {
      points.emplace_back(ClampUnit(u + v), ClampUnit(1.0 - u + v));
    } else {
      points.emplace_back(ClampUnit(u + v), ClampUnit(u - v));
    }
  }
  return points;
}

auto GenerateUniformSpreadPoints(std::size_t count, std::mt19937& generator) -> std::vector<std::pair<double, double>>
{
  std::vector<std::pair<double, double>> points;
  if (count == 0U) {
    return points;
  }

  const auto grid_side = std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count)))));
  std::vector<std::size_t> cell_indices(grid_side * grid_side);
  std::iota(cell_indices.begin(), cell_indices.end(), 0U);
  std::shuffle(cell_indices.begin(), cell_indices.end(), generator);
  cell_indices.resize(count);

  std::uniform_real_distribution<double> unit_distribution(0.0, 1.0);
  points.reserve(count);
  for (const auto cell_index : cell_indices) {
    const auto row = cell_index / grid_side;
    const auto column = cell_index % grid_side;
    const double x = (static_cast<double>(column) + unit_distribution(generator)) / static_cast<double>(grid_side);
    const double y = (static_cast<double>(row) + unit_distribution(generator)) / static_cast<double>(grid_side);
    points.emplace_back(ClampUnit(x), ClampUnit(y));
  }
  return points;
}

auto GenerateRepresentativeNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, std::mt19937& generator)
    -> std::vector<std::pair<double, double>>
{
  switch (spec.family) {
    case SyntheticDistributionFamily::kArm9Subset:
      return GenerateArm9SubsetPoints(source_cloud, spec.load_count, generator);
    case SyntheticDistributionFamily::kGaussianHotspots:
      return GenerateGaussianHotspotPoints(source_cloud, spec.load_count, generator);
    case SyntheticDistributionFamily::kAxisBands:
      return GenerateAxisBandPoints(spec.load_count, generator);
    case SyntheticDistributionFamily::kDiagonalBand:
      return GenerateDiagonalBandPoints(spec.load_count, generator);
    case SyntheticDistributionFamily::kUniformSpread:
      return GenerateUniformSpreadPoints(spec.load_count, generator);
  }
  return {};
}

auto BuildPinsFromNormalizedPoints(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec,
                                   const std::vector<std::pair<double, double>>& normalized_points, unsigned seed) -> GeneratedPins
{
  GeneratedPins generated;
  if (source_cloud.points.empty() || normalized_points.empty()) {
    return generated;
  }

  const double target_width = std::max(1.0, source_cloud.source_width * spec.die_scale_x);
  const double target_height = std::max(1.0, source_cloud.source_height * spec.die_scale_y);
  const double margin_x = std::max(8.0, source_cloud.source_width * 0.01);
  const double margin_y = std::max(8.0, source_cloud.source_height * 0.01);
  const double usable_width = std::max(1.0, target_width - (2.0 * margin_x));
  const double usable_height = std::max(1.0, target_height - (2.0 * margin_y));

  std::mt19937 generator(seed ^ 0x9e3779b9U);
  std::uniform_real_distribution<double> jitter_x_distribution(-(source_cloud.source_width * spec.local_jitter_ratio),
                                                               source_cloud.source_width * spec.local_jitter_ratio);
  std::uniform_real_distribution<double> jitter_y_distribution(-(source_cloud.source_height * spec.local_jitter_ratio),
                                                               source_cloud.source_height * spec.local_jitter_ratio);

  std::vector<std::size_t> template_indices(source_cloud.points.size());
  std::iota(template_indices.begin(), template_indices.end(), 0U);
  std::shuffle(template_indices.begin(), template_indices.end(), generator);

  generated.storage.reserve(normalized_points.size());
  generated.loads.reserve(normalized_points.size());
  for (std::size_t point_index = 0; point_index < normalized_points.size(); ++point_index) {
    const auto& normalized_point = normalized_points.at(point_index);
    const auto& source_point = source_cloud.points.at(template_indices.at(point_index % template_indices.size()));
    const auto* source_pin = source_point.pin;
    if (source_pin == nullptr) {
      continue;
    }

    const double jitter_x = jitter_x_distribution(generator);
    const double jitter_y = jitter_y_distribution(generator);
    const int mapped_x
        = ClampToIntRange(std::clamp(margin_x + (ClampUnit(normalized_point.first) * usable_width) + jitter_x, 0.0, target_width));
    const int mapped_y
        = ClampToIntRange(std::clamp(margin_y + (ClampUnit(normalized_point.second) * usable_height) + jitter_y, 0.0, target_height));

    const bool keep_exact_cap_context = HasExactCapContext(source_pin);
    const auto pin_name = keep_exact_cap_context ? std::string(source_pin->get_name())
                                                 : std::string(source_pin->get_name()) + "__" + SyntheticDistributionFamilyName(spec.family)
                                                       + "_" + std::to_string(point_index);
    auto synthetic_pin = std::make_unique<icts::Pin>(pin_name, source_pin->get_type(), icts::Point<int>(mapped_x, mapped_y),
                                                     keep_exact_cap_context ? source_pin->get_inst() : nullptr,
                                                     keep_exact_cap_context ? source_pin->get_net() : nullptr, source_pin->is_io());
    generated.loads.push_back(synthetic_pin.get());
    generated.storage.push_back(std::move(synthetic_pin));
  }

  generated.width = ClampToIntRange(target_width);
  generated.height = ClampToIntRange(target_height);
  return generated;
}

auto BuildRepresentativePins(const SourcePointCloud& source_cloud, const SyntheticCaseSpec& spec, unsigned seed) -> GeneratedPins
{
  std::mt19937 generator(seed);
  const auto normalized_points = GenerateRepresentativeNormalizedPoints(source_cloud, spec, generator);
  return BuildPinsFromNormalizedPoints(source_cloud, spec, normalized_points, seed);
}

}  // namespace icts_test::linear_clustering::realtech::experiment
