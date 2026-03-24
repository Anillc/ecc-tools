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
 * @brief Standalone BST router adapter implementation for bounded-skew routing.
 */

#include "BSTRouter.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BSTTypes.hh"
#include "BoundSkewTree.hh"
#include "Components.hh"
#include "Point.hh"
#include "RoutingTerminal.hh"
#include "geometry/Geometry.hh"
#include "logger/Logger.hh"

namespace icts {
namespace {

using bst::Area;
using bst::BoundSkewTree;
using bst::kLeft;
using bst::kRight;

constexpr int kInvalidCoordinate = -1;
constexpr double kPiElmoreQuadraticFactor = 2.0;
constexpr double kTreeCoordinateScale = 1.0;

struct BinaryTopologyNode
{
  std::string name;
  Point<int> location = Point<int>(kInvalidCoordinate, kInvalidCoordinate);
  bool is_terminal = false;
  double min_delay = 0.0;
  double max_delay = 0.0;
  double cap_load = 0.0;
  double sub_len = 0.0;
  RCPattern pattern = RCPattern::kHV;
  BinaryTopologyNode* left = nullptr;
  BinaryTopologyNode* right = nullptr;
};

using BinaryNodeStore = std::vector<std::unique_ptr<BinaryTopologyNode>>;
using AreaStore = std::vector<std::unique_ptr<Area>>;

auto BuildDefaultParameters(const BSTParameters& parameters) -> BSTParameters
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

auto NormalizeTopoTypeForBuild(const BSTParameters& parameters) -> TopoType
{
  if (parameters.topo_type == TopoType::kInputTopo) {
    CTS_LOG_WARNING
        << "BSTRouter::buildTree received TopoType::kInputTopo. Falling back to TopoType::kGreedyDist for normal BST construction.";
    return TopoType::kGreedyDist;
  }
  return parameters.topo_type;
}

auto NormalizeTopoTypeForInputTopology(const BSTParameters& parameters) -> TopoType
{
  if (parameters.topo_type != TopoType::kInputTopo) {
    CTS_LOG_WARNING << "BSTRouter::buildTreeFromTopology received non-input topology type. Overriding topo_type to TopoType::kInputTopo.";
  }
  return TopoType::kInputTopo;
}

auto FindInitDelay(const std::string& terminal_name, const BSTParameters& parameters) -> double
{
  auto iter = parameters.init_delay_map.find(terminal_name);
  if (iter != parameters.init_delay_map.end()) {
    return iter->second;
  }
  return 0.0;
}

auto FindInitCap(const std::string& terminal_name, const BSTParameters& parameters) -> double
{
  auto iter = parameters.init_cap_map.find(terminal_name);
  if (iter != parameters.init_cap_map.end()) {
    return iter->second;
  }
  return 0.0;
}

auto BuildLoadArea(const RoutingTerminal& terminal, const BSTParameters& parameters, AreaStore& owned_areas) -> Area*
{
  auto min_delay = FindInitDelay(terminal.name, parameters);
  auto max_delay = min_delay;
  if (max_delay - min_delay > parameters.skew_bound) {
    min_delay = max_delay - parameters.skew_bound;
  }

  auto cap_load = FindInitCap(terminal.name, parameters);
  auto area = std::make_unique<Area>(terminal.name, static_cast<double>(terminal.location.get_x()) / parameters.db_unit,
                                     static_cast<double>(terminal.location.get_y()) / parameters.db_unit, cap_load, min_delay, max_delay,
                                     0.0, parameters.pattern, true);
  auto* area_ptr = area.get();
  owned_areas.push_back(std::move(area));
  return area_ptr;
}

auto CreateBinaryNode(const std::string& name, const Point<int>& location, bool is_terminal, const BSTParameters& parameters,
                      BinaryNodeStore& owned_nodes) -> BinaryTopologyNode*
{
  auto node = std::make_unique<BinaryTopologyNode>();
  auto* node_ptr = node.get();
  node_ptr->name = name;
  node_ptr->location = location;
  node_ptr->is_terminal = is_terminal;
  node_ptr->pattern = parameters.pattern;
  if (is_terminal) {
    node_ptr->cap_load = FindInitCap(name, parameters);
    node_ptr->min_delay = FindInitDelay(name, parameters);
    node_ptr->max_delay = node_ptr->min_delay;
    if (node_ptr->max_delay - node_ptr->min_delay > parameters.skew_bound) {
      node_ptr->min_delay = node_ptr->max_delay - parameters.skew_bound;
    }
  }
  owned_nodes.push_back(std::move(node));
  return node_ptr;
}

auto CollectChildNodeIds(const BSTRouter::ClockSteinerTreeType& input_topology, std::size_t node_id) -> std::vector<std::size_t>
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

enum class BuildState : std::uint8_t
{
  kEnter,
  kAfterOnlyChild,
  kAfterLeftChild,
  kAfterRightChild,
  kAfterTrunkLeftChild,
  kAfterTrunkRightChild,
  kAfterThirdChild,
  kAfterLeftCopyLeftChild,
  kAfterLeftCopyRightChild,
  kAfterRightCopyLeftChild,
  kAfterRightCopyRightChild,
};

struct BuildFrame
{
  std::size_t node_id = 0;
  BuildState state = BuildState::kEnter;
  BinaryTopologyNode* node = nullptr;
  std::vector<std::size_t> child_ids;
  BinaryTopologyNode* first_child = nullptr;
  BinaryTopologyNode* second_child = nullptr;
  BinaryTopologyNode* third_child = nullptr;
  BinaryTopologyNode* fourth_child = nullptr;
  BinaryTopologyNode* trunk = nullptr;
  BinaryTopologyNode* left_copy = nullptr;
  BinaryTopologyNode* right_copy = nullptr;
};

constexpr std::size_t kTraversalOrderReserve = 64;

void PushBuildFrame(std::vector<BuildFrame>& frame_stack, std::size_t child_id)
{
  frame_stack.push_back(BuildFrame{child_id});
}

auto CreateSteinerNode(const Point<int>& location, const BSTParameters& parameters, std::size_t& next_steiner_id,
                       BinaryNodeStore& owned_nodes) -> BinaryTopologyNode*
{
  return CreateBinaryNode(std::string("steiner_") + std::to_string(next_steiner_id++), location, false, parameters, owned_nodes);
}

auto CollectNonNullChildren(BinaryTopologyNode* node) -> std::vector<BinaryTopologyNode*>
{
  std::vector<BinaryTopologyNode*> children;
  children.reserve(2);
  if (node->left != nullptr) {
    children.push_back(node->left);
  }
  if (node->right != nullptr) {
    children.push_back(node->right);
  }
  return children;
}

void EnterBuildFrame(BuildFrame& frame, const BSTRouter::ClockSteinerTreeType& input_topology, const BSTParameters& parameters,
                     std::size_t& next_steiner_id, BinaryNodeStore& owned_nodes, BinaryTopologyNode*& return_value,
                     std::vector<BuildFrame>& frame_stack)
{
  const auto* tree_node = input_topology.get_node(frame.node_id);
  CTS_LOG_FATAL_IF(tree_node == nullptr) << "BST input-topology node is null.";

  auto node_name = tree_node->name.empty() ? std::string("steiner_") + std::to_string(frame.node_id) : tree_node->name;
  frame.node = CreateBinaryNode(node_name, tree_node->location, tree_node->is_terminal, parameters, owned_nodes);
  frame.child_ids = CollectChildNodeIds(input_topology, frame.node_id);

  if (frame.child_ids.size() > 2) {
    std::ranges::sort(frame.child_ids, [&](std::size_t lhs_id, std::size_t rhs_id) {
      const auto* lhs = input_topology.get_node(lhs_id);
      const auto* rhs = input_topology.get_node(rhs_id);
      if (lhs == nullptr || rhs == nullptr) {
        CTS_LOG_FATAL << "BST input-topology child node is null.";
        return false;
      }
      return geometry::Manhattan(tree_node->location, lhs->location) < geometry::Manhattan(tree_node->location, rhs->location);
    });
  }

  switch (frame.child_ids.size()) {
    case 0:
      CTS_LOG_FATAL_IF(!frame.node->is_terminal) << "BST input-topology Steiner node has no children.";
      return_value = frame.node;
      frame_stack.pop_back();
      return;
    case 1:
      frame.state = BuildState::kAfterOnlyChild;
      PushBuildFrame(frame_stack, frame.child_ids.front());
      return;
    case 2:
      frame.state = BuildState::kAfterLeftChild;
      PushBuildFrame(frame_stack, frame.child_ids[kLeft]);
      return;
    case 3:
      frame.trunk = CreateSteinerNode(frame.node->location, parameters, next_steiner_id, owned_nodes);
      frame.state = BuildState::kAfterTrunkLeftChild;
      PushBuildFrame(frame_stack, frame.child_ids[0]);
      return;
    case 4:
      frame.left_copy = CreateSteinerNode(frame.node->location, parameters, next_steiner_id, owned_nodes);
      frame.state = BuildState::kAfterLeftCopyLeftChild;
      PushBuildFrame(frame_stack, frame.child_ids[0]);
      return;
    default:
      CTS_LOG_FATAL << "BST input-topology node child size " << frame.child_ids.size() << " is unsupported.";
  }
}

void ExitSingleChildFrame(BuildFrame& frame, std::size_t& next_steiner_id, BinaryNodeStore& owned_nodes, BinaryTopologyNode*& return_value,
                          std::vector<BuildFrame>& frame_stack)
{
  auto* child = return_value;
  CTS_LOG_FATAL_IF(child == nullptr) << "BST input-topology child build returned null.";

  if (frame.node->is_terminal) {
    auto copy = std::make_unique<BinaryTopologyNode>(*frame.node);
    auto* copy_ptr = copy.get();
    copy_ptr->name = std::string("steiner_") + std::to_string(next_steiner_id++);
    copy_ptr->is_terminal = false;
    copy_ptr->left = child;
    copy_ptr->right = frame.node;
    owned_nodes.push_back(std::move(copy));
    frame.node->left = nullptr;
    frame.node->right = nullptr;
    return_value = copy_ptr;
    frame_stack.pop_back();
    return;
  }

  auto grandchildren = CollectNonNullChildren(child);
  CTS_LOG_FATAL_IF(grandchildren.empty()) << "BST input-topology single-child Steiner node flatten result is empty.";

  frame.node->left = grandchildren.front();
  frame.node->right = grandchildren.size() > 1 ? grandchildren[kRight] : nullptr;
  return_value = frame.node;
  frame_stack.pop_back();
}

void ProcessBuildFrame(BuildFrame& frame, const BSTRouter::ClockSteinerTreeType& input_topology, const BSTParameters& parameters,
                       std::size_t& next_steiner_id, BinaryNodeStore& owned_nodes, BinaryTopologyNode*& return_value,
                       std::vector<BuildFrame>& frame_stack)
{
  switch (frame.state) {
    case BuildState::kEnter:
      EnterBuildFrame(frame, input_topology, parameters, next_steiner_id, owned_nodes, return_value, frame_stack);
      return;
    case BuildState::kAfterOnlyChild:
      ExitSingleChildFrame(frame, next_steiner_id, owned_nodes, return_value, frame_stack);
      return;
    case BuildState::kAfterLeftChild:
      frame.first_child = return_value;
      frame.state = BuildState::kAfterRightChild;
      PushBuildFrame(frame_stack, frame.child_ids[kRight]);
      return;
    case BuildState::kAfterRightChild:
      frame.second_child = return_value;
      frame.node->left = frame.first_child;
      frame.node->right = frame.second_child;
      return_value = frame.node;
      frame_stack.pop_back();
      return;
    case BuildState::kAfterTrunkLeftChild:
      frame.first_child = return_value;
      frame.state = BuildState::kAfterTrunkRightChild;
      PushBuildFrame(frame_stack, frame.child_ids[1]);
      return;
    case BuildState::kAfterTrunkRightChild:
      frame.second_child = return_value;
      frame.trunk->left = frame.first_child;
      frame.trunk->right = frame.second_child;
      frame.node->left = frame.trunk;
      frame.state = BuildState::kAfterThirdChild;
      PushBuildFrame(frame_stack, frame.child_ids[2]);
      return;
    case BuildState::kAfterThirdChild:
      frame.third_child = return_value;
      frame.node->right = frame.third_child;
      return_value = frame.node;
      frame_stack.pop_back();
      return;
    case BuildState::kAfterLeftCopyLeftChild:
      frame.first_child = return_value;
      frame.state = BuildState::kAfterLeftCopyRightChild;
      PushBuildFrame(frame_stack, frame.child_ids[1]);
      return;
    case BuildState::kAfterLeftCopyRightChild:
      frame.second_child = return_value;
      frame.left_copy->left = frame.first_child;
      frame.left_copy->right = frame.second_child;
      frame.right_copy = CreateSteinerNode(frame.node->location, parameters, next_steiner_id, owned_nodes);
      frame.state = BuildState::kAfterRightCopyLeftChild;
      PushBuildFrame(frame_stack, frame.child_ids[2]);
      return;
    case BuildState::kAfterRightCopyLeftChild:
      frame.third_child = return_value;
      frame.state = BuildState::kAfterRightCopyRightChild;
      PushBuildFrame(frame_stack, frame.child_ids[3]);
      return;
    case BuildState::kAfterRightCopyRightChild:
      frame.fourth_child = return_value;
      frame.right_copy->left = frame.third_child;
      frame.right_copy->right = frame.fourth_child;
      frame.node->left = frame.left_copy;
      frame.node->right = frame.right_copy;
      return_value = frame.node;
      frame_stack.pop_back();
      return;
  }
}

auto BuildBinaryTopologyNode(const BSTRouter::ClockSteinerTreeType& input_topology, std::size_t node_id, const BSTParameters& parameters,
                             std::size_t& next_steiner_id, BinaryNodeStore& owned_nodes) -> BinaryTopologyNode*
{
  std::vector<BuildFrame> frame_stack;
  PushBuildFrame(frame_stack, node_id);
  BinaryTopologyNode* return_value = nullptr;

  while (!frame_stack.empty()) {
    ProcessBuildFrame(frame_stack.back(), input_topology, parameters, next_steiner_id, owned_nodes, return_value, frame_stack);
  }

  return return_value;
}

void FinalizeElectricalState(BinaryTopologyNode* node, const BSTParameters& parameters)
{
  if (node == nullptr) {
    return;
  }

  std::vector<BinaryTopologyNode*> traversal_stack;
  traversal_stack.push_back(node);
  std::vector<BinaryTopologyNode*> traversal_order;
  traversal_order.reserve(kTraversalOrderReserve);

  while (!traversal_stack.empty()) {
    auto* current = traversal_stack.back();
    traversal_stack.pop_back();
    if (current == nullptr) {
      continue;
    }
    traversal_order.push_back(current);
    if (current->left != nullptr) {
      traversal_stack.push_back(current->left);
    }
    if (current->right != nullptr) {
      traversal_stack.push_back(current->right);
    }
  }

  for (auto* current : std::ranges::reverse_view(traversal_order)) {
    if (current->left == nullptr && current->right == nullptr) {
      continue;
    }

    auto children = CollectNonNullChildren(current);

    double cap_load = 0.0;
    for (auto* child : children) {
      auto wire_cap = parameters.unit_h_cap * child->sub_len;
      cap_load += child->cap_load + wire_cap;
    }
    current->cap_load = cap_load;

    double max_delay = 0.0;
    double min_delay = std::numeric_limits<double>::max();
    for (auto* child : children) {
      auto delay
          = parameters.unit_h_res * child->sub_len * (parameters.unit_h_cap * child->sub_len / kPiElmoreQuadraticFactor + child->cap_load);
      max_delay = std::max(max_delay, child->max_delay + delay);
      min_delay = std::min(min_delay, child->min_delay + delay);
    }
    current->max_delay = max_delay;
    current->min_delay = min_delay == std::numeric_limits<double>::max() ? 0.0 : min_delay;
    if (current->max_delay - current->min_delay > parameters.skew_bound) {
      current->min_delay = current->max_delay - parameters.skew_bound;
    }
  }
}

auto BuildAreaTree(BinaryTopologyNode* node, const BSTParameters& parameters, AreaStore& owned_areas) -> Area*
{
  if (node == nullptr) {
    CTS_LOG_FATAL << "BST binary topology node is null.";
    return nullptr;
  }

  enum class BuildAreaState : std::uint8_t
  {
    kEnter,
    kAfterLeftChild,
    kAfterRightChild,
  };

  struct BuildAreaFrame
  {
    BinaryTopologyNode* node = nullptr;
    Area* area = nullptr;
    Area* left_area = nullptr;
    Area* right_area = nullptr;
    BuildAreaState state = BuildAreaState::kEnter;
  };

  auto create_area = [&](BinaryTopologyNode* current_node) -> Area* {
    auto area = std::make_unique<Area>(current_node->name, kTreeCoordinateScale * current_node->location.get_x() / parameters.db_unit,
                                       kTreeCoordinateScale * current_node->location.get_y() / parameters.db_unit, current_node->cap_load,
                                       current_node->min_delay, current_node->max_delay, current_node->sub_len / parameters.db_unit,
                                       current_node->pattern, current_node->is_terminal);
    auto* area_ptr = area.get();
    owned_areas.push_back(std::move(area));
    return area_ptr;
  };

  Area* return_area = nullptr;
  std::vector<BuildAreaFrame> frame_stack;
  frame_stack.push_back(BuildAreaFrame{node, nullptr, nullptr, nullptr, BuildAreaState::kEnter});

  while (!frame_stack.empty()) {
    auto& frame = frame_stack.back();
    switch (frame.state) {
      case BuildAreaState::kEnter:
        frame.area = create_area(frame.node);
        if (frame.node->left == nullptr && frame.node->right == nullptr) {
          return_area = frame.area;
          frame_stack.pop_back();
          break;
        }
        if (frame.node->left == nullptr || frame.node->right == nullptr) {
          CTS_LOG_FATAL << "BST area adaptation expects binary topology.";
          return frame.area;
        }
        frame.state = BuildAreaState::kAfterLeftChild;
        frame_stack.push_back(BuildAreaFrame{frame.node->left, nullptr, nullptr, nullptr, BuildAreaState::kEnter});
        break;
      case BuildAreaState::kAfterLeftChild:
        frame.left_area = return_area;
        frame.state = BuildAreaState::kAfterRightChild;
        frame_stack.push_back(BuildAreaFrame{frame.node->right, nullptr, nullptr, nullptr, BuildAreaState::kEnter});
        break;
      case BuildAreaState::kAfterRightChild:
        frame.right_area = return_area;
        frame.area->set_left(frame.left_area);
        frame.area->set_right(frame.right_area);
        frame.left_area->set_parent(frame.area);
        frame.right_area->set_parent(frame.area);
        return_area = frame.area;
        frame_stack.pop_back();
        break;
    }
  }

  return return_area;
}

auto ExportAreaNode(const Area* area, const BSTParameters& parameters, BSTRouter::ClockSteinerTreeType& tree,
                    std::unordered_map<const Area*, std::size_t>& area_to_node_id) -> std::size_t
{
  enum class ExportState : std::uint8_t
  {
    kEnter,
    kAfterLeftChild,
    kAfterRightChild,
  };

  struct ExportFrame
  {
    const Area* area = nullptr;
    std::size_t node_id = BSTRouter::ClockSteinerTreeType::kInvalidId;
    std::size_t child_id = BSTRouter::ClockSteinerTreeType::kInvalidId;
    ExportState state = ExportState::kEnter;
  };

  auto add_edge = [&](std::size_t parent_id, std::size_t child_id, const Area* parent_area, std::size_t side) {
    const auto* parent_node = tree.get_node(parent_id);
    const auto* child_node = tree.get_node(child_id);
    CTS_LOG_FATAL_IF(parent_node == nullptr || child_node == nullptr) << "BST exported node is null.";

    auto distance = geometry::Manhattan(parent_node->location, child_node->location);
    auto routed_distance = static_cast<int>(std::lround(parent_area->get_edge_len(side) * parameters.db_unit));
    CTS_LOG_FATAL_IF(routed_distance < distance) << "BST routed edge length is shorter than embedded Manhattan distance.";

    auto edge_id = tree.addEdge(parent_id, child_id, distance, routed_distance);
    CTS_LOG_FATAL_IF(edge_id == BSTRouter::ClockSteinerTreeType::kInvalidId) << "Failed to add edge when exporting BST ClockSteinerTree.";
  };

  std::size_t return_node_id = BSTRouter::ClockSteinerTreeType::kInvalidId;
  std::vector<ExportFrame> frame_stack;
  frame_stack.push_back(ExportFrame{area});

  while (!frame_stack.empty()) {
    auto& frame = frame_stack.back();
    switch (frame.state) {
      case ExportState::kEnter: {
        auto iter = area_to_node_id.find(frame.area);
        if (iter != area_to_node_id.end()) {
          return_node_id = iter->second;
          frame_stack.pop_back();
          break;
        }

        const auto& location = frame.area->get_location();
        frame.node_id = tree.addNode(frame.area->get_name(),
                                     Point<int>(static_cast<int>(std::lround(location.x * parameters.db_unit)),
                                                static_cast<int>(std::lround(location.y * parameters.db_unit))),
                                     frame.area->is_fixed_terminal());
        CTS_LOG_FATAL_IF(frame.node_id == BSTRouter::ClockSteinerTreeType::kInvalidId)
            << "Failed to add node when exporting BST ClockSteinerTree.";
        area_to_node_id[frame.area] = frame.node_id;

        if (frame.area->get_left() == nullptr) {
          frame.state = ExportState::kAfterLeftChild;
          break;
        }
        frame.state = ExportState::kAfterLeftChild;
        frame_stack.push_back(ExportFrame{frame.area->get_left()});
        break;
      }
      case ExportState::kAfterLeftChild:
        if (frame.area->get_left() != nullptr) {
          frame.child_id = return_node_id;
          add_edge(frame.node_id, frame.child_id, frame.area, kLeft);
        }
        if (frame.area->get_right() == nullptr) {
          return_node_id = frame.node_id;
          frame_stack.pop_back();
          break;
        }
        frame.state = ExportState::kAfterRightChild;
        frame_stack.push_back(ExportFrame{frame.area->get_right()});
        break;
      case ExportState::kAfterRightChild:
        frame.child_id = return_node_id;
        add_edge(frame.node_id, frame.child_id, frame.area, kRight);
        return_node_id = frame.node_id;
        frame_stack.pop_back();
        break;
    }
  }

  return return_node_id;
}

auto ExportClockTree(const Area* root, const BSTParameters& parameters) -> BSTRouter::ClockSteinerTreeType
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

auto BSTRouter::buildTree(const std::vector<Terminal>& load_terminals, const BSTParameters& parameters) -> BSTRouter::ClockSteinerTreeType
{
  auto normalized = BuildDefaultParameters(parameters);
  auto topo_type = NormalizeTopoTypeForBuild(normalized);

  ClockSteinerTreeType empty_tree;
  if (load_terminals.empty()) {
    return empty_tree;
  }

  AreaStore load_areas;
  load_areas.reserve(load_terminals.size());
  for (const auto& terminal : load_terminals) {
    BuildLoadArea(terminal, normalized, load_areas);
  }

  BoundSkewTree solver(std::move(load_areas), normalized, topo_type);
  solver.run();
  return ExportClockTree(solver.get_root(), normalized);
}

auto BSTRouter::buildTreeFromTopology(const ClockSteinerTreeType& input_topology,
                                      const BSTParameters& parameters) -> BSTRouter::ClockSteinerTreeType
{
  auto normalized = BuildDefaultParameters(parameters);
  normalized.topo_type = NormalizeTopoTypeForInputTopology(normalized);

  ClockSteinerTreeType empty_tree;
  if (input_topology.node_count() == 0) {
    return empty_tree;
  }

  CTS_LOG_FATAL_IF(!input_topology.validate()) << "Input BST topology tree is invalid.";

  std::size_t next_steiner_id = input_topology.node_count();
  BinaryNodeStore owned_nodes;
  auto root_id = input_topology.get_root();
  auto* root = BuildBinaryTopologyNode(input_topology, root_id, normalized, next_steiner_id, owned_nodes);
  FinalizeElectricalState(root, normalized);

  AreaStore owned_areas;
  auto* root_area = BuildAreaTree(root, normalized, owned_areas);
  BoundSkewTree solver(std::move(owned_areas), root_area, normalized);
  solver.run();
  return ExportClockTree(solver.get_root(), normalized);
}

}  // namespace icts
