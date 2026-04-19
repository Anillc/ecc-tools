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
 * @file LinearOrderGenerator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Geometry-only sequence generation for linear clustering.
 */

#include "LinearOrderGenerator.hh"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

#include "LinearClusteringTypes.hh"
#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"

namespace icts {
namespace {

constexpr std::uint32_t kMaxDiscreteOrderBits = 31U;
constexpr std::uint32_t kDefaultDiscreteOrderBits = 16U;
constexpr double kDegenerateAxisNormalizedCoord = 0.5;
constexpr double kDensityEmptyBinFloor = 1.0e-6;
constexpr double kHilbertTerminalTheta = 0.5;
constexpr double kHilbertCenterCoord = 0.5;
constexpr double kHilbertAxisScale = 2.0;
constexpr double kHilbertQuadrantDivisor = 4.0;
constexpr double kHilbertPhaseOffset = 7.0 / 8.0;

struct HilbertAxes
{
  std::uint32_t x_coord = 0U;
  std::uint32_t y_coord = 0U;
};

struct DensityEqualizationModel
{
  std::size_t density_grid_size = 1;
  std::vector<double> x_source_edges;
  std::vector<double> y_source_edges;
  std::vector<double> x_mapped_edges;
  std::vector<double> y_mapped_edges;
};

struct NormalizedCoord
{
  double value = 0.0;
};

struct DensityScaleExponent
{
  double value = 1.0;
};

struct DensityGridSize
{
  std::size_t value = 1;
};

struct DensityBinIndex
{
  std::size_t value = 0;
};

struct ProjectedLoad
{
  OrderedLoad load;
  NormalizedCoord x;
  NormalizedCoord y;
};

struct ContinuousOrderEntry
{
  OrderedLoad load;
  double theta = 0.0;
};

struct DiscreteOrderEntry
{
  OrderedLoad load;
  std::uint64_t hilbert_index = 0U;
  double cell_theta = 0.0;
  double theta = 0.0;
  double projected_x = 0.0;
  double projected_y = 0.0;
  double tangent_position = 0.0;
  double orthogonal_position = 0.0;
};

auto CalcContinuousHilbertTheta(double x_coord, double y_coord) -> double;

auto NormalizeCoord(int coord, int min_coord, int max_coord) -> NormalizedCoord
{
  if (max_coord <= min_coord) {
    return {.value = kDegenerateAxisNormalizedCoord};
  }
  const auto ratio = static_cast<double>(coord - min_coord) / static_cast<double>(max_coord - min_coord);
  return {.value = std::clamp(ratio, 0.0, 1.0)};
}

auto NormalizeToBinLocalRatio(double coord, const std::vector<double>& source_edges, DensityBinIndex bin_index) -> double
{
  if (source_edges.size() < 2U || (bin_index.value + 1U) >= source_edges.size()) {
    return kDegenerateAxisNormalizedCoord;
  }
  const auto cell_begin = source_edges.at(bin_index.value);
  const auto cell_end = source_edges.at(bin_index.value + 1U);
  if (cell_end <= cell_begin) {
    return kDegenerateAxisNormalizedCoord;
  }
  return std::clamp((coord - cell_begin) / (cell_end - cell_begin), 0.0, 1.0);
}

auto BuildUniformSourceEdges(int min_coord, int max_coord, DensityGridSize density_grid_size) -> std::vector<double>
{
  const auto resolved_grid_size = std::max<std::size_t>(1U, density_grid_size.value);
  std::vector<double> source_edges(resolved_grid_size + 1U, static_cast<double>(min_coord));
  if (max_coord <= min_coord) {
    return source_edges;
  }

  const auto span = static_cast<double>(max_coord - min_coord);
  for (std::size_t bin = 0; bin <= resolved_grid_size; ++bin) {
    source_edges.at(bin) = static_cast<double>(min_coord) + (static_cast<double>(bin) * span / static_cast<double>(resolved_grid_size));
  }
  source_edges.back() = static_cast<double>(max_coord);
  return source_edges;
}

auto BuildUniformMappedEdges(DensityGridSize density_grid_size) -> std::vector<double>
{
  const auto resolved_grid_size = std::max<std::size_t>(1U, density_grid_size.value);
  std::vector<double> mapped_edges(resolved_grid_size + 1U, 0.0);
  const auto grid_size_f64 = static_cast<double>(resolved_grid_size);
  for (std::size_t bin = 0; bin < resolved_grid_size; ++bin) {
    mapped_edges.at(bin + 1U) = static_cast<double>(bin + 1U) / grid_size_f64;
  }
  mapped_edges.back() = 1.0;
  return mapped_edges;
}

auto BuildDensityAxisModel(const std::vector<std::size_t>& density_counts, int min_coord, int max_coord, DensityGridSize density_grid_size,
                           DensityScaleExponent density_exponent) -> std::pair<std::vector<double>, std::vector<double>>
{
  auto source_edges = BuildUniformSourceEdges(min_coord, max_coord, density_grid_size);
  auto mapped_edges = BuildUniformMappedEdges(density_grid_size);
  if (density_counts.empty() || max_coord <= min_coord) {
    return {std::move(source_edges), std::move(mapped_edges)};
  }

  std::vector<double> weights(density_counts.size(), 0.0);
  for (std::size_t bin = 0; bin < density_counts.size(); ++bin) {
    const auto floored_count = std::max(static_cast<double>(density_counts.at(bin)), kDensityEmptyBinFloor);
    weights.at(bin) = std::pow(floored_count, density_exponent.value);
  }

  const auto total_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (total_weight <= 0.0) {
    return {std::move(source_edges), std::move(mapped_edges)};
  }

  mapped_edges.at(0) = 0.0;
  for (std::size_t bin = 0; bin < weights.size(); ++bin) {
    mapped_edges.at(bin + 1U) = mapped_edges.at(bin) + (weights.at(bin) / total_weight);
  }
  mapped_edges.back() = 1.0;
  return {std::move(source_edges), std::move(mapped_edges)};
}

auto LocateCoordInDensityBin(double coord, const std::vector<double>& source_edges) -> DensityBinIndex
{
  if (source_edges.size() < 2U) {
    return {.value = 0U};
  }
  if (coord <= source_edges.front()) {
    return {.value = 0U};
  }
  if (coord >= source_edges.back()) {
    return {.value = source_edges.size() - 2U};
  }

  const auto upper = std::ranges::upper_bound(source_edges, coord);
  const auto offset = static_cast<std::size_t>(upper - source_edges.begin());
  return {.value = std::max<std::size_t>(0U, offset - 1U)};
}

auto HilbertRotate(std::uint32_t span_limit, HilbertAxes axes, std::uint32_t rotation_x, std::uint32_t rotation_y) -> HilbertAxes
{
  if (rotation_y == 0U) {
    if (rotation_x == 1U) {
      axes.x_coord = span_limit - 1U - axes.x_coord;
      axes.y_coord = span_limit - 1U - axes.y_coord;
    }
    std::swap(axes.x_coord, axes.y_coord);
  }
  return axes;
}

auto CalcDiscreteHilbertIndex(HilbertAxes axes, std::uint32_t order_bits) -> std::uint64_t
{
  if (order_bits == 0U) {
    return 0U;
  }

  std::uint64_t index = 0U;
  const auto grid_width = static_cast<std::uint32_t>(1U << order_bits);
  for (std::uint32_t step = grid_width >> 1U; step > 0U; step >>= 1U) {
    const auto rotation_x = (axes.x_coord & step) > 0U ? 1U : 0U;
    const auto rotation_y = (axes.y_coord & step) > 0U ? 1U : 0U;
    index
        += static_cast<std::uint64_t>(step) * static_cast<std::uint64_t>(step) * static_cast<std::uint64_t>((3U * rotation_x) ^ rotation_y);
    axes = HilbertRotate(step, axes, rotation_x, rotation_y);
  }
  return index;
}

auto CalcDiscreteHilbertTheta(HilbertAxes axes, std::uint32_t order_bits) -> double
{
  if (order_bits == 0U) {
    return CalcContinuousHilbertTheta(kHilbertCenterCoord, kHilbertCenterCoord);
  }

  const auto grid_width = static_cast<double>(std::uint64_t{1} << order_bits);
  const auto cell_center_x = (static_cast<double>(axes.x_coord) + kHilbertTerminalTheta) / grid_width;
  const auto cell_center_y = (static_cast<double>(axes.y_coord) + kHilbertTerminalTheta) / grid_width;
  return CalcContinuousHilbertTheta(cell_center_x, cell_center_y);
}

auto CalcDiscreteCellLocalCoord(NormalizedCoord normalized_coord, std::uint32_t axis_coord, std::uint32_t order_bits) -> double
{
  if (order_bits == 0U) {
    return kDegenerateAxisNormalizedCoord;
  }

  const auto grid_width = static_cast<double>(std::uint64_t{1} << order_bits);
  const auto scaled = (std::clamp(normalized_coord.value, 0.0, 1.0) * grid_width) - static_cast<double>(axis_coord);
  return std::clamp(scaled, 0.0, 1.0);
}

auto CalcHilbertAxesFromIndex(std::uint64_t hilbert_index, std::uint32_t order_bits) -> HilbertAxes
{
  if (order_bits == 0U) {
    return {};
  }

  HilbertAxes axes;
  const auto grid_width = static_cast<std::uint32_t>(1U << order_bits);
  for (std::uint32_t step = 1U; step < grid_width; step <<= 1U) {
    const auto rotation_x = static_cast<std::uint32_t>((hilbert_index >> 1U) & 1U);
    const auto rotation_y = static_cast<std::uint32_t>((hilbert_index ^ rotation_x) & 1U);
    axes = HilbertRotate(step, axes, rotation_x, rotation_y);
    axes.x_coord += step * rotation_x;
    axes.y_coord += step * rotation_y;
    hilbert_index >>= 2U;
  }
  return axes;
}

auto CalcHilbertTangentProjection(std::uint64_t hilbert_index, std::uint32_t order_bits, double local_x, double local_y)
    -> std::pair<double, double>
{
  if (order_bits == 0U) {
    return {0.0, 0.0};
  }

  const auto current_axes = CalcHilbertAxesFromIndex(hilbert_index, order_bits);
  const auto max_index = (std::uint64_t{1} << (2U * order_bits)) - 1U;

  auto previous_axes = current_axes;
  auto next_axes = current_axes;
  if (hilbert_index > 0U) {
    previous_axes = CalcHilbertAxesFromIndex(hilbert_index - 1U, order_bits);
  }
  if (hilbert_index < max_index) {
    next_axes = CalcHilbertAxesFromIndex(hilbert_index + 1U, order_bits);
  }

  double direction_x = static_cast<double>(next_axes.x_coord) - static_cast<double>(previous_axes.x_coord);
  double direction_y = static_cast<double>(next_axes.y_coord) - static_cast<double>(previous_axes.y_coord);
  if (direction_x == 0.0 && direction_y == 0.0) {
    if (hilbert_index > 0U) {
      direction_x = static_cast<double>(current_axes.x_coord) - static_cast<double>(previous_axes.x_coord);
      direction_y = static_cast<double>(current_axes.y_coord) - static_cast<double>(previous_axes.y_coord);
    } else if (hilbert_index < max_index) {
      direction_x = static_cast<double>(next_axes.x_coord) - static_cast<double>(current_axes.x_coord);
      direction_y = static_cast<double>(next_axes.y_coord) - static_cast<double>(current_axes.y_coord);
    }
  }

  const auto direction_norm = std::hypot(direction_x, direction_y);
  if (direction_norm <= 0.0) {
    return {0.0, 0.0};
  }

  direction_x /= direction_norm;
  direction_y /= direction_norm;
  const auto centered_x = local_x - kHilbertCenterCoord;
  const auto centered_y = local_y - kHilbertCenterCoord;
  const auto tangent_position = (centered_x * direction_x) + (centered_y * direction_y);
  const auto orthogonal_position = (-centered_x * direction_y) + (centered_y * direction_x);
  return {tangent_position, orthogonal_position};
}

auto ApplyHilbertTransform(NormalizedCoord x_coord, NormalizedCoord y_coord, HilbertTransform transform)
    -> std::pair<NormalizedCoord, NormalizedCoord>
{
  const auto x = std::clamp(x_coord.value, 0.0, 1.0);
  const auto y = std::clamp(y_coord.value, 0.0, 1.0);

  switch (transform) {
    case HilbertTransform::kIdentity:
      return {NormalizedCoord{.value = x}, NormalizedCoord{.value = y}};
    case HilbertTransform::kMirrorX:
      return {NormalizedCoord{.value = 1.0 - x}, NormalizedCoord{.value = y}};
    case HilbertTransform::kMirrorY:
      return {NormalizedCoord{.value = x}, NormalizedCoord{.value = 1.0 - y}};
    case HilbertTransform::kMirrorXY:
      return {NormalizedCoord{.value = 1.0 - x}, NormalizedCoord{.value = 1.0 - y}};
    case HilbertTransform::kSwapXY:
      return {NormalizedCoord{.value = y}, NormalizedCoord{.value = x}};
    case HilbertTransform::kSwapMirrorX:
      return {NormalizedCoord{.value = 1.0 - y}, NormalizedCoord{.value = x}};
    case HilbertTransform::kSwapMirrorY:
      return {NormalizedCoord{.value = y}, NormalizedCoord{.value = 1.0 - x}};
    case HilbertTransform::kSwapMirrorXY:
      return {NormalizedCoord{.value = 1.0 - y}, NormalizedCoord{.value = 1.0 - x}};
  }
  return {NormalizedCoord{.value = x}, NormalizedCoord{.value = y}};
}

auto QuantizeToDiscreteAxisCoord(NormalizedCoord normalized_coord, std::uint32_t order_bits) -> std::uint32_t
{
  if (order_bits == 0U) {
    return 0U;
  }

  // Quantize the projected continuous coordinate directly so density equalization
  // can change the discrete Hilbert walk; axis-rank compression would erase any
  // monotone remapping and collapse density-scaled discrete ordering.
  const auto clamped = std::clamp(normalized_coord.value, 0.0, 1.0);
  const auto grid_width = (std::uint64_t{1} << order_bits);
  if (clamped >= 1.0) {
    return static_cast<std::uint32_t>(grid_width - 1U);
  }

  const auto quantized = static_cast<std::uint64_t>(std::floor(clamped * static_cast<double>(grid_width)));
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(quantized, grid_width - 1U));
}

auto IsContinuousHilbert(LinearOrderStrategy strategy) -> bool
{
  return strategy == LinearOrderStrategy::kContinuousHilbert || strategy == LinearOrderStrategy::kDensityScaledContinuousHilbert;
}

auto IsDiscreteHilbert(LinearOrderStrategy strategy) -> bool
{
  return strategy == LinearOrderStrategy::kDiscreteHilbert || strategy == LinearOrderStrategy::kDensityScaledDiscreteHilbert;
}

auto UsesDensityEqualization(LinearOrderStrategy strategy) -> bool
{
  return strategy == LinearOrderStrategy::kDensityScaledContinuousHilbert || strategy == LinearOrderStrategy::kDensityScaledDiscreteHilbert;
}

auto BuildIdentityDensityEqualizationModel(std::size_t density_grid_size) -> DensityEqualizationModel
{
  DensityEqualizationModel model;
  model.density_grid_size = std::max<std::size_t>(1U, density_grid_size);
  const DensityGridSize resolved_grid_size{.value = model.density_grid_size};
  model.x_source_edges = BuildUniformMappedEdges(resolved_grid_size);
  model.y_source_edges = BuildUniformMappedEdges(resolved_grid_size);
  model.x_mapped_edges = BuildUniformMappedEdges(resolved_grid_size);
  model.y_mapped_edges = BuildUniformMappedEdges(resolved_grid_size);
  return model;
}

auto BuildDensityEqualizationModel(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                                   const LinearOrderGenerator::Bounds& bounds) -> DensityEqualizationModel
{
  auto model = BuildIdentityDensityEqualizationModel(std::max<std::size_t>(1U, config.density_grid_size));
  if (ordered_loads.empty() || model.density_grid_size <= 1U) {
    return model;
  }

  model.x_source_edges = BuildUniformSourceEdges(bounds.min_x, bounds.max_x, DensityGridSize{.value = model.density_grid_size});
  model.y_source_edges = BuildUniformSourceEdges(bounds.min_y, bounds.max_y, DensityGridSize{.value = model.density_grid_size});

  std::vector<std::size_t> density_x(model.density_grid_size, 0U);
  std::vector<std::size_t> density_y(model.density_grid_size, 0U);
  const DensityGridSize density_grid_size{.value = model.density_grid_size};
  for (const auto& load : ordered_loads) {
    const auto column = LocateCoordInDensityBin(static_cast<double>(load.location.get_x()), model.x_source_edges);
    const auto row = LocateCoordInDensityBin(static_cast<double>(load.location.get_y()), model.y_source_edges);
    ++density_x.at(column.value);
    ++density_y.at(row.value);
  }

  const DensityScaleExponent density_exponent{.value = std::clamp(config.density_scale_power, 0.0, 4.0)};
  std::tie(model.x_source_edges, model.x_mapped_edges)
      = BuildDensityAxisModel(density_x, bounds.min_x, bounds.max_x, density_grid_size, density_exponent);
  std::tie(model.y_source_edges, model.y_mapped_edges)
      = BuildDensityAxisModel(density_y, bounds.min_y, bounds.max_y, density_grid_size, density_exponent);
  return model;
}

auto ApplyDensityEqualization(int x_coord, int y_coord, const DensityEqualizationModel& model, const LinearOrderGenerator::Bounds& bounds)
    -> std::pair<NormalizedCoord, NormalizedCoord>
{
  if (model.density_grid_size <= 1U || model.x_source_edges.size() != (model.density_grid_size + 1U)
      || model.y_source_edges.size() != (model.density_grid_size + 1U) || model.x_mapped_edges.size() != (model.density_grid_size + 1U)
      || model.y_mapped_edges.size() != (model.density_grid_size + 1U)) {
    return {NormalizeCoord(x_coord, bounds.min_x, bounds.max_x), NormalizeCoord(y_coord, bounds.min_y, bounds.max_y)};
  }

  double mapped_x = NormalizeCoord(x_coord, bounds.min_x, bounds.max_x).value;
  if (bounds.max_x > bounds.min_x) {
    const auto column = LocateCoordInDensityBin(static_cast<double>(x_coord), model.x_source_edges);
    const auto column_local_ratio = NormalizeToBinLocalRatio(static_cast<double>(x_coord), model.x_source_edges, column);
    const auto mapped_x_begin = model.x_mapped_edges.at(column.value);
    const auto mapped_x_end = model.x_mapped_edges.at(column.value + 1U);
    mapped_x = mapped_x_begin + (column_local_ratio * (mapped_x_end - mapped_x_begin));
  }

  double mapped_y = NormalizeCoord(y_coord, bounds.min_y, bounds.max_y).value;
  if (bounds.max_y > bounds.min_y) {
    const auto row = LocateCoordInDensityBin(static_cast<double>(y_coord), model.y_source_edges);
    const auto row_local_ratio = NormalizeToBinLocalRatio(static_cast<double>(y_coord), model.y_source_edges, row);
    const auto mapped_y_begin = model.y_mapped_edges.at(row.value);
    const auto mapped_y_end = model.y_mapped_edges.at(row.value + 1U);
    mapped_y = mapped_y_begin + (row_local_ratio * (mapped_y_end - mapped_y_begin));
  }

  return {NormalizedCoord{.value = std::clamp(mapped_x, 0.0, 1.0)}, NormalizedCoord{.value = std::clamp(mapped_y, 0.0, 1.0)}};
}

auto IsSinkOne(double pos) -> bool
{
  return (1.0 - pos) < std::numeric_limits<double>::epsilon();
}

auto NumSinkVertex(unsigned x_coord, unsigned y_coord) -> unsigned
{
  if ((x_coord == 0U) && (y_coord == 0U)) {
    return 0U;
  }
  if ((x_coord == 0U) && (y_coord == 1U)) {
    return 1U;
  }
  if ((x_coord == 1U) && (y_coord == 1U)) {
    return 2U;
  }
  return 3U;
}

auto CalcContinuousHilbertTheta(double x_coord, double y_coord) -> double
{
  std::vector<unsigned> quadrant_trace;
  while (!(IsSinkOne(x_coord) && IsSinkOne(y_coord))) {
    quadrant_trace.push_back(NumSinkVertex(std::min(static_cast<unsigned>(kHilbertAxisScale * x_coord), 1U),
                                           std::min(static_cast<unsigned>(kHilbertAxisScale * y_coord), 1U)));
    x_coord = kHilbertAxisScale * std::fabs(x_coord - kHilbertCenterCoord);
    y_coord = kHilbertAxisScale * std::fabs(y_coord - kHilbertCenterCoord);
  }

  double theta = kHilbertTerminalTheta;
  for (const auto quad : std::views::reverse(quadrant_trace)) {
    if ((quad % 2U) == 1U) {
      theta = 1.0 - theta;
    }

    double integral = 0.0;
    theta = std::modf(((static_cast<double>(quad) + theta) / kHilbertQuadrantDivisor) + kHilbertPhaseOffset, &integral);
  }
  return theta;
}

auto BuildOrderedLoads(const std::vector<Pin*>& loads) -> std::vector<OrderedLoad>
{
  std::vector<OrderedLoad> ordered_loads;
  ordered_loads.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    ordered_loads.push_back(OrderedLoad{.pin = pin, .location = pin->get_location(), .original_index = index});
  }
  return ordered_loads;
}

auto BuildProjectedLoads(const std::vector<OrderedLoad>& ordered_loads, const LinearClusteringConfig& config,
                         const LinearOrderGenerator::Bounds& bounds) -> std::vector<ProjectedLoad>
{
  std::vector<ProjectedLoad> projected_loads;
  projected_loads.reserve(ordered_loads.size());

  DensityEqualizationModel equalization_model;
  if (UsesDensityEqualization(config.order_strategy)) {
    equalization_model = BuildDensityEqualizationModel(ordered_loads, config, bounds);
  }

  for (const auto& load : ordered_loads) {
    auto projected_x = NormalizeCoord(load.location.get_x(), bounds.min_x, bounds.max_x);
    auto projected_y = NormalizeCoord(load.location.get_y(), bounds.min_y, bounds.max_y);
    if (UsesDensityEqualization(config.order_strategy)) {
      std::tie(projected_x, projected_y)
          = ApplyDensityEqualization(load.location.get_x(), load.location.get_y(), equalization_model, bounds);
    }
    projected_loads.push_back(ProjectedLoad{.load = load, .x = projected_x, .y = projected_y});
  }

  return projected_loads;
}

auto ResolveDiscreteOrderBits(int configured_order_bits) -> std::uint32_t
{
  std::uint32_t resolved_bits = kDefaultDiscreteOrderBits;
  if (configured_order_bits > 0) {
    resolved_bits = static_cast<std::uint32_t>(configured_order_bits);
  }
  resolved_bits = std::max<std::uint32_t>(2U, resolved_bits);
  return std::min(resolved_bits, kMaxDiscreteOrderBits);
}

auto SortByContinuousHilbert(const std::vector<ProjectedLoad>& projected_loads) -> std::vector<OrderedLoad>
{
  std::vector<ContinuousOrderEntry> entries;
  entries.reserve(projected_loads.size());
  for (const auto& projected_load : projected_loads) {
    entries.push_back(ContinuousOrderEntry{
        .load = projected_load.load,
        .theta = CalcContinuousHilbertTheta(projected_load.x.value, projected_load.y.value),
    });
  }

  std::ranges::stable_sort(entries, [](const ContinuousOrderEntry& lhs, const ContinuousOrderEntry& rhs) -> bool {
    if (const auto theta_order = lhs.theta <=> rhs.theta; theta_order != std::partial_ordering::equivalent) {
      return theta_order == std::partial_ordering::less;
    }
    return lhs.load.original_index < rhs.load.original_index;
  });

  std::vector<OrderedLoad> ordered_loads;
  ordered_loads.reserve(entries.size());
  for (auto& entry : entries) {
    ordered_loads.push_back(entry.load);
  }
  return ordered_loads;
}

auto SortByDiscreteHilbert(const std::vector<ProjectedLoad>& projected_loads, const LinearClusteringConfig& config)
    -> std::vector<OrderedLoad>
{
  const auto order_bits = ResolveDiscreteOrderBits(config.order_bits);

  std::vector<DiscreteOrderEntry> entries;
  entries.reserve(projected_loads.size());
  for (const auto& projected_load : projected_loads) {
    auto [transformed_x, transformed_y] = ApplyHilbertTransform(projected_load.x, projected_load.y, config.hilbert_transform);
    const HilbertAxes axes{
        .x_coord = QuantizeToDiscreteAxisCoord(transformed_x, order_bits),
        .y_coord = QuantizeToDiscreteAxisCoord(transformed_y, order_bits),
    };
    const auto hilbert_index = CalcDiscreteHilbertIndex(axes, order_bits);
    const auto local_x = CalcDiscreteCellLocalCoord(transformed_x, axes.x_coord, order_bits);
    const auto local_y = CalcDiscreteCellLocalCoord(transformed_y, axes.y_coord, order_bits);
    const auto [tangent_position, orthogonal_position] = CalcHilbertTangentProjection(hilbert_index, order_bits, local_x, local_y);
    entries.push_back(DiscreteOrderEntry{
        .load = projected_load.load,
        .hilbert_index = hilbert_index,
        .cell_theta = CalcDiscreteHilbertTheta(axes, order_bits),
        .theta = CalcContinuousHilbertTheta(transformed_x.value, transformed_y.value),
        .projected_x = transformed_x.value,
        .projected_y = transformed_y.value,
        .tangent_position = tangent_position,
        .orthogonal_position = orthogonal_position,
    });
  }

  const auto encoding = config.discrete_hilbert_encoding;
  std::ranges::stable_sort(entries, [encoding](const DiscreteOrderEntry& lhs, const DiscreteOrderEntry& rhs) -> bool {
    auto compare_doubles = [](double lhs_value, double rhs_value) -> std::optional<bool> {
      if (const auto order = lhs_value <=> rhs_value; order != std::partial_ordering::equivalent) {
        return order == std::partial_ordering::less;
      }
      return std::nullopt;
    };
    auto compare_indices = [](std::uint64_t lhs_value, std::uint64_t rhs_value) -> std::optional<bool> {
      if (lhs_value == rhs_value) {
        return std::nullopt;
      }
      return lhs_value < rhs_value;
    };

    const auto compare_standard_tail = [&lhs, &rhs, &compare_doubles]() -> std::optional<bool> {
      if (const auto order = compare_doubles(lhs.theta, rhs.theta); order.has_value()) {
        return order;
      }
      if (const auto order = compare_doubles(lhs.projected_x, rhs.projected_x); order.has_value()) {
        return order;
      }
      if (const auto order = compare_doubles(lhs.projected_y, rhs.projected_y); order.has_value()) {
        return order;
      }
      return std::nullopt;
    };

    switch (encoding) {
      case DiscreteHilbertEncoding::kSinkThetaCell:
        if (const auto order = compare_doubles(lhs.cell_theta, rhs.cell_theta); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_indices(lhs.hilbert_index, rhs.hilbert_index); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_standard_tail(); order.has_value()) {
          return order.value();
        }
        break;
      case DiscreteHilbertEncoding::kSinkThetaCellTangent:
        if (const auto order = compare_doubles(lhs.cell_theta, rhs.cell_theta); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_indices(lhs.hilbert_index, rhs.hilbert_index); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_doubles(lhs.tangent_position, rhs.tangent_position); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_doubles(lhs.orthogonal_position, rhs.orthogonal_position); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_standard_tail(); order.has_value()) {
          return order.value();
        }
        break;
      case DiscreteHilbertEncoding::kClassicIndex:
        if (const auto order = compare_indices(lhs.hilbert_index, rhs.hilbert_index); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_doubles(lhs.cell_theta, rhs.cell_theta); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_standard_tail(); order.has_value()) {
          return order.value();
        }
        break;
      case DiscreteHilbertEncoding::kClassicIndexTangent:
        if (const auto order = compare_indices(lhs.hilbert_index, rhs.hilbert_index); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_doubles(lhs.tangent_position, rhs.tangent_position); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_doubles(lhs.orthogonal_position, rhs.orthogonal_position); order.has_value()) {
          return order.value();
        }
        if (const auto order = compare_standard_tail(); order.has_value()) {
          return order.value();
        }
        break;
    }
    return lhs.load.original_index < rhs.load.original_index;
  });

  std::vector<OrderedLoad> ordered_loads;
  ordered_loads.reserve(entries.size());
  for (auto& entry : entries) {
    ordered_loads.push_back(entry.load);
  }
  return ordered_loads;
}

}  // namespace

auto LinearOrderGenerator::generateOrder(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> std::vector<OrderedLoad>
{
  auto ordered_loads = BuildOrderedLoads(loads);
  if (ordered_loads.size() <= 1U) {
    return ordered_loads;
  }

  const auto bounds = calcBounds(loads);
  auto projected_loads = BuildProjectedLoads(ordered_loads, config, bounds);
  if (IsContinuousHilbert(config.order_strategy)) {
    return SortByContinuousHilbert(projected_loads);
  }
  if (IsDiscreteHilbert(config.order_strategy)) {
    return SortByDiscreteHilbert(projected_loads, config);
  }
  return SortByContinuousHilbert(projected_loads);
}

auto LinearOrderGenerator::calcBounds(const std::vector<Pin*>& loads) -> LinearOrderGenerator::Bounds
{
  Bounds bounds;
  if (loads.empty()) {
    return bounds;
  }

  bool initialized = false;
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    if (!initialized) {
      bounds.min_x = location.get_x();
      bounds.min_y = location.get_y();
      bounds.max_x = location.get_x();
      bounds.max_y = location.get_y();
      initialized = true;
      continue;
    }
    bounds.min_x = std::min(bounds.min_x, location.get_x());
    bounds.min_y = std::min(bounds.min_y, location.get_y());
    bounds.max_x = std::max(bounds.max_x, location.get_x());
    bounds.max_y = std::max(bounds.max_y, location.get_y());
  }

  if (!initialized) {
    bounds = {};
  }
  return bounds;
}

}  // namespace icts
