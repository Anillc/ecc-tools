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

#include <algorithm>
#include <cmath>
#include <limits>

#include "utils/geometry/Geometry.hh"
#include "utils/logger/Logger.hh"

namespace icts {
namespace {

struct LoadBounds
{
  int min_x = std::numeric_limits<int>::max();
  int min_y = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int max_y = std::numeric_limits<int>::min();
};

LoadBounds CalcLoadBounds(const std::vector<Pin*>& loads)
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

}  // namespace

TopologyGen::TopologyGen() : _config(), _clustering(_config)
{
}

TopologyGen::TopologyGen(const Config& config) : _config(config), _clustering(config)
{
}

void TopologyGen::updateConfig(const Config& config)
{
  _config = config;
  _clustering = Clustering(config);
}

Tree TopologyGen::build(const std::vector<Pin*>& loads)
{
  Tree tree;
  if (loads.empty()) {
    CTS_LOG_WARNING << "Topology generation skipped: no loads.";
    return tree;
  }

  reportLoadDistribution(loads);
  const auto bounds = CalcLoadBounds(loads);

  const std::size_t leaf_count = calcLeafCount(loads.size());
  if (leaf_count == 0) {
    CTS_LOG_WARNING << "Topology generation skipped: leaf count is zero.";
    return tree;
  }

  const auto root = tree.create_node();
  tree.set_root(root);
  tree.get_node(root)->get_position() = geometry::CalcMedian(loads, [](Pin* pin) { return pin->get_location(); });

  int height = 0;
  for (std::size_t count = leaf_count; count > 1; count >>= 1) {
    ++height;
  }
  // Topology construction (full binary tree)
  buildFullTree(tree, root, 0, height);
  // Position embedding
  embedPositions(tree, root, loads, leaf_count);
  // Balance topology (H-Tree like)
  balanceTopology(tree, bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y);
  reportRootToLeafLengths(tree);

  return tree;
}

void TopologyGen::reportLoadDistribution(const std::vector<Pin*>& loads) const
{
  if (loads.empty()) {
    CTS_LOG_WARNING << "Load distribution: empty load list.";
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
  const double core_area = static_cast<double>(width) * static_cast<double>(height);
  const double core_length = std::sqrt(std::max(0.0, core_area));

  const auto center = geometry::CalcCenter(loads, [](Pin* pin) { return pin->get_location(); });
  const auto median = geometry::CalcMedian(loads, [](Pin* pin) { return pin->get_location(); });

  CTS_LOG_INFO << "Load distribution: bbox=(" << min_x << "," << min_y << ") - (" << max_x << "," << max_y << "), area=" << core_area
               << "DBU^2, sqrt_area=" << core_length << "DBU, (|X|+|Y|)/2=" << (max_x + max_y - min_x - min_y) / 2 << "DBU";
  CTS_LOG_INFO << "Load distribution: center=(" << center.get_x() << "," << center.get_y() << "), median=(" << median.get_x() << "," << median.get_y()
               << ")";
}

void TopologyGen::reportRootToLeafLengths(const Tree& tree) const
{
  if (tree.get_size() == 0 || tree.get_root() == std::numeric_limits<std::size_t>::max()) {
    CTS_LOG_WARNING << "Topology length report skipped: invalid tree.";
    return;
  }

  double min_len = std::numeric_limits<double>::max();
  double max_len = 0.0;
  double sum_len = 0.0;
  std::size_t leaf_count = 0;
  std::size_t invalid_count = 0;

  for (std::size_t id = 0; id < tree.get_size(); ++id) {
    const auto* node = tree.get_node(id);
    if (node == nullptr || !node->isLeaf()) {
      continue;
    }
    double length = 0.0;
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
    CTS_LOG_WARNING << "Topology length report skipped: no valid leaf paths.";
    return;
  }

  const double avg_len = sum_len / static_cast<double>(leaf_count);
  CTS_LOG_INFO << "Topology source-to-leaf length: min=" << min_len << ", max=" << max_len << ", avg=" << avg_len
               << ", leafs=" << leaf_count << ", invalid=" << invalid_count;
}

std::size_t TopologyGen::calcLeafCount(std::size_t load_count) const
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

void TopologyGen::buildFullTree(Tree& tree, std::size_t node, int depth, int height) const
{
  if (depth >= height) {
    return;
  }
  const auto left = tree.add_child(node, 0);
  const auto right = tree.add_child(node, 1);
  buildFullTree(tree, left, depth + 1, height);
  buildFullTree(tree, right, depth + 1, height);
}

void TopologyGen::embedPositions(Tree& tree, std::size_t node, const std::vector<Pin*>& loads, std::size_t leaf_need)
{
  auto* node_ptr = tree.get_node(node);
  if (node_ptr == nullptr) {
    return;
  }
  node_ptr->get_loads() = loads;
  if (loads.empty()) {
    return;
  }

  if (node_ptr->isLeaf() || leaf_need <= 1 || loads.size() <= 1) {
    const auto center = geometry::CalcCenter(loads, [](Pin* pin) { return pin->get_location(); });
    node_ptr->get_position() = Point<int>(static_cast<int>(std::lround(center.get_x())), static_cast<int>(std::lround(center.get_y())));
    return;
  }

  const std::size_t child_leaf_need = leaf_need / 2;
  auto result = _clustering.biPartition(loads, child_leaf_need);
  if (result.clusters.size() < 2) {
    return;
  }

  const auto& children = node_ptr->get_children();
  if (children.size() < 2 || children[0] == std::numeric_limits<std::size_t>::max()
      || children[1] == std::numeric_limits<std::size_t>::max()) {
    return;
  }

  auto* left = tree.get_node(children[0]);
  auto* right = tree.get_node(children[1]);
  if (left == nullptr || right == nullptr) {
    return;
  }

  if (result.centers.size() >= 2) {
    left->get_position() = result.centers[0];
    right->get_position() = result.centers[1];
  }

  embedPositions(tree, children[0], result.clusters[0], child_leaf_need);
  embedPositions(tree, children[1], result.clusters[1], child_leaf_need);
}

void TopologyGen::balanceTopology(Tree& tree, int min_x, int min_y, int max_x, int max_y) const
{
  auto levels = tree.levels();
  if (levels.size() <= 1) {
    return;
  }

  for (std::size_t level = 1; level < levels.size(); ++level) {
    double sum_dist = 0.0;
    std::size_t count = 0;
    for (const auto node_id : levels[level]) {
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
    const double avg_dist = sum_dist / static_cast<double>(count);
    for (const auto node_id : levels[level]) {
      auto* node = tree.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }
      auto* parent = tree.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }
      node->get_position() = geometry::ProjectToL1Circle(parent->get_position(), node->get_position(), avg_dist, min_x, min_y, max_x, max_y);
    }
  }
}

}  // namespace icts
