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
 * @file Router.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Unified routing dispatch facade
 */

#pragma once

#include <optional>
#include <vector>

#include "RCTree.hh"
#include "RoutingTerminal.hh"
#include "SteinerTree.hh"
#include "local_legalization/LocalLegalization.hh"

namespace icts {

struct BSTParameters;
class Pin;

class Router
{
 public:
  using ClockTerminal = ClockRoutingTerminal;
  using ClockSteinerTreeType = ClockSteinerTree<>;
  using RCTreeType = RCTree;
  using LegalizationRegion = LocalLegalization::RegionType;
  using LegalizationOptions = LocalLegalization::Options;
  using LegalizationResult = LocalLegalization::Result;

  struct RCTreeBuildOptions
  {
    std::optional<int> routing_layer = std::nullopt;
    std::optional<double> wire_width = std::nullopt;
  };

  Router() = delete;
  ~Router() = default;

  static ClockSteinerTreeType buildFluteTree(const ClockTerminal& driver_terminal, const std::vector<ClockTerminal>& load_terminals);
  static ClockSteinerTreeType buildSaltTree(const ClockTerminal& driver_terminal, const std::vector<ClockTerminal>& load_terminals);
  static ClockSteinerTreeType buildBstTree(const std::vector<ClockTerminal>& load_terminals, const BSTParameters& parameters);
  static ClockSteinerTreeType buildCbsTree(const std::vector<ClockTerminal>& load_terminals, const BSTParameters& parameters);

  static LegalizationResult legalizePins(std::vector<Pin*>& movable_pins, const std::vector<Pin*>& fixed_pins,
                                         const LegalizationRegion& feasible_region, const LegalizationRegion& block_region);
  static LegalizationResult legalizePins(std::vector<Pin*>& movable_pins, const std::vector<Pin*>& fixed_pins,
                                         const LegalizationRegion& feasible_region, const LegalizationRegion& block_region,
                                         const LegalizationOptions& options);

  static RCTreeType buildRCTree(const ClockSteinerTreeType& clock_tree);
  static RCTreeType buildRCTree(const ClockSteinerTreeType& clock_tree, const RCTreeBuildOptions& options);
};

}  // namespace icts
