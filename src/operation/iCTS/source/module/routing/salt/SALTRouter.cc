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
 * @file SALTRouter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-15
 * @brief SALT router adapter implementation for Steiner tree construction.
 */

#include "SALTRouter.hh"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Point.hh"
#include "RoutingTerminal.hh"
#include "geometry/Geometry.hh"
#include "logger/Logger.hh"
#include "salt/base/net.h"
#include "salt/base/tree.h"
#include "salt/salt.h"

namespace icts {
namespace {

auto BuildSaltPins(const RoutingTerminal& driver_terminal,
                   const std::vector<RoutingTerminal>& load_terminals) -> std::vector<std::shared_ptr<salt::Pin>>
{
  std::vector<std::shared_ptr<salt::Pin>> salt_pins;
  salt_pins.reserve(load_terminals.size() + 1);
  salt_pins.push_back(std::make_shared<salt::Pin>(driver_terminal.location.get_x(), driver_terminal.location.get_y(), 0, 0.0));
  for (std::size_t i = 0; i < load_terminals.size(); ++i) {
    const auto& terminal = load_terminals[i];
    salt_pins.push_back(std::make_shared<salt::Pin>(terminal.location.get_x(), terminal.location.get_y(), static_cast<int>(i + 1), 0.0));
  }
  return salt_pins;
}

}  // namespace

auto SALTRouter::buildTree(const Terminal& driver_terminal, const std::vector<Terminal>& load_terminals) -> SALTRouter::SteinerTreeType
{
  SteinerTreeType steiner_tree;
  auto root_id = steiner_tree.addNode(driver_terminal.name, driver_terminal.location, true);
  steiner_tree.setRoot(root_id);

  std::unordered_map<int, std::size_t> salt_to_tree_id;
  salt_to_tree_id[0] = root_id;
  for (std::size_t i = 0; i < load_terminals.size(); ++i) {
    auto node_id = steiner_tree.addNode(load_terminals[i].name, load_terminals[i].location, true);
    salt_to_tree_id[static_cast<int>(i + 1)] = node_id;
  }

  if (load_terminals.empty()) {
    return steiner_tree;
  }

  auto salt_pins = BuildSaltPins(driver_terminal, load_terminals);
  salt::Net net;
  net.init(0, "SALT", salt_pins);

  salt::Tree tree;
  salt::SaltBuilder salt_builder;
  salt_builder.Run(net, tree, 0);

  auto ensure_node = [&](const std::shared_ptr<salt::TreeNode>& salt_node) {
    auto iter = salt_to_tree_id.find(salt_node->id);
    if (iter != salt_to_tree_id.end()) {
      return iter->second;
    }

    auto node_id = steiner_tree.addNode(std::string("steiner_") + std::to_string(salt_node->id),
                                        Point<int>(salt_node->loc.x, salt_node->loc.y), false);
    salt_to_tree_id[salt_node->id] = node_id;
    return node_id;
  };

  auto source = tree.source;
  auto source_id = ensure_node(source);
  steiner_tree.setRoot(source_id);

  auto connect_node_func = [&](const std::shared_ptr<salt::TreeNode>& salt_node) {
    auto current_id = ensure_node(salt_node);
    if (salt_node->id == source->id) {
      return;
    }

    auto parent_id = ensure_node(salt_node->parent);
    const auto* current_node = steiner_tree.get_node(current_id);
    const auto* parent_node = steiner_tree.get_node(parent_id);
    CTS_LOG_FATAL_IF(current_node == nullptr || parent_node == nullptr) << "SALT routing tree node is null.";

    auto edge_id = steiner_tree.addEdge(parent_id, current_id, geometry::Manhattan(parent_node->location, current_node->location));
    CTS_LOG_FATAL_IF(edge_id == SteinerTreeType::kInvalidId) << "Failed to add edge when building SALT SteinerTree.";
  };
  salt::TreeNode::preOrder(source, connect_node_func);

  CTS_LOG_FATAL_IF(!steiner_tree.validate()) << "Constructed SALT SteinerTree is invalid.";
  return steiner_tree;
}

}  // namespace icts
