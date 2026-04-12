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
 * @file GeomCalc.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Geometry helpers for bound-skew tree routing
 */
#include "GeomCalc.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>
#include <vector>

#include "Components.hh"
#include "logger/Logger.hh"

namespace icts::bst {
namespace {

constexpr double kAverageFactor = 2.0;
constexpr size_t kLinePointCount = 2;

struct EndpointIntersectionContext
{
  Point* intersection_point = nullptr;
  size_t* intersection_count = nullptr;
};

struct LinePairView
{
  const Line* source_line = nullptr;
  const Line* target_line = nullptr;
};

[[nodiscard]] auto HeadPoint(const Line& line) -> const Point&
{
  return line.at(kHead);
}

[[nodiscard]] auto TailPoint(const Line& line) -> const Point&
{
  return line.at(kTail);
}

[[nodiscard]] auto HasZeroLength(const Line& line) -> bool
{
  return Equal(GeomCalc::distance(HeadPoint(line), TailPoint(line)), 0.0);
}

[[nodiscard]] auto IsSameDirectedLine(const Line& first_line, const Line& second_line) -> bool
{
  return GeomCalc::distance(HeadPoint(first_line), HeadPoint(second_line))
             + GeomCalc::distance(TailPoint(first_line), TailPoint(second_line))
         < kEpsilon;
}

auto AccumulateEndpointIntersections(const EndpointIntersectionContext& context, const LinePairView& line_pair) -> void
{
  CTS_LOG_FATAL_IF(context.intersection_point == nullptr || context.intersection_count == nullptr)
      << "endpoint intersection context is null";
  CTS_LOG_FATAL_IF(line_pair.source_line == nullptr || line_pair.target_line == nullptr) << "line pair is null";

  for (const Point& point_source : *line_pair.source_line) {
    Point candidate_point = point_source;
    if (GeomCalc::onLine(candidate_point, *line_pair.target_line)) {
      *context.intersection_point = candidate_point;
      ++(*context.intersection_count);
    }
  }
}

auto RemoveDuplicatedEndpointIntersections(size_t& intersection_count, const std::array<LinePairView, kLinePointCount>& line_pairs) -> void
{
  const LinePairView& left_to_right = line_pairs.at(kLeft);
  const LinePairView& right_to_left = line_pairs.at(kRight);
  CTS_LOG_FATAL_IF(left_to_right.source_line == nullptr || right_to_left.source_line == nullptr) << "line pair is null";

  for (const Point& first_point : *left_to_right.source_line) {
    for (const Point& second_point : *right_to_left.source_line) {
      if (GeomCalc::distance(first_point, second_point) < kEpsilon) {
        --intersection_count;
      }
    }
  }
}

[[nodiscard]] auto CountEndpointIntersections(Point& intersection_point, const Line& first_line, const Line& second_line) -> size_t
{
  size_t intersection_count = 0;
  const EndpointIntersectionContext context{.intersection_point = &intersection_point, .intersection_count = &intersection_count};
  const std::array<LinePairView, kLinePointCount> line_pairs = {
      LinePairView{.source_line = &first_line, .target_line = &second_line},
      LinePairView{.source_line = &second_line, .target_line = &first_line},
  };

  for (const LinePairView& line_pair : line_pairs) {
    AccumulateEndpointIntersections(context, line_pair);
  }
  RemoveDuplicatedEndpointIntersections(intersection_count, line_pairs);
  return intersection_count;
}

[[nodiscard]] auto CalcLineSlope(const Line& line) -> double
{
  return (TailPoint(line).y - HeadPoint(line).y) / (TailPoint(line).x - HeadPoint(line).x);
}

[[nodiscard]] auto CalcLineYAtX(const Line& line, const double target_x) -> double
{
  return ((HeadPoint(line).y - TailPoint(line).y) * (target_x - HeadPoint(line).x) / (HeadPoint(line).x - TailPoint(line).x))
         + HeadPoint(line).y;
}

[[nodiscard]] auto SolveInfiniteLineIntersection(Point& intersection_point, const Line& first_line, const Line& second_line) -> bool
{
  const bool first_line_is_vertical = Equal(HeadPoint(first_line).x, TailPoint(first_line).x);
  const bool second_line_is_vertical = Equal(HeadPoint(second_line).x, TailPoint(second_line).x);

  if (first_line_is_vertical && second_line_is_vertical) {
    return false;
  }

  if (first_line_is_vertical) {
    intersection_point.x = HeadPoint(first_line).x;
    intersection_point.y = CalcLineYAtX(second_line, intersection_point.x);
    return true;
  }

  if (second_line_is_vertical) {
    intersection_point.x = HeadPoint(second_line).x;
    intersection_point.y = CalcLineYAtX(first_line, intersection_point.x);
    return true;
  }

  const double first_line_slope = CalcLineSlope(first_line);
  const double second_line_slope = CalcLineSlope(second_line);
  if (Equal(first_line_slope, second_line_slope)) {
    return false;
  }

  intersection_point.x = (HeadPoint(second_line).y - HeadPoint(first_line).y + (HeadPoint(first_line).x * first_line_slope)
                          - (HeadPoint(second_line).x * second_line_slope))
                         / (first_line_slope - second_line_slope);
  intersection_point.y
      = (HeadPoint(first_line).y + HeadPoint(second_line).y + (first_line_slope * (intersection_point.x - HeadPoint(first_line).x))
         + (second_line_slope * (intersection_point.x - HeadPoint(second_line).x)))
        / kAverageFactor;
  return true;
}

}  // namespace

auto GeomCalc::isSame(const Point& first_point, const Point& second_point) -> bool
{
  const auto dist = distance(first_point, second_point);
  return Equal(dist, 0);
}

auto GeomCalc::distance(const Point& first_point, const Point& second_point) -> double
{
  return std::abs(first_point.x - second_point.x) + std::abs(first_point.y - second_point.y);
}

auto GeomCalc::pointToLineDistanceManhattan(const Point& point, const Line& line, Point& closest_point) -> double
{
  TransformedRect point_transformed_rect(point, 0);
  TransformedRect line_transformed_rect;
  lineToTransformedRect(line_transformed_rect, line);
  const auto dist = transformedRectDistance(point_transformed_rect, line_transformed_rect);
  TransformedRect expanded_point_transformed_rect;
  expanded_point_transformed_rect.makeDiamond(point, dist);
  TransformedRect intersection_transformed_rect;
  makeIntersection(expanded_point_transformed_rect, line_transformed_rect, intersection_transformed_rect);
  coreMidPoint(intersection_transformed_rect, closest_point);
  return dist;
}

auto GeomCalc::pointToLineDistanceNonManhattan(const Point& point, const Line& line, Point& closest_point) -> double
{
  std::vector<Point> candidate_points;

  if (!Equal(HeadPoint(line).x, TailPoint(line).x) && (point.x - HeadPoint(line).x) * (point.x - TailPoint(line).x) <= 0) {
    Point candidate_point;
    candidate_point.x = point.x;
    candidate_point.y = ((TailPoint(line).x - point.x) * (HeadPoint(line).y - TailPoint(line).y) / (TailPoint(line).x - HeadPoint(line).x))
                        + TailPoint(line).y;
    candidate_points.push_back(candidate_point);
  }

  if (!Equal(HeadPoint(line).y, TailPoint(line).y) && (point.y - HeadPoint(line).y) * (point.y - TailPoint(line).y) <= 0) {
    Point candidate_point;
    candidate_point.x = ((TailPoint(line).y - point.y) * (HeadPoint(line).x - TailPoint(line).x) / (TailPoint(line).y - HeadPoint(line).y))
                        + TailPoint(line).x;
    candidate_point.y = point.y;
    candidate_points.push_back(candidate_point);
  }

  if (candidate_points.size() < kLinePointCount) {
    candidate_points.push_back(HeadPoint(line));
    candidate_points.push_back(TailPoint(line));
  }

  double min_distance = std::numeric_limits<double>::max();
  std::ranges::for_each(candidate_points, [&](const Point& candidate_point) -> void {
    const auto dist = distance(point, candidate_point);
    if (dist < min_distance) {
      min_distance = dist;
      closest_point = candidate_point;
    }
  });

  return min_distance;
}

auto GeomCalc::pointToLineDistance(const Point& point, const Line& line, Point& closest_point) -> double
{
  auto min_distance = std::numeric_limits<double>::max();
  const auto delta_x = std::abs(line.at(kHead).x - line.at(kTail).x);
  const auto delta_y = std::abs(line.at(kHead).y - line.at(kTail).y);

  if (isSame(line.at(kHead), line.at(kTail))) {
    closest_point = line.at(kHead);
    min_distance = distance(point, closest_point);
  } else {
    Point snapped_point = point;
    if (onLine(snapped_point, line)) {
      closest_point = snapped_point;
      min_distance = 0.0;
    } else if (Equal(delta_x, delta_y)) {
      min_distance = pointToLineDistanceManhattan(point, line, closest_point);
    } else {
      min_distance = pointToLineDistanceNonManhattan(point, line, closest_point);
    }
  }

  Point validated_point = closest_point;
  CTS_LOG_FATAL_IF(!onLine(validated_point, line)) << "closest point is not on line";
  return min_distance;
}

auto GeomCalc::pointToTransformedRectDistance(const Point& point, TransformedRect& transformed_rect) -> double
{
  TransformedRect point_transformed_rect(point, 0);
  if (containsTransformedRect(point_transformed_rect, transformed_rect)) {
    return 0;
  }

  std::vector<TransformedRect> candidate_transformed_rects(4, transformed_rect);
  candidate_transformed_rects.at(kLeft + kHead).y_low(transformed_rect.y_high());
  candidate_transformed_rects.at(kLeft + kTail).y_high(transformed_rect.y_low());
  candidate_transformed_rects.at(kRight + kHead).x_low(transformed_rect.x_high());
  candidate_transformed_rects.at(kRight + kTail).x_high(transformed_rect.x_low());

  double min_distance = std::numeric_limits<double>::max();
  std::ranges::for_each(candidate_transformed_rects, [&](TransformedRect& candidate_transformed_rect) -> void {
    const auto dist = transformedRectDistance(point_transformed_rect, candidate_transformed_rect);
    min_distance = std::min(min_distance, dist);
  });
  return min_distance;
}

auto GeomCalc::calcCoord(Point& point, const Line& line, const double& shift) -> void
{
  const auto current_line_type = lineType(line);
  double head_distance = 0.0;
  double tail_distance = 0.0;
  if (current_line_type == LineType::kHorizontal || current_line_type == LineType::kFlat) {
    head_distance = std::abs(line.at(kHead).x - shift);
    tail_distance = std::abs(line.at(kTail).x - shift);
  } else {
    head_distance = std::abs(line.at(kHead).y - shift);
    tail_distance = std::abs(line.at(kTail).y - shift);
  }

  point.x = ((line.at(kHead).x * tail_distance) + (line.at(kTail).x * head_distance)) / (head_distance + tail_distance);
  point.y = ((line.at(kHead).y * tail_distance) + (line.at(kTail).y * head_distance)) / (head_distance + tail_distance);
}

auto GeomCalc::calcRelativeCoord(Point& point, const RelativeType& type, const double& shift) -> void
{
  switch (type) {
    case RelativeType::kLeft:
      point.x += shift;
      break;
    case RelativeType::kRight:
      point.x -= shift;
      break;
    case RelativeType::kTop:
      point.y -= shift;
      break;
    case RelativeType::kBottom:
      point.y += shift;
      break;
    default:
      break;
  }
}

auto GeomCalc::crossProduct(const Point& origin_point, const Point& lhs_point, const Point& rhs_point) -> double
{
  return ((lhs_point.x - origin_point.x) * (rhs_point.y - origin_point.y))
         - ((rhs_point.x - origin_point.x) * (lhs_point.y - origin_point.y));
}

auto GeomCalc::lineType(const Line& line) -> LineType
{
  return lineType(line.at(kHead), line.at(kTail));
}

auto GeomCalc::lineType(const Point& first_point, const Point& second_point) -> LineType
{
  const auto delta_x = std::abs(first_point.x - second_point.x);
  const auto delta_y = std::abs(first_point.y - second_point.y);
  if (Equal(delta_x, delta_y)) {
    return LineType::kManhattan;
  }
  if (Equal(delta_x, 0)) {
    return LineType::kVertical;
  }
  if (Equal(delta_y, 0)) {
    return LineType::kHorizontal;
  }
  if (delta_x > delta_y) {
    return LineType::kFlat;
  }
  return LineType::kTilt;
}

auto GeomCalc::lineIntersect(Point& intersection_point, const Line& first_line, const Line& second_line) -> IntersectType
{
  CTS_LOG_FATAL_IF(HasZeroLength(first_line) || HasZeroLength(second_line)) << "line length is zero";
  if (!boundBoxOverlap(first_line, second_line)) {
    return IntersectType::kNone;
  }
  if (IsSameDirectedLine(first_line, second_line)) {
    intersection_point = first_line.at(kHead);
    return IntersectType::kSame;
  }

  const size_t endpoint_intersection_count = CountEndpointIntersections(intersection_point, first_line, second_line);
  if (endpoint_intersection_count >= kLinePointCount) {
    return IntersectType::kOverlap;
  }

  if (!SolveInfiniteLineIntersection(intersection_point, first_line, second_line)) {
    const bool first_line_is_vertical = Equal(HeadPoint(first_line).x, TailPoint(first_line).x);
    const bool second_line_is_vertical = Equal(HeadPoint(second_line).x, TailPoint(second_line).x);
    if (!(first_line_is_vertical && second_line_is_vertical)) {
      return IntersectType::kNone;
    }
  }

  if (inBoundBox(intersection_point, first_line) && inBoundBox(intersection_point, second_line)) {
    return IntersectType::kCrossing;
  }
  return IntersectType::kNone;
}

auto GeomCalc::lineRelative(const Line& lhs_line, const Line& rhs_line, const size_t& reference_side) -> RelativeType
{
  const auto lhs_line_type = lineType(lhs_line);
  const auto rhs_line_type = lineType(rhs_line);
  CTS_LOG_FATAL_IF(lhs_line_type != rhs_line_type) << "line type is not same";

  if (lhs_line_type == LineType::kVertical || lhs_line_type == LineType::kTilt) {
    const auto lhs_max_x = std::max(lhs_line.at(kHead).x, lhs_line.at(kTail).x);
    const auto rhs_max_x = std::max(rhs_line.at(kHead).x, rhs_line.at(kTail).x);
    if ((lhs_max_x <= rhs_max_x && reference_side == kLeft) || (lhs_max_x >= rhs_max_x && reference_side == kRight)) {
      return RelativeType::kLeft;
    }
    return RelativeType::kRight;
  }

  if (lhs_line_type == LineType::kHorizontal || lhs_line_type == LineType::kFlat) {
    const auto lhs_max_y = std::max(lhs_line.at(kHead).y, lhs_line.at(kTail).y);
    const auto rhs_max_y = std::max(rhs_line.at(kHead).y, rhs_line.at(kTail).y);
    if ((lhs_max_y <= rhs_max_y && reference_side == kLeft) || (lhs_max_y >= rhs_max_y && reference_side == kRight)) {
      return RelativeType::kBottom;
    }
    return RelativeType::kTop;
  }

  if (lhs_line_type == LineType::kManhattan) {
    return RelativeType::kManhattanParallel;
  }

  CTS_LOG_FATAL << "line type error";
  return RelativeType::kManhattanParallel;
}

auto GeomCalc::lineDist(const Line& lhs_line, const Line& rhs_line) -> LineDistanceResult
{
  LineDistanceResult result;
  result.distance = std::numeric_limits<double>::max();

  Point intersection_point;
  const bool lhs_is_point = isSame(HeadPoint(lhs_line), TailPoint(lhs_line));
  const bool rhs_is_point = isSame(HeadPoint(rhs_line), TailPoint(rhs_line));
  const std::array<LinePairView, kLinePointCount> line_pairs = {
      LinePairView{.source_line = &lhs_line, .target_line = &rhs_line},
      LinePairView{.source_line = &rhs_line, .target_line = &lhs_line},
  };
  const std::array<size_t, kLinePointCount> point_counts = {
      lhs_is_point ? 1U : kLinePointCount,
      rhs_is_point ? 1U : kLinePointCount,
  };

  if (!lhs_is_point && !rhs_is_point && lineIntersect(intersection_point, lhs_line, rhs_line) != IntersectType::kNone) {
    result.closest_points.at(kLeft) = intersection_point;
    result.closest_points.at(kRight) = intersection_point;
    result.distance = 0.0;
    return result;
  }

  for (size_t side = 0; side < line_pairs.size(); ++side) {
    const size_t opposite_side = (side + 1) % kLinePointCount;
    const LinePairView& line_pair = line_pairs.at(side);
    CTS_LOG_FATAL_IF(line_pair.source_line == nullptr || line_pair.target_line == nullptr) << "line pair is null";

    for (size_t point_index = 0; point_index < point_counts.at(side); ++point_index) {
      Point closest_point_on_target_line;
      const Point& source_point = line_pair.source_line->at(point_index);
      const double dist = pointToLineDistance(source_point, *line_pair.target_line, closest_point_on_target_line);
      if (dist < result.distance) {
        result.distance = dist;
        result.closest_points.at(side) = source_point;
        result.closest_points.at(opposite_side) = closest_point_on_target_line;
      }
    }
  }

  return result;
}

auto GeomCalc::onLine(Point& point, const Line& line) -> bool
{
  const auto line_length = distance(line.at(kHead), line.at(kTail));
  const auto distance_to_head = distance(point, line.at(kHead));
  const auto distance_to_tail = distance(point, line.at(kTail));
  if (std::abs(distance_to_head + distance_to_tail - line_length) < 2 * kEpsilon) {
    if (Equal(distance_to_head, 0)) {
      point = line.at(kHead);
      return true;
    }
    if (Equal(distance_to_tail, 0)) {
      point = line.at(kTail);
      return true;
    }

    const auto delta_x = std::abs(line.at(kTail).x - line.at(kHead).x);
    const auto delta_y = std::abs(line.at(kTail).y - line.at(kHead).y);
    if (delta_y > delta_x) {
      const auto snapped_x = ((line.at(kTail).x - line.at(kHead).x) * (point.y - line.at(kHead).y) / (line.at(kTail).y - line.at(kHead).y))
                             + line.at(kHead).x;
      if (Equal(snapped_x, point.x)) {
        point.x = snapped_x;
        return true;
      }
    } else {
      const auto snapped_y = ((line.at(kTail).y - line.at(kHead).y) * (point.x - line.at(kHead).x) / (line.at(kTail).x - line.at(kHead).x))
                             + line.at(kHead).y;
      if (Equal(snapped_y, point.y)) {
        point.y = snapped_y;
        return true;
      }
    }
  }
  return false;
}

auto GeomCalc::isParallel(const Line& lhs_line, const Line& rhs_line) -> bool
{
  if (isSame(lhs_line.at(kHead), lhs_line.at(kTail)) || isSame(rhs_line.at(kHead), rhs_line.at(kTail))) {
    return false;
  }

  const auto lhs_delta_x = std::abs(lhs_line.at(kHead).x - lhs_line.at(kTail).x);
  const auto lhs_delta_y = std::abs(lhs_line.at(kHead).y - lhs_line.at(kTail).y);
  const auto rhs_delta_x = std::abs(rhs_line.at(kHead).x - rhs_line.at(kTail).x);
  const auto rhs_delta_y = std::abs(rhs_line.at(kHead).y - rhs_line.at(kTail).y);
  if (Equal(lhs_delta_x, 0) && Equal(rhs_delta_x, 0)) {
    return true;
  }
  if (!Equal(lhs_delta_x, 0) && !Equal(rhs_delta_x, 0) && Equal(lhs_delta_y / lhs_delta_x, rhs_delta_y / rhs_delta_x)) {
    return true;
  }
  return false;
}

auto GeomCalc::inBoundBox(const Point& point, const Line& line) -> bool
{
  return point.x <= std::max(line.at(kHead).x, line.at(kTail).x) + kEpsilon
         && point.x >= std::min(line.at(kHead).x, line.at(kTail).x) - kEpsilon
         && point.y <= std::max(line.at(kHead).y, line.at(kTail).y) + kEpsilon
         && point.y >= std::min(line.at(kHead).y, line.at(kTail).y) - kEpsilon;
}

auto GeomCalc::boundBoxOverlap(const Line& lhs_line, const Line& rhs_line, const double& epsilon) -> bool
{
  const BoundingBox lhs_box{.min_x = std::min(lhs_line.at(kHead).x, lhs_line.at(kTail).x),
                            .min_y = std::min(lhs_line.at(kHead).y, lhs_line.at(kTail).y),
                            .max_x = std::max(lhs_line.at(kHead).x, lhs_line.at(kTail).x),
                            .max_y = std::max(lhs_line.at(kHead).y, lhs_line.at(kTail).y)};
  const BoundingBox rhs_box{.min_x = std::min(rhs_line.at(kHead).x, rhs_line.at(kTail).x),
                            .min_y = std::min(rhs_line.at(kHead).y, rhs_line.at(kTail).y),
                            .max_x = std::max(rhs_line.at(kHead).x, rhs_line.at(kTail).x),
                            .max_y = std::max(rhs_line.at(kHead).y, rhs_line.at(kTail).y)};
  return boundBoxOverlap(lhs_box, rhs_box, epsilon);
}

auto GeomCalc::boundBoxOverlap(const BoundingBox& lhs_box, const BoundingBox& rhs_box, const double& overlap_epsilon) -> bool
{
  if (lhs_box.min_x - rhs_box.max_x >= overlap_epsilon) {
    return false;
  }
  if (rhs_box.min_x - lhs_box.max_x >= overlap_epsilon) {
    return false;
  }
  if (lhs_box.min_y - rhs_box.max_y >= overlap_epsilon) {
    return false;
  }
  return rhs_box.min_y - lhs_box.max_y < overlap_epsilon;
}

auto GeomCalc::transformedRectDistance(TransformedRect& lhs_transformed_rect, TransformedRect& rhs_transformed_rect) -> double
{
  normalizeTransformedRect(lhs_transformed_rect);
  normalizeTransformedRect(rhs_transformed_rect);

  const auto lhs_x_low = lhs_transformed_rect.x_low();
  const auto lhs_x_high = lhs_transformed_rect.x_high();
  const auto lhs_y_low = lhs_transformed_rect.y_low();
  const auto lhs_y_high = lhs_transformed_rect.y_high();
  const auto rhs_x_low = rhs_transformed_rect.x_low();
  const auto rhs_x_high = rhs_transformed_rect.x_high();
  const auto rhs_y_low = rhs_transformed_rect.y_low();
  const auto rhs_y_high = rhs_transformed_rect.y_high();

  const bool x_is_intersect = (lhs_x_low <= rhs_x_high) && (rhs_x_low <= lhs_x_high);
  const bool y_is_intersect = (lhs_y_low <= rhs_y_high) && (rhs_y_low <= lhs_y_high);
  if (!x_is_intersect && !y_is_intersect) {
    const auto low_to_low = std::max(std::abs(lhs_x_low - rhs_x_low), std::abs(lhs_y_low - rhs_y_low));
    const auto low_to_high = std::max(std::abs(lhs_x_low - rhs_x_high), std::abs(lhs_y_low - rhs_y_high));
    const auto high_to_low = std::max(std::abs(lhs_x_high - rhs_x_low), std::abs(lhs_y_high - rhs_y_low));
    const auto high_to_high = std::max(std::abs(lhs_x_high - rhs_x_high), std::abs(lhs_y_high - rhs_y_high));
    return std::min({low_to_low, low_to_high, high_to_low, high_to_high});
  }
  if (!x_is_intersect) {
    const auto lhs_low_to_rhs_high = lhs_x_low - rhs_x_high;
    const auto rhs_low_to_lhs_high = rhs_x_low - lhs_x_high;
    return std::max(lhs_low_to_rhs_high, rhs_low_to_lhs_high);
  }
  if (!y_is_intersect) {
    const auto lhs_low_to_rhs_high = lhs_y_low - rhs_y_high;
    const auto rhs_low_to_lhs_high = rhs_y_low - lhs_y_high;
    return std::max(lhs_low_to_rhs_high, rhs_low_to_lhs_high);
  }
  return 0;
}

auto GeomCalc::makeIntersection(const TransformedRect& first_transformed_rect, const TransformedRect& second_transformed_rect,
                                TransformedRect& intersection) -> void
{
  intersection.x_low(std::max(first_transformed_rect.x_low(), second_transformed_rect.x_low()));
  intersection.x_high(std::min(first_transformed_rect.x_high(), second_transformed_rect.x_high()));
  intersection.y_low(std::max(first_transformed_rect.y_low(), second_transformed_rect.y_low()));
  intersection.y_high(std::min(first_transformed_rect.y_high(), second_transformed_rect.y_high()));
  normalizeTransformedRect(intersection);
}

auto GeomCalc::coreMidPoint(TransformedRect& transformed_rect, Point& midpoint) -> void
{
  const auto transformed_x = (transformed_rect.x_low() + transformed_rect.x_high()) / 2;
  const auto transformed_y = (transformed_rect.y_low() + transformed_rect.y_high()) / 2;
  midpoint.x = (transformed_x + transformed_y) / 2;
  midpoint.y = (transformed_y - transformed_x) / 2;
}

auto GeomCalc::containsTransformedRect(const TransformedRect& inner_transformed_rect, const TransformedRect& outer_transformed_rect) -> bool
{
  return inner_transformed_rect.x_high() <= outer_transformed_rect.x_high() + kEpsilon
         && inner_transformed_rect.x_low() >= outer_transformed_rect.x_low() - kEpsilon
         && inner_transformed_rect.y_high() <= outer_transformed_rect.y_high() + kEpsilon
         && inner_transformed_rect.y_low() >= outer_transformed_rect.y_low() - kEpsilon;
}

auto GeomCalc::buildTransformedRect(const TransformedRect& transformed_rect, const double& radius,
                                    TransformedRect& expanded_transformed_rect) -> void
{
  expanded_transformed_rect.x_low(transformed_rect.x_low() - radius);
  expanded_transformed_rect.x_high(transformed_rect.x_high() + radius);
  expanded_transformed_rect.y_low(transformed_rect.y_low() - radius);
  expanded_transformed_rect.y_high(transformed_rect.y_high() + radius);
}

auto GeomCalc::transformedRectCore(const TransformedRect& transformed_rect, TransformedRect& core_transformed_rect) -> void
{
  if (transformed_rect.x_high() - transformed_rect.x_low() < transformed_rect.y_high() - transformed_rect.y_low()) {
    core_transformed_rect.y_low(transformed_rect.y_low());
    core_transformed_rect.y_high(transformed_rect.y_high());
    const auto center_x = (transformed_rect.x_low() + transformed_rect.x_high()) / 2;
    core_transformed_rect.x_low(center_x);
    core_transformed_rect.x_high(center_x);
    return;
  }

  core_transformed_rect.x_low(transformed_rect.x_low());
  core_transformed_rect.x_high(transformed_rect.x_high());
  const auto center_y = (transformed_rect.y_low() + transformed_rect.y_high()) / 2;
  core_transformed_rect.y_low(center_y);
  core_transformed_rect.y_high(center_y);
}

auto GeomCalc::transformedRectToPoint(const TransformedRect& transformed_rect, Point& point) -> void
{
  point.x = (transformed_rect.y_low() + transformed_rect.x_high()) / 2;
  point.y = (transformed_rect.y_low() - transformed_rect.x_high()) / 2;
}

auto GeomCalc::transformedRectToRegion(TransformedRect& transformed_rect, Region& region) -> void
{
  const auto transformed_width = transformed_rect.x_high() - transformed_rect.x_low();
  const auto transformed_height = transformed_rect.y_high() - transformed_rect.y_low();
  if (Equal(transformed_width, 0) && Equal(transformed_height, 0)) {
    Point point;
    transformedRectToPoint(transformed_rect, point);
    region.push_back(point);
    return;
  }
  if (Equal(transformed_width, 0) || Equal(transformed_height, 0)) {
    Point head_point;
    Point tail_point;
    transformedRectToLine(transformed_rect, head_point, tail_point);
    region.push_back(head_point);
    region.push_back(tail_point);
    return;
  }

  TransformedRect edge_transformed_rect(transformed_rect.x_high(), transformed_rect.x_high(), transformed_rect.y_low(),
                                        transformed_rect.y_high());
  Point head_point;
  Point tail_point;

  transformedRectToLine(edge_transformed_rect, head_point, tail_point);
  region.push_back(head_point);
  region.push_back(tail_point);

  edge_transformed_rect.x_low(transformed_rect.x_low());
  edge_transformed_rect.x_high(transformed_rect.x_low());

  transformedRectToLine(edge_transformed_rect, head_point, tail_point);
  region.push_back(head_point);
  region.push_back(tail_point);
}

auto GeomCalc::isSegmentTransformedRect(const TransformedRect& transformed_rect) -> bool
{
  return Equal(transformed_rect.x_low(), transformed_rect.x_high()) || Equal(transformed_rect.y_low(), transformed_rect.y_high());
}

auto GeomCalc::sortPointsByFront(Points& points) -> void
{
  std::ranges::for_each(points, [&](Point& point) -> void { point.val = distance(point, points.front()); });
  sortPointsByValue(points);
}

auto GeomCalc::sortPointsByValue(Points& points) -> void
{
  if (points.empty()) {
    return;
  }
  std::ranges::sort(points, [](const Point& lhs_point, const Point& rhs_point) -> bool { return lhs_point.val < rhs_point.val; });
}

auto GeomCalc::sortPointsByValueDesc(Points& points) -> void
{
  if (points.empty()) {
    return;
  }
  std::ranges::sort(points, [](const Point& lhs_point, const Point& rhs_point) -> bool { return lhs_point.val > rhs_point.val; });
}

auto GeomCalc::uniquePointLocations(std::vector<Point>& points) -> void
{
  if (points.size() < kLinePointCount) {
    return;
  }

  std::vector<Point> unique_points = {points.front()};
  std::ranges::for_each(points, [&](const Point& point) -> void {
    if (!isSame(point, unique_points.back())) {
      unique_points.push_back(point);
    }
  });
  if (unique_points.size() > 1 && isSame(unique_points.front(), unique_points.back())) {
    unique_points.pop_back();
  }
  points = unique_points;
}

auto GeomCalc::uniquePointValues(std::vector<Point>& points) -> void
{
  auto [first, last] = std::ranges::unique(
      points, [](const Point& lhs_point, const Point& rhs_point) -> bool { return Equal(lhs_point.val, rhs_point.val); });
  points.erase(first, last);
}

auto GeomCalc::convexHull(std::vector<Point>& points) -> void
{
  if (points.size() < kLinePointCount) {
    return;
  }
  if (points.size() == kLinePointCount) {
    const auto dist = distance(points.front(), points.back());
    if (Equal(dist, 0)) {
      points.pop_back();
    }
    return;
  }

  std::ranges::sort(points, [](const Point& lhs_point, const Point& rhs_point) -> bool {
    return lhs_point.x + kEpsilon < rhs_point.x || (Equal(lhs_point.x, rhs_point.x) && lhs_point.y < rhs_point.y);
  });

  std::vector<Point> hull_points(2 * points.size());
  size_t hull_size = 0;
  for (const auto& point : points) {
    while (hull_size > 1 && crossProduct(hull_points.at(hull_size - 2), hull_points.at(hull_size - 1), point) <= kEpsilon) {
      --hull_size;
    }
    hull_points.at(hull_size++) = point;
  }
  for (size_t point_index = points.size() - 1, lower_hull_size = hull_size + 1; point_index > 0; --point_index) {
    while (hull_size >= lower_hull_size
           && crossProduct(hull_points.at(hull_size - 2), hull_points.at(hull_size - 1), points.at(point_index - 1)) <= kEpsilon) {
      --hull_size;
    }
    hull_points.at(hull_size++) = points.at(point_index - 1);
  }
  points = {hull_points.begin(), hull_points.begin() + static_cast<std::ptrdiff_t>(hull_size - 1)};
}

auto GeomCalc::centerPoint(const std::vector<Point>& points) -> Point
{
  if (points.empty()) {
    return {0, 0};
  }

  double x_sum = 0.0;
  double y_sum = 0.0;
  std::ranges::for_each(points, [&](const Point& point) -> void {
    x_sum += point.x;
    y_sum += point.y;
  });
  const auto point_count = static_cast<double>(points.size());
  return {x_sum / point_count, y_sum / point_count};
}

auto GeomCalc::isRegionContain(const Point& point, const std::vector<Point>& region) -> bool
{
  auto is_in_region = false;
  const auto point_x = point.x;
  const auto point_y = point.y;
  const auto region_point_count = region.size();
  auto previous_point_index = region_point_count - 1;

  for (size_t point_index = 0; point_index < region_point_count; previous_point_index = point_index, ++point_index) {
    const auto source_x = region.at(point_index).x;
    const auto source_y = region.at(point_index).y;
    const auto target_x = region.at(previous_point_index).x;
    const auto target_y = region.at(previous_point_index).y;
    if ((source_y < point_y && target_y >= point_y) || (target_y < point_y && source_y >= point_y)) {
      if (source_x + ((point_y - source_y) / (target_y - source_y) * (target_x - source_x)) < point_x) {
        is_in_region = !is_in_region;
      }
    }
  }
  if (is_in_region) {
    return true;
  }

  Point point_on_boundary = point;
  for (size_t point_index = 0; point_index < region.size(); ++point_index) {
    const auto next_point_index = (point_index + 1) % region.size();
    if (onLine(point_on_boundary, {region.at(point_index), region.at(next_point_index)})) {
      return true;
    }
  }
  return false;
}

auto GeomCalc::closestPointOnRegion(const Point& point, const std::vector<Point>& region) -> Point
{
  if (isRegionContain(point, region)) {
    return point;
  }

  Point closest_point_on_line;
  Point best_point;
  auto min_distance = std::numeric_limits<double>::max();
  for (size_t point_index = 0; point_index < region.size(); ++point_index) {
    const auto next_point_index = (point_index + 1) % region.size();
    const auto dist = pointToLineDistance(point, {region.at(point_index), region.at(next_point_index)}, closest_point_on_line);
    if (dist < min_distance) {
      min_distance = dist;
      best_point = closest_point_on_line;
    }
  }
  return best_point;
}

auto GeomCalc::lineToTransformedRect(TransformedRect& transformed_rect, const Line& line) -> void
{
  lineToTransformedRect(transformed_rect, line.at(kHead), line.at(kTail));
}

auto GeomCalc::lineToTransformedRect(TransformedRect& transformed_rect, const Point& first_point, const Point& second_point) -> void
{
  if (first_point.y <= second_point.y) {
    transformed_rect.x_low(second_point.x - second_point.y);
    transformed_rect.x_high(first_point.x - first_point.y);
    transformed_rect.y_low(first_point.x + first_point.y);
    transformed_rect.y_high(second_point.x + second_point.y);
  } else {
    transformed_rect.x_low(first_point.x - first_point.y);
    transformed_rect.x_high(second_point.x - second_point.y);
    transformed_rect.y_low(second_point.x + second_point.y);
    transformed_rect.y_high(first_point.x + first_point.y);
  }
  normalizeTransformedRect(transformed_rect);
}

auto GeomCalc::transformedRectToLine(TransformedRect& transformed_rect, Line& line) -> void
{
  transformedRectToLine(transformed_rect, line.at(kHead), line.at(kTail));
}

auto GeomCalc::transformedRectToLine(TransformedRect& transformed_rect, Point& first_point, Point& second_point) -> void
{
  normalizeTransformedRect(transformed_rect);
  first_point.x = (transformed_rect.y_low() + transformed_rect.x_high()) / 2;
  first_point.y = (transformed_rect.y_low() - transformed_rect.x_high()) / 2;
  second_point.x = (transformed_rect.y_high() + transformed_rect.x_low()) / 2;
  second_point.y = (transformed_rect.y_high() - transformed_rect.x_low()) / 2;
  CTS_LOG_FATAL_IF(first_point.y > second_point.y) << "second point y should be larger than first point y";
}

auto GeomCalc::normalizeTransformedRect(TransformedRect& transformed_rect) -> void
{
  const auto x_low = transformed_rect.x_low();
  const auto x_high = transformed_rect.x_high();
  if (Equal(x_low, x_high)) {
    const auto average_x = (x_low + x_high) / 2;
    transformed_rect.x_low(average_x);
    transformed_rect.x_high(average_x);
  }

  const auto y_low = transformed_rect.y_low();
  const auto y_high = transformed_rect.y_high();
  if (Equal(y_low, y_high)) {
    const auto average_y = (y_low + y_high) / 2;
    transformed_rect.y_low(average_y);
    transformed_rect.y_high(average_y);
  }
  CTS_LOG_FATAL_IF(transformed_rect.x_low() > transformed_rect.x_high() || transformed_rect.y_low() > transformed_rect.y_high())
      << "transformed rect is not valid";
}

}  // namespace icts::bst
