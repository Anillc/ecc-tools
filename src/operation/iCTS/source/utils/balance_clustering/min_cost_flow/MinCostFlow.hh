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
 * @file MinCostFlow.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once
#include <optional>
#include <ranges>
#include <vector>

#include "lemon/list_graph.h"
#include "lemon/maps.h"
#include "lemon/network_simplex.h"

using namespace lemon;
namespace icts {
/**
 * @brief MinCostFlow template class for solving clustring by min cost flow
 *       input: nodes, centers
 *       constraint: max_dist, max_fanout
 *       output: clusters
 *
 * @tparam Value
 */
template <typename Value>
class MinCostFlow
{
 public:
  MinCostFlow() = default;
  ~MinCostFlow() = default;

  void add_node(const double& x, const double& y, Value& value) { _nodes.push_back({FlowPoint{x, y}, value}); }

  void add_center(const double& x, const double& y) { _centers.push_back({x, y}); }

  std::vector<std::vector<Value>> run(const size_t& max_fanout)
  {
    std::vector<int> lower_bounds(_centers.size(), 0);
    std::vector<int> upper_bounds(_centers.size(), static_cast<int>(max_fanout));
    return run(lower_bounds, upper_bounds);
  }

  std::vector<std::vector<Value>> run(const std::vector<int>& lower_bounds, const std::vector<int>& upper_bounds)
  {
    // Flow problem:
    // virturl source -> [sinks] -> [buffers] -> virturl target
    // requirement: meet the max_fanout constraint

    // Define the node and arc types
    using Node = ListDigraph::Node;
    using Arc = ListDigraph::Arc;
    using ArcMap = ListDigraph::ArcMap<int>;
    using ArcIt = ListDigraph::ArcIt;
    using MinCostFlowSolver = NetworkSimplex<ListDigraph, int, int>;
    using NodeInfo = std::pair<int, int>;

    // Define the network
    ListDigraph network;

    // Add nodes to the network
    Node source = network.addNode();
    Node target = network.addNode();
    std::vector<Node> sinks, buffers;
    std::ranges::for_each(_nodes, [&](auto& node) { sinks.emplace_back(network.addNode()); });
    std::ranges::for_each(_centers, [&](auto& center) { buffers.emplace_back(network.addNode()); });

    // Add arcs to the network
    std::vector<Arc> source_sink_arcs, sink_buffer_arcs, buffer_target_arcs;
    // source sink arc
    std::ranges::for_each(sinks, [&](auto& sink) { source_sink_arcs.emplace_back(network.addArc(source, sink)); });
    // sink buffer arc, dist cost
    std::vector<float> dist_costs;
    for (size_t i = 0; i < sinks.size(); ++i) {
      for (size_t j = 0; j < buffers.size(); ++j) {
        auto dist = calcManhDist(_nodes[i].point, _centers[j]);
        sink_buffer_arcs.emplace_back(network.addArc(sinks[i], buffers[j]));
        dist_costs.emplace_back(dist);
      }
    }
    // buffer target arc
    std::ranges::for_each(buffers, [&](auto& buffer) { buffer_target_arcs.emplace_back(network.addArc(buffer, target)); });
    ArcMap arc_cost(network), arc_capacity(network), arc_lower(network);
    std::ranges::for_each(source_sink_arcs, [&](auto& arc) {
      arc_lower[arc] = 0;
      arc_capacity[arc] = 1;
    });
    for (size_t i = 0; i < sink_buffer_arcs.size(); ++i) {
      arc_lower[sink_buffer_arcs[i]] = 0;
      arc_capacity[sink_buffer_arcs[i]] = 1;
      arc_cost[sink_buffer_arcs[i]] = dist_costs[i];
    }
    for (size_t i = 0; i < buffer_target_arcs.size(); ++i) {
      arc_lower[buffer_target_arcs[i]] = i < lower_bounds.size() ? lower_bounds[i] : 0;
      arc_capacity[buffer_target_arcs[i]] = i < upper_bounds.size() ? upper_bounds[i] : 0;
    }

    // mcf solver by lemon
    MinCostFlowSolver mcf(network);
    mcf.costMap(arc_cost);
    mcf.lowerMap(arc_lower);
    mcf.upperMap(arc_capacity);
    mcf.stSupply(source, target, _nodes.size());
    auto status = mcf.run();
    if (status != MinCostFlowSolver::OPTIMAL) {
      return {};
    }
    ArcMap solution(network);
    mcf.flowMap(solution);

    // init the node map
    std::vector<NodeInfo> node_map(static_cast<size_t>(network.maxNodeId()) + 1, {-2, -2});
    auto get_node_info = [&](Node node) -> NodeInfo& { return node_map[static_cast<size_t>(network.id(node))]; };
    for (size_t i = 0; i < sinks.size(); ++i) {
      get_node_info(sinks[i]) = {static_cast<int>(i), -1};
    }

    for (size_t i = 0; i < buffers.size(); ++i) {
      get_node_info(buffers[i]) = {-1, static_cast<int>(i)};
    }

    // get the clusters
    std::vector<std::vector<Value>> clusters(_centers.size());
    for (ArcIt it(network); it != INVALID; ++it) {
      if (solution[it] == 0) {
        continue;
      }
      const auto& source_info = get_node_info(network.source(it));
      const auto& target_info = get_node_info(network.target(it));
      if (source_info.second == -1 && target_info.first == -1) {
        auto cluster_id = target_info.second;
        clusters[cluster_id].emplace_back(_nodes[source_info.first].value);
      }
    }
    // remove empty cluster
    clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [](auto& cluster) { return cluster.empty(); }), clusters.end());
    return clusters;
  }

 private:
  struct FlowPoint
  {
    double x;
    double y;
  };
  struct FlowNode
  {
    FlowPoint point;
    Value value;
  };
  static double calcManhDist(const FlowPoint& p1, const FlowPoint& p2) { return std::fabs(p1.x - p2.x) + std::fabs(p1.y - p2.y); }
  std::vector<FlowPoint> _centers;
  std::vector<FlowNode> _nodes;
};

}  // namespace icts
