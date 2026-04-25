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
 * @file TopologyGen.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Topology generator for CTS.
 */

#include "TopologyGen.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "Log.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Schema.hh"
#include "Tree.hh"
#include "clustering/Clustering.hh"
#include "config/TopologyConfig.hh"
#include "fast_clustering/FastClustering.hh"

namespace icts {
namespace {

struct LoadBounds
{
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
};

auto CalcLoadBounds(const std::vector<Pin*>& loads) -> LoadBounds
{
  LoadBounds bounds;
  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    bounds.min_x = std::min(bounds.min_x, loc.get_x());
    bounds.min_y = std::min(bounds.min_y, loc.get_y());
    bounds.max_x = std::max(bounds.max_x, loc.get_x());
    bounds.max_y = std::max(bounds.max_y, loc.get_y());
  }
  if (loads.empty()) {
    bounds.min_x = 0;
    bounds.min_y = 0;
    bounds.max_x = 0;
    bounds.max_y = 0;
  }
  return bounds;
}

auto FormatFixed(double value, int precision = 2) -> std::string
{
  std::ostringstream output_stream;
  output_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  output_stream.precision(precision);
  output_stream << value;
  return output_stream.str();
}

auto FormatPoint(int x, int y) -> std::string
{
  return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

auto FormatPoint(double x, double y) -> std::string
{
  return "(" + FormatFixed(x, 2) + ", " + FormatFixed(y, 2) + ")";
}

}  // namespace

auto TopologyGen::build(const std::vector<Pin*>& loads) -> Tree
{
  return build(loads, BuildOptions{});
}

auto TopologyGen::build(const std::vector<Pin*>& loads, const BuildOptions& options) -> Tree
{
  return build(loads, options.partition_config, options.target_depth);
}

auto TopologyGen::buildFastClusteringElectricalConfig(std::size_t max_fanout, double max_cap) -> ClusterConfig
{
  return FastClustering::buildElectricalBaseConfig(max_fanout, max_cap);
}

auto TopologyGen::fastClustering(const std::vector<Pin*>& loads) -> ClusterResult
{
  return defaultFastClustering(loads, ClusterConfig{});
}

auto TopologyGen::defaultFastClustering(const std::vector<Pin*>& loads, const ClusterConfig& base_config) -> ClusterResult
{
  return Clustering::defaultFastClustering(loads, base_config);
}

auto TopologyGen::fastClustering(const std::vector<Pin*>& loads, const ClusterConfig& config) -> ClusterResult
{
  return Clustering::fastClustering(loads, config);
}

auto TopologyGen::build(const std::vector<Pin*>& loads, const BiPartitionConfig& config) -> Tree
{
  return build(loads, BuildOptions{.partition_config = config});
}

auto TopologyGen::build(const std::vector<Pin*>& loads, const BiPartitionConfig& config, std::optional<unsigned> target_depth) -> Tree
{
  Tree tree;
  schema::ScopedStage build_stage("TopologyGen", "Build H-tree topology for " + std::to_string(loads.size()) + " loads");
  if (loads.empty()) {
    LOG_WARNING << "Topology generation skipped: no loads.";
    build_stage.skip({{"load_count", "0"}});
    return tree;
  }

  reportLoadDistribution(loads);
  const auto bounds = CalcLoadBounds(loads);

  std::size_t leaf_count = calcLeafCount(loads.size());
  const unsigned max_depth = calcMaxDepth(loads.size());
  if (target_depth.has_value()) {
    const unsigned resolved_depth = std::min(*target_depth, max_depth);
    leaf_count = resolved_depth == 0U ? 1U : (std::size_t{1} << resolved_depth);
  }
  if (leaf_count == 0) {
    LOG_WARNING << "Topology generation skipped: leaf count is zero.";
    build_stage.skip({{"leaf_count", "0"}});
    return tree;
  }

  const auto root = tree.create_node();
  tree.set_root(root);
  tree.get_node(root)->get_position() = geometry::CalcMedian(loads, [](Pin* pin) -> auto { return pin->get_location(); });

  int height = 0;
  for (std::size_t count = leaf_count; count > 1; count >>= 1) {
    ++height;
  }

  build_stage.markRunning("Embed coordinates and balance topology");
  buildFullTree(tree, BuildCursor{.node_id = root, .depth = 0}, height);
  embedPositions(tree, root, loads, leaf_count, config);
  balanceTopology(tree, bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y, config.htree_topology_tolerance);
  reportRootToLeafLengths(tree);
  build_stage.finish({
      {"nodes", std::to_string(tree.get_size())},
      {"depth", std::to_string(height)},
      {"leaf_count", std::to_string(leaf_count)},
  });

  return tree;
}

auto TopologyGen::reportLoadDistribution(const std::vector<Pin*>& loads) -> void
{
  if (loads.empty()) {
    LOG_WARNING << "Load distribution: empty load list.";
    return;
  }

  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();

  for (const auto* pin : loads) {
    const auto& loc = pin->get_location();
    min_x = std::min(min_x, loc.get_x());
    min_y = std::min(min_y, loc.get_y());
    max_x = std::max(max_x, loc.get_x());
    max_y = std::max(max_y, loc.get_y());
  }

  const int width = max_x - min_x;
  const int height = max_y - min_y;
  const double core_area = static_cast<double>(width) * height;
  const double core_length = std::sqrt(std::max(0.0, core_area));
  const int half_perimeter = (width + height) / 2;

  const auto center = geometry::CalcCenter(loads, [](Pin* pin) -> auto { return pin->get_location(); });
  const auto median = geometry::CalcMedian(loads, [](Pin* pin) -> auto { return pin->get_location(); });

  schema::EmitKeyValueTable("TopologyGen Load Distribution Summary",
                            {
                                {"load_count", std::to_string(loads.size())},
                                {"bbox_min", FormatPoint(min_x, min_y)},
                                {"bbox_max", FormatPoint(max_x, max_y)},
                                {"span_width_height", std::to_string(width) + " x " + std::to_string(height) + " DBU"},
                                {"area", FormatFixed(core_area, 2) + " DBU^2"},
                                {"sqrt_area", FormatFixed(core_length, 2) + " DBU"},
                                {"half_perimeter", std::to_string(half_perimeter) + " DBU"},
                                {"center", FormatPoint(center.get_x(), center.get_y())},
                                {"median", FormatPoint(median.get_x(), median.get_y())},
                            });
}

auto TopologyGen::reportRootToLeafLengths(const Tree& tree) -> void
{
  if (tree.get_size() == 0 || tree.get_root() == std::numeric_limits<std::size_t>::max()) {
    LOG_WARNING << "Topology length report skipped: invalid tree.";
    return;
  }

  int min_len = std::numeric_limits<int>::max();
  int max_len = 0;
  int sum_len = 0;
  std::size_t leaf_count = 0;
  std::size_t invalid_count = 0;

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr || !node->isLeaf()) {
      continue;
    }
    int length = 0;
    bool valid = true;
    std::size_t cur = id;
    while (cur != tree.get_root()) {
      const auto* cur_node = tree.get_node(cur);
      if (cur_node == nullptr) {
        valid = false;
        break;
      }
      const auto parent_id = cur_node->get_parent();
      if (parent_id == std::numeric_limits<std::size_t>::max()) {
        valid = false;
        break;
      }
      const auto* parent = tree.get_node(parent_id);
      if (parent == nullptr) {
        valid = false;
        break;
      }
      length += geometry::Manhattan(cur_node->get_position(), parent->get_position());
      cur = parent_id;
    }

    if (!valid) {
      ++invalid_count;
      continue;
    }

    ++leaf_count;
    sum_len += length;
    min_len = std::min(min_len, length);
    max_len = std::max(max_len, length);
  }

  if (leaf_count == 0) {
    LOG_WARNING << "Topology length report skipped: no valid leaf paths.";
    return;
  }

  const double avg_len = static_cast<double>(sum_len) / static_cast<double>(leaf_count);
  schema::EmitKeyValueTable("TopologyGen Root-To-Leaf Path Summary", {
                                                                         {"min_path_length", std::to_string(min_len) + " DBU"},
                                                                         {"max_path_length", std::to_string(max_len) + " DBU"},
                                                                         {"avg_path_length", FormatFixed(avg_len, 2) + " DBU"},
                                                                         {"valid_leaf_paths", std::to_string(leaf_count)},
                                                                         {"invalid_leaf_paths", std::to_string(invalid_count)},
                                                                     });
}

auto TopologyGen::calcMaxDepth(std::size_t load_count) -> unsigned
{
  unsigned depth = 0U;
  for (std::size_t leaf_count = calcLeafCount(load_count); leaf_count > 1U; leaf_count >>= 1U) {
    ++depth;
  }
  return depth;
}

auto TopologyGen::calcLeafCount(std::size_t load_count) -> std::size_t
{
  if (load_count == 0) {
    return 0;
  }
  std::size_t leaf_count = 1;
  while ((leaf_count << 1) <= load_count) {
    leaf_count <<= 1;
  }
  return leaf_count;
}

auto TopologyGen::buildFullTree(Tree& tree, const BuildCursor& cursor, int height) -> void
{
  std::vector<BuildCursor> build_stack;
  build_stack.push_back(cursor);

  while (!build_stack.empty()) {
    auto current = build_stack.back();
    build_stack.pop_back();

    if (current.depth >= height) {
      continue;
    }

    const auto left = tree.add_child(current.node_id, 0);
    const auto right = tree.add_child(current.node_id, 1);
    build_stack.push_back(BuildCursor{.node_id = right, .depth = current.depth + 1});
    build_stack.push_back(BuildCursor{.node_id = left, .depth = current.depth + 1});
  }
}

auto TopologyGen::embedPositions(Tree& tree, std::size_t node, const std::vector<Pin*>& loads, std::size_t leaf_need,
                                 const BiPartitionConfig& config) -> void
{
  struct EmbedFrame
  {
    std::size_t node_id = 0;
    std::vector<Pin*> node_loads;
    std::size_t node_leaf_need = 0;
  };

  std::vector<EmbedFrame> embed_stack;
  embed_stack.push_back(EmbedFrame{.node_id = node, .node_loads = loads, .node_leaf_need = leaf_need});

  while (!embed_stack.empty()) {
    auto frame = std::move(embed_stack.back());
    embed_stack.pop_back();

    auto* node_ptr = tree.get_node(frame.node_id);
    if (node_ptr == nullptr) {
      continue;
    }
    node_ptr->get_loads() = frame.node_loads;
    if (frame.node_loads.empty()) {
      continue;
    }

    if (node_ptr->isLeaf() || frame.node_leaf_need <= 1 || frame.node_loads.size() <= 1) {
      const auto center = geometry::CalcCenter(frame.node_loads, [](Pin* pin) -> auto { return pin->get_location(); });
      node_ptr->get_position() = Point<int>(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
      continue;
    }

    const std::size_t child_leaf_need = frame.node_leaf_need / 2;
    auto result = Clustering::biPartition(frame.node_loads, child_leaf_need, config);
    if (result.clusters.size() < 2) {
      continue;
    }

    const auto& children = node_ptr->get_children();
    if (children.size() < 2 || children.at(0) == std::numeric_limits<std::size_t>::max()
        || children.at(1) == std::numeric_limits<std::size_t>::max()) {
      continue;
    }

    auto* left = tree.get_node(children.at(0));
    auto* right = tree.get_node(children.at(1));
    if (left == nullptr || right == nullptr) {
      continue;
    }

    if (result.centers.size() >= 2) {
      left->get_position() = result.centers.at(0);
      right->get_position() = result.centers.at(1);
    }

    embed_stack.push_back(EmbedFrame{.node_id = children.at(1), .node_loads = result.clusters.at(1), .node_leaf_need = child_leaf_need});
    embed_stack.push_back(EmbedFrame{.node_id = children.at(0), .node_loads = result.clusters.at(0), .node_leaf_need = child_leaf_need});
  }
}

auto TopologyGen::balanceTopology(Tree& tree, int min_x, int min_y, int max_x, int max_y, double topology_tolerance) -> void
{
  auto levels = tree.levels();
  if (levels.size() <= 1) {
    return;
  }

  topology_tolerance = std::max(0.0, topology_tolerance);

  for (std::size_t level = 1; level < levels.size(); ++level) {
    int sum_dist = 0;
    std::size_t count = 0;
    for (const auto node_id : levels.at(level)) {
      auto* node = tree.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* parent = tree.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }
      sum_dist += geometry::Manhattan(node->get_position(), parent->get_position());
      ++count;
    }
    if (count == 0) {
      continue;
    }
    const double avg_dist = static_cast<double>(sum_dist) / static_cast<double>(count);
    const double min_allowed_dist = avg_dist * std::max(0.0, 1.0 - topology_tolerance);
    const double max_allowed_dist = avg_dist * (1.0 + topology_tolerance);
    for (const auto node_id : levels.at(level)) {
      auto* node = tree.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* parent = tree.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }
      const auto current_dist = static_cast<double>(geometry::Manhattan(node->get_position(), parent->get_position()));
      if (current_dist >= min_allowed_dist && current_dist <= max_allowed_dist) {
        continue;
      }
      const double target_dist = current_dist < min_allowed_dist ? min_allowed_dist : max_allowed_dist;
      node->get_position()
          = geometry::ProjectToL1Circle(parent->get_position(), node->get_position(), target_dist, min_x, min_y, max_x, max_y);
    }
  }
}

}  // namespace icts
