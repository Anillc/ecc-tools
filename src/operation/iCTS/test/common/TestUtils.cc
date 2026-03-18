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
 * @file TestUtils.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-19
 * @brief Test utilities for iCTS module testing.
 */

#include "common/TestUtils.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <utility>

namespace icts_test {
namespace {

struct Bounds
{
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
  bool valid = false;
};

struct SvgTransform
{
  int min_x = 0;
  int max_y = 0;
  double scale = 1.0;
  int margin = 20;
  int width = 0;
  int height = 0;

  double map_x(int x) const { return ((x - min_x) * scale) + margin; }
  double map_y(int y) const { return ((max_y - y) * scale) + margin; }
};

const std::array<const char*, 12> kPalette
    = {"#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf", "#4c78a8", "#f58518"};

int clamp_int(int value, int lower, int upper)
{
  return std::max(lower, std::min(value, upper));
}

Bounds compute_bounds(const std::vector<icts::Pin*>& loads, const std::vector<icts::Point<int>>& extras)
{
  Bounds bounds;
  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, loc.get_x());
    bounds.min_y = std::min(bounds.min_y, loc.get_y());
    bounds.max_x = std::max(bounds.max_x, loc.get_x());
    bounds.max_y = std::max(bounds.max_y, loc.get_y());
    bounds.valid = true;
  }
  for (const auto& point : extras) {
    bounds.min_x = std::min(bounds.min_x, point.get_x());
    bounds.min_y = std::min(bounds.min_y, point.get_y());
    bounds.max_x = std::max(bounds.max_x, point.get_x());
    bounds.max_y = std::max(bounds.max_y, point.get_y());
    bounds.valid = true;
  }
  return bounds;
}

Bounds compute_bounds(const std::vector<icts::Pin*>& loads, const icts::Tree& tree)
{
  Bounds bounds;
  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, loc.get_x());
    bounds.min_y = std::min(bounds.min_y, loc.get_y());
    bounds.max_x = std::max(bounds.max_x, loc.get_x());
    bounds.max_y = std::max(bounds.max_y, loc.get_y());
    bounds.valid = true;
  }
  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    const auto& pos = node->get_position();
    if (pos.get_x() < 0 || pos.get_y() < 0) {
      continue;
    }
    bounds.min_x = std::min(bounds.min_x, pos.get_x());
    bounds.min_y = std::min(bounds.min_y, pos.get_y());
    bounds.max_x = std::max(bounds.max_x, pos.get_x());
    bounds.max_y = std::max(bounds.max_y, pos.get_y());
    bounds.valid = true;
  }
  return bounds;
}

SvgTransform make_transform(const Bounds& bounds)
{
  SvgTransform transform;
  if (!bounds.valid) {
    transform.width = 2 * transform.margin;
    transform.height = 2 * transform.margin;
    return transform;
  }

  const int width = std::max(1, bounds.max_x - bounds.min_x);
  const int height = std::max(1, bounds.max_y - bounds.min_y);
  const int canvas_max = 1000;
  const double scale = static_cast<double>(canvas_max) / std::max(width, height);

  transform.min_x = bounds.min_x;
  transform.max_y = bounds.max_y;
  transform.scale = scale;
  transform.width = static_cast<int>(std::lround(width * scale)) + 2 * transform.margin;
  transform.height = static_cast<int>(std::lround(height * scale)) + 2 * transform.margin;
  return transform;
}

template <typename Generator>
int sample_and_clamp(Generator& gen, std::normal_distribution<double>& dist, int lower, int upper)
{
  int value = static_cast<int>(std::lround(dist(gen)));
  return clamp_int(value, lower, upper);
}

GeneratedPins build_pins(std::vector<icts::Point<int>> points, int width, int height)
{
  GeneratedPins result;
  result.width = width;
  result.height = height;
  result.storage.reserve(points.size());
  result.loads.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    std::ostringstream name;
    name << "load_" << i;
    auto pin = std::make_unique<icts::Pin>(name.str(), icts::PinType::kClock, points[i]);
    result.loads.push_back(pin.get());
    result.storage.push_back(std::move(pin));
  }
  return result;
}

}  // namespace

GeneratedPins MakeNormal(std::size_t count, int width, int height, unsigned seed)
{
  if (count == 0 || width <= 0 || height <= 0) {
    return {};
  }

  std::mt19937 gen(seed);
  const double mean_x = width * 0.5;
  const double mean_y = height * 0.5;
  const double sigma_x = width * 0.18;
  const double sigma_y = height * 0.18;

  std::normal_distribution<double> dist_x(mean_x, sigma_x);
  std::normal_distribution<double> dist_y(mean_y, sigma_y);

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const int x = sample_and_clamp(gen, dist_x, 0, width);
    const int y = sample_and_clamp(gen, dist_y, 0, height);
    points.emplace_back(x, y);
  }
  return build_pins(std::move(points), width, height);
}

GeneratedPins MakeGaussianMixture(std::size_t count, int width, int height, unsigned seed)
{
  if (count == 0 || width <= 0 || height <= 0) {
    return {};
  }

  std::mt19937 gen(seed);
  const std::array<double, 3> weights = {0.5, 0.3, 0.2};
  std::discrete_distribution<int> pick(weights.begin(), weights.end());

  const std::array<icts::Point<double>, 3> centers
      = {icts::Point<double>(width * 0.25, height * 0.25), icts::Point<double>(width * 0.75, height * 0.35),
         icts::Point<double>(width * 0.55, height * 0.75)};
  const double sigma_x = width * 0.08;
  const double sigma_y = height * 0.08;

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const int cluster = pick(gen);
    std::normal_distribution<double> dist_x(centers[cluster].get_x(), sigma_x);
    std::normal_distribution<double> dist_y(centers[cluster].get_y(), sigma_y);
    const int x = sample_and_clamp(gen, dist_x, 0, width);
    const int y = sample_and_clamp(gen, dist_y, 0, height);
    points.emplace_back(x, y);
  }
  return build_pins(std::move(points), width, height);
}

GeneratedPins MakeWeightedQuadrants(std::size_t count, int width, int height, unsigned seed, const std::array<double, 4>& weights)
{
  if (count == 0 || width <= 0 || height <= 0) {
    return {};
  }

  std::mt19937 gen(seed);
  std::array<double, 4> safe_weights = weights;
  const double sum = safe_weights[0] + safe_weights[1] + safe_weights[2] + safe_weights[3];
  if (sum <= 0.0) {
    safe_weights = {1.0, 1.0, 1.0, 1.0};
  }
  std::discrete_distribution<int> pick(safe_weights.begin(), safe_weights.end());

  const int mid_x = width / 2;
  const int mid_y = height / 2;

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const int quadrant = pick(gen);
    int x_low = 0;
    int x_high = mid_x;
    int y_low = 0;
    int y_high = mid_y;
    switch (quadrant) {
      case 1:
        x_low = mid_x;
        x_high = width;
        y_low = 0;
        y_high = mid_y;
        break;
      case 2:
        x_low = 0;
        x_high = mid_x;
        y_low = mid_y;
        y_high = height;
        break;
      case 3:
        x_low = mid_x;
        x_high = width;
        y_low = mid_y;
        y_high = height;
        break;
      default:
        break;
    }
    std::uniform_int_distribution<int> dist_x(x_low, x_high);
    std::uniform_int_distribution<int> dist_y(y_low, y_high);
    points.emplace_back(dist_x(gen), dist_y(gen));
  }
  return build_pins(std::move(points), width, height);
}

bool AnalyzeTopology(const icts::Tree& tree, const std::vector<icts::Pin*>& loads, TopologyStats& stats,
                     std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                     std::string& error)
{
  stats = {};
  cluster_map.clear();
  centers.clear();

  if (tree.get_size() == 0) {
    error = "tree is empty";
    return false;
  }
  if (tree.get_root() == std::numeric_limits<std::size_t>::max()) {
    error = "tree root is invalid";
    return false;
  }

  stats.tree_size = tree.get_size();
  std::vector<std::size_t> leaf_ids;
  leaf_ids.reserve(tree.get_size());
  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    if (node->isLeaf()) {
      leaf_ids.push_back(id);
    }
  }

  if (leaf_ids.empty()) {
    error = "tree has no leaves";
    return false;
  }

  stats.leaf_count = leaf_ids.size();
  stats.min_leaf_load = std::numeric_limits<std::size_t>::max();
  stats.max_leaf_load = 0;
  std::size_t total_loads = 0;

  cluster_map.reserve(loads.size());
  centers.reserve(leaf_ids.size());

  for (std::size_t idx = 0; idx < leaf_ids.size(); ++idx) {
    const auto* node = tree.get_node(leaf_ids[idx]);
    if (node == nullptr) {
      continue;
    }
    const std::size_t load_count = node->get_loads().size();
    total_loads += load_count;
    if (load_count == 0) {
      ++stats.empty_leaf_count;
    }
    stats.min_leaf_load = std::min(stats.min_leaf_load, load_count);
    stats.max_leaf_load = std::max(stats.max_leaf_load, load_count);
    centers.push_back(node->get_position());

    for (const auto* pin : node->get_loads()) {
      cluster_map[pin] = idx;
    }
  }

  if (stats.min_leaf_load == std::numeric_limits<std::size_t>::max()) {
    stats.min_leaf_load = 0;
  }
  stats.avg_leaf_load = stats.leaf_count == 0 ? 0.0 : static_cast<double>(total_loads) / stats.leaf_count;

  if (total_loads != loads.size()) {
    std::ostringstream oss;
    oss << "load count mismatch: expected " << loads.size() << ", got " << total_loads;
    error = oss.str();
    return false;
  }

  if (cluster_map.size() != loads.size()) {
    std::ostringstream oss;
    oss << "cluster map size mismatch: expected " << loads.size() << ", got " << cluster_map.size();
    error = oss.str();
    return false;
  }
  return true;
}

namespace {
// Helper: recursively collect all loads under a node
void collect_loads_under_node(const icts::Tree& tree, std::size_t node_id, std::vector<icts::Pin*>& collected_loads)
{
  const auto* node = tree.get_node(node_id);
  if (node == nullptr) {
    return;
  }
  // Collect loads directly attached to this node
  for (auto* pin : node->get_loads()) {
    collected_loads.push_back(pin);
  }
  // Recursively collect from children
  for (auto child_id : node->get_children()) {
    if (child_id != std::numeric_limits<std::size_t>::max()) {
      collect_loads_under_node(tree, child_id, collected_loads);
    }
  }
}

// Helper: compute centroid of loads
icts::Point<int> compute_centroid(const std::vector<icts::Pin*>& loads)
{
  if (loads.empty()) {
    return icts::Point<int>(-1, -1);
  }
  long long sum_x = 0;
  long long sum_y = 0;
  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    sum_x += loc.get_x();
    sum_y += loc.get_y();
  }
  return icts::Point<int>(static_cast<int>(sum_x / static_cast<long long>(loads.size())),
                          static_cast<int>(sum_y / static_cast<long long>(loads.size())));
}
}  // namespace

bool AnalyzeFirstLevelClusters(const icts::Tree& tree, const std::vector<icts::Pin*>& loads,
                               std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                               std::string& error)
{
  cluster_map.clear();
  centers.clear();

  if (tree.get_size() == 0) {
    error = "tree is empty";
    return false;
  }
  const std::size_t root_id = tree.get_root();
  if (root_id == std::numeric_limits<std::size_t>::max()) {
    error = "tree root is invalid";
    return false;
  }

  const auto* root_node = tree.get_node(root_id);
  if (root_node == nullptr) {
    error = "root node is null";
    return false;
  }

  // Get first-level children (biPartition result)
  const auto& children = root_node->get_children();
  std::vector<std::size_t> valid_children;
  for (auto child_id : children) {
    if (child_id != std::numeric_limits<std::size_t>::max()) {
      valid_children.push_back(child_id);
    }
  }

  if (valid_children.empty()) {
    // No children, all loads under root
    for (const auto* pin : loads) {
      cluster_map[pin] = 0;
    }
    centers.push_back(compute_centroid(loads));
    return true;
  }

  // Collect loads for each first-level child
  cluster_map.reserve(loads.size());
  centers.reserve(valid_children.size());

  for (std::size_t idx = 0; idx < valid_children.size(); ++idx) {
    std::vector<icts::Pin*> child_loads;
    collect_loads_under_node(tree, valid_children[idx], child_loads);

    for (const auto* pin : child_loads) {
      cluster_map[pin] = idx;
    }
    centers.push_back(compute_centroid(child_loads));
  }

  // Verify all loads are accounted for
  if (cluster_map.size() != loads.size()) {
    std::ostringstream oss;
    oss << "first-level cluster map size mismatch: expected " << loads.size() << ", got " << cluster_map.size();
    error = oss.str();
    return false;
  }

  return true;
}

bool WriteClusterSvg(const std::string& path, const std::vector<icts::Pin*>& loads,
                     const std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, const std::vector<icts::Point<int>>& centers)
{
  if (loads.empty()) {
    return false;
  }

  const auto bounds = compute_bounds(loads, centers);
  const auto transform = make_transform(bounds);

  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    return false;
  }

  ofs << std::fixed << std::setprecision(2);
  ofs << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << transform.width << "\" height=\"" << transform.height
      << "\" viewBox=\"0 0 " << transform.width << " " << transform.height << "\">\n";
  ofs << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" />\n";

  for (const auto* pin : loads) {
    const auto it = cluster_map.find(pin);
    const std::size_t cluster_id = (it == cluster_map.end()) ? 0 : it->second;
    const auto& loc = pin->get_location();
    ofs << "<circle cx=\"" << transform.map_x(loc.get_x()) << "\" cy=\"" << transform.map_y(loc.get_y()) << "\" r=\"2\" fill=\""
        << kPalette[cluster_id % kPalette.size()] << "\" fill-opacity=\"0.8\" />\n";
  }

  for (std::size_t i = 0; i < centers.size(); ++i) {
    const auto& center = centers[i];
    const double cx = transform.map_x(center.get_x());
    const double cy = transform.map_y(center.get_y());
    ofs << "<circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"5\" fill=\"none\" stroke=\"" << kPalette[i % kPalette.size()]
        << "\" stroke-width=\"2\" />\n";
    ofs << "<line x1=\"" << (cx - 6) << "\" y1=\"" << cy << "\" x2=\"" << (cx + 6) << "\" y2=\"" << cy
        << "\" stroke=\"#222222\" stroke-width=\"1\" />\n";
    ofs << "<line x1=\"" << cx << "\" y1=\"" << (cy - 6) << "\" x2=\"" << cx << "\" y2=\"" << (cy + 6)
        << "\" stroke=\"#222222\" stroke-width=\"1\" />\n";
  }

  ofs << "</svg>\n";
  return true;
}

bool WriteTopologySvg(const std::string& path, const icts::Tree& tree, const std::vector<icts::Pin*>& loads)
{
  if (tree.get_size() == 0) {
    return false;
  }
  const auto bounds = compute_bounds(loads, tree);
  const auto transform = make_transform(bounds);

  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    return false;
  }

  ofs << std::fixed << std::setprecision(2);
  ofs << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << transform.width << "\" height=\"" << transform.height
      << "\" viewBox=\"0 0 " << transform.width << " " << transform.height << "\">\n";
  ofs << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" />\n";

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    if (node->get_parent() == std::numeric_limits<std::size_t>::max()) {
      continue;
    }
    const auto* parent = tree.get_node(node->get_parent());
    if (parent == nullptr) {
      continue;
    }
    const auto& src = node->get_position();
    const auto& dst = parent->get_position();
    if (src.get_x() < 0 || src.get_y() < 0 || dst.get_x() < 0 || dst.get_y() < 0) {
      continue;
    }
    ofs << "<line x1=\"" << transform.map_x(src.get_x()) << "\" y1=\"" << transform.map_y(src.get_y()) << "\" x2=\""
        << transform.map_x(dst.get_x()) << "\" y2=\"" << transform.map_y(dst.get_y()) << "\" stroke=\"#666666\" stroke-width=\"1\" />\n";
  }

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    const auto& pos = node->get_position();
    if (pos.get_x() < 0 || pos.get_y() < 0) {
      continue;
    }
    const bool is_root = node->get_parent() == std::numeric_limits<std::size_t>::max();
    ofs << "<circle cx=\"" << transform.map_x(pos.get_x()) << "\" cy=\"" << transform.map_y(pos.get_y()) << "\" r=\"" << (is_root ? 5 : 3)
        << "\" fill=\"" << (is_root ? "#d62728" : "#444444") << "\" />\n";
  }

  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    ofs << "<circle cx=\"" << transform.map_x(loc.get_x()) << "\" cy=\"" << transform.map_y(loc.get_y()) << "\" r=\"2\" fill=\"#1f77b4\""
        << " fill-opacity=\"0.6\" />\n";
  }

  ofs << "</svg>\n";
  return true;
}

std::filesystem::path ResolveOutputDir()
{
  const char* env_dir = std::getenv("ICTS_TEST_OUTPUT_DIR");
  if (env_dir != nullptr && env_dir[0] != '\0') {
    return std::filesystem::path(env_dir);
  }
  return std::filesystem::path("icts_test_output");
}

}  // namespace icts_test
