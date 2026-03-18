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
 * @file RCTree.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-11
 * @brief RC tree data structures for iCTS timing database.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace icts {

struct RCTreeVertex
{
  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  std::size_t id = kInvalidId;
  std::string name = "";
  bool is_terminal = false;
  std::size_t parent_arc_id = kInvalidId;
  std::vector<std::size_t> child_arc_ids;
  double lumped_cap = 0.0;
  double downstream_cap = 0.0;
  double arrival = 0.0;
  double slew = 0.0;
  double min_downstream_delay = 0.0;
  double max_downstream_delay = 0.0;
};

struct RCTreeArc
{
  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  std::size_t id = kInvalidId;
  std::size_t source_vertex_id = kInvalidId;
  std::size_t sink_vertex_id = kInvalidId;
  double resistance = 0.0;
  double capacitance = 0.0;
  double increase_delay = 0.0;
};

class RCTree
{
 public:
  using VertexType = RCTreeVertex;
  using ArcType = RCTreeArc;

  static constexpr std::size_t kInvalidId = std::numeric_limits<std::size_t>::max();

  RCTree() = default;
  RCTree(const RCTree&) = default;
  RCTree& operator=(const RCTree&) = default;
  RCTree(RCTree&&) = default;
  RCTree& operator=(RCTree&&) = default;
  ~RCTree() = default;

  void reserveVertices(std::size_t n) { _vertices.reserve(n); }
  void reserveArcs(std::size_t n) { _arcs.reserve(n); }

  std::size_t addVertex(const std::string& name, bool is_terminal = false, double lumped_cap = 0.0)
  {
    if (name.empty() || _vertex_name_to_id.contains(name)) {
      return kInvalidId;
    }

    std::size_t id = _vertices.size();
    _vertices.push_back(VertexType{id, name, is_terminal, kInvalidId, std::vector<std::size_t>{}, lumped_cap, 0.0, 0.0, 0.0, 0.0, 0.0});
    _vertex_name_to_id[name] = id;
    return id;
  }

  std::size_t findVertexId(const std::string& name) const
  {
    auto iter = _vertex_name_to_id.find(name);
    if (iter == _vertex_name_to_id.end()) {
      return kInvalidId;
    }
    return iter->second;
  }

  VertexType* findVertex(const std::string& name) { return get_vertex(findVertexId(name)); }
  const VertexType* findVertex(const std::string& name) const { return get_vertex(findVertexId(name)); }
  bool hasVertex(const std::string& name) const { return _vertex_name_to_id.contains(name); }

  void setRoot(std::size_t vertex_id)
  {
    if (vertex_id < _vertices.size()) {
      _root_vertex_id = vertex_id;
    }
  }

  std::size_t addArc(std::size_t source_vertex_id, std::size_t sink_vertex_id, double resistance = 0.0, double capacitance = 0.0)
  {
    if (source_vertex_id >= _vertices.size() || sink_vertex_id >= _vertices.size() || source_vertex_id == sink_vertex_id) {
      return kInvalidId;
    }

    auto& sink_vertex = _vertices[sink_vertex_id];
    if (sink_vertex.parent_arc_id != kInvalidId) {
      return kInvalidId;
    }

    std::size_t arc_id = _arcs.size();
    _arcs.push_back(ArcType{arc_id, source_vertex_id, sink_vertex_id, resistance, capacitance, 0.0});
    _vertices[source_vertex_id].child_arc_ids.push_back(arc_id);
    sink_vertex.parent_arc_id = arc_id;
    return arc_id;
  }

  std::size_t addArc(const std::string& source_vertex_name, const std::string& sink_vertex_name, double resistance = 0.0,
                     double capacitance = 0.0)
  {
    auto source_vertex_id = findVertexId(source_vertex_name);
    auto sink_vertex_id = findVertexId(sink_vertex_name);
    if (source_vertex_id == kInvalidId || sink_vertex_id == kInvalidId) {
      return kInvalidId;
    }
    return addArc(source_vertex_id, sink_vertex_id, resistance, capacitance);
  }

  VertexType* get_vertex(std::size_t id)
  {
    if (id >= _vertices.size()) {
      return nullptr;
    }
    return &_vertices[id];
  }
  const VertexType* get_vertex(std::size_t id) const
  {
    if (id >= _vertices.size()) {
      return nullptr;
    }
    return &_vertices[id];
  }

  ArcType* get_arc(std::size_t id)
  {
    if (id >= _arcs.size()) {
      return nullptr;
    }
    return &_arcs[id];
  }
  const ArcType* get_arc(std::size_t id) const
  {
    if (id >= _arcs.size()) {
      return nullptr;
    }
    return &_arcs[id];
  }

  const std::vector<VertexType>& get_vertices() const { return _vertices; }
  std::vector<VertexType>& get_vertices() { return _vertices; }
  const std::vector<ArcType>& get_arcs() const { return _arcs; }
  std::vector<ArcType>& get_arcs() { return _arcs; }
  const std::unordered_map<std::string, std::size_t>& get_vertex_name_map() const { return _vertex_name_to_id; }

  std::size_t get_root() const { return _root_vertex_id; }
  std::size_t vertex_count() const { return _vertices.size(); }
  std::size_t arc_count() const { return _arcs.size(); }

  bool is_root(std::size_t vertex_id) const { return vertex_id == _root_vertex_id; }

  bool is_leaf(std::size_t vertex_id) const
  {
    const auto* vertex = get_vertex(vertex_id);
    return vertex != nullptr && vertex->child_arc_ids.empty();
  }

  void clearTimingCache()
  {
    for (auto& vertex : _vertices) {
      vertex.downstream_cap = 0.0;
      vertex.arrival = 0.0;
      vertex.slew = 0.0;
      vertex.min_downstream_delay = 0.0;
      vertex.max_downstream_delay = 0.0;
    }
    for (auto& arc : _arcs) {
      arc.increase_delay = 0.0;
    }
  }

  bool validate() const
  {
    if (_vertices.empty()) {
      return _root_vertex_id == kInvalidId && _arcs.empty();
    }
    if (_root_vertex_id >= _vertices.size()) {
      return false;
    }
    if (_arcs.size() != _vertices.size() - 1) {
      return false;
    }

    std::vector<std::size_t> indegree(_vertices.size(), 0);
    for (std::size_t arc_id = 0; arc_id < _arcs.size(); ++arc_id) {
      const auto& arc = _arcs[arc_id];
      if (arc.id != arc_id || arc.source_vertex_id >= _vertices.size() || arc.sink_vertex_id >= _vertices.size()
          || arc.source_vertex_id == arc.sink_vertex_id) {
        return false;
      }
      ++indegree[arc.sink_vertex_id];
    }

    std::unordered_map<std::string, std::size_t> validated_name_to_id;
    for (std::size_t vertex_id = 0; vertex_id < _vertices.size(); ++vertex_id) {
      const auto& vertex = _vertices[vertex_id];
      if (vertex.id != vertex_id || vertex.name.empty()) {
        return false;
      }
      if (validated_name_to_id.contains(vertex.name)) {
        return false;
      }
      validated_name_to_id[vertex.name] = vertex_id;
      if (is_root(vertex_id)) {
        if (vertex.parent_arc_id != kInvalidId || indegree[vertex_id] != 0) {
          return false;
        }
      } else {
        if (vertex.parent_arc_id >= _arcs.size() || indegree[vertex_id] != 1) {
          return false;
        }
        if (_arcs[vertex.parent_arc_id].sink_vertex_id != vertex_id) {
          return false;
        }
      }
      for (auto arc_id : vertex.child_arc_ids) {
        if (arc_id >= _arcs.size()) {
          return false;
        }
        if (_arcs[arc_id].source_vertex_id != vertex_id) {
          return false;
        }
      }
    }

    std::vector<bool> visited(_vertices.size(), false);
    std::queue<std::size_t> queue;
    queue.push(_root_vertex_id);
    visited[_root_vertex_id] = true;
    std::size_t visited_count = 0;

    while (!queue.empty()) {
      auto vertex_id = queue.front();
      queue.pop();
      ++visited_count;

      const auto& vertex = _vertices[vertex_id];
      for (auto arc_id : vertex.child_arc_ids) {
        auto child_id = _arcs[arc_id].sink_vertex_id;
        if (visited[child_id]) {
          return false;
        }
        visited[child_id] = true;
        queue.push(child_id);
      }
    }

    return visited_count == _vertices.size() && validated_name_to_id == _vertex_name_to_id;
  }

 private:
  std::vector<VertexType> _vertices;
  std::vector<ArcType> _arcs;
  std::unordered_map<std::string, std::size_t> _vertex_name_to_id;
  std::size_t _root_vertex_id = kInvalidId;
};

}  // namespace icts
