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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file RootedTreeLCA.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Header-only rooted-tree LCA and ancestor-path helper.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace icts::graph {

class RootedTreeLCA
{
 public:
  using NodeId = std::size_t;

  static constexpr std::size_t kInvalidNode = std::numeric_limits<std::size_t>::max();
  static constexpr NodeId kInvalidNodeId = kInvalidNode;

  struct Edge
  {
    NodeId parent = kInvalidNodeId;
    NodeId child = kInvalidNodeId;
  };

  struct BuildResult
  {
    bool success = false;
    std::string failure_reason;
  };

  RootedTreeLCA() = default;

  explicit RootedTreeLCA(const std::vector<std::size_t>& parent_by_node) { reset(parent_by_node); }

  auto build(NodeId root_id, const std::vector<Edge>& edges) -> BuildResult
  {
    if (root_id == kInvalidNodeId) {
      reset({});
      return BuildResult{.success = false, .failure_reason = "invalid_root"};
    }

    std::size_t max_node_id = root_id;
    for (const auto& edge : edges) {
      if (edge.parent == kInvalidNodeId || edge.child == kInvalidNodeId) {
        reset({});
        return BuildResult{.success = false, .failure_reason = "invalid_node_id"};
      }
      max_node_id = std::max({max_node_id, edge.parent, edge.child});
    }

    std::vector<std::size_t> parent_by_node(max_node_id + 1U, kInvalidNode);
    parent_by_node.at(root_id) = kInvalidNode;
    for (const auto& edge : edges) {
      if (edge.child == root_id) {
        reset({});
        return BuildResult{.success = false, .failure_reason = "root_has_parent"};
      }
      if (parent_by_node.at(edge.child) != kInvalidNode) {
        reset({});
        return BuildResult{.success = false, .failure_reason = "node_has_multiple_parents"};
      }
      parent_by_node.at(edge.child) = edge.parent;
    }

    reset(parent_by_node);
    return _valid ? BuildResult{.success = true, .failure_reason = ""} : BuildResult{.success = false, .failure_reason = "invalid_tree"};
  }

  auto reset(const std::vector<std::size_t>& parent_by_node) -> void
  {
    _parent_by_node = parent_by_node;
    _depth_by_node.assign(parent_by_node.size(), 0U);
    _valid = !parent_by_node.empty();

    std::size_t root_count = 0U;
    for (std::size_t node_id = 0U; node_id < parent_by_node.size(); ++node_id) {
      const auto parent = parent_by_node.at(node_id);
      if (parent == kInvalidNode) {
        ++root_count;
        continue;
      }
      if (parent >= parent_by_node.size() || parent == node_id) {
        _valid = false;
      }
    }
    if (root_count != 1U) {
      _valid = false;
    }
    if (!_valid) {
      return;
    }

    std::vector<unsigned char> state(parent_by_node.size(), 0U);
    for (std::size_t node_id = 0U; node_id < parent_by_node.size(); ++node_id) {
      (void) calcDepth(node_id, state);
    }
  }

  auto isValid() const -> bool { return _valid; }
  auto size() const -> std::size_t { return _parent_by_node.size(); }
  auto get_root_id() const -> NodeId
  {
    if (!_valid) {
      return kInvalidNodeId;
    }
    const auto root_iter = std::ranges::find(_parent_by_node, kInvalidNode);
    return root_iter == _parent_by_node.end() ? kInvalidNodeId : static_cast<NodeId>(root_iter - _parent_by_node.begin());
  }
  auto contains(NodeId node_id) const -> bool { return _valid && node_id < _parent_by_node.size(); }

  auto parent(std::size_t node_id) const -> std::size_t
  {
    return node_id < _parent_by_node.size() ? _parent_by_node.at(node_id) : kInvalidNode;
  }

  auto depth(std::size_t node_id) const -> std::size_t
  {
    return node_id < _depth_by_node.size() && _valid ? _depth_by_node.at(node_id) : 0U;
  }

  auto lca(std::size_t lhs, std::size_t rhs) const -> std::size_t
  {
    if (!_valid || lhs >= _parent_by_node.size() || rhs >= _parent_by_node.size()) {
      return kInvalidNode;
    }

    auto left = lhs;
    auto right = rhs;
    while (_depth_by_node.at(left) > _depth_by_node.at(right)) {
      left = _parent_by_node.at(left);
    }
    while (_depth_by_node.at(right) > _depth_by_node.at(left)) {
      right = _parent_by_node.at(right);
    }
    while (left != right) {
      left = _parent_by_node.at(left);
      right = _parent_by_node.at(right);
      if (left == kInvalidNode || right == kInvalidNode) {
        return kInvalidNode;
      }
    }
    return left;
  }

  auto lowestCommonAncestor(NodeId lhs, NodeId rhs) const -> std::optional<NodeId>
  {
    const auto node_id = lca(lhs, rhs);
    return node_id == kInvalidNode ? std::nullopt : std::optional<NodeId>{node_id};
  }

  auto ancestorPath(std::size_t ancestor, std::size_t descendant, bool include_ancestor = true, bool include_descendant = true) const
      -> std::vector<std::size_t>
  {
    if (!_valid || ancestor >= _parent_by_node.size() || descendant >= _parent_by_node.size()) {
      return {};
    }
    std::vector<std::size_t> path;
    auto node = descendant;
    while (node != kInvalidNode) {
      path.push_back(node);
      if (node == ancestor) {
        break;
      }
      node = _parent_by_node.at(node);
    }
    if (path.empty() || path.back() != ancestor) {
      return {};
    }
    std::ranges::reverse(path);
    if (!include_ancestor && !path.empty()) {
      path.erase(path.begin());
    }
    if (!include_descendant && !path.empty()) {
      path.pop_back();
    }
    return path;
  }

 private:
  auto calcDepth(std::size_t node_id, std::vector<unsigned char>& state) -> bool
  {
    if (!_valid || node_id >= _parent_by_node.size()) {
      _valid = false;
      return false;
    }

    std::vector<std::size_t> path;
    auto current = node_id;
    while (current != kInvalidNode) {
      if (current >= _parent_by_node.size()) {
        _valid = false;
        return false;
      }
      if (state.at(current) == 2U) {
        break;
      }
      if (state.at(current) == 1U) {
        _valid = false;
        return false;
      }
      state.at(current) = 1U;
      path.push_back(current);
      current = _parent_by_node.at(current);
    }

    std::ranges::reverse(path);
    for (const auto path_node : path) {
      const auto parent_node = _parent_by_node.at(path_node);
      _depth_by_node.at(path_node) = parent_node == kInvalidNode ? 0U : _depth_by_node.at(parent_node) + 1U;
      state.at(path_node) = 2U;
    }
    return true;
  }

  std::vector<std::size_t> _parent_by_node;
  std::vector<std::size_t> _depth_by_node;
  bool _valid = false;
};

}  // namespace icts::graph
