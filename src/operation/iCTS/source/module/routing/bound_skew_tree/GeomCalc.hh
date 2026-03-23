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
 * @file GeomCalc.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Geometry primitives and helpers for bound-skew tree routing
 */
#pragma once

#include <cstddef>
#include <vector>

#include "Components.hh"

namespace icts::bst {
enum class LineType
{
  kVertical,
  kHorizontal,
  kManhattan,
  kFlat,
  kTilt,
};

enum class IntersectType
{
  kNone,
  kEndpoint,  // intersect point is endpoint of line segment
  kCrossing,  // intersect point is in both line segment
  kOverlap,   // intersect some part of line segment
  kSame,      // two line are same
};

enum class RelativeType
{
  kLeft,
  kRight,
  kTop,
  kBottom,
  kManhattanParallel,
};

struct BoundingBox
{
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
};

struct LineDistanceResult
{
  PointPair closest_points;
  double distance = 0.0;
};

class GeomCalc
{
 public:
  GeomCalc() = delete;

  /* Calculate */
  // point
  static bool isSame(const Point& first_point, const Point& second_point);

  static double distance(const Point& first_point, const Point& second_point);

  static double pointToLineDistanceManhattan(const Point& point, const Line& line, Point& closest_point);

  static double pointToLineDistanceNonManhattan(const Point& point, const Line& line, Point& closest_point);

  static double pointToLineDistance(const Point& point, const Line& line, Point& closest_point);

  static double pointToTransformedRectDistance(const Point& point, TransformedRect& transformed_rect);

  static void calcCoord(Point& point, const Line& line, const double& shift);

  static void calcRelativeCoord(Point& point, const RelativeType& type, const double& shift);

  static double crossProduct(const Point& origin_point, const Point& lhs_point, const Point& rhs_point);

  // line
  static LineType lineType(const Line& line);

  static LineType lineType(const Point& first_point, const Point& second_point);

  static IntersectType lineIntersect(Point& intersection_point, const Line& first_line, const Line& second_line);

  static RelativeType lineRelative(const Line& lhs_line, const Line& rhs_line, const size_t& reference_side);

  static LineDistanceResult lineDist(const Line& lhs_line, const Line& rhs_line);

  static bool onLine(Point& point, const Line& line);

  static bool isParallel(const Line& lhs_line, const Line& rhs_line);

  // box
  static bool inBoundBox(const Point& point, const Line& line);

  static bool boundBoxOverlap(const Line& lhs_line, const Line& rhs_line, const double& epsilon = kEpsilon);

  static bool boundBoxOverlap(const BoundingBox& lhs_box, const BoundingBox& rhs_box, const double& overlap_epsilon = kEpsilon);

  // transformed rect
  static double transformedRectDistance(TransformedRect& lhs_transformed_rect, TransformedRect& rhs_transformed_rect);
  static void makeIntersection(const TransformedRect& first_transformed_rect, const TransformedRect& second_transformed_rect,
                               TransformedRect& intersection);
  static void coreMidPoint(TransformedRect& transformed_rect, Point& midpoint);
  static bool containsTransformedRect(const TransformedRect& inner_transformed_rect, const TransformedRect& outer_transformed_rect);
  static void buildTransformedRect(const TransformedRect& transformed_rect, const double& radius,
                                   TransformedRect& expanded_transformed_rect);
  static void transformedRectCore(const TransformedRect& transformed_rect, TransformedRect& core_transformed_rect);
  static void transformedRectToPoint(const TransformedRect& transformed_rect, Point& point);
  static void transformedRectToRegion(TransformedRect& transformed_rect, Region& region);
  static bool isSegmentTransformedRect(const TransformedRect& transformed_rect);

  // points
  static void sortPointsByFront(Points& points);
  static void sortPointsByValue(Points& points);
  static void sortPointsByValueDesc(Points& points);
  static void uniquePointLocations(std::vector<Point>& points);
  static void uniquePointValues(std::vector<Point>& points);

  // region
  static void convexHull(std::vector<Point>& points);
  static Point centerPoint(const std::vector<Point>& points);
  static bool isRegionContain(const Point& point, const std::vector<Point>& region);
  static Point closestPointOnRegion(const Point& point, const std::vector<Point>& region);

  /* Convert */
  static void lineToTransformedRect(TransformedRect& transformed_rect, const Line& line);
  static void lineToTransformedRect(TransformedRect& transformed_rect, const Point& first_point, const Point& second_point);
  static void transformedRectToLine(TransformedRect& transformed_rect, Line& line);
  static void transformedRectToLine(TransformedRect& transformed_rect, Point& first_point, Point& second_point);

  /* Check */
  static void normalizeTransformedRect(TransformedRect& transformed_rect);
};

}  // namespace icts::bst
