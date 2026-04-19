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
#include <tuple>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "linear_clustering/LinearClusteringTypes.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic::detail {
namespace {

constexpr double kReferenceDensityEmptyBinFloor = 1.0e-6;
constexpr std::uint32_t kReferenceDefaultDiscreteOrderBits = 16U;
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

struct ReferenceDiscreteOrderEntry
{
  double cell_theta = 0.0;
  std::uint64_t hilbert_index = 0U;
  double fine_theta = 0.0;
  double projected_x = 0.0;
  double projected_y = 0.0;
  double tangent_position = 0.0;
  double orthogonal_position = 0.0;
  std::size_t original_index = 0U;
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

auto ComputeDiscreteHilbertTheta(ReferenceDiscretePoint point, ReferenceOrderBits order_bits) -> double
{
  if (order_bits.value == 0U) {
    return ComputeSinkReferenceTheta(kSinkReferenceCenterCoord, kSinkReferenceCenterCoord);
  }

  const auto grid_width = static_cast<double>(std::uint64_t{1} << order_bits.value);
  const auto cell_center_x = (static_cast<double>(point.x_coord) + kSinkReferenceTerminalTheta) / grid_width;
  const auto cell_center_y = (static_cast<double>(point.y_coord) + kSinkReferenceTerminalTheta) / grid_width;
  return ComputeSinkReferenceTheta(cell_center_x, cell_center_y);
}

auto ApplyHilbertTransform(double x_coord, double y_coord, icts::HilbertTransform transform) -> std::pair<double, double>
{
  const auto x = std::clamp(x_coord, 0.0, 1.0);
  const auto y = std::clamp(y_coord, 0.0, 1.0);

  switch (transform) {
    case icts::HilbertTransform::kIdentity:
      return {x, y};
    case icts::HilbertTransform::kMirrorX:
      return {1.0 - x, y};
    case icts::HilbertTransform::kMirrorY:
      return {x, 1.0 - y};
    case icts::HilbertTransform::kMirrorXY:
      return {1.0 - x, 1.0 - y};
    case icts::HilbertTransform::kSwapXY:
      return {y, x};
    case icts::HilbertTransform::kSwapMirrorX:
      return {1.0 - y, x};
    case icts::HilbertTransform::kSwapMirrorY:
      return {y, 1.0 - x};
    case icts::HilbertTransform::kSwapMirrorXY:
      return {1.0 - y, 1.0 - x};
  }
  return {x, y};
}

auto CalcDiscreteCellLocalCoord(double normalized_coord, std::uint32_t axis_coord, ReferenceOrderBits order_bits) -> double
{
  if (order_bits.value == 0U) {
    return kSinkReferenceCenterCoord;
  }

  const auto grid_width = static_cast<double>(std::uint64_t{1} << order_bits.value);
  const auto scaled = (std::clamp(normalized_coord, 0.0, 1.0) * grid_width) - static_cast<double>(axis_coord);
  return std::clamp(scaled, 0.0, 1.0);
}

auto CalcHilbertPointFromIndex(std::uint64_t hilbert_index, ReferenceOrderBits order_bits) -> ReferenceDiscretePoint
{
  if (order_bits.value == 0U) {
    return {};
  }

  ReferenceDiscretePoint point;
  const auto grid_width = static_cast<std::uint32_t>(1U << order_bits.value);
  for (std::uint32_t step = 1U; step < grid_width; step <<= 1U) {
    const auto rotation_x = static_cast<std::uint32_t>((hilbert_index >> 1U) & 1U);
    const auto rotation_y = static_cast<std::uint32_t>((hilbert_index ^ rotation_x) & 1U);
    auto rotated_x = point.x_coord;
    auto rotated_y = point.y_coord;
    HilbertRotate(step, rotated_x, rotated_y, rotation_x, rotation_y);
    point.x_coord = rotated_x + (step * rotation_x);
    point.y_coord = rotated_y + (step * rotation_y);
    hilbert_index >>= 2U;
  }
  return point;
}

auto CalcHilbertTangentProjection(std::uint64_t hilbert_index, ReferenceOrderBits order_bits, double local_x, double local_y)
    -> std::pair<double, double>
{
  if (order_bits.value == 0U) {
    return {0.0, 0.0};
  }

  const auto current_point = CalcHilbertPointFromIndex(hilbert_index, order_bits);
  const auto max_index = (std::uint64_t{1} << (2U * order_bits.value)) - 1U;

  auto previous_point = current_point;
  auto next_point = current_point;
  if (hilbert_index > 0U) {
    previous_point = CalcHilbertPointFromIndex(hilbert_index - 1U, order_bits);
  }
  if (hilbert_index < max_index) {
    next_point = CalcHilbertPointFromIndex(hilbert_index + 1U, order_bits);
  }

  double direction_x = static_cast<double>(next_point.x_coord) - static_cast<double>(previous_point.x_coord);
  double direction_y = static_cast<double>(next_point.y_coord) - static_cast<double>(previous_point.y_coord);
  if (direction_x == 0.0 && direction_y == 0.0) {
    if (hilbert_index > 0U) {
      direction_x = static_cast<double>(current_point.x_coord) - static_cast<double>(previous_point.x_coord);
      direction_y = static_cast<double>(current_point.y_coord) - static_cast<double>(previous_point.y_coord);
    } else if (hilbert_index < max_index) {
      direction_x = static_cast<double>(next_point.x_coord) - static_cast<double>(current_point.x_coord);
      direction_y = static_cast<double>(next_point.y_coord) - static_cast<double>(current_point.y_coord);
    }
  }

  const auto direction_norm = std::hypot(direction_x, direction_y);
  if (direction_norm <= 0.0) {
    return {0.0, 0.0};
  }

  direction_x /= direction_norm;
  direction_y /= direction_norm;
  const auto centered_x = local_x - kSinkReferenceCenterCoord;
  const auto centered_y = local_y - kSinkReferenceCenterCoord;
  return {
      (centered_x * direction_x) + (centered_y * direction_y),
      (-centered_x * direction_y) + (centered_y * direction_x),
  };
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

  std::vector<ReferenceDiscreteOrderEntry> indexed_order;
  indexed_order.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    const auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    auto projected_x = ApplyReferenceDensityScaledCoord(pin->get_location().get_x(), x_axis_model);
    auto projected_y = ApplyReferenceDensityScaledCoord(pin->get_location().get_y(), y_axis_model);
    std::tie(projected_x, projected_y) = ApplyHilbertTransform(projected_x, projected_y, config.hilbert_transform);
    const ReferenceDiscretePoint point{
        .x_coord = QuantizeToDiscreteAxisCoord(projected_x, resolved_order_bits),
        .y_coord = QuantizeToDiscreteAxisCoord(projected_y, resolved_order_bits),
    };
    const auto hilbert_index = CalcDiscreteHilbertIndex(point, resolved_order_bits);
    const auto local_x = CalcDiscreteCellLocalCoord(projected_x, point.x_coord, resolved_order_bits);
    const auto local_y = CalcDiscreteCellLocalCoord(projected_y, point.y_coord, resolved_order_bits);
    const auto [tangent_position, orthogonal_position] = CalcHilbertTangentProjection(hilbert_index, resolved_order_bits, local_x, local_y);
    indexed_order.push_back(ReferenceDiscreteOrderEntry{
        .cell_theta = ComputeDiscreteHilbertTheta(point, resolved_order_bits),
        .hilbert_index = hilbert_index,
        .fine_theta = ComputeSinkReferenceTheta(projected_x, projected_y),
        .projected_x = projected_x,
        .projected_y = projected_y,
        .tangent_position = tangent_position,
        .orthogonal_position = orthogonal_position,
        .original_index = index,
    });
  }

  const auto encoding = config.discrete_hilbert_encoding;
  std::ranges::stable_sort(
      indexed_order, [encoding](const ReferenceDiscreteOrderEntry& lhs, const ReferenceDiscreteOrderEntry& rhs) -> bool {
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
          if (const auto order = compare_doubles(lhs.fine_theta, rhs.fine_theta); order.has_value()) {
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
          case icts::DiscreteHilbertEncoding::kSinkThetaCell:
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
          case icts::DiscreteHilbertEncoding::kSinkThetaCellTangent:
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
          case icts::DiscreteHilbertEncoding::kClassicIndex:
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
          case icts::DiscreteHilbertEncoding::kClassicIndexTangent:
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
        return lhs.original_index < rhs.original_index;
      });

  std::vector<std::size_t> reference_order;
  reference_order.reserve(indexed_order.size());
  for (const auto& entry : indexed_order) {
    reference_order.push_back(entry.original_index);
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
