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
 * @file Components.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Shared geometry and timing helpers for bound-skew tree routing
 */
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

#include "BSTTypes.hh"
namespace icts::bst {

template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

/**
 * @brief Global constant
 *
 */
constexpr static size_t kHead = 0;
constexpr static size_t kTail = 1;
constexpr static size_t kLeft = 0;
constexpr static size_t kRight = 1;
constexpr static size_t kMin = 0;
constexpr static size_t kMax = 1;
constexpr static size_t kX = 0;
constexpr static size_t kY = 1;
constexpr static size_t kH = 0;
constexpr static size_t kV = 1;
constexpr static double kEpsilon = 1e-7;

/**
 * @brief Global function
 *
 */
#define FOR_EACH_BST_SIDE(side) for (size_t side = 0; side < 2; ++side)

template <Numeric T1, Numeric T2>
constexpr static bool Equal(const T1& a, const T2& b, const double& epsilon = kEpsilon)
{
  return std::abs(a - b) < epsilon;
}

class Point
{
 public:
  Point() = default;
  Point(const double& t_x, const double& t_y, const double& t_max, const double& t_min, const double& t_val)
      : x(t_x), y(t_y), max(t_max), min(t_min), val(t_val)
  {
  }
  Point(const double& t_x, const double& t_y) : x(t_x), y(t_y), max(0), min(0), val(0) {}

  Point operator+(const Point& other) const { return Point(x + other.x, y + other.y); }
  Point operator-(const Point& other) const { return Point(x - other.x, y - other.y); }
  Point operator*(const double& scale) const { return Point(x * scale, y * scale); }
  Point operator/(const double& scale) const { return Point(x / scale, y / scale); }
  Point operator+=(const Point& other)
  {
    x += other.x;
    y += other.y;
    return *this;
  }
  Point operator-=(const Point& other)
  {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  Point operator*=(const double& scale)
  {
    x *= scale;
    y *= scale;
    return *this;
  }
  Point operator/=(const double& scale)
  {
    x /= scale;
    y /= scale;
    return *this;
  }

  double x = 0;
  double y = 0;
  double max = 0;
  double min = 0;
  double val = 0;
};

/**
 * @brief type alias
 *
 */
using JoinSegment = std::vector<Point>;
using Points = std::vector<Point>;
using Region = std::vector<Point>;
using Line = std::array<Point, 2>;
using PointPair = std::array<Point, 2>;
template <typename T>
using Side = std::array<T, 2>;

class Area
{
 public:
  Area(const size_t& id) { _name = "steiner_" + std::to_string(id); };
  Area(const std::string& name, const Point& location, const double& cap_load, const double& sub_len = 0.0,
       const RCPattern& pattern = RCPattern::kHV, const bool is_fixed_terminal = false)
      : _name(name), _cap_load(cap_load), _sub_len(sub_len), _pattern(pattern), _location(location), _is_fixed_terminal(is_fixed_terminal)
  {
    if (is_fixed_terminal) {
      _merge_region.push_back(_location);
      _convex_hull.push_back(_location);
    }
  }

  Area(const std::string& name, const double& x, const double& y, const double& cap_load, const double& min_delay = 0.0,
       const double& max_delay = 0.0, const double& sub_len = 0.0, const RCPattern& pattern = RCPattern::kHV,
       const bool is_fixed_terminal = false)
      : Area(name, Point(x, y, max_delay, min_delay, cap_load), cap_load, sub_len, pattern, is_fixed_terminal)
  {
  }
  // get
  const std::string& get_name() const { return _name; }
  const double& get_cap_load() const { return _cap_load; }
  const double& get_sub_len() const { return _sub_len; }
  const double& get_edge_len(const size_t& side) const { return _edge_len[side]; }
  const double& get_radius() const { return _radius; }
  const RCPattern& get_pattern() const { return _pattern; }

  const Point& get_location() const { return _location; }
  Area* get_parent() const { return _parent; }
  Area* get_left() const { return _left; }
  Area* get_right() const { return _right; }
  Line get_line(const size_t& side) const { return _lines[side]; }
  Side<Line> get_lines() const { return _lines; }
  Region get_merge_region() const { return _merge_region; }
  std::vector<Line> getMergeRegionLines() const
  {
    std::vector<Line> lines;
    for (size_t i = 0; i < _merge_region.size(); ++i) {
      auto j = (i + 1) % _merge_region.size();
      lines.push_back({_merge_region[i], _merge_region[j]});
    }
    return lines;
  }

  Region get_convex_hull() const { return _convex_hull; }
  std::vector<Line> getConvexHullLines() const
  {
    std::vector<Line> lines;
    for (size_t i = 0; i < _convex_hull.size(); ++i) {
      auto j = (i + 1) % _convex_hull.size();
      lines.push_back({_convex_hull[i], _convex_hull[j]});
    }
    return lines;
  }
  // set
  void set_name(const std::string& name) { _name = name; }
  void set_cap_load(const double& cap_load) { _cap_load = cap_load; }
  void set_sub_len(const double& sub_len) { _sub_len = sub_len; }
  void set_edge_len(const size_t& side, const double& edge_len) { _edge_len[side] = edge_len; }
  void set_radius(const double& radius) { _radius = radius; }
  void set_pattern(const RCPattern& pattern) { _pattern = pattern; }

  void set_location(const Point& location) { _location = location; }
  void set_parent(Area* parent) { _parent = parent; }
  void set_left(Area* left) { _left = left; }
  void set_right(Area* right) { _right = right; }
  void set_line(const size_t& side, const Line& line) { _lines[side] = line; }
  void set_merge_region(const Region& merge_region) { _merge_region = merge_region; }
  void set_convex_hull(const Region& convex_hull) { _convex_hull = convex_hull; }

  // add
  void add_merge_region_point(const Point& point) { _merge_region.push_back(point); }
  void add_convex_hull_point(const Point& point) { _convex_hull.push_back(point); }

  bool is_fixed_terminal() const { return _is_fixed_terminal; }
  void set_is_fixed_terminal(bool is_fixed_terminal) { _is_fixed_terminal = is_fixed_terminal; }

 private:
  std::string _name;
  double _cap_load = 0;
  double _sub_len = 0;
  Side<double> _edge_len = {0, 0};
  double _radius = 0;
  RCPattern _pattern = RCPattern::kHV;

  Point _location;
  Area* _parent = nullptr;
  Area* _left = nullptr;
  Area* _right = nullptr;
  Side<Line> _lines;
  Region _merge_region;
  Region _convex_hull;
  bool _is_fixed_terminal = false;
};

struct Match
{
  Area* left = nullptr;
  Area* right = nullptr;
  double merge_cost = 0.0;
};

class Interval
{
 public:
  Interval() = default;
  Interval(const double& val) : _low(val), _high(val) {}
  Interval(const double& low, const double& high) : _low(low), _high(high) {}
  Interval(const Interval& other) : _low(other._low), _high(other._high) {}

  const double& low() const { return _low; }
  const double& high() const { return _high; }

  bool is_empty() const { return _low > _high; }
  bool is_point() const { return _low == _high; }

  void enclose(const double& val)
  {
    if (is_empty()) {
      _low = val;
      _high = val;
    } else {
      _low = std::min(_low, val);
      _high = std::max(_high, val);
    }
  }
  void enclose(const Interval& other)
  {
    if (!other.is_empty()) {
      enclose(other.low());
      enclose(other.high());
    }
  }

  bool isEnclosed(const double& val) const { return _low <= val && val <= _high; }
  bool isEnclosed(const Interval& other) const { return _low <= other.low() && other.high() <= _high; }

  double width() const { return is_empty() ? 0 : _high - _low; }

 private:
  double _low = 1;
  double _high = 0;
};

class TransformedRect
{
 public:
  TransformedRect() = default;
  TransformedRect(const double& x_low, const double& x_high, const double& y_low, const double& y_high)
      : _x_low(x_low), _x_high(x_high), _y_low(y_low), _y_high(y_high)
  {
  }
  TransformedRect(const Point& point, const double& radius) { makeDiamond(point, radius); }

  void init()
  {
    _x_low = _y_low = 1;
    _x_high = _y_high = 0;
  }
  const double& x_low() const { return _x_low; }
  const double& x_high() const { return _x_high; }
  const double& y_low() const { return _y_low; }
  const double& y_high() const { return _y_high; }
  void x_low(const double& val) { _x_low = val; }
  void x_high(const double& val) { _x_high = val; }
  void y_low(const double& val) { _y_low = val; }
  void y_high(const double& val) { _y_high = val; }

  bool is_empty() const
  {
    auto x_interval = Interval(_x_low, _x_high);
    auto y_interval = Interval(_y_low, _y_high);
    return x_interval.is_empty() || y_interval.is_empty();
  }
  void makeDiamond(const Point& point, const double& radius)
  {
    auto val = point.x - point.y;
    _x_low = val - radius;
    _x_high = val + radius;
    val = point.x + point.y;
    _y_low = val - radius;
    _y_high = val + radius;
  }
  void enclose(const TransformedRect& other)
  {
    if (is_empty()) {
      _x_low = other._x_low;
      _x_high = other._x_high;
      _y_low = other._y_low;
      _y_high = other._y_high;
    } else {
      _x_low = std::min(_x_low, other._x_low);
      _x_high = std::max(_x_high, other._x_high);
      _y_low = std::min(_y_low, other._y_low);
      _y_high = std::max(_y_high, other._y_high);
    }
  }

  double width(const size_t& side) const
  {
    if (side == 0) {
      return _x_high - _x_low;
    } else {
      return _y_high - _y_low;
    }
  }

  double diameter() const { return std::max(width(0), width(1)); }

  static auto intersect(const TransformedRect& trr1, const TransformedRect& trr2) -> TransformedRect;

 private:
  void check();

  void correction();
  double _x_low = 1;
  double _x_high = 0;
  double _y_low = 1;
  double _y_high = 0;
};
}  // namespace icts::bst