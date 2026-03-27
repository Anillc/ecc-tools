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
#include <memory>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "database/spatial/Tree.hh"

namespace icts_test {
namespace {

constexpr int kInvalidCoord = -1;
constexpr int kCanvasMax = 1000;
constexpr int kSvgMargin = 20;
constexpr int kClusterCenterRadius = 5;
constexpr int kRootRadius = 5;
constexpr int kNodeRadius = 3;
constexpr int kCenterCrossHalfSize = 6;
constexpr int kLoadRadius = 2;
constexpr const char* kSvgOpenTagPrefix = R"(<svg xmlns="http://www.w3.org/2000/svg" canvas.width=")";
constexpr const char* kSvgHeightTag = R"(" height=")";
constexpr const char* kSvgViewBoxPrefix = R"(" viewBox="0 0 )";
constexpr const char* kSvgOpenTagSuffix = R"(">
)";
constexpr const char* kSvgBackgroundRect = R"(<rect canvas.width="100%" height="100%" fill="#ffffff" />
)";
constexpr const char* kSvgClosingTag = R"(</svg>
)";
constexpr const char* kClusterCenterOpenTagPrefix = R"(<circle cx=")";
constexpr const char* kCircleCenterYTag = R"(" cy=")";
constexpr const char* kCircleRadiusTag = R"(" r=")";
constexpr const char* kCircleStrokeTag = R"(" fill="none" stroke=")";
constexpr const char* kCircleStrokeWidthTag = R"(" stroke-canvas.width="2" />
)";
constexpr double kNormalMeanRatio = 0.5;
constexpr double kNormalSigmaRatio = 0.18;
constexpr std::array<double, 3> kGaussianMixtureWeights = {0.5, 0.3, 0.2};
constexpr double kFirstCenterXRatio = 0.25;
constexpr double kFirstCenterYRatio = 0.25;
constexpr double kSecondCenterXRatio = 0.75;
constexpr double kSecondCenterYRatio = 0.35;
constexpr double kThirdCenterXRatio = 0.55;
constexpr double kThirdCenterYRatio = 0.75;
constexpr double kMixtureSigmaRatio = 0.08;
constexpr std::size_t kInvalidNodeId = std::numeric_limits<std::size_t>::max();

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
  int margin = kSvgMargin;
  int width = 0;
  int height = 0;
};

const std::array<const char*, 12> kPalette
    = {"#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf", "#4c78a8", "#f58518"};

[[nodiscard]] auto map_x(const SvgTransform& transform, int x_coord) -> double
{
  return ((x_coord - transform.min_x) * transform.scale) + transform.margin;
}

[[nodiscard]] auto map_y(const SvgTransform& transform, int y_coord) -> double
{
  return ((transform.max_y - y_coord) * transform.scale) + transform.margin;
}

auto clamp_int(int value, int lower, int upper) -> int
{
  return std::max(lower, std::min(value, upper));
}

auto compute_bounds(const std::vector<icts::Pin*>& loads, const std::vector<icts::Point<int>>& extras) -> Bounds
{
  Bounds bounds;
  for (const auto* pin : loads) {
    const auto& location = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, location.get_x());
    bounds.min_y = std::min(bounds.min_y, location.get_y());
    bounds.max_x = std::max(bounds.max_x, location.get_x());
    bounds.max_y = std::max(bounds.max_y, location.get_y());
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

auto compute_bounds(const std::vector<icts::Pin*>& loads, const icts::Tree& tree) -> Bounds
{
  Bounds bounds;
  for (const auto* pin : loads) {
    const auto& location = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, location.get_x());
    bounds.min_y = std::min(bounds.min_y, location.get_y());
    bounds.max_x = std::max(bounds.max_x, location.get_x());
    bounds.max_y = std::max(bounds.max_y, location.get_y());
    bounds.valid = true;
  }
  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    const auto& position = node->get_position();
    if (position.get_x() < 0 || position.get_y() < 0) {
      continue;
    }
    bounds.min_x = std::min(bounds.min_x, position.get_x());
    bounds.min_y = std::min(bounds.min_y, position.get_y());
    bounds.max_x = std::max(bounds.max_x, position.get_x());
    bounds.max_y = std::max(bounds.max_y, position.get_y());
    bounds.valid = true;
  }
  return bounds;
}

auto make_transform(const Bounds& bounds) -> SvgTransform
{
  SvgTransform transform;
  if (!bounds.valid) {
    transform.width = 2 * transform.margin;
    transform.height = 2 * transform.margin;
    return transform;
  }

  const int width = std::max(1, bounds.max_x - bounds.min_x);
  const int height = std::max(1, bounds.max_y - bounds.min_y);
  const double scale = static_cast<double>(kCanvasMax) / static_cast<double>(std::max(width, height));

  transform.min_x = bounds.min_x;
  transform.max_y = bounds.max_y;
  transform.scale = scale;
  transform.width = static_cast<int>(std::lround(width * scale)) + (2 * transform.margin);
  transform.height = static_cast<int>(std::lround(height * scale)) + (2 * transform.margin);
  return transform;
}

template <typename Generator>
auto sample_and_clamp(Generator& generator, std::normal_distribution<double>& distribution, int lower, int upper) -> int
{
  const int sampled_value = static_cast<int>(std::lround(distribution(generator)));
  return clamp_int(sampled_value, lower, upper);
}

auto build_pins(std::vector<icts::Point<int>> points, CanvasSize canvas) -> GeneratedPins
{
  GeneratedPins result;
  result.width = canvas.width;
  result.height = canvas.height;
  result.storage.reserve(points.size());
  result.loads.reserve(points.size());
  for (std::size_t index = 0; index < points.size(); ++index) {
    std::ostringstream name;
    name << "load_" << index;
    auto pin = std::make_unique<icts::Pin>(name.str(), icts::PinType::kClock, points.at(index));
    result.loads.push_back(pin.get());
    result.storage.push_back(std::move(pin));
  }
  return result;
}

}  // namespace

auto MakeNormal(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins
{
  if (count == 0 || canvas.width <= 0 || canvas.height <= 0) {
    return {};
  }

  std::mt19937 generator(seed);
  const double mean_x = static_cast<double>(canvas.width) * kNormalMeanRatio;
  const double mean_y = static_cast<double>(canvas.height) * kNormalMeanRatio;
  const double sigma_x = static_cast<double>(canvas.width) * kNormalSigmaRatio;
  const double sigma_y = static_cast<double>(canvas.height) * kNormalSigmaRatio;

  std::normal_distribution<double> dist_x(mean_x, sigma_x);
  std::normal_distribution<double> dist_y(mean_y, sigma_y);

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const int x_coord = sample_and_clamp(generator, dist_x, 0, canvas.width);
    const int y_coord = sample_and_clamp(generator, dist_y, 0, canvas.height);
    points.emplace_back(x_coord, y_coord);
  }
  return build_pins(std::move(points), canvas);
}

auto MakeGaussianMixture(std::size_t count, CanvasSize canvas, unsigned seed) -> GeneratedPins
{
  if (count == 0 || canvas.width <= 0 || canvas.height <= 0) {
    return {};
  }

  std::mt19937 generator(seed);
  std::discrete_distribution<int> pick(kGaussianMixtureWeights.begin(), kGaussianMixtureWeights.end());

  const std::array<icts::Point<double>, 3> centers = {
      icts::Point<double>(static_cast<double>(canvas.width) * kFirstCenterXRatio, static_cast<double>(canvas.height) * kFirstCenterYRatio),
      icts::Point<double>(static_cast<double>(canvas.width) * kSecondCenterXRatio,
                          static_cast<double>(canvas.height) * kSecondCenterYRatio),
      icts::Point<double>(static_cast<double>(canvas.width) * kThirdCenterXRatio, static_cast<double>(canvas.height) * kThirdCenterYRatio)};
  const double sigma_x = static_cast<double>(canvas.width) * kMixtureSigmaRatio;
  const double sigma_y = static_cast<double>(canvas.height) * kMixtureSigmaRatio;

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto cluster_index = static_cast<std::size_t>(pick(generator));
    const auto& center = centers.at(cluster_index);
    std::normal_distribution<double> dist_x(center.get_x(), sigma_x);
    std::normal_distribution<double> dist_y(center.get_y(), sigma_y);
    const int x_coord = sample_and_clamp(generator, dist_x, 0, canvas.width);
    const int y_coord = sample_and_clamp(generator, dist_y, 0, canvas.height);
    points.emplace_back(x_coord, y_coord);
  }
  return build_pins(std::move(points), canvas);
}

auto MakeWeightedQuadrants(std::size_t count, CanvasSize canvas, unsigned seed, const std::array<double, 4>& weights) -> GeneratedPins
{
  if (count == 0 || canvas.width <= 0 || canvas.height <= 0) {
    return {};
  }

  std::mt19937 generator(seed);
  std::array<double, 4> safe_weights = weights;
  const double sum = safe_weights.at(0) + safe_weights.at(1) + safe_weights.at(2) + safe_weights.at(3);
  if (sum <= 0.0) {
    safe_weights = {1.0, 1.0, 1.0, 1.0};
  }
  std::discrete_distribution<int> pick(safe_weights.begin(), safe_weights.end());

  const int mid_x = canvas.width / 2;
  const int mid_y = canvas.height / 2;

  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const int quadrant = pick(generator);
    int x_low = 0;
    int x_high = mid_x;
    int y_low = 0;
    int y_high = mid_y;
    switch (quadrant) {
      case 1:
        x_low = mid_x;
        x_high = canvas.width;
        y_low = 0;
        y_high = mid_y;
        break;
      case 2:
        x_low = 0;
        x_high = mid_x;
        y_low = mid_y;
        y_high = canvas.height;
        break;
      case 3:
        x_low = mid_x;
        x_high = canvas.width;
        y_low = mid_y;
        y_high = canvas.height;
        break;
      default:
        break;
    }
    std::uniform_int_distribution<int> dist_x(x_low, x_high);
    std::uniform_int_distribution<int> dist_y(y_low, y_high);
    points.emplace_back(dist_x(generator), dist_y(generator));
  }
  return build_pins(std::move(points), canvas);
}

auto AnalyzeTopology(const icts::Tree& tree, const std::vector<icts::Pin*>& loads, TopologyStats& stats,
                     std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                     std::string& error) -> bool
{
  stats = {};
  cluster_map.clear();
  centers.clear();

  if (tree.get_size() == 0) {
    error = "tree is empty";
    return false;
  }
  if (tree.get_root() == kInvalidNodeId) {
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

  for (std::size_t index = 0; index < leaf_ids.size(); ++index) {
    const auto* node = tree.get_node(leaf_ids.at(index));
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
      cluster_map[pin] = index;
    }
  }

  if (stats.min_leaf_load == std::numeric_limits<std::size_t>::max()) {
    stats.min_leaf_load = 0;
  }
  stats.avg_leaf_load = stats.leaf_count == 0 ? 0.0 : static_cast<double>(total_loads) / static_cast<double>(stats.leaf_count);

  if (total_loads != loads.size()) {
    std::ostringstream stream;
    stream << "load count mismatch: expected " << loads.size() << ", got " << total_loads;
    error = stream.str();
    return false;
  }

  if (cluster_map.size() != loads.size()) {
    std::ostringstream stream;
    stream << "cluster map size mismatch: expected " << loads.size() << ", got " << cluster_map.size();
    error = stream.str();
    return false;
  }
  return true;
}

namespace {
// Helper: collect all loads under a node
auto collect_loads_under_node(const icts::Tree& tree, std::size_t node_id, std::vector<icts::Pin*>& collected_loads) -> void
{
  std::queue<std::size_t> pending_nodes;
  pending_nodes.push(node_id);

  while (!pending_nodes.empty()) {
    const std::size_t current_node_id = pending_nodes.front();
    pending_nodes.pop();

    const auto* node = tree.get_node(current_node_id);
    if (node == nullptr) {
      continue;
    }

    for (auto* pin : node->get_loads()) {
      collected_loads.push_back(pin);
    }
    for (auto child_id : node->get_children()) {
      if (child_id != kInvalidNodeId) {
        pending_nodes.push(child_id);
      }
    }
  }
}

// Helper: compute centroid of loads
auto compute_centroid(const std::vector<icts::Pin*>& loads) -> icts::Point<int>
{
  if (loads.empty()) {
    return {kInvalidCoord, kInvalidCoord};
  }
  long long sum_x = 0;
  long long sum_y = 0;
  for (const auto* pin : loads) {
    const auto& location = pin->get_location();
    sum_x += location.get_x();
    sum_y += location.get_y();
  }
  return {static_cast<int>(sum_x / static_cast<long long>(loads.size())), static_cast<int>(sum_y / static_cast<long long>(loads.size()))};
}
}  // namespace

auto AnalyzeFirstLevelClusters(const icts::Tree& tree, const std::vector<icts::Pin*>& loads,
                               std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                               std::string& error) -> bool
{
  cluster_map.clear();
  centers.clear();

  if (tree.get_size() == 0) {
    error = "tree is empty";
    return false;
  }
  const std::size_t root_id = tree.get_root();
  if (root_id == kInvalidNodeId) {
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
    if (child_id != kInvalidNodeId) {
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

  for (std::size_t index = 0; index < valid_children.size(); ++index) {
    std::vector<icts::Pin*> child_loads;
    collect_loads_under_node(tree, valid_children.at(index), child_loads);

    for (const auto* pin : child_loads) {
      cluster_map[pin] = index;
    }
    centers.push_back(compute_centroid(child_loads));
  }

  // Verify all loads are accounted for
  if (cluster_map.size() != loads.size()) {
    std::ostringstream stream;
    stream << "first-level cluster map size mismatch: expected " << loads.size() << ", got " << cluster_map.size();
    error = stream.str();
    return false;
  }

  return true;
}

auto WriteClusterSvg(const std::string& path, const std::vector<icts::Pin*>& loads,
                     const std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, const std::vector<icts::Point<int>>& centers)
    -> bool
{
  if (loads.empty()) {
    return false;
  }

  const auto bounds = compute_bounds(loads, centers);
  const auto transform = make_transform(bounds);

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }

  output_stream << std::fixed << std::setprecision(2);
  output_stream << kSvgOpenTagPrefix << transform.width << kSvgHeightTag << transform.height << kSvgViewBoxPrefix << transform.width << ' '
                << transform.height << kSvgOpenTagSuffix;
  output_stream << kSvgBackgroundRect;

  for (const auto* pin : loads) {
    const auto cluster_itr = cluster_map.find(pin);
    const std::size_t cluster_id = cluster_itr == cluster_map.end() ? 0 : cluster_itr->second;
    const auto& location = pin->get_location();
    output_stream << "<circle cx=\"" << map_x(transform, location.get_x()) << "\" cy=\"" << map_y(transform, location.get_y()) << "\" r=\""
                  << kLoadRadius << "\" fill=\"" << kPalette.at(cluster_id % kPalette.size()) << "\" fill-opacity=\"0.8\" />\n";
  }

  for (std::size_t index = 0; index < centers.size(); ++index) {
    const auto& center = centers.at(index);
    const double center_x = map_x(transform, center.get_x());
    const double center_y = map_y(transform, center.get_y());
    output_stream << kClusterCenterOpenTagPrefix << center_x << kCircleCenterYTag << center_y << kCircleRadiusTag << kClusterCenterRadius
                  << kCircleStrokeTag << kPalette.at(index % kPalette.size()) << kCircleStrokeWidthTag;
    output_stream << "<line x1=\"" << (center_x - kCenterCrossHalfSize) << "\" y1=\"" << center_y << "\" x2=\""
                  << (center_x + kCenterCrossHalfSize) << "\" y2=\"" << center_y << "\" stroke=\"#222222\" stroke-canvas.width=\"1\" />\n";
    output_stream << "<line x1=\"" << center_x << "\" y1=\"" << (center_y - kCenterCrossHalfSize) << "\" x2=\"" << center_x << "\" y2=\""
                  << (center_y + kCenterCrossHalfSize) << "\" stroke=\"#222222\" stroke-canvas.width=\"1\" />\n";
  }

  output_stream << kSvgClosingTag;
  return true;
}

auto WriteTopologySvg(const std::string& path, const icts::Tree& tree, const std::vector<icts::Pin*>& loads) -> bool
{
  if (tree.get_size() == 0) {
    return false;
  }
  const auto bounds = compute_bounds(loads, tree);
  const auto transform = make_transform(bounds);

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }

  output_stream << std::fixed << std::setprecision(2);
  output_stream << kSvgOpenTagPrefix << transform.width << kSvgHeightTag << transform.height << kSvgViewBoxPrefix << transform.width << ' '
                << transform.height << kSvgOpenTagSuffix;
  output_stream << kSvgBackgroundRect;

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    if (node->get_parent() == kInvalidNodeId) {
      continue;
    }
    const auto* parent = tree.get_node(node->get_parent());
    if (parent == nullptr) {
      continue;
    }
    const auto& source = node->get_position();
    const auto& target = parent->get_position();
    if (source.get_x() < 0 || source.get_y() < 0 || target.get_x() < 0 || target.get_y() < 0) {
      continue;
    }
    output_stream << "<line x1=\"" << map_x(transform, source.get_x()) << "\" y1=\"" << map_y(transform, source.get_y()) << "\" x2=\""
                  << map_x(transform, target.get_x()) << "\" y2=\"" << map_y(transform, target.get_y())
                  << R"(" stroke="#666666" stroke-canvas.width="1" />
)";
  }

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr) {
      continue;
    }
    const auto& position = node->get_position();
    if (position.get_x() < 0 || position.get_y() < 0) {
      continue;
    }
    const bool is_root = node->get_parent() == kInvalidNodeId;
    output_stream << "<circle cx=\"" << map_x(transform, position.get_x()) << "\" cy=\"" << map_y(transform, position.get_y()) << "\" r=\""
                  << (is_root ? kRootRadius : kNodeRadius) << "\" fill=\"" << (is_root ? "#d62728" : "#444444") << R"(" />
)";
  }

  for (const auto* pin : loads) {
    const auto& location = pin->get_location();
    output_stream << "<circle cx=\"" << map_x(transform, location.get_x()) << "\" cy=\"" << map_y(transform, location.get_y()) << "\" r=\""
                  << kLoadRadius << R"(" fill="#1f77b4" fill-opacity="0.6" />
)";
  }

  output_stream << kSvgClosingTag;
  return true;
}

auto ResolveOutputDir() -> std::filesystem::path
{
  const char* env_dir = std::getenv("ICTS_TEST_OUTPUT_DIR");
  if (env_dir != nullptr && *env_dir != '\0') {
    return {env_dir};
  }
  return {"icts_test_output"};
}

}  // namespace icts_test
