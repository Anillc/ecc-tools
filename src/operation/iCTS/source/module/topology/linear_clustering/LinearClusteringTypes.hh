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
 * @file LinearClusteringTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Internal data structures shared across topology linear clustering stages.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "spatial/Point.hh"

namespace icts {

class Pin;

struct OrderedLoad
{
  Pin* pin = nullptr;
  Point<int> location = Point<int>(0, 0);
  std::size_t original_index = 0;
};

struct SegmentRange
{
  std::size_t begin = 0;
  std::size_t end = 0;

  auto size() const -> std::size_t { return end >= begin ? end - begin : 0; }
  auto isEmpty() const -> bool { return begin >= end; }
};

struct ClusterSpanMetrics
{
  int min_x = 0;
  int min_y = 0;
  int max_x = 0;
  int max_y = 0;
  int diameter = 0;
  Point<int> median_root = Point<int>(0, 0);
  Point<int> center_root = Point<int>(0, 0);
};

struct ElectricalEstimate
{
  bool exact = false;
  bool route_success = false;
  Point<int> synthetic_root = Point<int>(0, 0);
  Point<int> legalized_root = Point<int>(0, 0);
  Point<int> routed_root = Point<int>(0, 0);
  double pin_cap = 0.0;
  double wire_cap = 0.0;
  double total_cap = 0.0;
  double wirelength = 0.0;
};

struct ClusterConstraintMetrics
{
  std::size_t fanout = 0;
  int diameter = 0;
  double cap_lower_bound = 0.0;
  double total_cap = 0.0;
  double wirelength = 0.0;
  ElectricalEstimate electrical;
};

enum class ConstraintViolation
{
  kNone,
  kEmptyCluster,
  kFanout,
  kDiameter,
  kCapacitance,
  kRoutingFailed,
};

struct ConstraintEvaluation
{
  bool legal = false;
  ConstraintViolation violation = ConstraintViolation::kEmptyCluster;
  ClusterConstraintMetrics metrics;
};

struct ClusterScoreBreakdown
{
  bool legal = false;
  double total_score = std::numeric_limits<double>::infinity();
  ConstraintEvaluation constraint;
};

struct PartitionScore
{
  bool legal = false;
  double total_score = std::numeric_limits<double>::infinity();
  std::size_t rotation_offset = 0;
  std::vector<SegmentRange> segments;
  std::vector<ClusterScoreBreakdown> clusters;
};

struct SegmentCacheKey
{
  std::size_t begin = 0;
  std::size_t end = 0;

  auto operator==(const SegmentCacheKey& rhs) const -> bool { return begin == rhs.begin && end == rhs.end; }
};

struct SegmentCacheKeyHash
{
  auto operator()(const SegmentCacheKey& key) const -> std::size_t
  {
    constexpr std::size_t hash_shift = 1;
    return key.begin ^ (key.end + 0x9e3779b9UL + (key.begin << 6) + (key.begin >> hash_shift));
  }
};

inline auto IsFiniteCapLimit(double max_cap) -> bool
{
  return max_cap > 0.0 && std::isfinite(max_cap);
}

}  // namespace icts
