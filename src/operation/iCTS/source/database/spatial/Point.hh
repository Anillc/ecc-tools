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
 * @file Point.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief Point class representing a point in 2D space
 */

#pragma once
#include <ostream>

namespace icts {
template <typename T>
class Point
{
 public:
  using CoordType = T;

  Point() = default;

  Point(const CoordType& x, const CoordType& y) : _x(x), _y(y) {}

  Point(const Point&) = default;
  Point& operator=(const Point&) = default;

  CoordType x() const { return _x; }
  CoordType y() const { return _y; }

  Point& x(const CoordType& x)
  {
    _x = x;
    return *this;
  }

  Point& y(const CoordType& y)
  {
    _y = y;
    return *this;
  }

  bool operator==(const Point& rhs) const { return _x == rhs._x && _y == rhs._y; }

  bool operator!=(const Point& rhs) const { return !(*this == rhs); }

  bool operator<(const Point& rhs) const { return _x < rhs._x || (_x == rhs._x && _y < rhs._y); }

  bool operator<=(const Point& rhs) const { return !(rhs < *this); }
  bool operator>(const Point& rhs) const { return rhs < *this; }
  bool operator>=(const Point& rhs) const { return !(*this < rhs); }

  Point operator+(const Point& rhs) const { return Point(_x + rhs._x, _y + rhs._y); }

  Point operator-(const Point& rhs) const { return Point(_x - rhs._x, _y - rhs._y); }

  Point& operator+=(const Point& rhs)
  {
    _x += rhs._x;
    _y += rhs._y;
    return *this;
  }

  Point& operator-=(const Point& rhs)
  {
    _x -= rhs._x;
    _y -= rhs._y;
    return *this;
  }

  template <typename Scalar>
  Point operator*(const Scalar& s) const
  {
    return Point(static_cast<CoordType>(_x * s), static_cast<CoordType>(_y * s));
  }

  template <typename Scalar>
  Point operator/(const Scalar& s) const
  {
    return Point(static_cast<CoordType>(_x / s), static_cast<CoordType>(_y / s));
  }

  template <typename Scalar>
  Point& operator*=(const Scalar& s)
  {
    _x = static_cast<CoordType>(_x * s);
    _y = static_cast<CoordType>(_y * s);
    return *this;
  }

  template <typename Scalar>
  Point& operator/=(const Scalar& s)
  {
    _x = static_cast<CoordType>(_x / s);
    _y = static_cast<CoordType>(_y / s);
    return *this;
  }

 private:
  CoordType _x{};
  CoordType _y{};
};

template <typename T>
inline std::ostream& operator<<(std::ostream& os, const Point<T>& p)
{
  os << p.x() << " : " << p.y();
  return os;
}

}  // namespace icts