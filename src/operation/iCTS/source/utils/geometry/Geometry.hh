// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file Geometry.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Geometry helpers for topology algorithms.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "database/spatial/Point.hh"

namespace icts::geometry {

template <typename T>
inline double manhattan(const Point<T>& a, const Point<T>& b)
{
  return std::fabs(static_cast<double>(a.x()) - static_cast<double>(b.x()))
         + std::fabs(static_cast<double>(a.y()) - static_cast<double>(b.y()));
}

template <typename Value, typename PointGetter>
inline Point<double> calc_center(const std::vector<Value>& values, PointGetter getter)
{
  if (values.empty()) {
    return Point<double>(0.0, 0.0);
  }
  double sum_x = 0.0;
  double sum_y = 0.0;
  for (const auto& value : values) {
    auto p = getter(value);
    sum_x += static_cast<double>(p.x());
    sum_y += static_cast<double>(p.y());
  }
  const double count = static_cast<double>(values.size());
  return Point<double>(sum_x / count, sum_y / count);
}

template <typename Value, typename PointGetter>
inline Point<int> calc_median(const std::vector<Value>& values, PointGetter getter)
{
  if (values.empty()) {
    return Point<int>(-1, -1);
  }
  std::vector<int> xs;
  std::vector<int> ys;
  xs.reserve(values.size());
  ys.reserve(values.size());
  for (const auto& value : values) {
    auto p = getter(value);
    xs.push_back(static_cast<int>(std::lround(p.x())));
    ys.push_back(static_cast<int>(std::lround(p.y())));
  }

  const auto mid = xs.size() / 2;
  std::nth_element(xs.begin(), xs.begin() + mid, xs.end());
  std::nth_element(ys.begin(), ys.begin() + mid, ys.end());
  return Point<int>(xs[mid], ys[mid]);
}

inline Point<int> project_to_l1_circle(const Point<int>& center, const Point<int>& point, double r, int min_x = 0, int min_y = 0,
                                       int max_x = std::numeric_limits<int>::max(), int max_y = std::numeric_limits<int>::max())
{
  if (min_x > max_x) {
    std::swap(min_x, max_x);
  }
  if (min_y > max_y) {
    std::swap(min_y, max_y);
  }

  auto clampi = [](int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); };
  auto clampd = [](double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); };

  if (r <= 0.0) {
    return Point<int>(clampi(center.x(), min_x, max_x), clampi(center.y(), min_y, max_y));
  }

  auto inside = [&](double x, double y) {
    return x >= static_cast<double>(min_x) && x <= static_cast<double>(max_x) && y >= static_cast<double>(min_y)
           && y <= static_cast<double>(max_y);
  };

  const double cx = static_cast<double>(center.x());
  const double cy = static_cast<double>(center.y());
  const double tx = static_cast<double>(point.x());
  const double ty = static_cast<double>(point.y());

  auto to_int_best = [&](double x, double y) {
    const int fx = static_cast<int>(std::floor(x));
    const int cx2 = static_cast<int>(std::ceil(x));
    const int fy = static_cast<int>(std::floor(y));
    const int cy2 = static_cast<int>(std::ceil(y));
    const int rx = static_cast<int>(std::lround(x));
    const int ry = static_cast<int>(std::lround(y));

    const std::pair<int, int> cand[5] = {{fx, fy}, {fx, cy2}, {cx2, fy}, {cx2, cy2}, {rx, ry}};

    bool has = false;
    int best_ix = clampi(rx, min_x, max_x);
    int best_iy = clampi(ry, min_y, max_y);
    double best_l1err = std::numeric_limits<double>::infinity();
    double best_d2 = std::numeric_limits<double>::infinity();

    for (const auto& c : cand) {
      int ix = c.first;
      int iy = c.second;
      if (ix < min_x || ix > max_x || iy < min_y || iy > max_y)
        continue;

      const double l1 = std::fabs(static_cast<double>(ix) - cx) + std::fabs(static_cast<double>(iy) - cy);
      const double l1err = std::fabs(l1 - r);

      const double dx = static_cast<double>(ix) - x;
      const double dy = static_cast<double>(iy) - y;
      const double d2 = dx * dx + dy * dy;

      if (!has || l1err < best_l1err || (l1err == best_l1err && d2 < best_d2)) {
        has = true;
        best_l1err = l1err;
        best_d2 = d2;
        best_ix = ix;
        best_iy = iy;
      }
    }

    return Point<int>(best_ix, best_iy);
  };

  bool found = false;
  double best_x = cx, best_y = cy;
  double best_d2 = std::numeric_limits<double>::infinity();

  auto relax = [&](double x, double y) {
    if (!inside(x, y))
      return;
    const double dx = x - tx;
    const double dy = y - ty;
    const double d2 = dx * dx + dy * dy;
    if (!found || d2 < best_d2) {
      best_d2 = d2;
      best_x = x;
      best_y = y;
      found = true;
    }
  };

  for (int su : {+1, -1})
    for (int sv : {+1, -1}) {
      double tL = 0.0, tR = r;

      auto intersect_t = [&](double a, double b, double lo, double hi) {
        if (std::fabs(a) < 1e-12)
          return (b >= lo && b <= hi);
        double t1 = (lo - b) / a;
        double t2 = (hi - b) / a;
        if (t1 > t2)
          std::swap(t1, t2);
        tL = std::max(tL, t1);
        tR = std::min(tR, t2);
        return tL <= tR;
      };

      if (!intersect_t(static_cast<double>(su), cx, min_x, max_x))
        continue;
      if (!intersect_t(static_cast<double>(-sv), cy + static_cast<double>(sv) * r, min_y, max_y))
        continue;

      const double ax = cx;
      const double ay = cy + static_cast<double>(sv) * r;
      const double dx = static_cast<double>(su);
      const double dy = static_cast<double>(-sv);
      const double num = (tx - ax) * dx + (ty - ay) * dy;
      const double den = dx * dx + dy * dy;
      double t = num / den;
      t = clampd(t, tL, tR);

      relax(cx + static_cast<double>(su) * t, cy + static_cast<double>(sv) * (r - t));
    }

  if (found) {
    return to_int_best(best_x, best_y);
  }

  const double x = clampd(tx, min_x, max_x);
  const double y = clampd(ty, min_y, max_y);
  return Point<int>(clampi(static_cast<int>(std::lround(x)), min_x, max_x), clampi(static_cast<int>(std::lround(y)), min_y, max_y));
}

}  // namespace icts::geometry
