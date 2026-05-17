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
 * @file TreeBufferSizingBranch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Critical-branch extraction for clock-tree buffer sizing.
 */

#include <cstddef>
#include <optional>
#include <vector>

#include "RootedTreeLCA.hh"
#include "buffer_sizing/BufferSizingTypes.hh"
#include "buffer_sizing/TreeBufferSizing.hh"

namespace icts::buffer_sizing {
namespace {

auto buildParentVector(const TreeSizingProblem& problem) -> std::optional<std::vector<std::size_t>>
{
  if (problem.nodes.empty() || problem.root_node_id >= problem.nodes.size()) {
    return std::nullopt;
  }
  std::vector<std::size_t> parents(problem.nodes.size(), graph::RootedTreeLCA::kInvalidNode);
  for (std::size_t node_id = 0U; node_id < problem.nodes.size(); ++node_id) {
    if (node_id == problem.root_node_id) {
      continue;
    }
    if (problem.nodes.at(node_id).parent_id >= problem.nodes.size()) {
      return std::nullopt;
    }
    parents.at(node_id) = problem.nodes.at(node_id).parent_id;
  }
  return parents;
}

}  // namespace

auto TreeBufferSizing::criticalBranchCandidates(const TreeSizingProblem& problem, const TreeEvaluation& evaluation)
    -> std::vector<std::size_t>
{
  if (!evaluation.valid) {
    return {};
  }
  const auto parents = buildParentVector(problem);
  if (!parents.has_value()) {
    return {};
  }
  const graph::RootedTreeLCA lca(*parents);
  if (!lca.isValid()) {
    return {};
  }
  const auto ancestor = lca.lca(evaluation.min_sink_node_id, evaluation.max_sink_node_id);
  if (ancestor == graph::RootedTreeLCA::kInvalidNode) {
    return {};
  }

  const auto path = lca.ancestorPath(ancestor, evaluation.max_sink_node_id, false, true);
  std::vector<std::size_t> buffer_nodes;
  for (const auto node_id : path) {
    if (node_id < problem.nodes.size() && problem.nodes.at(node_id).kind == TreeNodeKind::kBuffer) {
      buffer_nodes.push_back(node_id);
    }
  }
  return buffer_nodes;
}

}  // namespace icts::buffer_sizing
