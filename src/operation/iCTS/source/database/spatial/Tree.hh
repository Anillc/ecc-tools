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
 * @file Tree.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Topology tree data structure for CTS.
 */

#pragma once

#include <limits>
#include <memory>
#include <queue>
#include <vector>

#include "Point.hh"

namespace icts {

class Pin;

class TreeNode
{
 public:
  TreeNode() = default;
  explicit TreeNode(std::size_t id) : _id(id) {}
  ~TreeNode() = default;

  std::size_t get_id() const { return _id; }
  std::size_t get_parent() const { return _parent; }
  const std::vector<std::size_t>& get_children() const { return _children; }
  std::vector<std::size_t>& get_children() { return _children; }
  const Point<int>& get_position() const { return _position; }
  Point<int>& get_position() { return _position; }
  const std::vector<Pin*>& get_loads() const { return _loads; }
  std::vector<Pin*>& get_loads() { return _loads; }

  void set_parent(std::size_t parent) { _parent = parent; }
  void set_child(std::size_t index, std::size_t child)
  {
    if (index >= _children.size()) {
      _children.resize(index + 1, std::numeric_limits<std::size_t>::max());
    }
    _children[index] = child;
  }
  void add_child(std::size_t child) { _children.push_back(child); }
  bool isLeaf() const
  {
    for (auto child : _children) {
      if (child != std::numeric_limits<std::size_t>::max()) {
        return false;
      }
    }
    return true;
  }

 private:
  std::size_t _id = std::numeric_limits<std::size_t>::max();
  std::size_t _parent = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> _children;
  Point<int> _position = Point<int>(-1, -1);
  std::vector<Pin*> _loads;
};

class Tree
{
 public:
  Tree() = default;
  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;
  Tree(Tree&&) = default;
  Tree& operator=(Tree&&) = default;
  ~Tree() = default;

  std::size_t create_node()
  {
    std::size_t id = _nodes.size();
    _nodes.push_back(std::make_unique<TreeNode>(id));
    return id;
  }

  std::size_t add_child(std::size_t parent, std::size_t index)
  {
    auto* parent_node = get_node(parent);
    if (parent_node == nullptr) {
      return std::numeric_limits<std::size_t>::max();
    }
    std::size_t child = create_node();
    auto* child_node = get_node(child);
    child_node->set_parent(parent);
    parent_node->set_child(index, child);
    return child;
  }

  std::size_t add_child(std::size_t parent)
  {
    auto* parent_node = get_node(parent);
    if (parent_node == nullptr) {
      return std::numeric_limits<std::size_t>::max();
    }
    std::size_t child = create_node();
    auto* child_node = get_node(child);
    child_node->set_parent(parent);
    parent_node->add_child(child);
    return child;
  }

  TreeNode* get_node(std::size_t id)
  {
    if (id >= _nodes.size()) {
      return nullptr;
    }
    return _nodes[id].get();
  }
  const TreeNode* get_node(std::size_t id) const
  {
    if (id >= _nodes.size()) {
      return nullptr;
    }
    return _nodes[id].get();
  }

  void set_root(std::size_t root) { _root = root; }
  std::size_t get_root() const { return _root; }
  std::size_t get_size() const { return _nodes.size(); }

  std::vector<std::vector<std::size_t>> levels() const
  {
    std::vector<std::vector<std::size_t>> result;
    if (_root == std::numeric_limits<std::size_t>::max()) {
      return result;
    }

    std::queue<std::size_t> queue;
    queue.push(_root);
    while (!queue.empty()) {
      std::size_t level_size = queue.size();
      std::vector<std::size_t> level;
      level.reserve(level_size);
      for (std::size_t i = 0; i < level_size; ++i) {
        std::size_t id = queue.front();
        queue.pop();
        level.push_back(id);

        const auto* node_ptr = get_node(id);
        if (node_ptr == nullptr) {
          continue;
        }
        for (auto child : node_ptr->get_children()) {
          if (child != std::numeric_limits<std::size_t>::max()) {
            queue.push(child);
          }
        }
      }
      result.push_back(std::move(level));
    }

    return result;
  }

 private:
  std::vector<std::unique_ptr<TreeNode>> _nodes;
  std::size_t _root = std::numeric_limits<std::size_t>::max();
};

}  // namespace icts
