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
 * @file SteinerTree.hh
 * @author Claude
 * @date 2026-03-11
 * @brief Point-based Steiner tree data structures for iCTS routing.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

#include "database/spatial/Point.hh"

namespace icts {

template <typename T>
struct SteinerNode
{
  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  std::size_t id = kInvalidId;
  Point<T> location;
  bool is_terminal = false;
  std::size_t parent_edge_id = kInvalidId;
  std::vector<std::size_t> child_edge_ids;
};

template <typename T>
struct SteinerEdge
{
  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  std::size_t id = kInvalidId;
  std::size_t source_node_id = kInvalidId;
  std::size_t target_node_id = kInvalidId;
  T dist_length{};
};

template <typename T, typename EdgeT = SteinerEdge<T>>
class SteinerTree
{
 public:
  using CoordType = T;
  using NodeType = SteinerNode<T>;
  using EdgeType = EdgeT;

  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  SteinerTree() = default;
  SteinerTree(const SteinerTree&) = default;
  SteinerTree& operator=(const SteinerTree&) = default;
  SteinerTree(SteinerTree&&) = default;
  SteinerTree& operator=(SteinerTree&&) = default;
  ~SteinerTree() = default;

  void reserve_nodes(std::size_t n) { _nodes.reserve(n); }
  void reserve_edges(std::size_t n) { _edges.reserve(n); }

  std::size_t add_node(const Point<T>& location, bool is_terminal = false)
  {
    std::size_t id = _nodes.size();
    _nodes.push_back(NodeType{id, location, is_terminal, kInvalidId, {}});
    return id;
  }

  void set_root(std::size_t node_id)
  {
    if (node_id < _nodes.size()) {
      _root_node_id = node_id;
    }
  }

  template <typename... EdgeArgs>
  std::size_t add_edge(std::size_t source_node_id, std::size_t target_node_id, const T& dist_length, EdgeArgs&&... edge_args)
  {
    if (source_node_id >= _nodes.size() || target_node_id >= _nodes.size() || source_node_id == target_node_id) {
      return kInvalidId;
    }

    auto& target_node = _nodes[target_node_id];
    if (target_node.parent_edge_id != kInvalidId) {
      return kInvalidId;
    }

    std::size_t edge_id = _edges.size();
    _edges.push_back(EdgeType{edge_id, source_node_id, target_node_id, dist_length, std::forward<EdgeArgs>(edge_args)...});
    _nodes[source_node_id].child_edge_ids.push_back(edge_id);
    target_node.parent_edge_id = edge_id;
    return edge_id;
  }

  NodeType* get_node(std::size_t id)
  {
    if (id >= _nodes.size()) {
      return nullptr;
    }
    return &_nodes[id];
  }
  const NodeType* get_node(std::size_t id) const
  {
    if (id >= _nodes.size()) {
      return nullptr;
    }
    return &_nodes[id];
  }

  EdgeType* get_edge(std::size_t id)
  {
    if (id >= _edges.size()) {
      return nullptr;
    }
    return &_edges[id];
  }
  const EdgeType* get_edge(std::size_t id) const
  {
    if (id >= _edges.size()) {
      return nullptr;
    }
    return &_edges[id];
  }

  const std::vector<NodeType>& get_nodes() const { return _nodes; }
  std::vector<NodeType>& get_nodes() { return _nodes; }
  const std::vector<EdgeType>& get_edges() const { return _edges; }
  std::vector<EdgeType>& get_edges() { return _edges; }

  std::size_t get_root() const { return _root_node_id; }
  std::size_t node_count() const { return _nodes.size(); }
  std::size_t edge_count() const { return _edges.size(); }

  bool is_root(std::size_t node_id) const { return node_id == _root_node_id; }

  bool is_leaf(std::size_t node_id) const
  {
    const auto* node = get_node(node_id);
    return node != nullptr && node->child_edge_ids.empty();
  }

  bool validate() const
  {
    if (_nodes.empty()) {
      return _root_node_id == kInvalidId && _edges.empty();
    }
    if (_root_node_id >= _nodes.size()) {
      return false;
    }
    if (_edges.size() != _nodes.size() - 1) {
      return false;
    }

    std::vector<std::size_t> indegree(_nodes.size(), 0);
    for (std::size_t edge_id = 0; edge_id < _edges.size(); ++edge_id) {
      const auto& edge = _edges[edge_id];
      if (edge.id != edge_id || edge.source_node_id >= _nodes.size() || edge.target_node_id >= _nodes.size()
          || edge.source_node_id == edge.target_node_id) {
        return false;
      }
      ++indegree[edge.target_node_id];
    }

    for (std::size_t node_id = 0; node_id < _nodes.size(); ++node_id) {
      const auto& node = _nodes[node_id];
      if (node.id != node_id) {
        return false;
      }
      if (is_root(node_id)) {
        if (node.parent_edge_id != kInvalidId || indegree[node_id] != 0) {
          return false;
        }
      } else {
        if (node.parent_edge_id >= _edges.size() || indegree[node_id] != 1) {
          return false;
        }
        if (_edges[node.parent_edge_id].target_node_id != node_id) {
          return false;
        }
      }
      for (auto edge_id : node.child_edge_ids) {
        if (edge_id >= _edges.size()) {
          return false;
        }
        if (_edges[edge_id].source_node_id != node_id) {
          return false;
        }
      }
    }

    std::vector<bool> visited(_nodes.size(), false);
    std::queue<std::size_t> queue;
    queue.push(_root_node_id);
    visited[_root_node_id] = true;
    std::size_t visited_count = 0;

    while (!queue.empty()) {
      auto node_id = queue.front();
      queue.pop();
      ++visited_count;

      const auto& node = _nodes[node_id];
      for (auto edge_id : node.child_edge_ids) {
        auto child_id = _edges[edge_id].target_node_id;
        if (visited[child_id]) {
          return false;
        }
        visited[child_id] = true;
        queue.push(child_id);
      }
    }

    return visited_count == _nodes.size();
  }

 protected:
  std::vector<NodeType> _nodes;
  std::vector<EdgeType> _edges;
  std::size_t _root_node_id = kInvalidId;
};

template <typename T>
struct ClockSteinerEdge : public SteinerEdge<T>
{
  T routed_length{};
};

template <typename T>
using ClockSteinerTree = SteinerTree<T, ClockSteinerEdge<T>>;

}  // namespace icts
