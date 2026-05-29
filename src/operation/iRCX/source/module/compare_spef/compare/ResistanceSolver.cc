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
#include "compare/ResistanceSolver.hh"

#include <Eigen/Dense>
#include <cmath>
#include <map>
#include <vector>

#include "compare/CompareMath.hh"

namespace ircx {
namespace compare_spef {

auto ResistanceSolver::equivalentResistance(const Net& net, const std::string& from_node,
                                            const std::string& to_node) const -> std::optional<double>
{
  if (from_node == to_node) {
    return 0.0;
  }

  std::map<std::string, std::size_t> node_to_index;
  auto add_node = [&](const std::string& node) {
    if (!node_to_index.contains(node)) {
      node_to_index[node] = node_to_index.size();
    }
  };

  for (const auto& resistor : net.resistors) {
    if (resistor.resistance <= math::kEpsilon) {
      continue;
    }
    add_node(resistor.node1);
    add_node(resistor.node2);
  }

  if (!node_to_index.contains(from_node) || !node_to_index.contains(to_node)) {
    return std::nullopt;
  }

  const std::size_t ground = node_to_index[to_node];
  const std::size_t matrix_size = node_to_index.size() - 1;
  if (matrix_size == 0) {
    return 0.0;
  }

  std::vector<int> unknown_index(node_to_index.size(), -1);
  int next_unknown = 0;
  for (const auto& node_pair : node_to_index) {
    const std::size_t index = node_pair.second;
    if (index != ground) {
      unknown_index[index] = next_unknown++;
    }
  }

  Eigen::MatrixXd conductance = Eigen::MatrixXd::Zero(matrix_size, matrix_size);
  Eigen::VectorXd rhs = Eigen::VectorXd::Zero(matrix_size);

  const auto from_it = node_to_index.find(from_node);
  if (from_it == node_to_index.end() || from_it->second == ground) {
    return std::nullopt;
  }
  rhs[unknown_index[from_it->second]] = 1.0;

  for (const auto& resistor : net.resistors) {
    if (resistor.resistance <= math::kEpsilon) {
      continue;
    }
    const std::size_t idx1 = node_to_index[resistor.node1];
    const std::size_t idx2 = node_to_index[resistor.node2];
    const double g = 1.0 / resistor.resistance;
    const int u = unknown_index[idx1];
    const int v = unknown_index[idx2];
    if (u >= 0) {
      conductance(u, u) += g;
    }
    if (v >= 0) {
      conductance(v, v) += g;
    }
    if (u >= 0 && v >= 0) {
      conductance(u, v) -= g;
      conductance(v, u) -= g;
    }
  }

  Eigen::FullPivLU<Eigen::MatrixXd> solver(conductance);
  if (!solver.isInvertible()) {
    return std::nullopt;
  }

  const Eigen::VectorXd solution = solver.solve(rhs);
  const double value = solution[unknown_index[from_it->second]];
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  return std::abs(value);
}

}  // namespace compare_spef
}  // namespace ircx
