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
 * @file CBSRouter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Standalone CBS router implementation.
 */
#include "CBSRouter.hh"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BSTRouter.hh"
#include "BSTTypes.hh"
#include "Point.hh"
#include "geometry/Geometry.hh"
#include "logger/Logger.hh"
#include "salt/base/net.h"
#include "salt/base/rsa.h"
#include "salt/base/tree.h"
#include "salt/refine/refine.h"
#include "salt/utils/geo.h"

namespace icts {

namespace {

inline auto ToSaltIndex(int node_id) -> std::size_t
{
  CTS_LOG_FATAL_IF(node_id < 0) << "SALT node id is negative.";
  return static_cast<std::size_t>(node_id);
}

}  // namespace

CustomSaltBuilder::~CustomSaltBuilder() = default;

namespace {

constexpr double kDefaultCbsEps = 0.0;
constexpr int kDefaultCbsRefineLevel = 3;

auto BuildSaltPin(const CBSRouter::ClockSteinerTreeType::NodeType& node,
                  const std::unordered_map<std::string, double>& init_cap_map) -> std::shared_ptr<salt::Pin>
{
  if (!node.is_terminal) {
    return nullptr;
  }

  CTS_LOG_FATAL_IF(node.name.empty()) << "Missing node name for CBS salt conversion terminal node " << node.id << ".";
  auto cap_iter = init_cap_map.find(node.name);
  auto cap_load = cap_iter == init_cap_map.end() ? 0.0 : cap_iter->second;
  return std::make_shared<salt::Pin>(node.location.get_x(), node.location.get_y(), static_cast<int>(node.id), cap_load);
}

auto BuildSaltPinList(const CBSRouter::ClockSteinerTreeType& clock_tree,
                      const std::unordered_map<std::string, double>& init_cap_map) -> std::vector<std::shared_ptr<salt::Pin>>
{
  std::vector<std::shared_ptr<salt::Pin>> salt_pins;
  salt_pins.reserve(clock_tree.node_count());
  for (const auto& node : clock_tree.get_nodes()) {
    if (!node.is_terminal) {
      continue;
    }
    salt_pins.push_back(BuildSaltPin(node, init_cap_map));
  }
  std::ranges::sort(salt_pins, [](const auto& lhs, const auto& rhs) { return lhs->id < rhs->id; });
  return salt_pins;
}

auto BuildSaltTreeFromClockTree(const CBSRouter::ClockSteinerTreeType& clock_tree, const salt::Net& salt_net,
                                const std::unordered_map<std::string, double>& init_cap_map) -> salt::Tree
{
  std::vector<std::shared_ptr<salt::TreeNode>> salt_nodes(clock_tree.node_count());
  for (const auto& node : clock_tree.get_nodes()) {
    auto salt_pin = BuildSaltPin(node, init_cap_map);
    salt_nodes[node.id]
        = std::make_shared<salt::TreeNode>(node.location.get_x(), node.location.get_y(), salt_pin, static_cast<int>(node.id));
  }

  for (const auto& edge : clock_tree.get_edges()) {
    auto parent = salt_nodes[edge.source_node_id];
    auto child = salt_nodes[edge.target_node_id];
    salt::TreeNode::setParent(child, parent);
  }

  auto root_id = clock_tree.get_root();
  CTS_LOG_FATAL_IF(root_id >= salt_nodes.size()) << "CBS input clock tree root id is invalid.";
  return {salt_nodes[root_id], &salt_net};
}

auto ExportSaltTree(const salt::Tree& tree, const CBSRouter::ClockSteinerTreeType& initial_tree) -> CBSRouter::ClockSteinerTreeType
{
  CBSRouter::ClockSteinerTreeType clock_tree;
  auto nodes = tree.ObtainNodes();
  std::unordered_map<int, std::size_t> salt_to_clock_id;
  salt_to_clock_id.reserve(nodes.size());

  for (const auto& node : nodes) {
    std::string node_name = std::string("steiner_") + std::to_string(node->id);
    if (node->pin != nullptr) {
      const auto* initial_node = initial_tree.get_node(ToSaltIndex(node->id));
      CTS_LOG_FATAL_IF(initial_node == nullptr || initial_node->name.empty())
          << "Missing terminal name when exporting CBS refined topology.";
      node_name = initial_node->name;
    }
    auto node_id = clock_tree.addNode(node_name, Point<int>(node->loc.x, node->loc.y), node->pin != nullptr);
    CTS_LOG_FATAL_IF(node_id == CBSRouter::ClockSteinerTreeType::kInvalidId) << "Failed to add node when exporting CBS refined tree.";
    salt_to_clock_id[node->id] = node_id;
  }

  auto source = tree.source;
  CTS_LOG_FATAL_IF(source == nullptr) << "CBS refined salt tree source is null.";
  auto root_iter = salt_to_clock_id.find(source->id);
  CTS_LOG_FATAL_IF(root_iter == salt_to_clock_id.end()) << "CBS refined salt tree root id is missing.";
  clock_tree.setRoot(root_iter->second);

  for (const auto& node : nodes) {
    if (node->parent == nullptr) {
      continue;
    }

    auto parent_iter = salt_to_clock_id.find(node->parent->id);
    auto child_iter = salt_to_clock_id.find(node->id);
    CTS_LOG_FATAL_IF(parent_iter == salt_to_clock_id.end() || child_iter == salt_to_clock_id.end())
        << "CBS refined salt tree node id mapping is missing.";

    const auto* parent_node = clock_tree.get_node(parent_iter->second);
    const auto* child_node = clock_tree.get_node(child_iter->second);
    CTS_LOG_FATAL_IF(parent_node == nullptr || child_node == nullptr) << "CBS refined topology node is null.";
    auto distance = geometry::Manhattan(parent_node->location, child_node->location);
    auto edge_id = clock_tree.addEdge(parent_iter->second, child_iter->second, distance, distance);
    CTS_LOG_FATAL_IF(edge_id == CBSRouter::ClockSteinerTreeType::kInvalidId) << "Failed to add edge when exporting CBS refined tree.";
  }

  CTS_LOG_FATAL_IF(!clock_tree.validate()) << "Constructed CBS refined ClockSteinerTree is invalid.";
  return clock_tree;
}

auto RefineTopology(const CBSRouter::ClockSteinerTreeType& initial_tree, const BSTParameters& parameters) -> CBSRouter::ClockSteinerTreeType
{
  auto root_id = initial_tree.get_root();
  const auto* root_node = initial_tree.get_node(root_id);
  CTS_LOG_FATAL_IF(root_node == nullptr || root_node->name.empty()) << "Missing root node name for CBS initial BST tree.";

  auto salt_pins = BuildSaltPinList(initial_tree, parameters.init_cap_map);
  salt::Net salt_net;
  salt_net.init(0, "CBS", salt_pins);
  auto salt_tree = BuildSaltTreeFromClockTree(initial_tree, salt_net, parameters.init_cap_map);

  CustomSaltBuilder builder;
  builder.run(salt_net, salt_tree, kDefaultCbsEps, kDefaultCbsRefineLevel);
  return ExportSaltTree(salt_tree, initial_tree);
}

}  // namespace

void CustomSaltBuilder::init(const salt::Tree& min_tree, const std::shared_ptr<salt::Pin>& src_pin)
{
  auto min_tree_copy = min_tree;
  min_tree_copy.UpdateId();
  auto min_tree_nodes = min_tree_copy.ObtainNodes();
  _nodes.resize(min_tree_nodes.size());
  _shortest_latency.resize(min_tree_nodes.size());
  _cur_latency.resize(min_tree_nodes.size());
  for (const auto& min_tree_node : min_tree_nodes) {
    const auto node_index = ToSaltIndex(min_tree_node->id);
    const auto latency_index = ToSaltIndex(min_tree_node->id);
    _nodes[node_index] = std::make_shared<salt::TreeNode>(min_tree_node->loc, min_tree_node->pin, min_tree_node->id);
    _shortest_latency[latency_index] = utils::Dist(src_pin->loc, min_tree_node->loc);
    _cur_latency[latency_index] = std::numeric_limits<double>::max();
  }
  const auto src_latency_index = ToSaltIndex(src_pin->id);
  const auto src_node_index = ToSaltIndex(src_pin->id);
  _cur_latency[src_latency_index] = 0.0;
  _src = _nodes[src_node_index];
}

void CustomSaltBuilder::finalize(const salt::Net& net, salt::Tree& tree) const
{
  for (const auto& node : _nodes) {
    if (node->parent) {
      _nodes[ToSaltIndex(node->parent->id)]->children.push_back(node);
    }
  }
  tree.source = _src;
  tree.net = &net;
}

void CustomSaltBuilder::run(const salt::Net& net, salt::Tree& input_tree, double eps, int refine_level)
{
  auto tree = input_tree;
  if (refine_level >= 1) {
    salt::Refine::flip(tree);
    salt::Refine::uShift(tree);
    salt::Refine::removeRedundantCoincident(tree);
  }

  init(tree, net.source());
  dfs(tree.source, _src, eps);
  finalize(net, input_tree);
  input_tree.RemoveTopoRedundantSteiner();

  salt::RsaBuilder rsa_builder;
  rsa_builder.ReplaceRootChildren(input_tree);
  input_tree.UpdateId();

  if (refine_level >= 1) {
    salt::Refine::cancelIntersect(input_tree);
    salt::Refine::flip(input_tree);
    salt::Refine::uShift(input_tree);
    if (refine_level >= 2) {
      salt::Refine::substitute(input_tree, eps, refine_level == 3);
    }
  }
}

auto CustomSaltBuilder::relax(const std::shared_ptr<salt::TreeNode>& source_node,
                              const std::shared_ptr<salt::TreeNode>& target_node) -> bool
{
  const auto source_index = ToSaltIndex(source_node->id);
  const auto target_index = ToSaltIndex(target_node->id);
  const auto edge_distance = utils::Dist(source_node->loc, target_node->loc);
  const auto new_latency = _cur_latency[source_index] + edge_distance;
  if (_cur_latency[target_index] > new_latency) {
    _cur_latency[target_index] = new_latency;
    target_node->parent = source_node;
    return true;
  }
  if (_cur_latency[target_index] == new_latency && edge_distance < target_node->WireToParentChecked()) {
    target_node->parent = source_node;
    return true;
  }
  return false;
}

void CustomSaltBuilder::dfs(const std::shared_ptr<salt::TreeNode>& tree_node, const std::shared_ptr<salt::TreeNode>& cbs_node, double eps)
{
  struct DfsFrame
  {
    std::shared_ptr<salt::TreeNode> tree_node;
    std::shared_ptr<salt::TreeNode> cbs_node;
    std::size_t next_child_index = 0;
    bool entered = false;
  };

  std::vector<DfsFrame> frame_stack;
  frame_stack.push_back(DfsFrame{tree_node, cbs_node, 0, false});

  while (!frame_stack.empty()) {
    auto& frame = frame_stack.back();
    auto current_tree_node = frame.tree_node;
    auto current_cbs_node = frame.cbs_node;

    if (!frame.entered) {
      frame.entered = true;
      const auto current_latency_index = ToSaltIndex(current_cbs_node->id);
      if (current_tree_node->pin && _cur_latency[current_latency_index] > (1 + eps) * _shortest_latency[current_latency_index]) {
        current_cbs_node->parent = _src;
        _cur_latency[current_latency_index] = _shortest_latency[current_latency_index];
      }
    }

    if (frame.next_child_index >= current_tree_node->children.size()) {
      frame_stack.pop_back();
      if (!frame_stack.empty()) {
        relax(current_cbs_node, frame_stack.back().cbs_node);
      }
      continue;
    }

    auto child = current_tree_node->children[frame.next_child_index++];
    auto child_cbs_node = _nodes[ToSaltIndex(child->id)];
    relax(current_cbs_node, child_cbs_node);
    frame_stack.push_back(DfsFrame{child, child_cbs_node, 0, false});
  }
}

auto CBSRouter::buildTree(const std::vector<Terminal>& load_terminals, const BSTParameters& parameters) -> CBSRouter::ClockSteinerTreeType
{
  auto initial_tree = BSTRouter::buildTree(load_terminals, parameters);
  if (initial_tree.node_count() == 0) {
    return initial_tree;
  }

  auto refined_topology = RefineTopology(initial_tree, parameters);
  return BSTRouter::buildTreeFromTopology(refined_topology, parameters);
}

}  // namespace icts
