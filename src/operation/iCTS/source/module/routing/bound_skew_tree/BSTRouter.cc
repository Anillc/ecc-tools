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
 * @file BSTRouter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Standalone BST router adapter implementation for bounded-skew routing.
 */

#include "BSTRouter.hh"

#include <glog/logging.h>

#include <cmath>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

#include "BSTRouterInternal.hh"
#include "BSTTypes.hh"
#include "BoundSkewTree.hh"
#include "Components.hh"
#include "Log.hh"
#include "Point.hh"
#include "RoutingTerminal.hh"

namespace icts {
namespace {

using bst::Area;
using bst::BoundSkewTree;

using AreaStore = std::vector<std::unique_ptr<Area>>;

auto BuildDefaultParameters(const BSTParameters& parameters) -> BSTParameters
{
  auto normalized = parameters;
  if (normalized.db_unit <= 0) {
    normalized.db_unit = 1;
  }
  if (normalized.skew_bound <= 0) {
    normalized.skew_bound = 0.0;
  }
  return normalized;
}

auto NormalizeTopoTypeForBuild(const BSTParameters& parameters) -> TopoType
{
  LOG_FATAL_IF(parameters.topo_type == TopoType::kInputTopo)
      << "BSTRouter::buildTree received TopoType::kInputTopo; call buildTreeFromTopology for input-topology routing.";
  return parameters.topo_type;
}

auto BuildLoadArea(const ClockRoutingTerminal& terminal, const BSTParameters& parameters, AreaStore& owned_areas) -> Area*
{
  auto min_delay = terminal.insertion_delay;
  auto max_delay = terminal.insertion_delay;
  if (max_delay - min_delay > parameters.skew_bound) {
    min_delay = max_delay - parameters.skew_bound;
  }

  auto cap_load = terminal.pin_cap;
  auto area = std::make_unique<Area>(terminal.name, static_cast<double>(terminal.location.get_x()) / parameters.db_unit,
                                     static_cast<double>(terminal.location.get_y()) / parameters.db_unit, cap_load, min_delay, max_delay,
                                     0.0, parameters.pattern, true);
  auto* area_ptr = area.get();
  owned_areas.push_back(std::move(area));
  return area_ptr;
}

}  // namespace

auto BSTRouter::buildTree(const std::vector<Terminal>& load_terminals, const BSTParameters& parameters) -> BSTRouter::ClockSteinerTreeType
{
  auto normalized = BuildDefaultParameters(parameters);
  auto topo_type = NormalizeTopoTypeForBuild(normalized);

  const ClockSteinerTreeType empty_tree;
  if (load_terminals.empty()) {
    return empty_tree;
  }

  AreaStore load_areas;
  load_areas.reserve(load_terminals.size());
  for (const auto& terminal : load_terminals) {
    BuildLoadArea(terminal, normalized, load_areas);
  }

  BoundSkewTree solver(std::move(load_areas), normalized, topo_type);
  solver.run();
  return ExportBstClockTree(solver.get_root(), normalized);
}

auto BSTRouter::buildTreeFromTopology(const ClockSteinerTreeType& input_topology, const BSTParameters& parameters)
    -> BSTRouter::ClockSteinerTreeType
{
  auto normalized = BuildDefaultParameters(parameters);
  return BuildBstFromInputTopology(input_topology, normalized);
}

}  // namespace icts
