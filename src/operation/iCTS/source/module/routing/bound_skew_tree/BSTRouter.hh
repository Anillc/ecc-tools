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
 * @file BSTRouter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Standalone BST router adapter for bounded-skew routing.
 */

#pragma once

#include <vector>

#include "RoutingTypes.hh"
#include "SteinerTree.hh"

namespace icts {

struct BSTParameters;

class BSTRouter
{
 public:
  using Terminal = RoutingTerminal;
  using ClockSteinerTreeType = ClockSteinerTree<int>;

  BSTRouter() = delete;
  ~BSTRouter() = default;

  static ClockSteinerTreeType buildTree(const std::vector<Terminal>& load_terminals, const BSTParameters& parameters);

  static ClockSteinerTreeType buildTreeFromTopology(const ClockSteinerTreeType& input_topology, const BSTParameters& parameters);
};

}  // namespace icts
