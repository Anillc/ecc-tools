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
 * @file BSTRouter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Standalone BST router adapter implementation for stage-2 routing refactor.
 */

#include "BSTRouter.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "BoundSkewTree.hh"
#include "geometry/Geometry.hh"
#include "logger/Logger.hh"

namespace icts {
namespace {

using bst::Area;
using bst::BoundSkewTree;
using bst::kLeft;
using bst::kRight;

struct BinaryTopologyNode
{
  std::string name = "";
  Point<int> location = Point<int>(-1, -1);
  bool is_terminal = false;
  double min_delay = 0.0;
  double max_delay = 0.0;
  double cap_load = 0.0;
  double sub_len = 0.0;
  RCPattern pattern = RCPattern::kHV;
  BinaryTopologyNode* left = nullptr;
  BinaryTopologyNode* right = nullptr;
};

BSTParameters BuildDefaultParameters(const BSTParameters& parameters)
{
  auto normalized = parameters;
  if (normalized.db_unit <= 0) {
    normalized.db_unit = 1;
  }
  if (normalized.skew_bound <= 0) {
    normalized.skew_bound = 0.0;
  }
  return normalized;
}

TopoType NormalizeTopoTypeForBuild(const BSTParameters& parameters)
{
  if (parameters.topo_type == TopoType::kInputTopo) {
    CTS_LOG_WARNING
        << "BSTRouter::buildTree received TopoType::kInputTopo. Falling back to TopoType::kGreedyDist for normal BST construction.";
    return TopoType::kGreedyDist;
  }
  return parameters.topo_type;
}

TopoType NormalizeTopoTypeForInputTopology(const BSTParameters& parameters)
{
  if (parameters.topo_type != TopoType::kInputTopo) {
    CTS_LOG_WARNING << "BSTRouter::buildTreeFromTopology received non-input topology type. Overriding topo_type to TopoType::kInputTopo.";
  }
  return TopoType::kInputTopo;
}

double FindInitDelay(const std::string& terminal_name, const BSTParameters& parameters)
{
  auto iter = parameters.init_delay_map.find(terminal_name);
  if (iter != parameters.init_delay_map.end()) {
    return iter->second;
  }
  return 0.0;
}

double FindInitCap(const std::string& terminal_name, const BSTParameters& parameters)
{
  auto iter = parameters.init_cap_map.find(terminal_name);
  if (iter != parameters.init_cap_map.end()) {
    return iter->second;
  }
  return 0.0;
}

Area* BuildLoadArea(const RoutingTerminal& terminal, const BSTParameters& parameters)
{
  auto min_delay = FindInitDelay(terminal.name, parameters);
  auto max_delay = min_delay;
  if (max_delay - min_delay > parameters.skew_bound) {
    min_delay = max_delay - parameters.skew_bound;
  }

  auto cap_load = FindInitCap(terminal.name, parameters);
  return new Area(terminal.name, 1.0 * terminal.location.get_x() / parameters.db_unit, 1.0 * terminal.location.get_y() / parameters.db_unit,
                  cap_load, min_delay, max_delay, 0.0, parameters.pattern, true);
}

BinaryTopologyNode* CreateBinaryNode(const std::string& name, const Point<int>& location, bool is_terminal, const BSTParameters& parameters)
{
  auto* node = new BinaryTopologyNode();
  node->name = name;
  node->location = location;
  node->is_terminal = is_terminal;
  node->pattern = parameters.pattern;
  if (is_terminal) {
    node->cap_load = FindInitCap(name, parameters);
    node->min_delay = FindInitDelay(name, parameters);
    node->max_delay = node->min_delay;
    if (node->max_delay - node->min_delay > parameters.skew_bound) {
      node->min_delay = node->max_delay - parameters.skew_bound;
    }
  }
  return node;
}

std::vector<std::size_t> CollectChildNodeIds(const BSTRouter::ClockSteinerTreeType& input_topology, std::size_t node_id)
{
  std::vector<std::size_t> child_node_ids;
  const auto* node = input_topology.get_node(node_id);
  CTS_LOG_FATAL_IF(node == nullptr) << "BST input-topology node is null.";
  child_node_ids.reserve(node->child_edge_ids.size());
  for (auto edge_id : node->child_edge_ids) {
    const auto* edge = input_topology.get_edge(edge_id);
    CTS_LOG_FATAL_IF(edge == nullptr) << "BST input-topology edge is null.";
    child_node_ids.push_back(edge->target_node_id);
  }
  return child_node_ids;
}

BinaryTopologyNode* BuildBinaryTopologyNode(const BSTRouter::ClockSteinerTreeType& input_topology, std::size_t node_id,
                                            const BSTParameters& parameters, std::size_t& next_steiner_id)
{
  const auto* tree_node = input_topology.get_node(node_id);
  CTS_LOG_FATAL_IF(tree_node == nullptr) << "BST input-topology node is null.";

  auto node_name = tree_node->name.empty() ? std::string("steiner_") + std::to_string(node_id) : tree_node->name;
  auto* node = CreateBinaryNode(node_name, tree_node->location, tree_node->is_terminal, parameters);
  auto child_ids = CollectChildNodeIds(input_topology, node_id);

  if (child_ids.empty()) {
    CTS_LOG_FATAL_IF(!node->is_terminal) << "BST input-topology Steiner node has no children.";
    return node;
  }

  auto build_child = [&](std::size_t child_id) { return BuildBinaryTopologyNode(input_topology, child_id, parameters, next_steiner_id); };

  if (child_ids.size() == 1) {
    auto* child = build_child(child_ids.front());
    if (node->is_terminal) {
      auto* copy = new BinaryTopologyNode(*node);
      copy->name = std::string("steiner_") + std::to_string(next_steiner_id++);
      copy->is_terminal = false;
      copy->left = child;
      copy->right = node;
      node->left = nullptr;
      node->right = nullptr;
      return copy;
    }

    CTS_LOG_FATAL_IF(!child->left && !child->right) << "BST input-topology single-child Steiner node cannot be flattened.";
    auto grandchildren = std::vector<BinaryTopologyNode*>{};
    if (child->left) {
      grandchildren.push_back(child->left);
    }
    if (child->right) {
      grandchildren.push_back(child->right);
    }
    CTS_LOG_FATAL_IF(grandchildren.empty()) << "BST input-topology single-child Steiner node flatten result is empty.";
    if (grandchildren.size() == 1) {
      node->left = grandchildren.front();
      node->right = nullptr;
    } else {
      node->left = grandchildren[kLeft];
      node->right = grandchildren[kRight];
    }
    return node;
  }

  if (child_ids.size() == 2) {
    node->left = build_child(child_ids[kLeft]);
    node->right = build_child(child_ids[kRight]);
    return node;
  }

  std::ranges::sort(child_ids, [&](std::size_t lhs_id, std::size_t rhs_id) {
    const auto* lhs = input_topology.get_node(lhs_id);
    const auto* rhs = input_topology.get_node(rhs_id);
    if (lhs == nullptr || rhs == nullptr) {
      CTS_LOG_FATAL << "BST input-topology child node is null.";
      return false;
    }
    return geometry::Manhattan(tree_node->location, lhs->location) < geometry::Manhattan(tree_node->location, rhs->location);
  });

  if (child_ids.size() == 3) {
    auto* trunk = new BinaryTopologyNode();
    trunk->name = std::string("steiner_") + std::to_string(next_steiner_id++);
    trunk->location = tree_node->location;
    trunk->pattern = parameters.pattern;
    trunk->left = build_child(child_ids[0]);
    trunk->right = build_child(child_ids[1]);
    node->left = trunk;
    node->right = build_child(child_ids[2]);
    return node;
  }

  CTS_LOG_FATAL_IF(child_ids.size() != 4) << "BST input-topology node child size " << child_ids.size() << " is unsupported.";
  auto* left_copy = new BinaryTopologyNode();
  left_copy->name = std::string("steiner_") + std::to_string(next_steiner_id++);
  left_copy->location = tree_node->location;
  left_copy->pattern = parameters.pattern;
  left_copy->left = build_child(child_ids[0]);
  left_copy->right = build_child(child_ids[1]);

  auto* right_copy = new BinaryTopologyNode();
  right_copy->name = std::string("steiner_") + std::to_string(next_steiner_id++);
  right_copy->location = tree_node->location;
  right_copy->pattern = parameters.pattern;
  right_copy->left = build_child(child_ids[2]);
  right_copy->right = build_child(child_ids[3]);

  node->left = left_copy;
  node->right = right_copy;
  return node;
}

void FinalizeElectricalState(BinaryTopologyNode* node, const BSTParameters& parameters)
{
  if (node == nullptr) {
    return;
  }

  FinalizeElectricalState(node->left, parameters);
  FinalizeElectricalState(node->right, parameters);

  if (node->left == nullptr && node->right == nullptr) {
    return;
  }

  std::vector<BinaryTopologyNode*> children;
  if (node->left) {
    children.push_back(node->left);
  }
  if (node->right) {
    children.push_back(node->right);
  }

  double cap_load = 0.0;
  for (auto* child : children) {
    auto wire_cap = parameters.unit_h_cap * child->sub_len;
    cap_load += child->cap_load + wire_cap;
  }
  node->cap_load = cap_load;

  double max_delay = 0.0;
  double min_delay = std::numeric_limits<double>::max();
  for (auto* child : children) {
    auto delay = parameters.unit_h_res * child->sub_len * (parameters.unit_h_cap * child->sub_len / 2.0 + child->cap_load);
    max_delay = std::max(max_delay, child->max_delay + delay);
    min_delay = std::min(min_delay, child->min_delay + delay);
  }
  node->max_delay = max_delay;
  node->min_delay = min_delay == std::numeric_limits<double>::max() ? 0.0 : min_delay;
  if (node->max_delay - node->min_delay > parameters.skew_bound) {
    node->min_delay = node->max_delay - parameters.skew_bound;
  }
}

Area* BuildAreaTree(BinaryTopologyNode* node, const BSTParameters& parameters)
{
  if (node == nullptr) {
    CTS_LOG_FATAL << "BST binary topology node is null.";
    return nullptr;
  }
  auto* area = new Area(node->name, 1.0 * node->location.get_x() / parameters.db_unit, 1.0 * node->location.get_y() / parameters.db_unit,
                        node->cap_load, node->min_delay, node->max_delay, 1.0 * node->sub_len / parameters.db_unit, node->pattern,
                        node->is_terminal);

  if (node->left == nullptr && node->right == nullptr) {
    return area;
  }

  if (node->left == nullptr || node->right == nullptr) {
    CTS_LOG_FATAL << "BST area adaptation expects binary topology.";
    return area;
  }

  auto* left_area = BuildAreaTree(node->left, parameters);
  auto* right_area = BuildAreaTree(node->right, parameters);
  area->set_left(left_area);
  area->set_right(right_area);
  left_area->set_parent(area);
  right_area->set_parent(area);
  return area;
}

std::size_t ExportAreaNode(const Area* area, const BSTParameters& parameters, BSTRouter::ClockSteinerTreeType& tree,
                           std::unordered_map<const Area*, std::size_t>& area_to_node_id)
{
  auto iter = area_to_node_id.find(area);
  if (iter != area_to_node_id.end()) {
    return iter->second;
  }

  const auto& pt = area->get_location();
  auto node_id = tree.addNode(
      area->get_name(),
      Point<int>(static_cast<int>(std::lround(pt.x * parameters.db_unit)), static_cast<int>(std::lround(pt.y * parameters.db_unit))),
      area->is_fixed_terminal());
  CTS_LOG_FATAL_IF(node_id == BSTRouter::ClockSteinerTreeType::kInvalidId) << "Failed to add node when exporting BST ClockSteinerTree.";
  area_to_node_id[area] = node_id;

  auto export_child = [&](const Area* child, const std::size_t side) {
    if (child == nullptr) {
      return;
    }

    auto child_id = ExportAreaNode(child, parameters, tree, area_to_node_id);
    const auto* parent_node = tree.get_node(node_id);
    const auto* child_node = tree.get_node(child_id);
    CTS_LOG_FATAL_IF(parent_node == nullptr || child_node == nullptr) << "BST exported node is null.";

    auto distance = geometry::Manhattan(parent_node->location, child_node->location);
    auto routed_distance = static_cast<int>(std::lround(area->get_edge_len(side) * parameters.db_unit));
    CTS_LOG_FATAL_IF(routed_distance < distance) << "BST routed edge length is shorter than embedded Manhattan distance.";

    auto edge_id = tree.addEdge(node_id, child_id, distance, routed_distance);
    CTS_LOG_FATAL_IF(edge_id == BSTRouter::ClockSteinerTreeType::kInvalidId) << "Failed to add edge when exporting BST ClockSteinerTree.";
  };

  export_child(area->get_left(), kLeft);
  export_child(area->get_right(), kRight);
  return node_id;
}

BSTRouter::ClockSteinerTreeType ExportClockTree(const Area* root, const BSTParameters& parameters)
{
  BSTRouter::ClockSteinerTreeType tree;
  CTS_LOG_FATAL_IF(root == nullptr) << "BST root area is null when exporting ClockSteinerTree.";

  std::unordered_map<const Area*, std::size_t> area_to_node_id;
  auto root_id = ExportAreaNode(root, parameters, tree, area_to_node_id);
  tree.setRoot(root_id);
  CTS_LOG_FATAL_IF(!tree.validate()) << "Constructed BST ClockSteinerTree is invalid.";
  return tree;
}

}  // namespace

BSTRouter::ClockSteinerTreeType BSTRouter::buildTree(const std::vector<Terminal>& load_terminals, const BSTParameters& parameters)
{
  auto normalized = BuildDefaultParameters(parameters);
  auto topo_type = NormalizeTopoTypeForBuild(normalized);

  ClockSteinerTreeType empty_tree;
  if (load_terminals.empty()) {
    return empty_tree;
  }

  std::vector<Area*> load_areas;
  load_areas.reserve(load_terminals.size());
  for (const auto& terminal : load_terminals) {
    load_areas.push_back(BuildLoadArea(terminal, normalized));
  }

  BoundSkewTree solver(std::move(load_areas), normalized, topo_type);
  solver.run();
  return ExportClockTree(solver.get_root(), normalized);
}

BSTRouter::ClockSteinerTreeType BSTRouter::buildTreeFromTopology(const ClockSteinerTreeType& input_topology,
                                                                 const BSTParameters& parameters)
{
  auto normalized = BuildDefaultParameters(parameters);
  normalized.topo_type = NormalizeTopoTypeForInputTopology(normalized);

  ClockSteinerTreeType empty_tree;
  if (input_topology.node_count() == 0) {
    return empty_tree;
  }

  CTS_LOG_FATAL_IF(!input_topology.validate()) << "Input BST topology tree is invalid.";

  std::size_t next_steiner_id = input_topology.node_count();
  auto root_id = input_topology.get_root();
  auto* root = BuildBinaryTopologyNode(input_topology, root_id, normalized, next_steiner_id);
  FinalizeElectricalState(root, normalized);

  auto* root_area = BuildAreaTree(root, normalized);
  BoundSkewTree solver(root_area, normalized);
  solver.run();
  return ExportClockTree(solver.get_root(), normalized);
}

}  // namespace icts
