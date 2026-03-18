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

class GeomCalc
{
 public:
  GeomCalc() = delete;
  /* Calculate */
  // point
  static bool isSame(const Point& p1, const Point& p2);

  static double distance(const Point& p1, const Point& p2);

  static double ptToLineDistManhattan(Point& p, const Line& l, Point& closest);

  static double ptToLineDistNotManhattan(Point& p, const Line& l, Point& closest);

  static double ptToLineDist(Point& p, const Line& l, Point& closest);

  static double ptToTransformedRectDist(Point& p, TransformedRect& ms);

  static void calcCoord(Point& p, const Line& l, const double& shift);

  static void calcRelativeCoord(Point& p, const RelativeType& type, const double& shift);

  static double crossProduct(const Point& p1, const Point& p2, const Point& p3);
  // line
  static LineType lineType(const Line& l);

  static LineType lineType(const Point& p1, const Point& p2);

  static IntersectType lineIntersect(Point& p, Line& l1, Line& l2);

  static RelativeType lineRelative(const Line& l1, const Line& l2, const size_t& ref);

  static double lineDist(Line& l1, Line& l2, PointPair& closest);

  static bool onLine(Point& p, const Line& l);

  static bool isParallel(const Line& l1, const Line& l2);
  // box
  static bool inBoundBox(const Point& p, const Line& l);

  static bool boundBoxOverlap(const Line& l1, const Line& l2, const double& epsilon = kEpsilon);

  static bool boundBoxOverlap(const double& x1, const double& y1, const double& x2, const double& y2, const double& x3, const double& y3,
                              const double& x4, const double& y4, const double& epsilon = kEpsilon);

  // TransformedRect
  static double msDistance(TransformedRect& ms1, TransformedRect& ms2);
  static void makeIntersect(TransformedRect& ms1, TransformedRect& ms2, TransformedRect& intersect);
  static void coreMidPoint(TransformedRect& ms, Point& mid);
  static bool isTransformedRectContain(const TransformedRect& small, const TransformedRect& large);
  static void buildTransformedRect(const TransformedRect& ms, const double& r, TransformedRect& build_trr);
  static void trrCore(const TransformedRect& trr, TransformedRect& core);
  static void trrToPt(const TransformedRect& trr, Point& pt);
  static void trrToRegion(TransformedRect& trr, Region& region);
  static bool isSegmentTransformedRect(const TransformedRect& trr);
  // Points
  static void sortPtsByFront(Points& pts);
  static void sortPtsByVal(Points& pts);
  static void sortPtsByValDec(Points& pts);
  static void uniquePtsLoc(std::vector<Point>& pts);
  static void uniquePtsVal(std::vector<Point>& pts);
  // Region
  static std::vector<Line> getLines(const std::vector<Point>& pts);
  static void convexHull(std::vector<Point>& pts);
  static Point centerPt(const std::vector<Point>& pts);
  static bool isRegionContain(const Point& p, const std::vector<Point>& region);
  static Point closestPtOnRegion(const Point& p, const std::vector<Point>& region);
  /* Convert */
  static void lineToMs(TransformedRect& ms, const Line& l);
  static void lineToMs(TransformedRect& ms, const Point& p1, const Point& p2);
  static void msToLine(TransformedRect& ms, Line& l);
  static void msToLine(TransformedRect& ms, Point& p1, Point& p2);
  /* Check */
  static void checkMs(TransformedRect& ms);
};

}  // namespace icts::bst