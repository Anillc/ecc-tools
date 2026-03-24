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
 * @file BoundSkewTree.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Bound-skew tree construction implementation.
 */
#include "BoundSkewTree.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

#include "BSTTypes.hh"
#include "Components.hh"
#include "GeomCalc.hh"
#include "logger/Logger.hh"

namespace icts::bst {
namespace {
enum class TraversalStage : std::uint8_t
{
  kEnter,
  kProcessRight,
  kFinalize,
};

struct TraversalFrame
{
  Area* area = nullptr;
  TraversalStage stage = TraversalStage::kEnter;
};

struct TreeBuildFrame
{
  std::vector<Area*> areas;
  std::optional<size_t> parent_index;
  size_t side = 0;
  bool expanded = false;
  Area* left_result = nullptr;
  Area* right_result = nullptr;
};

auto copyAreaRange(const std::vector<Area*>& areas, const size_t begin_index, const size_t end_index) -> std::vector<Area*>
{
  CTS_LOG_FATAL_IF(begin_index > end_index || end_index > areas.size()) << "Area range is invalid.";

  std::vector<Area*> area_range;
  area_range.reserve(end_index - begin_index);
  for (size_t area_index = begin_index; area_index < end_index; ++area_index) {
    area_range.push_back(areas[area_index]);
  }
  return area_range;
}

auto assignTreeBuildResult(std::vector<TreeBuildFrame>& frames, const TreeBuildFrame& frame, Area* result, Area*& root) -> void
{
  if (frame.parent_index.has_value()) {
    auto& parent_frame = frames[*frame.parent_index];
    if (frame.side == kLeft) {
      parent_frame.left_result = result;
    } else {
      parent_frame.right_result = result;
    }
    return;
  }
  root = result;
}

template <typename SplitFunc, typename MergeFunc, typename CenterFunc>
auto buildBinaryTreeIteratively(const std::vector<Area*>& areas, const SplitFunc& split_func, const MergeFunc& merge_func,
                                const CenterFunc& center_func, std::string_view tree_name) -> Area*
{
  CTS_LOG_FATAL_IF(areas.empty()) << tree_name << " areas are empty.";

  std::vector<TreeBuildFrame> frames;
  frames.push_back(TreeBuildFrame{.areas = areas, .parent_index = std::nullopt});
  Area* root = nullptr;

  while (!frames.empty()) {
    const auto current_index = frames.size() - 1;
    auto& frame = frames.back();
    const auto area_count = frame.areas.size();

    if (area_count == 1) {
      assignTreeBuildResult(frames, frame, frame.areas.front(), root);
      frames.pop_back();
      continue;
    }

    if (!frame.expanded && area_count > 2) {
      auto [left_areas, right_areas] = split_func(frame.areas);
      frame.expanded = true;
      frames.push_back(TreeBuildFrame{.areas = right_areas, .parent_index = current_index, .side = kRight});
      frames.push_back(TreeBuildFrame{.areas = left_areas, .parent_index = current_index, .side = kLeft});
      continue;
    }

    Area* result = nullptr;
    if (area_count == 2) {
      result = merge_func(frame.areas.front(), frame.areas.back());
    } else {
      CTS_LOG_FATAL_IF(frame.left_result == nullptr || frame.right_result == nullptr) << tree_name << " child result is null.";
      result = merge_func(frame.left_result, frame.right_result);
    }
    result->set_location(center_func(frame.areas));
    assignTreeBuildResult(frames, frame, result, root);
    frames.pop_back();
  }

  CTS_LOG_FATAL_IF(root == nullptr) << tree_name << " root is null.";
  return root;
}

auto chooseInitialCenter(const std::vector<Area*>& areas, std::vector<Point>& center_points, std::mt19937& generator) -> void
{
  std::uniform_int_distribution<size_t> distribution(0, areas.size() - 1);
  center_points.push_back(areas[distribution(generator)]->get_location());
}

auto calcSquaredDistancesToCenters(const std::vector<Area*>& areas, const std::vector<Point>& center_points) -> std::vector<double>
{
  std::vector<double> squared_distances(areas.size(), std::numeric_limits<double>::max());
  for (size_t area_index = 0; area_index < areas.size(); ++area_index) {
    double min_distance = std::numeric_limits<double>::max();
    for (const auto& center_point : center_points) {
      min_distance = std::min(min_distance, Geom::distance(areas[area_index]->get_location(), center_point));
    }
    squared_distances[area_index] = min_distance * min_distance;
  }
  return squared_distances;
}

auto expandCentersByKMeansPlus(const std::vector<Area*>& areas, const size_t cluster_count, std::vector<Point>& center_points,
                               std::mt19937& generator) -> void
{
  while (center_points.size() < cluster_count) {
    auto squared_distances = calcSquaredDistancesToCenters(areas, center_points);
    std::discrete_distribution<> distribution(squared_distances.begin(), squared_distances.end());
    const auto selected_index = static_cast<std::size_t>(distribution(generator));
    center_points.push_back(areas[selected_index]->get_location());
  }
}

auto assignAreasToCenters(const std::vector<Area*>& areas, const std::vector<Point>& center_points,
                          std::vector<size_t>& center_assignments) -> void
{
  for (size_t area_index = 0; area_index < areas.size(); ++area_index) {
    double min_distance = std::numeric_limits<double>::max();
    size_t nearest_center_index = 0;
    for (size_t center_index = 0; center_index < center_points.size(); ++center_index) {
      const auto distance = Geom::distance(areas[area_index]->get_location(), center_points[center_index]);
      if (distance < min_distance) {
        min_distance = distance;
        nearest_center_index = center_index;
      }
    }
    center_assignments[area_index] = nearest_center_index;
  }
}

auto calcUpdatedCenters(const std::vector<Area*>& areas, const std::vector<size_t>& center_assignments,
                        const size_t cluster_count) -> std::vector<Point>
{
  std::vector<Point> center_points(cluster_count, Point(0, 0));
  std::vector<size_t> center_counts(cluster_count, 0);
  for (size_t area_index = 0; area_index < areas.size(); ++area_index) {
    const auto center_index = center_assignments[area_index];
    center_points[center_index] += areas[area_index]->get_location();
    ++center_counts[center_index];
  }
  for (size_t center_index = 0; center_index < cluster_count; ++center_index) {
    if (center_counts[center_index] > 0) {
      center_points[center_index] /= static_cast<double>(center_counts[center_index]);
    }
  }
  return center_points;
}

auto calcWithinClusterDistance(const std::vector<Area*>& areas, const std::vector<Point>& center_points,
                               const std::vector<size_t>& center_assignments) -> double
{
  double total_distance = 0.0;
  for (size_t area_index = 0; area_index < areas.size(); ++area_index) {
    total_distance += Geom::distance(areas[area_index]->get_location(), center_points[center_assignments[area_index]]);
  }
  return total_distance;
}

auto collectClusters(const std::vector<Area*>& areas, const std::vector<size_t>& center_assignments,
                     const size_t cluster_count) -> std::vector<std::vector<Area*>>
{
  std::vector<std::vector<Area*>> clusters(cluster_count);
  for (size_t area_index = 0; area_index < areas.size(); ++area_index) {
    clusters[center_assignments[area_index]].push_back(areas[area_index]);
  }
  clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [](const std::vector<Area*>& cluster) { return cluster.empty(); }),
                 clusters.end());
  return clusters;
}

auto checkMatchingEndpoint(const Point& joining_segment_point, const Point& line_point, const std::string_view side_name,
                           const std::string_view endpoint_name) -> void
{
  CTS_LOG_FATAL_IF(!Geom::isSame(joining_segment_point, line_point))
      << side_name << " joining segment is not same as " << side_name << " line at " << endpoint_name;
}

}  // namespace
/**
 * @brief bst flow
 *
 */
BoundSkewTree::BoundSkewTree(std::vector<std::unique_ptr<Area>> load_areas, const BSTParameters& parameters, const TopoType& topo_type)
    : _owned_areas(std::move(load_areas)),
      _topo_type(topo_type),
      _skew_bound(parameters.skew_bound),
      _pattern(parameters.pattern),
      _unit_horizontal_capacitance(parameters.unit_h_cap),
      _unit_horizontal_resistance(parameters.unit_h_res),
      _unit_vertical_capacitance(parameters.unit_v_cap),
      _unit_vertical_resistance(parameters.unit_v_res),
      _delay_quadratic_factor{kHalfFactor * _unit_horizontal_resistance * _unit_horizontal_capacitance,
                              kHalfFactor * _unit_vertical_resistance * _unit_vertical_capacitance}
{
  CTS_LOG_FATAL_IF(topo_type == TopoType::kInputTopo) << "Normal BST construction cannot use input-topology mode.";
  _unmerged_nodes.reserve(_owned_areas.size());
  for (const auto& area : _owned_areas) {
    _unmerged_nodes.push_back(area.get());
  }
  if (parameters.root_guide.has_value()) {
    set_root_guide(parameters.root_guide->get_x(), parameters.root_guide->get_y());
  }
}

BoundSkewTree::BoundSkewTree(std::vector<std::unique_ptr<Area>> owned_areas, Area* root, const BSTParameters& parameters)
    : _owned_areas(std::move(owned_areas)),
      _root(root),
      _skew_bound(parameters.skew_bound),
      _pattern(parameters.pattern),
      _unit_horizontal_capacitance(parameters.unit_h_cap),
      _unit_horizontal_resistance(parameters.unit_h_res),
      _unit_vertical_capacitance(parameters.unit_v_cap),
      _unit_vertical_resistance(parameters.unit_v_res),
      _delay_quadratic_factor{kHalfFactor * _unit_horizontal_resistance * _unit_horizontal_capacitance,
                              kHalfFactor * _unit_vertical_resistance * _unit_vertical_capacitance}
{
  CTS_LOG_FATAL_IF(root == nullptr) << "BST input-topology root area is null.";
  if (parameters.root_guide.has_value()) {
    set_root_guide(parameters.root_guide->get_x(), parameters.root_guide->get_y());
  }
}

auto BoundSkewTree::run() -> void
{
  bottomUp();
  topDown();
}
auto BoundSkewTree::getBestMatch(const CostFunc& cost_func) const -> Match
{
  auto min_cost = std::numeric_limits<double>::max();
  Match best_match;
  for (size_t i = 0; i < _unmerged_nodes.size(); ++i) {
    for (size_t j = i + 1; j < _unmerged_nodes.size(); ++j) {
      auto cost = cost_func(_unmerged_nodes[i], _unmerged_nodes[j]);
      if (cost < min_cost) {
        min_cost = cost;
        best_match = {_unmerged_nodes[i], _unmerged_nodes[j], cost};
      }
    }
  }
  return best_match;
}
auto BoundSkewTree::mergeCost(Area* left, Area* right) const -> double
{
  auto min_distance = std::numeric_limits<double>::max();
  auto left_merge_region = left->get_merge_region();
  auto right_merge_region = right->get_merge_region();
  Point closest_left_point;
  Point closest_right_point;
  for (const auto& left_point : left_merge_region) {
    for (const auto& right_point : right_merge_region) {
      const auto current_distance = Geom::distance(left_point, right_point);
      if (current_distance >= min_distance) {
        continue;
      }
      min_distance = current_distance;
      closest_left_point = left_point;
      closest_right_point = right_point;
    }
  }
  auto left_max = closest_left_point.max;
  auto right_max = closest_right_point.max;
  auto factor = left->get_cap_load() + right->get_cap_load() + _unit_horizontal_capacitance * min_distance;
  auto len_to_left = ((right_max - left_max) / _unit_horizontal_resistance
                      + kHalfFactor * _unit_horizontal_capacitance * min_distance * min_distance + min_distance * right->get_cap_load())
                     / factor;
  if (len_to_left < 0) {
    len_to_left = -len_to_left;
  } else if (len_to_left > min_distance) {
    len_to_left -= min_distance;
  }
  auto latency = left_max + kHalfFactor * _unit_horizontal_resistance * _unit_horizontal_capacitance * len_to_left * len_to_left
                 + _unit_horizontal_resistance * len_to_left * left->get_cap_load();
  return latency;
}
auto BoundSkewTree::distanceCost(Area* left, Area* right) -> double
{
  auto min_distance = std::numeric_limits<double>::max();
  auto left_merge_region = left->get_merge_region();
  auto right_merge_region = right->get_merge_region();
  for (const auto& left_point : left_merge_region) {
    for (const auto& right_point : right_merge_region) {
      min_distance = std::min(min_distance, Geom::distance(left_point, right_point));
    }
  }
  return min_distance;
}

auto BoundSkewTree::calcManhattanDistanceComponents(const Point& first_point, const Point& second_point) -> std::pair<double, double>
{
  return {std::abs(first_point.x - second_point.x), std::abs(first_point.y - second_point.y)};
}

auto BoundSkewTree::makeArea(const size_t& area_id) -> Area*
{
  auto area = std::make_unique<Area>(area_id);
  auto* area_ptr = area.get();
  _owned_areas.push_back(std::move(area));
  return area_ptr;
}

auto BoundSkewTree::merge(Area* left, Area* right) -> Area*
{
  auto* parent = makeArea(++_id);
  parent->set_pattern(_pattern);
  parent->set_left(left);
  parent->set_right(right);
  left->set_parent(parent);
  right->set_parent(parent);
  return parent;
}

auto BoundSkewTree::areaReset() -> void
{
  _unmerged_nodes.clear();
  _unmerged_nodes.push_back(_root);
  resetPointValues(_root);
}

auto BoundSkewTree::resetPointValues(Area* root) -> void
{
  if (root == nullptr) {
    return;
  }

  std::vector<Area*> stack{root};
  while (!stack.empty()) {
    auto* current = stack.back();
    stack.pop_back();

    auto point = current->get_location();
    point.val = 0;
    current->set_location(point);

    if (current->get_right() != nullptr) {
      stack.push_back(current->get_right());
    }
    if (current->get_left() != nullptr) {
      stack.push_back(current->get_left());
    }
  }
}
/**
 * @brief BiPartition method
 *
 */
auto BoundSkewTree::biPartition() -> void
{
  CTS_LOG_FATAL_IF(_unmerged_nodes.size() < 2) << "unmerged nodes size is less than 2";
  _root = buildBiPartitionTree(_unmerged_nodes);
  areaReset();
}
auto BoundSkewTree::buildBiPartitionTree(const std::vector<Area*>& areas) -> Area*
{
  return buildBinaryTreeIteratively(
      areas, [&](std::vector<Area*>& split_areas) { return octagonDivide(split_areas); },
      [&](Area* left_area, Area* right_area) { return merge(left_area, right_area); },
      [&](const std::vector<Area*>& center_areas) { return calcAreasCenter(center_areas); }, "Bi-partition");
}
auto BoundSkewTree::octagonDivide(std::vector<Area*>& areas) -> std::pair<std::vector<Area*>, std::vector<Area*>>
{
  auto cap_sum = std::accumulate(areas.begin(), areas.end(), 0.0, [](double sum, Area* area) { return sum + area->get_cap_load(); });
  auto half_cap = 1.0 * cap_sum / 2;

  auto octagon = calcOctagon(areas);
  auto bound_areas = areaOnOctagonBound(areas, octagon);
  const auto bound_area_count = bound_areas.size();
  const auto half_bound_area_count = bound_area_count / 2;

  const auto initial_bound_areas = bound_areas;
  for (size_t bound_area_index = 0; bound_area_index < half_bound_area_count; ++bound_area_index) {
    bound_areas.push_back(initial_bound_areas[bound_area_index]);
  }

  auto calc_diameter = [](Area* area, const std::vector<Area*>& refs) {
    auto min_dist = std::numeric_limits<double>::max();
    auto max_dist = std::numeric_limits<double>::lowest();
    std::ranges::for_each(refs, [&area, &min_dist, &max_dist](const Area* ref) {
      auto dist = Geom::distance(area->get_location(), ref->get_location());
      min_dist = std::min(min_dist, dist);
      max_dist = std::max(max_dist, dist);
    });
    return max_dist + min_dist;
  };

  auto bound_diameter = [&](const std::vector<Area*>& ref) {
    auto oct = calcOctagon(ref);
    auto bound = areaOnOctagonBound(ref, oct);
    double max_dist = std::numeric_limits<double>::lowest();
    for (auto left_iter = bound.begin(); left_iter != bound.end(); ++left_iter) {
      for (auto right_iter = std::next(left_iter); right_iter != bound.end(); ++right_iter) {
        max_dist = std::max(max_dist, Geom::distance((*left_iter)->get_location(), (*right_iter)->get_location()));
      }
    }
    return max_dist;
  };

  std::vector<Area*> left_set;
  std::vector<Area*> right_set;
  auto min_cost = std::numeric_limits<double>::max();

  for (size_t window_begin_index = 0; window_begin_index < bound_area_count; ++window_begin_index) {
    auto ref_set = copyAreaRange(bound_areas, window_begin_index, window_begin_index + half_bound_area_count);
    std::ranges::for_each(areas, [&ref_set, &calc_diameter](Area* area) {
      auto point = area->get_location();
      point.val = calc_diameter(area, ref_set);
      area->set_location(point);
    });
    std::ranges::sort(areas, [](Area* left, Area* right) { return left->get_location().val < right->get_location().val; });

    size_t left_area_count = 0;
    double accumulated_capacitance = 0.0;
    double min_cap_difference = std::numeric_limits<double>::max();
    for (size_t area_index = 0; area_index + 1 < areas.size(); ++area_index) {
      accumulated_capacitance += areas[area_index]->get_cap_load();
      const auto current_difference = std::abs(accumulated_capacitance - half_cap);
      if (current_difference < min_cap_difference) {
        min_cap_difference = current_difference;
        left_area_count = area_index + 1;
      }
    }
    auto left = copyAreaRange(areas, 0, left_area_count);
    auto right = copyAreaRange(areas, left_area_count, areas.size());
    auto cost = bound_diameter(left) + bound_diameter(right);
    if (cost < min_cost) {
      min_cost = cost;
      left_set = left;
      right_set = right;
    }
  }
  return {left_set, right_set};
}
auto BoundSkewTree::calcOctagon(const std::vector<Area*>& areas) -> std::vector<Point>
{
  auto x_p = std::numeric_limits<double>::lowest();
  auto y_p = std::numeric_limits<double>::lowest();
  auto ymx_p = std::numeric_limits<double>::lowest();
  auto ypx_p = std::numeric_limits<double>::lowest();
  auto x_m = std::numeric_limits<double>::max();
  auto y_m = std::numeric_limits<double>::max();
  auto ymx_m = std::numeric_limits<double>::max();
  auto ypx_m = std::numeric_limits<double>::max();
  std::ranges::for_each(areas, [&](const Area* area) {
    const auto location = area->get_location();
    const auto x_coord = location.x;
    const auto y_coord = location.y;
    x_p = std::max(x_coord, x_p);
    x_m = std::min(x_coord, x_m);
    y_p = std::max(y_coord, y_p);
    y_m = std::min(y_coord, y_m);
    ymx_p = std::max(y_coord - x_coord, ymx_p);
    ymx_m = std::min(y_coord - x_coord, ymx_m);
    ypx_p = std::max(y_coord + x_coord, ypx_p);
    ypx_m = std::min(y_coord + x_coord, ypx_m);
  });

  std::vector<Point> octagon{Point(y_p - ymx_p, y_p), Point(ypx_p - y_p, y_p), Point(x_p, ypx_p - x_p), Point(x_p, x_p + ymx_m),
                             Point(y_m - ymx_m, y_m), Point(ypx_m - y_m, y_m), Point(x_m, ypx_m - x_m), Point(x_m, x_m + ymx_p)};
  Geom::convexHull(octagon);
  return octagon;
}
auto BoundSkewTree::areaOnOctagonBound(const std::vector<Area*>& areas, const std::vector<Point>& octagon) -> std::vector<Area*>
{
  std::vector<Area*> result;
  std::ranges::for_each(areas, [&result, &octagon](Area* area) {
    for (size_t i = 0; i < octagon.size(); ++i) {
      auto line = Side<Point>{octagon[i], octagon[(i + 1) % octagon.size()]};
      auto point = area->get_location();
      if (Geom::onLine(point, line)) {
        result.push_back(area);
        break;
      }
    }
  });
  auto center = Geom::centerPoint(octagon);
  std::ranges::for_each(areas, [&center](Area* area) {
    auto point = area->get_location();
    auto arc_tan2 = std::atan2(point.y - center.y, point.x - center.x);
    if (arc_tan2 < 0) {
      arc_tan2 += 2 * std::numbers::pi;
    }
    point.val = arc_tan2;
    area->set_location(point);
  });
  std::ranges::sort(result, [](Area* left, Area* right) { return left->get_location().val < right->get_location().val; });
  return result;
}
/**
 * @brief BiCluster method
 *
 */
auto BoundSkewTree::biCluster() -> void
{
  CTS_LOG_FATAL_IF(_unmerged_nodes.size() < 2) << "unmerged nodes size is less than 2";
  _root = buildBiClusterTree(_unmerged_nodes);
  areaReset();
}
auto BoundSkewTree::buildBiClusterTree(const std::vector<Area*>& areas) -> Area*
{
  return buildBinaryTreeIteratively(
      areas,
      [&](const std::vector<Area*>& split_areas) {
        auto clusters = kMeansPlus(split_areas, KMeansConfig{.cluster_count = 2});
        CTS_LOG_FATAL_IF(clusters.size() != 2) << "Bi-cluster requires exactly two non-empty clusters.";
        return std::pair<std::vector<Area*>, std::vector<Area*>>{clusters.front(), clusters.back()};
      },
      [&](Area* left_area, Area* right_area) { return merge(left_area, right_area); },
      [&](const std::vector<Area*>& center_areas) { return calcAreasCenter(center_areas); }, "Bi-cluster");
}
auto BoundSkewTree::calcAreasCenter(const std::vector<Area*>& areas) -> Point
{
  std::vector<Point> points;
  points.reserve(areas.size());
  std::ranges::for_each(areas, [&](Area* area) { points.push_back(area->get_location()); });
  return Geom::centerPoint(points);
}
auto BoundSkewTree::kMeansPlus(const std::vector<Area*>& areas, const KMeansConfig& config) -> std::vector<std::vector<Area*>>
{
  std::vector<std::vector<Area*>> best_clusters(config.cluster_count);
  std::vector<Point> centers;
  std::vector<size_t> assignments(areas.size(), 0);
  std::mt19937 generator(config.seed);

  chooseInitialCenter(areas, centers, generator);
  expandCentersByKMeansPlus(areas, config.cluster_count, centers, generator);

  double min_total_distance = std::numeric_limits<double>::max();
  for (size_t iteration = 0; iteration < config.max_iter; ++iteration) {
    assignAreasToCenters(areas, centers, assignments);
    auto new_centers = calcUpdatedCenters(areas, assignments, config.cluster_count);
    auto total_distance = calcWithinClusterDistance(areas, new_centers, assignments);
    if (total_distance < min_total_distance) {
      min_total_distance = total_distance;
      best_clusters = collectClusters(areas, assignments, config.cluster_count);
    }
    centers = std::move(new_centers);
  }

  return best_clusters;
}

auto BoundSkewTree::bottomUp() -> void
{
  switch (_topo_type) {
    case TopoType::kBiCluster:
    case TopoType::kBiPartition:
    case TopoType::kInputTopo:
      bottomUpTopoBased();
      break;
    case TopoType::kGreedyDist:
    case TopoType::kGreedyMerge:
      bottomUpAllPairBased();
      break;
    default:
      CTS_LOG_FATAL << "topo type is not supported";
      break;
  }
}
auto BoundSkewTree::bottomUpAllPairBased() -> void
{
  // none input topo
  while (_unmerged_nodes.size() > 1) {
    // switch cost_func by topo_type
    CostFunc cost_func;
    switch (_topo_type) {
      case TopoType::kGreedyDist:
        cost_func = [&](Area* left, Area* right) { return distanceCost(left, right); };
        break;
      case TopoType::kGreedyMerge:
        cost_func = [&](Area* left, Area* right) { return mergeCost(left, right); };
        break;
      default:
        CTS_LOG_FATAL << "topo type is not supported";
        break;
    }
    auto best_match = getBestMatch(cost_func);
    auto* left = best_match.left;
    auto* right = best_match.right;
    auto* parent = makeArea(++_id);
    // random select RCpattern
    parent->set_pattern(_pattern);
    merge(parent, left, right);
    // erase left and right
    _unmerged_nodes.erase(
        std::remove_if(_unmerged_nodes.begin(), _unmerged_nodes.end(), [&](Area* node) { return node == left || node == right; }),
        _unmerged_nodes.end());
    _unmerged_nodes.push_back(parent);
  }
  _root = _unmerged_nodes.front();
}
auto BoundSkewTree::bottomUpTopoBased() -> void
{
  switch (_topo_type) {
    case TopoType::kBiCluster:
      biCluster();
      break;
    case TopoType::kBiPartition:
      biPartition();
      break;
    case TopoType::kInputTopo:
      break;
    default:
      CTS_LOG_FATAL << "topo type is not supported";
      break;
  }
  processBottomUpTopology();
}
auto BoundSkewTree::processBottomUpTopology() -> void
{
  if (_root == nullptr) {
    return;
  }

  std::vector<TraversalFrame> stack{{_root, TraversalStage::kEnter}};
  while (!stack.empty()) {
    auto frame = stack.back();
    stack.pop_back();

    auto* current = frame.area;
    auto* left = current->get_left();
    auto* right = current->get_right();
    if (left == nullptr || right == nullptr) {
      continue;
    }

    if (frame.stage == TraversalStage::kEnter) {
      stack.push_back(TraversalFrame{current, TraversalStage::kFinalize});
      stack.push_back(TraversalFrame{right, TraversalStage::kEnter});
      stack.push_back(TraversalFrame{left, TraversalStage::kEnter});
      continue;
    }

    merge(current, left, right);
  }
}
auto BoundSkewTree::topDown() -> void
{
  Point root_location;
  auto merge_region = _root->get_merge_region();
  if (_root_guide.has_value()) {
    root_location = Geom::closestPointOnRegion(_root_guide.value(), merge_region);
  } else {
    root_location = Geom::centerPoint(merge_region);
  }
  _root->set_location(root_location);
  embedTree();
}
auto BoundSkewTree::embedTree() const -> void
{
  if (_root == nullptr) {
    return;
  }

  std::vector<TraversalFrame> stack{{_root, TraversalStage::kEnter}};
  while (!stack.empty()) {
    auto frame = stack.back();
    stack.pop_back();

    auto* current = frame.area;
    auto* left = current->get_left();
    auto* right = current->get_right();
    if (left == nullptr || right == nullptr) {
      continue;
    }

    switch (frame.stage) {
      case TraversalStage::kEnter:
        embedChild(EmbeddingStep{current, left, kLeft});
        stack.push_back(TraversalFrame{current, TraversalStage::kProcessRight});
        stack.push_back(TraversalFrame{left, TraversalStage::kEnter});
        break;
      case TraversalStage::kProcessRight:
        embedChild(EmbeddingStep{current, right, kRight});
        stack.push_back(TraversalFrame{current, TraversalStage::kFinalize});
        stack.push_back(TraversalFrame{right, TraversalStage::kEnter});
        break;
      case TraversalStage::kFinalize:
        updateEmbeddedNodeTiming(current);
        break;
    }
  }
}
auto BoundSkewTree::updateEmbeddedNodeTiming(Area* current) const -> void
{
  auto* left = current->get_left();
  auto* right = current->get_right();
  CTS_LOG_FATAL_IF(left == nullptr || right == nullptr) << "Embedded node children are null";

  auto parent_point = current->get_location();
  const auto left_point = left->get_location();
  const auto right_point = right->get_location();
  parent_point.min = std::numeric_limits<double>::max();
  parent_point.max = std::numeric_limits<double>::lowest();
  const auto delay_to_left = pointDelayIncrease(parent_point, left_point, current->get_edge_len(kLeft), left->get_cap_load(), _pattern);
  const auto delay_to_right = pointDelayIncrease(parent_point, right_point, current->get_edge_len(kRight), right->get_cap_load(), _pattern);
  parent_point.min = std::min(left_point.min + delay_to_left, right_point.min + delay_to_right);
  parent_point.max = std::max(left_point.max + delay_to_left, right_point.max + delay_to_right);
  CTS_LOG_FATAL_IF(pointSkew(parent_point) > _skew_bound + 100 * kEpsilon)
      << "skew is so larger than skew bound, skew: " << pointSkew(parent_point);
  if (pointSkew(parent_point) > _skew_bound + kEpsilon) {
    CTS_LOG_WARNING << current->get_name() << " max delay: " << parent_point.max << " min delay: " << parent_point.min;
    CTS_LOG_WARNING << "skew is larger than skew bound with error: " << pointSkew(parent_point) - _skew_bound;
    parent_point.min = parent_point.max - _skew_bound + kEpsilon;
  }
  current->set_location(parent_point);
}
auto BoundSkewTree::merge(Area* parent, Area* left, Area* right) -> void
{
  parent->set_left(left);
  parent->set_right(right);
  left->set_parent(parent);
  right->set_parent(parent);
  calcJoiningSegment(MergeAreas{parent, left, right});
  parent->set_edge_len(kLeft, -1);
  parent->set_edge_len(kRight, -1);
  auto dist = parent->get_radius();
  processJoiningSegment(parent);
  auto left_line = parent->get_line(kLeft);
  auto right_line = parent->get_line(kRight);
  constructMergeRegion(MergeAreas{parent, left, right});
  if (Geom::lineType(getJoiningSegmentLine(kLeft)) == LineType::kManhattan) {
    CTS_LOG_FATAL_IF(Geom::lineType(getJoiningSegmentLine(kRight)) != LineType::kManhattan) << "right joining_segment is not manhattan";
    if (Geom::isSegmentTransformedRect(mergeSegment(kLeft))) {
      auto& left_merge_segment = mergeSegment(kLeft);
      Geom::transformedRectToLine(left_merge_segment, left_line);
    }
    if (Geom::isSegmentTransformedRect(mergeSegment(kRight))) {
      auto& right_merge_segment = mergeSegment(kRight);
      Geom::transformedRectToLine(right_merge_segment, right_line);
    }
  }
  parent->set_line(kLeft, left_line);
  parent->set_line(kRight, right_line);

  parent->set_radius(dist);
  if (parent->get_edge_len(kLeft) + parent->get_edge_len(kRight) < 0) {
    parent->set_cap_load(left->get_cap_load() + right->get_cap_load() + parent->get_radius() * _unit_horizontal_capacitance);
  } else {
    parent->set_cap_load(left->get_cap_load() + right->get_cap_load()
                         + (parent->get_edge_len(kLeft) + parent->get_edge_len(kRight)) * _unit_horizontal_capacitance);
  }
}
auto BoundSkewTree::calcJoiningSegment(const MergeAreas& merge_areas) -> void
{
  auto* parent = merge_areas.parent;
  auto* left = merge_areas.left;
  auto* right = merge_areas.right;
  initSide();
  parent->set_radius(std::numeric_limits<double>::max());
  auto left_lines = left->getConvexHullLines();
  auto right_lines = right->getConvexHullLines();
  std::ranges::for_each(left_lines, [&](Line& left_line) {
    std::ranges::for_each(right_lines, [&](Line& right_line) { calcJoiningSegment(parent, left_line, right_line); });
  });
  calcJoiningSegmentDelay(left, right);
  if (Geom::lineType(getJoiningSegmentLine(kLeft)) == LineType::kManhattan) {
    checkJoiningSegmentMergeSegment();
  }
}

auto BoundSkewTree::processJoiningSegment(Area* current_area) -> void
{
  FOR_EACH_BST_SIDE(side)
  {
    auto& joining_segment_points = joiningSegmentPoints(side);
    auto& head_point = pointAt(joining_segment_points, kHead);
    auto& tail_point = pointAt(joining_segment_points, kTail);
    if (Equal(head_point.y, tail_point.y)) {
      if (head_point.x < tail_point.x) {
        std::swap(head_point, tail_point);
      }
    } else if (head_point.y < tail_point.y) {
      std::swap(head_point, tail_point);
    }
  }
  FOR_EACH_BST_SIDE(side)
  {
    setJoiningRegionLine(side, getJoiningSegmentLine(side));
    current_area->set_line(side, getJoiningSegmentLine(side));
  }
}
auto BoundSkewTree::constructMergeRegion(const MergeAreas& merge_areas) -> void
{
  auto* parent = merge_areas.parent;
  calcJoiningRegion(merge_areas);
  calcJoiningRegionCorner(*parent);
  calcBalancePoint(*parent);
  calcFeasibleMergeSegmentPoints(*parent);
  if (hasFeasibleMergeSegmentOnJoiningRegion()) {
    constructFeasibleMergeRegion(parent);
  } else {
    constructInfeasibleMergeRegion(parent);
  }
  if (Geom::lineType(parent->get_line(kLeft)) == LineType::kManhattan && parent->get_edge_len(kLeft) >= 0) {
    CTS_LOG_FATAL_IF(parent->get_edge_len(kRight) < 0.0) << "right edge length is negative";
    constructTransformedRectMergeRegion(parent);
  }
  auto merge_region = parent->get_merge_region();
  Geom::uniquePointLocations(merge_region);
  parent->set_merge_region(merge_region);
  calcConvexHull(parent);
}
auto BoundSkewTree::initSide() -> void
{
  FOR_EACH_BST_SIDE(side)
  {
    joiningRegionPoints(side) = {Point(), Point()};
    joiningSegmentPoints(side) = {Point(), Point()};
  }
}
auto BoundSkewTree::calcJoiningSegment(Area* current_area, Line& left_line, Line& right_line) -> void
{
  auto line_distance = Geom::lineDist(left_line, right_line);
  auto line_dist = line_distance.distance;
  auto closest_pair = line_distance.closest_points;
  auto left_joining_segment_backup = getJoiningSegmentLine(kLeft);
  auto right_joining_segment_backup = getJoiningSegmentLine(kRight);
  auto left_merge_segment_backup = mergeSegment(kLeft);
  auto right_merge_segment_backup = mergeSegment(kRight);
  if (Equal(line_dist, current_area->get_radius())) {
    current_area->set_radius(line_dist);
    updateJoiningSegment(current_area, left_line, right_line, closest_pair);
    auto origin_area = calcJoiningRegionArea(left_joining_segment_backup, right_joining_segment_backup);
    auto new_area = calcJoiningRegionArea(getJoiningSegmentLine(kLeft), getJoiningSegmentLine(kRight));
    if (origin_area >= new_area) {
      setJoiningSegmentLine(kLeft, left_joining_segment_backup);
      setJoiningSegmentLine(kRight, right_joining_segment_backup);
      if (Geom::lineType(left_joining_segment_backup) == LineType::kManhattan) {
        mergeSegment(kLeft) = left_merge_segment_backup;
        mergeSegment(kRight) = right_merge_segment_backup;
      }
    }
  } else if (line_dist < current_area->get_radius()) {
    current_area->set_radius(line_dist);
    updateJoiningSegment(current_area, left_line, right_line, closest_pair);
  }
  if (Geom::lineType(getJoiningSegmentLine(kLeft)) == LineType::kManhattan) {
    checkJoiningSegmentMergeSegment();
  }
}
auto BoundSkewTree::calcJoiningSegmentDelay(Area* left, Area* right) -> void
{
  FOR_EACH_BST_SIDE(left_side)
  {
    Line line;
    auto& left_point = pointAt(joiningSegmentPoints(kLeft), left_side);
    locateBoundarySegment(left, left_point, line);
    calcPointDelays(*left, left_point, line);
  }
  FOR_EACH_BST_SIDE(right_side)
  {
    Line line;
    auto& right_point = pointAt(joiningSegmentPoints(kRight), right_side);
    locateBoundarySegment(right, right_point, line);
    calcPointDelays(*right, right_point, line);
  }
}
auto BoundSkewTree::updateJoiningSegment(Area* current_area, Line& left_line, Line& right_line, PointPair closest_pair) -> void
{
  auto left_type = Geom::lineType(left_line);
  auto right_type = Geom::lineType(right_line);
  auto left_is_manhattan = left_type == LineType::kManhattan;
  auto right_is_manhattan = right_type == LineType::kManhattan;
  TransformedRect left_merge_segment;
  TransformedRect right_merge_segment;
  if (left_is_manhattan) {
    Geom::lineToTransformedRect(left_merge_segment, left_line);
  }
  if (right_is_manhattan) {
    Geom::lineToTransformedRect(right_merge_segment, right_line);
  }
  const auto left_closest_point = linePoint(closest_pair, kLeft);
  const auto right_closest_point = linePoint(closest_pair, kRight);
  if (!left_is_manhattan && right_is_manhattan) {
    left_merge_segment.makeDiamond(left_closest_point, 0);
  }
  if (left_is_manhattan && !right_is_manhattan) {
    right_merge_segment.makeDiamond(right_closest_point, 0);
  }
  setJoiningSegmentLine(kLeft, {left_closest_point, left_closest_point});
  setJoiningSegmentLine(kRight, {right_closest_point, right_closest_point});
  if (left_is_manhattan || right_is_manhattan) {
    auto dist = Geom::transformedRectDistance(left_merge_segment, right_merge_segment);
    CTS_LOG_FATAL_IF(std::abs(dist - current_area->get_radius()) > kEpsilon) << "merge_segment distance is not equal to radius";
    current_area->set_radius(dist);
    mergeSegment(kLeft) = left_merge_segment;
    mergeSegment(kRight) = right_merge_segment;
    TransformedRect left_bound;
    TransformedRect right_bound;
    TransformedRect left_intersect;
    TransformedRect right_intersect;
    Geom::buildTransformedRect(left_merge_segment, dist, left_bound);
    Geom::buildTransformedRect(right_merge_segment, dist, right_bound);
    Geom::makeIntersection(right_bound, left_merge_segment, left_intersect);
    Geom::makeIntersection(left_bound, right_merge_segment, right_intersect);
    Geom::transformedRectToLine(left_intersect, joiningSegmentPoint(kLeft, kHead), joiningSegmentPoint(kLeft, kTail));
    Geom::transformedRectToLine(right_intersect, joiningSegmentPoint(kRight, kHead), joiningSegmentPoint(kRight, kTail));
  } else if (Geom::isParallel(left_line, right_line)) {
    const auto& left_head = linePoint(left_line, kHead);
    const auto& left_tail = linePoint(left_line, kTail);
    const auto& right_head = linePoint(right_line, kHead);
    const auto& right_tail = linePoint(right_line, kTail);
    const auto min_x = std::max(std::min(left_head.x, left_tail.x), std::min(right_head.x, right_tail.x));
    const auto max_x = std::min(std::max(left_head.x, left_tail.x), std::max(right_head.x, right_tail.x));
    const auto min_y = std::max(std::min(left_head.y, left_tail.y), std::min(right_head.y, right_tail.y));
    const auto max_y = std::min(std::max(left_head.y, left_tail.y), std::max(right_head.y, right_tail.y));
    if ((left_type == LineType::kVertical || left_type == LineType::kTilt) && max_y >= min_y) {
      Geom::calcCoord(joiningSegmentPoint(kLeft, kHead), left_line, min_y);
      Geom::calcCoord(joiningSegmentPoint(kLeft, kTail), left_line, max_y);
      Geom::calcCoord(joiningSegmentPoint(kRight, kHead), right_line, min_y);
      Geom::calcCoord(joiningSegmentPoint(kRight, kTail), right_line, max_y);
    } else if ((left_type == LineType::kHorizontal || left_type == LineType::kFlat) && max_x >= min_x) {
      Geom::calcCoord(joiningSegmentPoint(kLeft, kHead), left_line, min_x);
      Geom::calcCoord(joiningSegmentPoint(kLeft, kTail), left_line, max_x);
      Geom::calcCoord(joiningSegmentPoint(kRight, kHead), right_line, min_x);
      Geom::calcCoord(joiningSegmentPoint(kRight, kTail), right_line, max_x);
    }
  } else {
    // single point case
  }
  if (Geom::lineType(getJoiningSegmentLine(kLeft)) == LineType::kManhattan && left_type != LineType::kManhattan
      && right_type != LineType::kManhattan) {
    mergeSegment(kLeft).makeDiamond(left_closest_point, 0);
    mergeSegment(kRight).makeDiamond(right_closest_point, 0);
  }
  // checkUpdatedJoiningSegment(current_area, left_line, right_line);
}

auto BoundSkewTree::addJoiningSegmentPoints(const MergeAreas& merge_areas) -> void
{
  auto* parent = merge_areas.parent;
  auto* left = merge_areas.left;
  auto* right = merge_areas.right;
  // add points on origin joining_segment lines
  FOR_EACH_BST_SIDE(side)
  {
    auto& segment_points = joiningSegmentPoints(side);
    CTS_LOG_FATAL_IF(Geom::isSame(pointAt(segment_points, kHead), pointAt(segment_points, kTail))) << "join segment is a point";
    auto merge_region = side == kLeft ? left->get_merge_region() : right->get_merge_region();
    for (auto point : merge_region) {
      if (Geom::onLine(point, getJoiningSegmentLine(side)) && !Geom::isSame(point, pointAt(segment_points, kHead))
          && !Geom::isSame(point, pointAt(segment_points, kTail))) {
        segment_points.push_back(point);
      }
    }
    Geom::sortPointsByFront(segment_points);
  }
  // add points on other side
  auto updated_joining_segments = _joining_segment;
  FOR_EACH_BST_SIDE(side)
  {
    const auto other_side = otherSide(side);
    const auto other_merge_region = other_side == kLeft ? left->get_merge_region() : right->get_merge_region();
    const auto relative_type = Geom::lineRelative(getJoiningSegmentLine(kLeft), getJoiningSegmentLine(kRight), other_side);
    const auto& segment_points = joiningSegmentPoints(side);
    auto& updated_segment_points = updated_joining_segments.forSide(side);
    for (auto point : other_merge_region) {
      Geom::calcRelativeCoord(point, relative_type, parent->get_radius());
      for (size_t point_index = 0; point_index + 1 < segment_points.size(); ++point_index) {
        Line line = {pointAt(segment_points, point_index), pointAt(segment_points, point_index + 1)};
        if (Geom::onLine(point, line) && !Geom::isSame(point, pointAt(segment_points, point_index))
            && !Geom::isSame(point, pointAt(segment_points, point_index + 1))) {
          calcSegmentPointDelays(point, line);
          updated_segment_points.push_back(point);
          break;
        }
      }
    }
    Geom::sortPointsByFront(updated_segment_points);
  }
  FOR_EACH_BST_SIDE(side)
  {
    joiningSegmentPoints(side) = updated_joining_segments.forSide(side);
  }
}
auto BoundSkewTree::delayFromJoiningSegment(const JoiningSegmentDelayQuery& query, const SideDelay& delay_from) const -> double
{
  const auto& segment_point = joiningSegmentPoint(query.segment_side, query.point_index);
  double delay = query.timing_type == kMin ? segment_point.min : segment_point.max;
  delay += query.joining_region_side == query.segment_side ? 0.0 : delay_from.get(query.segment_side);
  return delay;
}
auto BoundSkewTree::calcJoiningRegion(const MergeAreas& merge_areas) -> void
{
  auto* parent = merge_areas.parent;
  if (calcAreaLineType(*parent) == LineType::kManhattan) {
    calcJoiningRegionEndpoints(*parent);
  } else {
    calcNonManhattanJoiningRegionEndpoints(merge_areas);
  }
  addFeasibleMergeSegmentToJoiningRegion();
}

auto BoundSkewTree::calcJoiningRegionEndpoints(const Area& current_area) -> void
{
  const auto left_line = current_area.get_line(kLeft);
  const auto right_line = current_area.get_line(kRight);
  checkMatchingEndpoint(joiningSegmentPoint(kLeft, kHead), linePoint(left_line, kHead), "left", "head");
  checkMatchingEndpoint(joiningSegmentPoint(kLeft, kTail), linePoint(left_line, kTail), "left", "tail");
  checkMatchingEndpoint(joiningSegmentPoint(kRight, kHead), linePoint(right_line, kHead), "right", "head");
  checkMatchingEndpoint(joiningSegmentPoint(kRight, kTail), linePoint(right_line, kTail), "right", "tail");

  joiningRegionPoint(kLeft, kHead) = joiningSegmentPoint(kLeft, kHead);
  joiningRegionPoint(kLeft, kTail) = joiningSegmentPoint(kLeft, kTail);
  joiningRegionPoint(kRight, kHead) = joiningSegmentPoint(kRight, kHead);
  joiningRegionPoint(kRight, kTail) = joiningSegmentPoint(kRight, kTail);
  updatePointDelaysByEndSide(current_area, kHead, joiningRegionPoint(kLeft, kHead));
  updatePointDelaysByEndSide(current_area, kHead, joiningRegionPoint(kRight, kHead));
  updatePointDelaysByEndSide(current_area, kTail, joiningRegionPoint(kLeft, kTail));
  updatePointDelaysByEndSide(current_area, kTail, joiningRegionPoint(kRight, kTail));
}
auto BoundSkewTree::calcNonManhattanJoiningRegionEndpoints(const MergeAreas& merge_areas) -> void
{
  auto* left = merge_areas.left;
  auto* right = merge_areas.right;
  addJoiningSegmentPoints(merge_areas);
  const SideDelay delay_from{
      .left = pointDelayIncrease(joiningSegmentPoint(kLeft, kHead), joiningSegmentPoint(kRight, kHead), left->get_cap_load(), _pattern),
      .right = pointDelayIncrease(joiningSegmentPoint(kLeft, kHead), joiningSegmentPoint(kRight, kHead), right->get_cap_load(), _pattern)};
  FOR_EACH_BST_SIDE(side)
  {
    const auto other_side = otherSide(side);
    auto& joining_region_points = joiningRegionPoints(side);
    const auto& segment_points = joiningSegmentPoints(side);
    const auto& other_segment_points = joiningSegmentPoints(other_side);
    joining_region_points = segment_points;
    for (size_t point_index = 0; point_index < segment_points.size(); ++point_index) {
      auto point = pointAt(segment_points, point_index);
      const auto& other_point = pointAt(other_segment_points, point_index);
      point.min = std::min(point.min, other_point.min + delay_from.get(other_side));
      point.max = std::max(point.max, other_point.max + delay_from.get(other_side));
      pointAt(joining_region_points, point_index) = point;
    }
    Geom::uniquePointLocations(joining_region_points);
  }
  FOR_EACH_BST_SIDE(side)
  {
    const auto other_side = otherSide(side);
    const auto section_count = joiningRegionPoints(side).size() - 1;
    for (size_t point_index = 0; point_index < section_count; ++point_index) {
      const auto min_delta
          = (joiningSegmentPoint(side, point_index).min - joiningSegmentPoint(other_side, point_index).min - delay_from.get(other_side))
            * (joiningSegmentPoint(side, point_index + 1).min - joiningSegmentPoint(other_side, point_index + 1).min
               - delay_from.get(other_side));
      if (min_delta < -kEpsilon) {
        addTurnPoint(side, point_index, kMin, delay_from);
      }
      const auto max_delta
          = (joiningSegmentPoint(side, point_index).max - joiningSegmentPoint(other_side, point_index).max - delay_from.get(other_side))
            * (joiningSegmentPoint(side, point_index + 1).max - joiningSegmentPoint(other_side, point_index + 1).max
               - delay_from.get(other_side));
      if (max_delta < -kEpsilon) {
        addTurnPoint(side, point_index, kMax, delay_from);
      }
    }
    auto& joining_region_points = joiningRegionPoints(side);
    Geom::sortPointsByFront(joining_region_points);
    Geom::uniquePointLocations(joining_region_points);
  }
  FOR_EACH_BST_SIDE(side)
  {
    auto& joining_region_points = joiningRegionPoints(side);
    for (size_t point_index = 0; point_index + 1 < joining_region_points.size(); ++point_index) {
      const auto first_point = pointAt(joining_region_points, point_index);
      const auto second_point = pointAt(joining_region_points, point_index + 1);
      const auto distance = Geom::distance(first_point, second_point);
      CTS_LOG_FATAL_IF(Equal(distance, 0)) << "distance is zero";
      pointAt(joining_region_points, point_index).val = (pointSkew(second_point) - pointSkew(first_point)) / distance;
    }
    Points increasing_points = {joining_region_points.front()};
    for (size_t point_index = 1; point_index + 1 < joining_region_points.size(); ++point_index) {
      const auto current_value = increasing_points.back().val;
      const auto next_value = pointAt(joining_region_points, point_index).val;
      CTS_LOG_FATAL_IF(current_value > next_value + 100 * kEpsilon)
          << "current_value: " << current_value << "> next_value: " << next_value << ", skew slope is not strictly monotone increasing";
      if (next_value > current_value) {
        increasing_points.push_back(pointAt(joining_region_points, point_index));
      }
    }
    increasing_points.push_back(joining_region_points.back());
    joining_region_points = increasing_points;
  }
}
auto BoundSkewTree::addTurnPoint(const size_t& side, const size_t& point_index, const size_t& timing_type,
                                 const SideDelay& delay_from) -> void
{
  const auto first_point = joiningRegionPoint(side, point_index);
  const auto second_point = joiningRegionPoint(side, point_index + 1);
  const double alpha = Equal(first_point.x, second_point.x) ? _delay_quadratic_factor.vertical : _delay_quadratic_factor.horizontal;
  const auto distance = Geom::distance(first_point, second_point);
  CTS_LOG_FATAL_IF(Equal(distance, 0)) << "distance is zero";

  SideState<TimingState<double>> beta;
  FOR_EACH_BST_SIDE(segment_side)
  {
    FOR_EACH_BST_SIDE(current_timing_type)
    {
      const auto first_delay = delayFromJoiningSegment(
          JoiningSegmentDelayQuery{
              .joining_region_side = side, .segment_side = segment_side, .point_index = point_index, .timing_type = current_timing_type},
          delay_from);
      const auto second_delay = delayFromJoiningSegment(JoiningSegmentDelayQuery{.joining_region_side = side,
                                                                                 .segment_side = segment_side,
                                                                                 .point_index = point_index + 1,
                                                                                 .timing_type = current_timing_type},
                                                        delay_from);
      beta.forSide(segment_side).forTiming(current_timing_type) = (second_delay - first_delay) / distance - alpha * distance;
    }
  }

  const auto left_delay = delayFromJoiningSegment(
      JoiningSegmentDelayQuery{.joining_region_side = kLeft, .segment_side = side, .point_index = point_index, .timing_type = timing_type},
      delay_from);
  const auto right_delay = delayFromJoiningSegment(
      JoiningSegmentDelayQuery{.joining_region_side = kRight, .segment_side = side, .point_index = point_index, .timing_type = timing_type},
      delay_from);
  const auto beta_delta = beta.right.forTiming(timing_type) - beta.left.forTiming(timing_type);
  const auto turn_distance = (left_delay - right_delay) / beta_delta;
  CTS_LOG_FATAL_IF(turn_distance <= 0 || turn_distance >= distance) << "turn dist is not in range";

  const auto reference_distance = distance - turn_distance;
  Point turn_point((first_point.x * reference_distance + second_point.x * turn_distance) / distance,
                   (first_point.y * reference_distance + second_point.y * turn_distance) / distance);
  SideState<TimingState<double>> delay_bound;
  FOR_EACH_BST_SIDE(segment_side)
  {
    FOR_EACH_BST_SIDE(current_timing_type)
    {
      delay_bound.forSide(segment_side).forTiming(current_timing_type)
          = delayFromJoiningSegment(JoiningSegmentDelayQuery{.joining_region_side = side,
                                                             .segment_side = segment_side,
                                                             .point_index = point_index,
                                                             .timing_type = current_timing_type},
                                    delay_from)
            + alpha * turn_distance * turn_distance + beta.forSide(segment_side).forTiming(current_timing_type) * turn_distance;
    }
  }
  turn_point.min = std::min(delay_bound.left.min, delay_bound.right.min);
  turn_point.max = std::max(delay_bound.left.max, delay_bound.right.max);
  joiningRegionPoints(side).push_back(turn_point);
}
auto BoundSkewTree::addFeasibleMergeSegmentToJoiningRegion() -> void
{
  FOR_EACH_BST_SIDE(side)
  {
    auto& joining_region_points = joiningRegionPoints(side);
    for (size_t point_index = 0; point_index + 1 < joining_region_points.size(); ++point_index) {
      const auto current_point = pointAt(joining_region_points, point_index);
      const auto next_point = pointAt(joining_region_points, point_index + 1);
      const auto current_delta = pointSkew(current_point) - _skew_bound;
      const auto next_delta = pointSkew(next_point) - _skew_bound;
      if (current_delta * next_delta < 0 && !Equal(current_delta, 0) && !Equal(next_delta, 0)) {
        const auto distance = Geom::distance(current_point, next_point);
        const auto turn_distance = (_skew_bound - pointSkew(current_point)) * distance / (pointSkew(next_point) - pointSkew(current_point));
        const auto reference_distance = distance - turn_distance;
        Point turn_point{(current_point.x * reference_distance + next_point.x * turn_distance) / distance,
                         (current_point.y * reference_distance + next_point.y * turn_distance) / distance};
        Line line = {current_point, next_point};
        calcSegmentPointDelays(turn_point, line);
        const auto insert_offset = static_cast<Points::difference_type>(point_index + 1);
        joining_region_points.insert(std::next(joining_region_points.begin(), insert_offset), turn_point);
      }
    }
  }
}
auto BoundSkewTree::calcJoiningRegionCorner(const Area& current_area) -> void
{
  FOR_EACH_BST_SIDE(side)
  {
    const auto& segment_points = joiningSegmentPoints(side);
    CTS_LOG_FATAL_IF(segment_points.front().y + kEpsilon < segment_points.back().y) << "join segment direction is not correct";
  }
  if (calcAreaLineType(current_area) == LineType::kManhattan && !Equal(current_area.get_radius(), 0)) {
    FOR_EACH_BST_SIDE(end_side)
    {
      if (joiningRegionCornerExists(end_side)) {
        const auto left_point = pointAt(joiningSegmentPoints(kLeft), end_side);
        const auto right_point = pointAt(joiningSegmentPoints(kRight), end_side);
        auto& joining_corner_point = joiningCornerPoint(end_side);
        if ((left_point.x - right_point.x) * (left_point.y - right_point.y) < 0) {
          if (end_side == kHead) {
            joining_corner_point = {std::max(left_point.x, right_point.x), std::max(left_point.y, right_point.y)};
          } else {
            joining_corner_point = {std::min(left_point.x, right_point.x), std::min(left_point.y, right_point.y)};
          }
        } else {
          if (end_side == kHead) {
            joining_corner_point = {std::min(left_point.x, right_point.x), std::max(left_point.y, right_point.y)};
          } else {
            joining_corner_point = {std::max(left_point.x, right_point.x), std::min(left_point.y, right_point.y)};
          }
        }
        updatePointDelaysByEndSide(current_area, end_side, joining_corner_point);
      }
    }
  }
}
auto BoundSkewTree::joiningRegionCornerExists(const size_t& end_side) const -> bool
{
  const auto first_point = pointAt(joiningSegmentPoints(kLeft), end_side);
  const auto second_point = pointAt(joiningSegmentPoints(kRight), end_side);
  return !Equal(first_point.x, second_point.x) && !Equal(first_point.y, second_point.y);
}
auto BoundSkewTree::calcBalancePoint(const Area& current_area) -> void
{
  auto* left_child = current_area.get_left();
  auto* right_child = current_area.get_right();
  if (left_child == nullptr || right_child == nullptr) {
    CTS_LOG_FATAL << "calcBalancePoint requires both child areas";
    return;
  }

  FOR_EACH_BST_SIDE(end_side)
  {
    balancePoints(end_side).clear();
  }
  if (Equal(current_area.get_radius(), 0)) {
    return;
  }
  FOR_EACH_BST_SIDE(end_side)
  {
    const auto left_line = current_area.get_line(kLeft);
    const auto right_line = current_area.get_line(kRight);
    auto left_point = linePoint(left_line, end_side);
    auto right_point = linePoint(right_line, end_side);
    left_point.val = left_child->get_cap_load();
    right_point.val = right_child->get_cap_load();
    const auto default_balance_ref_axis = end_side == kHead ? BalanceRefAxis::kX : BalanceRefAxis::kY;
    const auto swapped_balance_ref_axis = end_side == kHead ? BalanceRefAxis::kY : BalanceRefAxis::kX;
    const auto balance_ref_axis
        = (left_point.x - right_point.x) * (left_point.y - right_point.y) < 0 ? swapped_balance_ref_axis : default_balance_ref_axis;
    FOR_EACH_BST_SIDE(timing_type)
    {
      const BalancePointQuery query{.first_point = left_point,
                                    .second_point = right_point,
                                    .timing_type = timing_type,
                                    .balance_ref_axis = balance_ref_axis,
                                    .pattern = _pattern};
      BalancePointResult result;
      calcBalanceBetweenPoints(query, result);
      if (!Equal(result.distance_to_first, 0) && !Equal(result.distance_to_second, 0)) {
        updatePointDelaysByEndSide(current_area, end_side, result.balance_point);
        balancePoints(end_side).push_back(result.balance_point);
      }
    }
  }
}
auto BoundSkewTree::calcBalanceBetweenPoints(const BalancePointQuery& query, BalancePointResult& result) const -> void
{
  auto horizontal_distance = std::abs(query.first_point.x - query.second_point.x);
  auto vertical_distance = std::abs(query.first_point.y - query.second_point.y);
  if (Equal(horizontal_distance, 0) || Equal(vertical_distance, 0)) {
    calcBalancePointOnLine(query, result);
  } else if (query.first_point.x <= query.second_point.x) {
    calcBalancePointOffLine(query, result);
  } else {
    BalancePointQuery swapped_query = query;
    swapped_query.first_point = query.second_point;
    swapped_query.second_point = query.first_point;
    calcBalancePointOffLine(swapped_query, result);
    std::swap(result.distance_to_first, result.distance_to_second);
  }
}
auto BoundSkewTree::calcBalancePointOnLine(const BalancePointQuery& query, BalancePointResult& result) const -> void
{
  auto horizontal_distance = std::abs(query.first_point.x - query.second_point.x);
  auto vertical_distance = std::abs(query.first_point.y - query.second_point.y);
  CTS_LOG_FATAL_IF(!Equal(horizontal_distance, 0) && !Equal(vertical_distance, 0))
      << "h and v are not zero, which balance point is not on line";

  auto first_delay = query.timing_type == kMin ? query.first_point.min : query.first_point.max;
  auto second_delay = query.timing_type == kMin ? query.second_point.min : query.second_point.max;
  auto unit_resistance = Equal(horizontal_distance, 0) ? _unit_vertical_resistance : _unit_horizontal_resistance;
  auto unit_capacitance = Equal(horizontal_distance, 0) ? _unit_vertical_capacitance : _unit_horizontal_capacitance;
  const auto merge_distances = calcMergeDist(unit_resistance, unit_capacitance, query.first_point.val, first_delay, query.second_point.val,
                                             second_delay, horizontal_distance + vertical_distance);
  result.distance_to_first = merge_distances.distance_to_first;
  result.distance_to_second = merge_distances.distance_to_second;
  calcPointCoordOnLine(query.first_point, query.second_point, result.distance_to_first, result.distance_to_second, result.balance_point);
  double first_delay_increase = 0.0;
  double second_delay_increase = 0.0;
  if (Equal(horizontal_distance, 0)) {
    first_delay_increase = calcDelayIncrease(0, result.distance_to_first, query.first_point.val, query.pattern);
    second_delay_increase = calcDelayIncrease(0, result.distance_to_second, query.second_point.val, query.pattern);
  } else {
    first_delay_increase = calcDelayIncrease(result.distance_to_first, 0, query.first_point.val, query.pattern);
    second_delay_increase = calcDelayIncrease(result.distance_to_second, 0, query.second_point.val, query.pattern);
  }
  result.balance_point.min = std::min(query.first_point.min + first_delay_increase, query.second_point.min + second_delay_increase);
  result.balance_point.max = std::max(query.first_point.max + first_delay_increase, query.second_point.max + second_delay_increase);
}
auto BoundSkewTree::calcBalancePointOffLine(const BalancePointQuery& query, BalancePointResult& result) const -> void
{
  auto first_point = query.first_point;
  auto second_point = query.second_point;
  auto horizontal_distance = std::abs(first_point.x - second_point.x);
  auto vertical_distance = std::abs(first_point.y - second_point.y);
  CTS_LOG_FATAL_IF(Equal(horizontal_distance, 0) || Equal(vertical_distance, 0)) << "h or v is zero, which balance point is on line";
  CTS_LOG_FATAL_IF(first_point.x > second_point.x) << "first_point is not left of second_point";

  auto first_delay = query.timing_type == kMin ? first_point.min : first_point.max;
  auto second_delay = query.timing_type == kMin ? second_point.min : second_point.max;
  auto x_position = calcXBalancePosition(first_delay, second_delay, first_point.val, second_point.val, horizontal_distance,
                                         vertical_distance, query.balance_ref_axis);
  double y_position = 0.0;
  if (x_position < 0) {
    y_position = query.balance_ref_axis == BalanceRefAxis::kX
                     ? calcYBalancePosition(first_delay, second_delay, first_point.val, second_point.val, horizontal_distance,
                                            vertical_distance, query.balance_ref_axis)
                     : -1;
    x_position = y_position >= 0 ? 0 : x_position;
  } else if (x_position > horizontal_distance) {
    y_position = query.balance_ref_axis == BalanceRefAxis::kX
                     ? vertical_distance + 1
                     : calcYBalancePosition(first_delay, second_delay, first_point.val, second_point.val, horizontal_distance,
                                            vertical_distance, query.balance_ref_axis);
    x_position = y_position <= vertical_distance ? horizontal_distance : x_position;
  } else {
    y_position = query.balance_ref_axis == BalanceRefAxis::kX ? vertical_distance : 0;
  }

  if (x_position < 0) {
    CTS_LOG_FATAL_IF(y_position >= 0) << "y is illegal";
    auto adjusted_point = first_point;
    auto delay_increase = calcDelayIncrease(horizontal_distance, vertical_distance, second_point.val, query.pattern);
    adjusted_point.min = second_point.min + delay_increase;
    adjusted_point.max = second_point.max + delay_increase;
    adjusted_point.val
        = second_point.val + _unit_horizontal_capacitance * horizontal_distance + _unit_vertical_capacitance * vertical_distance;
    calcBalancePointOnLine(BalancePointQuery{.first_point = first_point,
                                             .second_point = adjusted_point,
                                             .timing_type = query.timing_type,
                                             .balance_ref_axis = query.balance_ref_axis,
                                             .pattern = query.pattern},
                           result);
    CTS_LOG_FATAL_IF(result.distance_to_first > kEpsilon) << "dist to first_point should be zero";
    auto new_delay_increase = calcDelayIncrease(0, result.distance_to_second, adjusted_point.val, query.pattern);
    CTS_LOG_FATAL_IF(!Equal(first_delay, delay_increase + new_delay_increase + second_delay)) << "delay is not equal";
    result.distance_to_second += horizontal_distance + vertical_distance;
  } else if (x_position > horizontal_distance) {
    CTS_LOG_FATAL_IF(y_position <= vertical_distance) << "y: " << y_position << " is not greater than v: " << vertical_distance;
    auto adjusted_point = second_point;
    auto delay_increase = calcDelayIncrease(horizontal_distance, vertical_distance, first_point.val, query.pattern);
    adjusted_point.min = first_point.min + delay_increase;
    adjusted_point.max = first_point.max + delay_increase;
    adjusted_point.val
        = first_point.val + _unit_horizontal_capacitance * horizontal_distance + _unit_vertical_capacitance * vertical_distance;
    calcBalancePointOnLine(BalancePointQuery{.first_point = adjusted_point,
                                             .second_point = second_point,
                                             .timing_type = query.timing_type,
                                             .balance_ref_axis = query.balance_ref_axis,
                                             .pattern = query.pattern},
                           result);
    CTS_LOG_FATAL_IF(result.distance_to_second > kEpsilon) << "dist to second_point should be zero";
    auto new_delay_increase = calcDelayIncrease(0, result.distance_to_first, adjusted_point.val, query.pattern);
    CTS_LOG_FATAL_IF(!Equal(second_delay, delay_increase + new_delay_increase + first_delay)) << "delay is not equal";
    result.distance_to_first += horizontal_distance + vertical_distance;
  } else {
    CTS_LOG_FATAL_IF(y_position < -kEpsilon || y_position > vertical_distance + kEpsilon)
        << "y: " << y_position << " is not in range [0, " << vertical_distance << "]";
    result.balance_point.x = first_point.x + x_position;
    result.balance_point.y = first_point.y < second_point.y ? first_point.y + y_position : first_point.y - y_position;
    auto first_delay_increase = calcDelayIncrease(x_position, y_position, first_point.val, query.pattern);
    auto second_delay_increase
        = calcDelayIncrease(horizontal_distance - x_position, vertical_distance - y_position, second_point.val, query.pattern);
    result.balance_point.min = std::min(first_point.min + first_delay_increase, second_point.min + second_delay_increase);
    result.balance_point.max = std::max(first_point.max + first_delay_increase, second_point.max + second_delay_increase);
    result.distance_to_first = x_position + y_position;
    result.distance_to_second = horizontal_distance + vertical_distance - result.distance_to_first;
    CTS_LOG_FATAL_IF(!Equal(first_delay_increase + first_delay, second_delay_increase + second_delay)) << "delay is not equal";
  }
  CTS_LOG_FATAL_IF(result.distance_to_first + result.distance_to_second < horizontal_distance + vertical_distance - kEpsilon)
      << "dist out of range";
}
auto BoundSkewTree::calcMergeDist(const double& unit_resistance, const double& unit_capacitance, const double& cap_load_1,
                                  const double& delay_1, const double& cap_load_2, const double& delay_2,
                                  const double& total_distance) -> MergeDistances
{
  MergeDistances merge_distances;
  auto distance_to_merge = (delay_2 - delay_1 + unit_resistance * total_distance * (cap_load_2 + unit_capacitance * total_distance / 2))
                           / (unit_resistance * (cap_load_1 + cap_load_2 + unit_capacitance * total_distance));
  if (distance_to_merge < 0) {
    auto capacitance_ratio = cap_load_2 / unit_capacitance;
    distance_to_merge = std::sqrt(capacitance_ratio * capacitance_ratio + 2 * (delay_1 - delay_2) / (unit_resistance * unit_capacitance))
                        - capacitance_ratio;
    merge_distances.distance_to_first = 0;
    merge_distances.distance_to_second = distance_to_merge;
  } else if (distance_to_merge > total_distance) {
    auto capacitance_ratio = cap_load_1 / unit_capacitance;
    distance_to_merge = std::sqrt(capacitance_ratio * capacitance_ratio + 2 * (delay_2 - delay_1) / (unit_resistance * unit_capacitance))
                        - capacitance_ratio;
    merge_distances.distance_to_first = distance_to_merge;
    merge_distances.distance_to_second = 0;
  } else {
    merge_distances.distance_to_first = distance_to_merge;
    merge_distances.distance_to_second = total_distance - distance_to_merge;
  }
  return merge_distances;
}
auto BoundSkewTree::calcPointCoordOnLine(const Point& first_point, const Point& second_point, const double& distance_to_first,
                                         const double& distance_to_second, Point& point) -> void
{
  auto total_distance = distance_to_first + distance_to_second;
  auto point_distance = Geom::distance(first_point, second_point);
  CTS_LOG_FATAL_IF(!Equal(total_distance, point_distance) && total_distance < point_distance) << "distance is less than points distance";
  if (Equal(distance_to_first, 0)) {
    point = first_point;
  } else if (Equal(distance_to_second, 0)) {
    point = second_point;
  } else {
    point = {(first_point.x * distance_to_second + second_point.x * distance_to_first) / total_distance,
             (first_point.y * distance_to_second + second_point.y * distance_to_first) / total_distance};
  }
}
auto BoundSkewTree::calcXBalancePosition(const double& delay_1, const double& delay_2, const double& cap_load_1, const double& cap_load_2,
                                         const double& horizontal_distance, const double& vertical_distance,
                                         BalanceRefAxis balance_ref_axis) const -> double
{
  auto resistance_capacitance_cross_term = _pattern == RCPattern::kHV ? _unit_vertical_resistance * _unit_horizontal_capacitance
                                                                      : _unit_horizontal_resistance * _unit_vertical_capacitance;
  double numerator = 0;
  if (balance_ref_axis == BalanceRefAxis::kX) {
    // assume (x, vertical_distance-y) and (horizontal_distance-x, y), then set y = 0
    numerator = delay_2 - delay_1 + _delay_quadratic_factor.horizontal * horizontal_distance * horizontal_distance
                - _delay_quadratic_factor.vertical * vertical_distance * vertical_distance
                + _unit_horizontal_resistance * horizontal_distance * cap_load_2
                - _unit_vertical_resistance * vertical_distance * cap_load_1;
  } else {
    // assume (x, y) and (horizontal_distance-x, vertical_distance-y), then set y = 0
    numerator = delay_2 - delay_1 + _delay_quadratic_factor.horizontal * horizontal_distance * horizontal_distance
                + _delay_quadratic_factor.vertical * vertical_distance * vertical_distance
                + cap_load_2 * (_unit_horizontal_resistance * horizontal_distance + _unit_vertical_resistance * vertical_distance)
                + resistance_capacitance_cross_term * horizontal_distance * vertical_distance;
  }
  auto denominator = _unit_horizontal_resistance * (cap_load_1 + cap_load_2) + resistance_capacitance_cross_term * vertical_distance
                     + 2 * horizontal_distance * _delay_quadratic_factor.horizontal;
  return numerator / denominator;
}
auto BoundSkewTree::calcYBalancePosition(const double& delay_1, const double& delay_2, const double& cap_load_1, const double& cap_load_2,
                                         const double& horizontal_distance, const double& vertical_distance,
                                         BalanceRefAxis balance_ref_axis) const -> double
{
  auto resistance_capacitance_cross_term = _pattern == RCPattern::kHV ? _unit_vertical_resistance * _unit_horizontal_capacitance
                                                                      : _unit_horizontal_resistance * _unit_vertical_capacitance;
  double numerator = 0;
  auto denominator = _unit_vertical_resistance * (cap_load_1 + cap_load_2) + 2 * vertical_distance * _delay_quadratic_factor.vertical
                     + resistance_capacitance_cross_term * horizontal_distance;
  double y_position = 0;
  if (balance_ref_axis == BalanceRefAxis::kX) {
    // assume (x, y) and (horizontal_distance-x, vertical_distance-y), then set x = 0
    numerator = delay_2 - delay_1 + _delay_quadratic_factor.horizontal * horizontal_distance * horizontal_distance
                + _delay_quadratic_factor.vertical * vertical_distance * vertical_distance
                + cap_load_2 * (_unit_horizontal_resistance * horizontal_distance + _unit_vertical_resistance * vertical_distance)
                + resistance_capacitance_cross_term * horizontal_distance * vertical_distance;
    y_position = numerator / denominator;
    CTS_LOG_FATAL_IF(y_position > vertical_distance + kEpsilon)
        << "y: " << y_position << " is larger than vertical_distance: " << vertical_distance;
  } else {
    // assume (horizontal_distance-x, y) and (x, vertical_distance-y), then set x = 0
    numerator = delay_2 - delay_1 + _delay_quadratic_factor.vertical * vertical_distance * vertical_distance
                - _delay_quadratic_factor.horizontal * horizontal_distance * horizontal_distance
                + _unit_vertical_resistance * vertical_distance * cap_load_2
                - _unit_horizontal_resistance * horizontal_distance * cap_load_1;
    y_position = numerator / denominator;
    CTS_LOG_FATAL_IF(y_position < -kEpsilon) << "y: " << y_position << " is less than 0";
  }
  return y_position;
}
auto BoundSkewTree::calcFeasibleMergeSegmentPoints(const Area& current_area) -> void
{
  FOR_EACH_BST_SIDE(end_side)
  {
    feasibleMergeSegmentPoints(end_side).clear();
    SideState<Point> candidate;
    if (end_side == kHead) {
      candidate.left = joiningRegionPoints(kLeft).front();
      candidate.right = joiningRegionPoints(kRight).front();
    } else {
      candidate.left = joiningRegionPoints(kLeft).back();
      candidate.right = joiningRegionPoints(kRight).back();
    }
    bool exist = false;
    if (joiningRegionCornerExists(end_side)) {
      const auto& joining_corner = joiningCornerPoint(end_side);
      exist = calcFeasibleMergeSegmentOnLine(current_area, candidate.left, joining_corner, end_side);
      if (exist) {
        exist = calcFeasibleMergeSegmentOnLine(current_area, candidate.right, joining_corner, end_side);
        if (!exist) {
          exist = calcFeasibleMergeSegmentOnLine(current_area, joiningCornerPoint(end_side), candidate.left, end_side);
          CTS_LOG_FATAL_IF(!exist) << "can't find feasible merge section on line";
        }
      } else {
        exist = calcFeasibleMergeSegmentOnLine(current_area, joiningCornerPoint(end_side), candidate.right, end_side);
        if (exist) {
          exist = calcFeasibleMergeSegmentOnLine(current_area, candidate.right, joining_corner, end_side);
          CTS_LOG_FATAL_IF(!exist) << "can't find feasible merge section on line";
        }
      }
    } else {
      exist = calcFeasibleMergeSegmentOnLine(current_area, candidate.left, candidate.right, end_side);
      if (exist) {
        calcFeasibleMergeSegmentOnLine(current_area, candidate.right, candidate.left, end_side);
      }
    }
    Geom::uniquePointLocations(feasibleMergeSegmentPoints(end_side));
  }
}
auto BoundSkewTree::calcFeasibleMergeSegmentOnLine(const Area& current_area, Point& point, const Point& reference_point,
                                                   const size_t& end_side) -> bool
{
  auto skew = pointSkew(point);
  if (Equal(skew, _skew_bound) || skew < _skew_bound) {
    feasibleMergeSegmentPoints(end_side).push_back(point);
    return true;
  }
  auto nearest_balance_point = reference_point;
  std::ranges::for_each(balancePoints(end_side), [&nearest_balance_point, &point](const Point& balance_point) {
    if (Geom::distance(point, balance_point) < Geom::distance(point, nearest_balance_point)) {
      nearest_balance_point = balance_point;
    }
  });
  skew = pointSkew(nearest_balance_point);
  if (Equal(skew, _skew_bound)) {
    feasibleMergeSegmentPoints(end_side).push_back(nearest_balance_point);
    return true;
  }
  if (skew < _skew_bound) {
    Point feasible_merge_point;
    calcFeasibleMergeSegmentBetweenPoints(point, nearest_balance_point, feasible_merge_point);
    updatePointDelaysByEndSide(current_area, end_side, feasible_merge_point);
    if (!Equal(pointSkew(feasible_merge_point), _skew_bound)) {
      auto first_point = point;
      auto second_point = nearest_balance_point;
      updatePointDelaysByEndSide(current_area, end_side, first_point);
      updatePointDelaysByEndSide(current_area, end_side, second_point);
      CTS_LOG_FATAL << "feasible merge section point should in skew bound";
    }
    feasibleMergeSegmentPoints(end_side).push_back(feasible_merge_point);
    return true;
  }
  return false;
}
auto BoundSkewTree::calcFeasibleMergeSegmentBetweenPoints(const Point& high_skew_point, const Point& low_skew_point,
                                                          Point& feasible_merge_point) const -> void
{
  auto high_skew = pointSkew(high_skew_point);
  auto low_skew = pointSkew(low_skew_point);
  CTS_LOG_FATAL_IF(low_skew > _skew_bound) << "low skew is larger than skew bound";
  CTS_LOG_FATAL_IF(high_skew < low_skew + kEpsilon) << "high skew is less than low skew";
  auto dist = Geom::distance(high_skew_point, low_skew_point);
  CTS_LOG_FATAL_IF(dist <= kEpsilon) << "distance is less than epsilon";
  auto dist_to_low = dist * (_skew_bound - low_skew) / (high_skew - low_skew);
  calcPointCoordOnLine(high_skew_point, low_skew_point, dist - dist_to_low, dist_to_low, feasible_merge_point);
}
auto BoundSkewTree::hasFeasibleMergeSegmentOnJoiningRegion() const -> bool
{
  if (!feasibleMergeSegmentPoints(kHead).empty() || !feasibleMergeSegmentPoints(kTail).empty()) {
    return true;
  }
  FOR_EACH_BST_SIDE(side)
  {
    for (const auto& point : joiningRegionPoints(side)) {
      if (pointSkew(point) <= _skew_bound) {
        return true;
      }
    }
  }
  return false;
}
auto BoundSkewTree::constructFeasibleMergeRegion(Area* parent) const -> void
{
  if (calcAreaLineType(*parent) == LineType::kManhattan) {
    // parallel manhattan arc
    addMergeRegionBetweenJoiningSegments(parent, kHead);
    if (!parent->get_merge_region().empty() && !isJoiningRegionLine()) {
      addMergeRegionBetweenJoiningSegments(parent, kTail);
    }
  } else {
    // parallel horizontal or vertical arc
    addMergeRegionBetweenJoiningSegments(parent, kHead);
    addMergeRegionOnJoiningSegment(parent, kLeft);
    addMergeRegionBetweenJoiningSegments(parent, kTail);
    if (parent->get_radius() > kEpsilon) {
      addMergeRegionOnJoiningSegment(parent, kRight);
    }
  }
}
auto BoundSkewTree::isJoiningRegionLine() const -> bool
{
  const auto& left_segment_points = joiningSegmentPoints(kLeft);
  const auto& right_segment_points = joiningSegmentPoints(kRight);
  const auto left_head = pointAt(left_segment_points, kHead);
  const auto left_tail = pointAt(left_segment_points, kTail);
  const auto right_head = pointAt(right_segment_points, kHead);
  const auto right_tail = pointAt(right_segment_points, kTail);
  const auto min_x = std::min({left_head.x, left_tail.x, right_head.x, right_tail.x});
  const auto min_y = std::min({left_head.y, left_tail.y, right_head.y, right_tail.y});
  const auto max_x = std::max({left_head.x, left_tail.x, right_head.x, right_tail.x});
  const auto max_y = std::max({left_head.y, left_tail.y, right_head.y, right_tail.y});
  return Equal(min_x, max_x) || Equal(min_y, max_y);
}
auto BoundSkewTree::addMergeRegionBetweenJoiningSegments(Area* current_area, const size_t& end_side) const -> void
{
  Points merge_region_points;
  std::ranges::for_each(balancePoints(end_side), [&merge_region_points](const Point& point) { merge_region_points.push_back(point); });
  std::ranges::for_each(feasibleMergeSegmentPoints(end_side),
                        [&merge_region_points](const Point& point) { merge_region_points.push_back(point); });
  if (joiningRegionCornerExists(end_side) && pointSkew(joiningCornerPoint(end_side)) < _skew_bound + kEpsilon) {
    merge_region_points.push_back(joiningCornerPoint(end_side));
  }
  if (merge_region_points.empty()) {
    return;
  }
  const auto left_line = current_area->get_line(kLeft);
  const auto right_line = current_area->get_line(kRight);
  Point reference_joining_segment_point = end_side == kHead ? linePoint(left_line, end_side) : linePoint(right_line, end_side);
  std::ranges::for_each(merge_region_points, [&](Point& point) { point.val = Geom::distance(point, reference_joining_segment_point); });
  Geom::sortPointsByValueDesc(merge_region_points);
  Geom::uniquePointLocations(merge_region_points);
  std::ranges::for_each(merge_region_points, [&](const Point& point) { current_area->add_merge_region_point(point); });
}
auto BoundSkewTree::addMergeRegionOnJoiningSegment(Area* current_area, const size_t& side) const -> void
{
  const auto& side_joining_region = joiningRegionPoints(side);
  const auto other_side = otherSide(side);
  const auto& other_joining_region = joiningRegionPoints(other_side);
  CTS_LOG_FATAL_IF(side_joining_region.size() < 2) << "join region size is less than 2";

  const auto first_point = side_joining_region.front();
  const auto other_first_point = other_joining_region.front();
  size_t joining_region_left_index = calcMergeRegionLeftIndex(side);
  if (feasibleMergeSegmentPoints(kHead).empty() && pointSkew(first_point) < pointSkew(other_first_point)) {
    joining_region_left_index = side_joining_region.size() - 1;
    for (size_t point_index = 1; point_index + 1 < side_joining_region.size(); ++point_index) {
      if (Equal(pointSkew(pointAt(side_joining_region, point_index)), _skew_bound)) {
        joining_region_left_index = point_index;
        break;
      }
    }
  }

  const auto last_point = side_joining_region.back();
  const auto other_last_point = other_joining_region.back();
  auto merge_region_span = calcMergeRegionSpan(side, joining_region_left_index);
  auto& joining_region_right_index = merge_region_span.right_index;
  if (feasibleMergeSegmentPoints(kTail).empty() && pointSkew(last_point) < pointSkew(other_last_point)) {
    while (joining_region_right_index >= joining_region_left_index) {
      if (Equal(pointSkew(pointAt(side_joining_region, joining_region_right_index)), _skew_bound)) {
        break;
      }
      if (joining_region_right_index == joining_region_left_index) {
        break;
      }
      --joining_region_right_index;
    }
  }

  appendMergeRegionPointsOnSegment(current_area, merge_region_span);
}
auto BoundSkewTree::calcMergeRegionLeftIndex(const size_t& side) const -> size_t
{
  CTS_LOG_FATAL_IF(joiningRegionPoints(side).size() < 2) << "join region size is less than 2";
  return 1;
}
auto BoundSkewTree::calcMergeRegionSpan(const size_t& side, const size_t& left_index) const -> MergeRegionSpan
{
  CTS_LOG_FATAL_IF(joiningRegionPoints(side).size() < 2) << "join region size is less than 2";
  const auto right_index = joiningRegionPoints(side).size() - 2;
  return MergeRegionSpan{.side = side, .left_index = left_index, .right_index = right_index < left_index ? left_index - 1 : right_index};
}
auto BoundSkewTree::appendMergeRegionPointsOnSegment(Area* current_area, const MergeRegionSpan& merge_region_span) const -> void
{
  if (merge_region_span.right_index < merge_region_span.left_index) {
    return;
  }
  if (merge_region_span.side == kLeft) {
    for (size_t point_index = merge_region_span.left_index; point_index <= merge_region_span.right_index; ++point_index) {
      addMergeRegionPointFromJoiningRegion(current_area, merge_region_span.side, point_index);
    }
    return;
  }

  size_t point_index = merge_region_span.right_index;
  while (point_index >= merge_region_span.left_index) {
    addMergeRegionPointFromJoiningRegion(current_area, merge_region_span.side, point_index);
    if (point_index == merge_region_span.left_index) {
      break;
    }
    --point_index;
  }
}
auto BoundSkewTree::addMergeRegionPointFromJoiningRegion(Area* current_area, const size_t& side, const size_t& point_index) const -> void
{
  auto point = joiningRegionPoint(side, point_index);
  const auto original_point = point;
  const auto slope = calcSkewSlope(*current_area);
  const auto dist = (pointSkew(point) - _skew_bound) / slope;
  if (dist <= 0) {
    current_area->add_merge_region_point(point);
  } else if (dist <= current_area->get_radius()) {
    const auto relative_type = Geom::lineRelative(getJoiningSegmentLine(kLeft), getJoiningSegmentLine(kRight), side);
    Geom::calcRelativeCoord(point, relative_type, dist);
    const auto horizontal_distance = std::abs(point.x - original_point.x);
    const auto vertical_distance = std::abs(point.y - original_point.y);
    CTS_LOG_FATAL_IF(!Equal(horizontal_distance, 0) && !Equal(vertical_distance, 0)) << "not horizontal or vertical";
    const auto incr_delay
        = side == kLeft ? calcDelayIncrease(horizontal_distance, vertical_distance, current_area->get_left()->get_cap_load(), _pattern)
                        : calcDelayIncrease(horizontal_distance, vertical_distance, current_area->get_right()->get_cap_load(), _pattern);
    point.min += incr_delay;
    point.max = _skew_bound + point.min;
    current_area->add_merge_region_point(point);
  }
}
auto BoundSkewTree::calcSkewSlope(const Area& current_area) const -> double
{
  auto* left_child = current_area.get_left();
  auto* right_child = current_area.get_right();
  if (left_child == nullptr || right_child == nullptr) {
    CTS_LOG_FATAL << "calcSkewSlope requires both child areas";
    return 0.0;
  }

  const auto left_line = current_area.get_line(kLeft);
  const auto right_line = current_area.get_line(kRight);
  const auto left_x_coord = linePoint(left_line, kHead).x;
  const auto left_y_coord = linePoint(left_line, kHead).y;
  const auto right_x_coord = linePoint(right_line, kHead).x;
  const auto right_y_coord = linePoint(right_line, kHead).y;
  const auto left_cap_load = left_child->get_cap_load();
  const auto right_cap_load = right_child->get_cap_load();
  if (Equal(left_x_coord, right_x_coord)) {
    return _unit_vertical_resistance * (left_cap_load + right_cap_load + current_area.get_radius() * _unit_vertical_capacitance);
  }
  if (Equal(left_y_coord, right_y_coord)) {
    return _unit_horizontal_resistance * (left_cap_load + right_cap_load + current_area.get_radius() * _unit_horizontal_capacitance);
  }
  CTS_LOG_FATAL << "line is not horizontal or vertical";
  return 0.0;
}
auto BoundSkewTree::constructInfeasibleMergeRegion(Area* parent) const -> void
{
  calcMinSkewSection(parent);
  calcDetourEdgeLength(parent);
  refineMergeRegionDelay(parent);
}
auto BoundSkewTree::calcMinSkewSection(Area* current_area) const -> void
{
  auto min_skew = std::numeric_limits<double>::max();
  auto min_skew_side = kLeft;
  FOR_EACH_BST_SIDE(side)
  {
    auto min_side_point_skew = std::numeric_limits<double>::max();
    std::ranges::for_each(joiningRegionPoints(side),
                          [&](const Point& point) { min_side_point_skew = std::min(min_side_point_skew, pointSkew(point)); });
    if (min_side_point_skew < min_skew) {
      min_skew = min_side_point_skew;
      min_skew_side = side;
    }
  }
  std::ranges::for_each(joiningRegionPoints(min_skew_side), [&](const Point& point) {
    if (Equal(pointSkew(point), min_skew)) {
      current_area->add_merge_region_point(point);
    }
  });
}
auto BoundSkewTree::calcDetourEdgeLength(Area* current_area) const -> void
{
  const auto left_line = current_area->get_line(kLeft);
  const auto right_line = current_area->get_line(kRight);
  auto left_point = linePoint(left_line, kHead);
  auto right_point = linePoint(right_line, kHead);
  left_point.val = current_area->get_left()->get_cap_load();
  right_point.val = current_area->get_right()->get_cap_load();
  auto delta = pointSkew(current_area->get_merge_region().front()) - _skew_bound;
  CTS_LOG_FATAL_IF(delta <= 0) << "remain skew less than 0";
  auto [horizontal_distance, vertical_distance] = calcManhattanDistanceComponents(left_point, right_point);
  if (left_point.max > right_point.max) {
    right_point.max = left_point.max - delta - calcDelayIncrease(horizontal_distance, vertical_distance, right_point.val, _pattern);
    BalancePointResult result;
    calcBalanceBetweenPoints(BalancePointQuery{.first_point = left_point,
                                               .second_point = right_point,
                                               .timing_type = kMax,
                                               .balance_ref_axis = BalanceRefAxis::kX,
                                               .pattern = _pattern},
                             result);
    CTS_LOG_FATAL_IF(result.distance_to_first > kEpsilon) << "dist to left_point should be zero";
    current_area->set_edge_len(kLeft, 0);
    current_area->set_edge_len(kRight, result.distance_to_second);
  } else {
    left_point.max = right_point.max - delta - calcDelayIncrease(horizontal_distance, vertical_distance, left_point.val, _pattern);
    BalancePointResult result;
    calcBalanceBetweenPoints(BalancePointQuery{.first_point = left_point,
                                               .second_point = right_point,
                                               .timing_type = kMax,
                                               .balance_ref_axis = BalanceRefAxis::kX,
                                               .pattern = _pattern},
                             result);
    CTS_LOG_FATAL_IF(result.distance_to_second > kEpsilon) << "dist to right_point should be zero";
    current_area->set_edge_len(kLeft, result.distance_to_first);
    current_area->set_edge_len(kRight, 0);
  }
}
auto BoundSkewTree::refineMergeRegionDelay(Area* current_area) const -> void
{
  auto merge_region = current_area->get_merge_region();
  std::ranges::for_each(merge_region, [&](Point& point) { point.min = point.max - _skew_bound; });
  current_area->set_merge_region(merge_region);
}
auto BoundSkewTree::constructTransformedRectMergeRegion(Area* current_area) const -> void
{
  TransformedRect left_transformed_rect;
  Geom::buildTransformedRect(mergeSegment(kLeft), current_area->get_edge_len(kLeft), left_transformed_rect);
  TransformedRect right_transformed_rect;
  Geom::buildTransformedRect(mergeSegment(kRight), current_area->get_edge_len(kRight), right_transformed_rect);

  TransformedRect intersect;
  Geom::makeIntersection(left_transformed_rect, right_transformed_rect, intersect);
  Geom::transformedRectCore(intersect, intersect);
  Region merge_region;
  Geom::transformedRectToRegion(intersect, merge_region);
  const auto reference_point = current_area->get_merge_region().front();
  std::ranges::for_each(merge_region, [&](Point& point) {
    point.min = reference_point.min;
    point.max = reference_point.max;
  });
  current_area->set_merge_region(merge_region);
}
auto BoundSkewTree::embedChild(const EmbeddingStep& embedding_target) -> void
{
  auto* parent = embedding_target.parent;
  auto* child = embedding_target.child;
  const size_t side = embedding_target.side;
  Point child_loc;
  const auto parent_loc = parent->get_location();
  auto merge_region = child->get_merge_region();
  if (merge_region.size() == 4 && isTransformedRectArea(child)) {
    TransformedRect transformed_rect;
    mergeRegionToTransformedRect(merge_region, transformed_rect);
    const auto dist = Geom::pointToTransformedRectDistance(parent_loc, transformed_rect);
    const TransformedRect parent_transformed_rect(parent_loc, dist);
    TransformedRect merge_segment;
    Geom::makeIntersection(parent_transformed_rect, transformed_rect, merge_segment);
    Geom::coreMidPoint(merge_segment, child_loc);
  } else {
    const auto joining_segment_line = parent->get_line(side);
    auto head = linePoint(joining_segment_line, kHead);
    auto tail = linePoint(joining_segment_line, kTail);
    Line temp;
    locateBoundarySegment(child, head, temp);
    locateBoundarySegment(child, tail, temp);
    auto horizontal_distance = std::abs(head.x - tail.x);
    auto vertical_distance = std::abs(head.y - tail.y);
    if (Equal(horizontal_distance, 0) && Equal(vertical_distance, 0)) {
      // kHead loc is same as kTail loc
      child_loc = head;
    } else if (Equal(horizontal_distance, 0)) {
      // vertical
      child_loc.x = head.x;
      child_loc.y = parent_loc.y;
    } else if (Equal(vertical_distance, 0)) {
      // horizontal
      child_loc.x = parent_loc.x;
      child_loc.y = head.y;
    } else {
      // others
      Geom::pointToLineDistance(parent_loc, joining_segment_line, child_loc);
    }
    CTS_LOG_FATAL_IF(!Geom::onLine(child_loc, joining_segment_line)) << "child loc is not on joining_segment line";
  }
  child->set_location(child_loc);
  if (parent->get_edge_len(side) >= 0) {
    CTS_LOG_FATAL_IF(parent->get_edge_len(side) < Geom::distance(parent_loc, child_loc) - kEpsilon) << "edge len is less than distance";
  } else {
    parent->set_edge_len(side, Geom::distance(parent_loc, child_loc));
  }
}
auto BoundSkewTree::isTransformedRectArea(Area* current_area) -> bool
{
  if (isManhattanArea(current_area)) {
    return true;
  }
  auto merge_region = current_area->get_merge_region();
  if (merge_region.size() != 4) {
    return false;
  }
  int manhattan_line_count = 0;
  for (const auto& line : current_area->getMergeRegionLines()) {
    if (Geom::lineType(line) == LineType::kManhattan) {
      ++manhattan_line_count;
    }
    auto min_delay_delta = std::abs(line[kHead].min - line[kTail].min);
    auto max_delay_delta = std::abs(line[kHead].max - line[kTail].max);
    if (min_delay_delta > kEpsilon || max_delay_delta > kEpsilon) {
      return false;
    }
  }
  return manhattan_line_count == 2;
}
auto BoundSkewTree::isManhattanArea(Area* current_area) -> bool
{
  auto merge_region = current_area->get_merge_region();
  if (merge_region.size() == 1) {
    return true;
  }
  if (merge_region.size() == 2 && Geom::lineType(merge_region[kHead], merge_region[kTail]) == LineType::kManhattan) {
    return true;
  }
  return false;
}

auto BoundSkewTree::mergeRegionToTransformedRect(const Region& merge_region, TransformedRect& transformed_rect) -> void
{
  if (merge_region.size() == 1) {
    transformed_rect.makeDiamond(merge_region.front(), 0);
    return;
  }
  if (merge_region.size() == 2) {
    Geom::lineToTransformedRect(transformed_rect, merge_region[kHead], merge_region[kTail]);
    return;
  }
  if (merge_region.size() == 4) {
    TransformedRect left_transformed_rect;
    if (Geom::lineType(merge_region[0], merge_region[1]) == LineType::kManhattan) {
      Geom::lineToTransformedRect(left_transformed_rect, merge_region[0], merge_region[1]);
    } else {
      CTS_LOG_FATAL_IF(Geom::lineType(merge_region[2], merge_region[1]) != LineType::kManhattan) << "merge_region is not manhattan";
      Geom::lineToTransformedRect(left_transformed_rect, merge_region[1], merge_region[2]);
    }
    TransformedRect right_transformed_rect;
    if (Geom::lineType(merge_region[2], merge_region[3]) == LineType::kManhattan) {
      Geom::lineToTransformedRect(right_transformed_rect, merge_region[2], merge_region[3]);
    } else {
      CTS_LOG_FATAL_IF(Geom::lineType(merge_region[0], merge_region[3]) != LineType::kManhattan) << "merge_region is not manhattan";
      Geom::lineToTransformedRect(right_transformed_rect, merge_region[3], merge_region[0]);
    }
    transformed_rect = left_transformed_rect;
    transformed_rect.enclose(right_transformed_rect);
    return;
  }
  CTS_LOG_FATAL << "merge_region size is not 1, 2 or 4";
}

auto BoundSkewTree::calcAreaLineType(const Area& current_area) -> LineType
{
  auto line = current_area.get_line(kLeft);
  return Geom::lineType(line);
}
auto BoundSkewTree::calcConvexHull(Area* current_area) -> void
{
  auto merge_region = current_area->get_merge_region();
  Geom::convexHull(merge_region);
  current_area->set_convex_hull(merge_region);
}

auto BoundSkewTree::calcJoiningRegionArea(const Line& first_line, const Line& second_line) -> double
{
  auto min_x = std::min({first_line[kHead].x, first_line[kTail].x, second_line[kHead].x, second_line[kTail].x});
  auto max_x = std::max({first_line[kHead].x, first_line[kTail].x, second_line[kHead].x, second_line[kTail].x});
  auto min_y = std::min({first_line[kHead].y, first_line[kTail].y, second_line[kHead].y, second_line[kTail].y});
  auto max_y = std::max({first_line[kHead].y, first_line[kTail].y, second_line[kHead].y, second_line[kTail].y});
  auto bound_area = (max_x - min_x) * (max_y - min_y);
  auto tri_area_1 = kHalfFactor * std::abs(first_line[kHead].x - first_line[kTail].x) * std::abs(first_line[kHead].y - first_line[kTail].y);
  auto tri_area_2
      = kHalfFactor * std::abs(second_line[kHead].x - second_line[kTail].x) * std::abs(second_line[kHead].y - second_line[kTail].y);
  auto jr_area = bound_area - tri_area_1 - tri_area_2;
  CTS_LOG_FATAL_IF(jr_area < 0) << "joining_region area is negative";
  return jr_area;
}

auto BoundSkewTree::locateBoundarySegment(Area* current_area, Point& point, Line& boundary_segment) -> void
{
  for (auto merge_region_line : current_area->getMergeRegionLines()) {
    boundary_segment = merge_region_line;
    if (Geom::onLine(point, boundary_segment)) {
      return;
    }
  }
  CTS_LOG_FATAL << "point is not located in area";
}
auto BoundSkewTree::calcSimplePointDelays(Point& point, Line& boundary_segment) const -> bool
{
  CTS_LOG_FATAL_IF(!Geom::onLine(point, boundary_segment)) << "point is not located in line";
  const auto dist = Geom::distance(point, boundary_segment[kHead]);
  const auto horizontal_distance = std::abs(boundary_segment[kHead].x - boundary_segment[kTail].x);
  const auto vertical_distance = std::abs(boundary_segment[kHead].y - boundary_segment[kTail].y);
  const auto length = horizontal_distance + vertical_distance;
  if (Equal(dist, 0)) {
    point.min = boundary_segment[kHead].min;
    point.max = boundary_segment[kHead].max;
    return true;
  }
  if (Geom::isSame(point, boundary_segment[kTail])) {
    point.min = boundary_segment[kTail].min;
    point.max = boundary_segment[kTail].max;
    return true;
  }
  if (Equal(horizontal_distance, vertical_distance)) {
    // line is manhattan arc
    CTS_LOG_FATAL_IF(!Equal(boundary_segment[kHead].min, boundary_segment[kTail].min)
                     || !Equal(boundary_segment[kHead].max, boundary_segment[kTail].max))
        << "manhattan arc endpoint's delay is not same";
    point.min = boundary_segment[kHead].min = boundary_segment[kTail].min;
    point.max = boundary_segment[kHead].max = boundary_segment[kTail].max;
    return true;
  }
  if (Equal(horizontal_distance, 0) || Equal(vertical_distance, 0)) {
    // line is vertical or horizontal
    auto alpha = Equal(horizontal_distance, 0) ? _delay_quadratic_factor.vertical : _delay_quadratic_factor.horizontal;
    auto beta = (boundary_segment[kTail].min - boundary_segment[kHead].min) / length - alpha * length;
    point.min = boundary_segment[kHead].min + alpha * dist * dist + beta * dist;
    beta = (boundary_segment[kTail].max - boundary_segment[kHead].max) / length - alpha * length;
    point.max = boundary_segment[kHead].max + alpha * dist * dist + beta * dist;
    return true;
  }
  return false;
}
auto BoundSkewTree::calcSegmentPointDelays(Point& point, Line& boundary_segment) const -> void
{
  if (!calcSimplePointDelays(point, boundary_segment)) {
    CTS_LOG_FATAL << "segment-only point delay calculation requires area context";
    return;
  }
  checkPointDelay(point);
}
auto BoundSkewTree::calcPointDelays(const Area& current_area, Point& point, Line& boundary_segment) const -> void
{
  if (!calcSimplePointDelays(point, boundary_segment)) {
    CTS_LOG_FATAL_IF(!Equal(pointSkew(boundary_segment[kHead]), _skew_bound) || !Equal(pointSkew(boundary_segment[kTail]), _skew_bound))
        << "thera are skew reservation in line";
    calcIrregularPointDelays(current_area, point, boundary_segment);
  }
  checkPointDelay(point);
}
auto BoundSkewTree::updatePointDelaysByEndSide(const Area& current_area, const size_t& end_side, Point& point) const -> void
{
  auto* left_child = current_area.get_left();
  auto* right_child = current_area.get_right();
  if (left_child == nullptr || right_child == nullptr) {
    CTS_LOG_FATAL << "updatePointDelaysByEndSide requires both child areas";
    return;
  }

  const auto left_line = current_area.get_line(kLeft);
  const auto right_line = current_area.get_line(kRight);
  const auto left_endpoint = linePoint(left_line, end_side);
  const auto right_endpoint = linePoint(right_line, end_side);
  const auto delay_left = pointDelayIncrease(point, left_endpoint, left_child->get_cap_load(), _pattern);
  const auto delay_right = pointDelayIncrease(point, right_endpoint, right_child->get_cap_load(), _pattern);
  point.min = std::min(left_endpoint.min + delay_left, right_endpoint.min + delay_right);
  point.max = std::max(left_endpoint.max + delay_left, right_endpoint.max + delay_right);
}
auto BoundSkewTree::calcIrregularPointDelays(const Area& current_area, Point& point, Line& boundary_segment) const -> void
{
  auto* left_child = current_area.get_left();
  auto* right_child = current_area.get_right();
  if (left_child == nullptr || right_child == nullptr) {
    CTS_LOG_FATAL << "calcIrregularPointDelays requires both child areas";
    return;
  }

  auto horizontal_distance = std::abs(boundary_segment[kHead].x - boundary_segment[kTail].x);
  auto vertical_distance = std::abs(boundary_segment[kHead].y - boundary_segment[kTail].y);
  auto left_line = current_area.get_line(kLeft);
  auto right_line = current_area.get_line(kRight);
  auto joining_segment_type = Geom::lineType(current_area.get_line(kLeft));
  if (joining_segment_type == LineType::kManhattan) {
    CTS_LOG_FATAL_IF(!Geom::isSame(left_line[kHead], left_line[kTail]) || !Geom::isSame(right_line[kHead], right_line[kTail]))
        << "endpoint should be same, left head: [" << left_line[kHead].x << ", " << left_line[kHead].y << "], left tail: ["
        << left_line[kTail].x << ", " << left_line[kTail].y << "], right head: [" << right_line[kHead].x << ", " << right_line[kHead].y
        << "], right tail: [" << right_line[kTail].x << ", " << right_line[kTail].y << "]";

    auto delay_left = pointDelayIncrease(left_line[kHead], point, left_child->get_cap_load(), _pattern);
    auto delay_right = pointDelayIncrease(right_line[kHead], point, right_child->get_cap_load(), _pattern);
    point.min = std::min(left_line[kHead].min + delay_left, right_line[kHead].min + delay_right);
    point.max = std::max(left_line[kHead].max + delay_left, right_line[kHead].max + delay_right);
    CTS_LOG_FATAL_IF(pointSkew(point) >= _skew_bound + kEpsilon) << "skew is larger than skew bound";
  } else {
    CTS_LOG_FATAL_IF(joining_segment_type != LineType::kVertical && joining_segment_type != LineType::kHorizontal)
        << "joining_segment type is not vertical or horizontal";
    auto dist = Geom::distance(point, boundary_segment[kHead]);
    auto length = horizontal_distance + vertical_distance;
    double alpha = 0;
    if (horizontal_distance > vertical_distance) {
      auto slope = vertical_distance / horizontal_distance;
      auto ratio = std::pow(1 + std::abs(slope), 2);
      alpha = (_delay_quadratic_factor.horizontal + slope * slope * _delay_quadratic_factor.vertical) / ratio;
    } else {
      auto slope = horizontal_distance / vertical_distance;
      auto ratio = std::pow(1 + std::abs(slope), 2);
      alpha = (_delay_quadratic_factor.vertical + slope * slope * _delay_quadratic_factor.horizontal) / ratio;
    }
    auto beta = (boundary_segment[kTail].max - boundary_segment[kHead].max) / length - alpha * length;
    point.max = boundary_segment[kHead].max + alpha * dist * dist + beta * dist;
    beta = (boundary_segment[kTail].min - boundary_segment[kHead].min) / length - alpha * length;
    point.min = boundary_segment[kHead].min + alpha * dist * dist + beta * dist;
  }
}
auto BoundSkewTree::pointDelayIncrease(const Point& lhs_point, const Point& rhs_point, const double& cap_load,
                                       const RCPattern& pattern) const -> double
{
  auto delay = calcDelayIncrease(std::abs(lhs_point.x - rhs_point.x), std::abs(lhs_point.y - rhs_point.y), cap_load, pattern);
  CTS_LOG_FATAL_IF(delay < 0) << "point increase delay is negative";
  return delay;
}
auto BoundSkewTree::pointDelayIncrease(const Point& lhs_point, const Point& rhs_point, const double& length, const double& cap_load,
                                       const RCPattern& pattern) const -> double
{
  auto [horizontal_distance, vertical_distance] = calcManhattanDistanceComponents(lhs_point, rhs_point);
  CTS_LOG_FATAL_IF(!Equal(length, horizontal_distance + vertical_distance) && length < horizontal_distance + vertical_distance)
      << "length is less than horizontal_distance + vertical_distance";
  double delay = 0;
  if (Equal(horizontal_distance, 0)) {
    delay = calcDelayIncrease(0, length, cap_load, pattern);
  } else if (Equal(vertical_distance, 0)) {
    delay = calcDelayIncrease(length, 0, cap_load, pattern);
  } else {
    delay = calcDelayIncrease(horizontal_distance, vertical_distance, cap_load, pattern);
    if (length > horizontal_distance + vertical_distance) {
      delay += calcDelayIncrease(
          0, length - horizontal_distance - vertical_distance,
          cap_load + _unit_horizontal_capacitance * horizontal_distance + _unit_vertical_capacitance * vertical_distance, pattern);
    }
  }
  CTS_LOG_FATAL_IF(delay < 0) << "point increase delay is negative";
  return delay;
}
auto BoundSkewTree::calcDelayIncrease(const double& horizontal_length, const double& vertical_length, const double& cap_load,
                                      const RCPattern& pattern) const -> double
{
  double delay = 0;
  switch (pattern) {
    case RCPattern::kHV:
      delay = _unit_horizontal_resistance * horizontal_length * (_unit_horizontal_capacitance * horizontal_length / 2 + cap_load)
              + _unit_vertical_resistance * vertical_length
                    * (_unit_vertical_capacitance * vertical_length / 2 + cap_load + horizontal_length * _unit_horizontal_capacitance);
      break;
    case RCPattern::kVH:
      delay = _unit_vertical_resistance * vertical_length * (_unit_vertical_capacitance * vertical_length / 2 + cap_load)
              + _unit_horizontal_resistance * horizontal_length
                    * (_unit_horizontal_capacitance * horizontal_length / 2 + cap_load + vertical_length * _unit_vertical_capacitance);
      break;
    case RCPattern::kSingle:
      delay = _unit_horizontal_resistance * (horizontal_length + vertical_length)
              * (_unit_horizontal_capacitance * (horizontal_length + vertical_length) / 2 + cap_load);
      break;
    default:
      CTS_LOG_FATAL << "unknown pattern";
      break;
  }
  return delay;
}

auto BoundSkewTree::pointSkew(const Point& point) -> double
{
  return point.max - point.min;
}
auto BoundSkewTree::getJoiningRegionLine(const size_t& side) const -> Line
{
  return Line{joiningRegionPoint(side, kHead), joiningRegionPoint(side, kTail)};
}
auto BoundSkewTree::getJoiningSegmentLine(const size_t& side) const -> Line
{
  return Line{joiningSegmentPoint(side, kHead), joiningSegmentPoint(side, kTail)};
}
auto BoundSkewTree::setJoiningRegionLine(const size_t& side, const Line& line) -> void
{
  joiningRegionPoint(side, kHead) = linePoint(line, kHead);
  joiningRegionPoint(side, kTail) = linePoint(line, kTail);
}
auto BoundSkewTree::setJoiningSegmentLine(const size_t& side, const Line& line) -> void
{
  joiningSegmentPoint(side, kHead) = linePoint(line, kHead);
  joiningSegmentPoint(side, kTail) = linePoint(line, kTail);
}
auto BoundSkewTree::checkPointDelay(Point& point) -> void
{
  // CTS_LOG_ERROR_IF(point.min <= -kEpsilon) << "point min delay is negative";
  CTS_LOG_FATAL_IF(point.max - point.min <= -kEpsilon) << "point skew is negative";
  if (point.min < -kEpsilon) {
    point.min = 0;
  }
  if (point.max < point.min + kEpsilon) {
    point.max = point.min;
  }
}
auto BoundSkewTree::checkJoiningSegmentMergeSegment() const -> void
{
  TransformedRect left;
  TransformedRect right;
  auto left_joining_segment = getJoiningSegmentLine(kLeft);
  auto right_joining_segment = getJoiningSegmentLine(kRight);
  Geom::lineToTransformedRect(left, left_joining_segment);
  Geom::lineToTransformedRect(right, right_joining_segment);
  CTS_LOG_FATAL_IF(!Geom::containsTransformedRect(left, mergeSegment(kLeft)))
      << "left joining_segment is not contain in left merge_segment";
  CTS_LOG_FATAL_IF(!Geom::containsTransformedRect(right, mergeSegment(kRight)))
      << "right joining_segment is not contain in right merge_segment";
}
auto BoundSkewTree::checkUpdatedJoiningSegment(const Area* current_area, Line& left_line, Line& right_line) const -> void
{
  const auto is_parallel = Geom::isParallel(left_line, right_line);
  const auto line_type = Geom::lineType(left_line);
  if (is_parallel) {
    CTS_LOG_FATAL_IF(line_type == LineType::kFlat || line_type == LineType::kTilt) << "not consider case";
  }
  const auto left_joining_segment = getJoiningSegmentLine(kLeft);
  const auto right_joining_segment = getJoiningSegmentLine(kRight);
  const auto line_distance = Geom::lineDist(left_joining_segment, right_joining_segment);
  const auto dist = line_distance.distance;
  CTS_LOG_FATAL_IF(!Geom::isSame(linePoint(left_joining_segment, kHead), linePoint(left_joining_segment, kTail))
                   && !Geom::isSame(linePoint(right_joining_segment, kHead), linePoint(right_joining_segment, kTail))
                   && !Geom::isParallel(left_joining_segment, right_joining_segment))
      << "joining_segment line error";
  CTS_LOG_FATAL_IF(!Equal(dist, current_area->get_radius())) << "distance between joinsegments not equal to radius";
  auto left_joining_segment_head = linePoint(left_joining_segment, kHead);
  auto left_joining_segment_tail = linePoint(left_joining_segment, kTail);
  CTS_LOG_FATAL_IF(!Geom::onLine(left_joining_segment_head, left_line) || !Geom::onLine(left_joining_segment_tail, left_line))
      << "left_joining_segment not in left section";
  auto right_joining_segment_head = linePoint(right_joining_segment, kHead);
  auto right_joining_segment_tail = linePoint(right_joining_segment, kTail);
  CTS_LOG_FATAL_IF(!Geom::onLine(right_joining_segment_head, right_line) || !Geom::onLine(right_joining_segment_tail, right_line))
      << "left_joining_segment not in left section";
}

}  // namespace icts::bst
