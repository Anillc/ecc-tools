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
 * @file LinearClusteringSyntheticReference.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Order-reference helpers for synthetic linear clustering tests.
 */

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic::detail {
namespace {

constexpr double kReferenceDensityEmptyBinFloor = 1.0e-6;
constexpr std::uint32_t kReferenceDefaultDiscreteOrderBits = 12U;
constexpr std::uint32_t kReferenceMaxDiscreteOrderBits = 31U;

struct ReferenceBounds
{
  int min_x = 0;
  int min_y = 0;
  int max_x = 0;
  int max_y = 0;
};

struct ReferenceDensityAxisModel
{
  std::vector<double> source_edges;
  std::vector<double> mapped_edges;
};

enum class ReferenceAxis : std::uint8_t
{
  kX,
  kY,
};

struct ReferenceGridSize
{
  std::size_t value = 1;
};

struct ReferenceScalePower
{
  double value = 1.0;
};

struct ReferenceOrderBits
{
  std::uint32_t value = kReferenceDefaultDiscreteOrderBits;
};

struct ReferenceDiscretePoint
{
  std::uint32_t x_coord = 0U;
  std::uint32_t y_coord = 0U;
};

auto NormalizeSinkReferenceCoord(int coord, int min_coord, int max_coord) -> double
{
  if (max_coord <= min_coord) {
    return kSinkReferenceCenterCoord;
  }
  return static_cast<double>(coord - min_coord) / static_cast<double>(max_coord - min_coord);
}

auto IsSinkReferenceOne(double pos) -> bool
{
  return (1.0 - pos) < std::numeric_limits<double>::epsilon();
}

auto SinkReferenceNumVertex(unsigned x_coord, unsigned y_coord) -> unsigned
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

auto ComputeSinkReferenceTheta(double x_coord, double y_coord) -> double
{
  std::vector<unsigned> quadrant_trace;
  while (!(IsSinkReferenceOne(x_coord) && IsSinkReferenceOne(y_coord))) {
    quadrant_trace.push_back(SinkReferenceNumVertex(std::min(static_cast<unsigned>(kSinkReferenceAxisScale * x_coord), 1U),
                                                    std::min(static_cast<unsigned>(kSinkReferenceAxisScale * y_coord), 1U)));
    x_coord = kSinkReferenceAxisScale * std::fabs(x_coord - kSinkReferenceCenterCoord);
    y_coord = kSinkReferenceAxisScale * std::fabs(y_coord - kSinkReferenceCenterCoord);
  }

  double theta = kSinkReferenceTerminalTheta;
  for (const auto quadrant : std::views::reverse(quadrant_trace)) {
    if ((quadrant % 2U) == 1U) {
      theta = 1.0 - theta;
    }

    double integral = 0.0;
    theta = std::modf(((static_cast<double>(quadrant) + theta) / kSinkReferenceQuadrantDivisor) + kSinkReferencePhaseOffset, &integral);
  }
  return theta;
}

auto CalcReferenceBounds(const std::vector<icts::Pin*>& loads) -> ReferenceBounds
{
  ReferenceBounds bounds;
  bool initialized = false;
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    if (!initialized) {
      bounds.min_x = location.get_x();
      bounds.max_x = location.get_x();
      bounds.min_y = location.get_y();
      bounds.max_y = location.get_y();
      initialized = true;
      continue;
    }
    bounds.min_x = std::min(bounds.min_x, location.get_x());
    bounds.max_x = std::max(bounds.max_x, location.get_x());
    bounds.min_y = std::min(bounds.min_y, location.get_y());
    bounds.max_y = std::max(bounds.max_y, location.get_y());
  }
  return bounds;
}

auto BuildUniformSourceEdges(const ReferenceBounds& bounds, ReferenceAxis axis, ReferenceGridSize density_grid_size) -> std::vector<double>
{
  const auto min_coord = axis == ReferenceAxis::kX ? bounds.min_x : bounds.min_y;
  const auto max_coord = axis == ReferenceAxis::kX ? bounds.max_x : bounds.max_y;
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

auto BuildUniformMappedEdges(ReferenceGridSize density_grid_size) -> std::vector<double>
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

auto LocateCoordInDensityBin(double coord, const std::vector<double>& source_edges) -> std::size_t
{
  if (source_edges.size() < 2U) {
    return 0U;
  }
  if (coord <= source_edges.front()) {
    return 0U;
  }
  if (coord >= source_edges.back()) {
    return source_edges.size() - 2U;
  }

  const auto upper = std::ranges::upper_bound(source_edges, coord);
  const auto offset = static_cast<std::size_t>(upper - source_edges.begin());
  return std::max<std::size_t>(0U, offset - 1U);
}

auto NormalizeToBinLocalRatio(double coord, const std::vector<double>& source_edges, std::size_t bin_index) -> double
{
  if (source_edges.size() < 2U || (bin_index + 1U) >= source_edges.size()) {
    return kSinkReferenceCenterCoord;
  }
  const auto cell_begin = source_edges.at(bin_index);
  const auto cell_end = source_edges.at(bin_index + 1U);
  if (cell_end <= cell_begin) {
    return kSinkReferenceCenterCoord;
  }
  return std::clamp((coord - cell_begin) / (cell_end - cell_begin), 0.0, 1.0);
}

auto BuildReferenceDensityAxisModel(const std::vector<icts::Pin*>& loads, ReferenceAxis axis, const ReferenceBounds& bounds,
                                    ReferenceGridSize density_grid_size, ReferenceScalePower density_scale_power)
    -> ReferenceDensityAxisModel
{
  ReferenceDensityAxisModel model;
  model.source_edges = BuildUniformSourceEdges(bounds, axis, density_grid_size);
  model.mapped_edges = BuildUniformMappedEdges(density_grid_size);

  if (loads.empty() || density_grid_size.value <= 1U) {
    return model;
  }

  std::vector<std::size_t> density_counts(std::max<std::size_t>(1U, density_grid_size.value), 0U);
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto coord = static_cast<double>(axis == ReferenceAxis::kX ? pin->get_location().get_x() : pin->get_location().get_y());
    const auto bin_index = LocateCoordInDensityBin(coord, model.source_edges);
    ++density_counts.at(bin_index);
  }

  const auto density_exponent = std::clamp(density_scale_power.value, 0.0, 4.0);
  std::vector<double> weights(density_counts.size(), 0.0);
  for (std::size_t index = 0; index < density_counts.size(); ++index) {
    const auto floored_count = std::max(static_cast<double>(density_counts.at(index)), kReferenceDensityEmptyBinFloor);
    weights.at(index) = std::pow(floored_count, density_exponent);
  }

  const auto total_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (total_weight <= 0.0) {
    return model;
  }

  model.mapped_edges.at(0) = 0.0;
  for (std::size_t index = 0; index < weights.size(); ++index) {
    model.mapped_edges.at(index + 1U) = model.mapped_edges.at(index) + (weights.at(index) / total_weight);
  }
  model.mapped_edges.back() = 1.0;
  return model;
}

auto ApplyReferenceDensityScaledCoord(int coord, const ReferenceDensityAxisModel& axis_model) -> double
{
  if (axis_model.source_edges.size() < 2U || axis_model.mapped_edges.size() < 2U
      || axis_model.source_edges.back() <= axis_model.source_edges.front()) {
    return kSinkReferenceCenterCoord;
  }

  const auto bin_index = LocateCoordInDensityBin(static_cast<double>(coord), axis_model.source_edges);
  const auto local_ratio = NormalizeToBinLocalRatio(static_cast<double>(coord), axis_model.source_edges, bin_index);
  const auto mapped_begin = axis_model.mapped_edges.at(bin_index);
  const auto mapped_end = axis_model.mapped_edges.at(bin_index + 1U);
  return std::clamp(mapped_begin + (local_ratio * (mapped_end - mapped_begin)), 0.0, 1.0);
}

auto ResolveDiscreteOrderBits(int configured_order_bits) -> ReferenceOrderBits
{
  std::uint32_t resolved_bits = kReferenceDefaultDiscreteOrderBits;
  if (configured_order_bits > 0) {
    resolved_bits = static_cast<std::uint32_t>(configured_order_bits);
  }
  resolved_bits = std::max<std::uint32_t>(2U, resolved_bits);
  return {.value = std::min(resolved_bits, kReferenceMaxDiscreteOrderBits)};
}

auto QuantizeToDiscreteAxisCoord(double normalized_coord, ReferenceOrderBits order_bits) -> std::uint32_t
{
  if (order_bits.value == 0U) {
    return 0U;
  }

  const auto clamped = std::clamp(normalized_coord, 0.0, 1.0);
  const auto grid_width = (std::uint64_t{1} << order_bits.value);
  if (clamped >= 1.0) {
    return static_cast<std::uint32_t>(grid_width - 1U);
  }

  const auto quantized = static_cast<std::uint64_t>(std::floor(clamped * static_cast<double>(grid_width)));
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(quantized, grid_width - 1U));
}

auto HilbertRotate(std::uint32_t span_limit, std::uint32_t& x_coord, std::uint32_t& y_coord, std::uint32_t rotation_x,
                   std::uint32_t rotation_y) -> void
{
  if (rotation_y == 0U) {
    if (rotation_x == 1U) {
      x_coord = span_limit - 1U - x_coord;
      y_coord = span_limit - 1U - y_coord;
    }
    std::swap(x_coord, y_coord);
  }
}

auto CalcDiscreteHilbertIndex(ReferenceDiscretePoint point, ReferenceOrderBits order_bits) -> std::uint64_t
{
  if (order_bits.value == 0U) {
    return 0U;
  }

  std::uint64_t index = 0U;
  auto x_coord = point.x_coord;
  auto y_coord = point.y_coord;
  const auto grid_width = static_cast<std::uint32_t>(1U << order_bits.value);
  for (std::uint32_t step = grid_width >> 1U; step > 0U; step >>= 1U) {
    const auto rotation_x = (x_coord & step) > 0U ? 1U : 0U;
    const auto rotation_y = (y_coord & step) > 0U ? 1U : 0U;
    index
        += static_cast<std::uint64_t>(step) * static_cast<std::uint64_t>(step) * static_cast<std::uint64_t>((3U * rotation_x) ^ rotation_y);
    HilbertRotate(step, x_coord, y_coord, rotation_x, rotation_y);
  }
  return index;
}

}  // namespace

auto ExtractOriginalIndices(const std::vector<icts::OrderedLoad>& ordered_loads) -> std::vector<std::size_t>
{
  std::vector<std::size_t> indices;
  indices.reserve(ordered_loads.size());
  for (const auto& load : ordered_loads) {
    indices.push_back(load.original_index);
  }
  return indices;
}

auto FormatOrderIndices(const std::vector<std::size_t>& indices) -> std::string
{
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < indices.size(); ++index) {
    if (index > 0U) {
      stream << ",";
    }
    stream << indices.at(index);
  }
  stream << "]";
  return stream.str();
}

auto FindFirstOrderDifference(const std::vector<std::size_t>& lhs, const std::vector<std::size_t>& rhs) -> std::optional<std::size_t>
{
  const auto compare_size = std::min(lhs.size(), rhs.size());
  for (std::size_t index = 0; index < compare_size; ++index) {
    if (lhs.at(index) != rhs.at(index)) {
      return index;
    }
  }
  if (lhs.size() != rhs.size()) {
    return compare_size;
  }
  return std::nullopt;
}

auto BuildSinkReferenceContinuousOrder(const std::vector<icts::Pin*>& loads) -> std::vector<std::size_t>
{
  if (loads.empty()) {
    return {};
  }

  int min_x = loads.front()->get_location().get_x();
  int min_y = loads.front()->get_location().get_y();
  int max_x = min_x;
  int max_y = min_y;
  for (const auto* pin : loads) {
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    min_x = std::min(min_x, location.get_x());
    min_y = std::min(min_y, location.get_y());
    max_x = std::max(max_x, location.get_x());
    max_y = std::max(max_y, location.get_y());
  }

  std::vector<std::pair<double, std::size_t>> theta_index_vector;
  theta_index_vector.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    const auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    const auto& location = pin->get_location();
    const auto normalized_x = NormalizeSinkReferenceCoord(location.get_x(), min_x, max_x);
    const auto normalized_y = NormalizeSinkReferenceCoord(location.get_y(), min_y, max_y);
    theta_index_vector.emplace_back(ComputeSinkReferenceTheta(normalized_x, normalized_y), index);
  }

  std::ranges::sort(theta_index_vector, [](const auto& lhs, const auto& rhs) -> bool {
    if (const auto theta_order = lhs.first <=> rhs.first; theta_order != std::partial_ordering::equivalent) {
      return theta_order == std::partial_ordering::less;
    }
    return lhs.second < rhs.second;
  });

  std::vector<std::size_t> reference_order;
  reference_order.reserve(theta_index_vector.size());
  for (const auto& [theta, original_index] : theta_index_vector) {
    (void) theta;
    reference_order.push_back(original_index);
  }
  return reference_order;
}

auto BuildReferenceDensityScaledDiscreteOrder(const std::vector<icts::Pin*>& loads, const ReferenceDensityScaledDiscreteConfig& config)
    -> std::vector<std::size_t>
{
  if (loads.empty()) {
    return {};
  }

  const auto bounds = CalcReferenceBounds(loads);
  const auto grid_size = ReferenceGridSize{.value = config.density_grid_size};
  const auto scale_power = ReferenceScalePower{.value = config.density_scale_power};
  const auto x_axis_model = BuildReferenceDensityAxisModel(loads, ReferenceAxis::kX, bounds, grid_size, scale_power);
  const auto y_axis_model = BuildReferenceDensityAxisModel(loads, ReferenceAxis::kY, bounds, grid_size, scale_power);
  const auto resolved_order_bits = ResolveDiscreteOrderBits(config.order_bits);

  std::vector<std::pair<std::uint64_t, std::size_t>> indexed_order;
  indexed_order.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    const auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    const auto projected_x = ApplyReferenceDensityScaledCoord(pin->get_location().get_x(), x_axis_model);
    const auto projected_y = ApplyReferenceDensityScaledCoord(pin->get_location().get_y(), y_axis_model);
    const ReferenceDiscretePoint point{
        .x_coord = QuantizeToDiscreteAxisCoord(projected_x, resolved_order_bits),
        .y_coord = QuantizeToDiscreteAxisCoord(projected_y, resolved_order_bits),
    };
    indexed_order.emplace_back(CalcDiscreteHilbertIndex(point, resolved_order_bits), index);
  }

  std::ranges::stable_sort(indexed_order, [](const auto& lhs, const auto& rhs) -> bool {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  });

  std::vector<std::size_t> reference_order;
  reference_order.reserve(indexed_order.size());
  for (const auto& [hilbert_index, original_index] : indexed_order) {
    (void) hilbert_index;
    reference_order.push_back(original_index);
  }
  return reference_order;
}

auto BuildReferenceDensityScaledContinuousOrder(const std::vector<icts::Pin*>& loads, const ReferenceDensityScaledContinuousConfig& config)
    -> std::vector<std::size_t>
{
  if (loads.empty()) {
    return {};
  }

  const auto bounds = CalcReferenceBounds(loads);
  const auto grid_size = ReferenceGridSize{.value = config.density_grid_size};
  const auto scale_power = ReferenceScalePower{.value = config.density_scale_power};
  const auto x_axis_model = BuildReferenceDensityAxisModel(loads, ReferenceAxis::kX, bounds, grid_size, scale_power);
  const auto y_axis_model = BuildReferenceDensityAxisModel(loads, ReferenceAxis::kY, bounds, grid_size, scale_power);

  std::vector<std::pair<double, std::size_t>> theta_index_vector;
  theta_index_vector.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    const auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    const auto projected_x = ApplyReferenceDensityScaledCoord(pin->get_location().get_x(), x_axis_model);
    const auto projected_y = ApplyReferenceDensityScaledCoord(pin->get_location().get_y(), y_axis_model);
    theta_index_vector.emplace_back(ComputeSinkReferenceTheta(projected_x, projected_y), index);
  }

  std::ranges::sort(theta_index_vector, [](const auto& lhs, const auto& rhs) -> bool {
    if (const auto theta_order = lhs.first <=> rhs.first; theta_order != std::partial_ordering::equivalent) {
      return theta_order == std::partial_ordering::less;
    }
    return lhs.second < rhs.second;
  });

  std::vector<std::size_t> reference_order;
  reference_order.reserve(theta_index_vector.size());
  for (const auto& [theta, original_index] : theta_index_vector) {
    (void) theta;
    reference_order.push_back(original_index);
  }
  return reference_order;
}

}  // namespace icts_test::linear_clustering::synthetic::detail
