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
 * @file Router.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Unified stage-1 routing dispatch facade implementation
 */

#include "Router.hh"

#include "BoundSkewTree.hh"
#include "CBS.hh"
#include "FLUTE.hh"
#include "SALT.hh"
#include "TreeBuilder.hh"
#include "utils/logger/Logger.hh"

namespace icts {

void Router::routeSteiner(SteinerRouterType router_type, const std::string& net_name, Pin* driver_pin, const std::vector<Pin*>& load_pins)
{
  switch (router_type) {
    case SteinerRouterType::kFlute:
      FLUTERouter::route(net_name, driver_pin, load_pins);
      return;
    case SteinerRouterType::kSalt:
      SALTRouter::route(net_name, driver_pin, load_pins);
      return;
    default:
      CTS_LOG_FATAL << "Unsupported Steiner router type";
      return;
  }
}

Inst* Router::routeClockTree(ClockTreeRouterType router_type, const std::string& net_name, const std::vector<Pin*>& load_pins,
                             const std::optional<double>& skew_bound, const std::optional<Point>& guide_loc)
{
  switch (router_type) {
    case ClockTreeRouterType::kBst:
      return BSTRouter::route(net_name, load_pins, skew_bound, guide_loc, TopoType::kGreedyDist);
    case ClockTreeRouterType::kBstSalt:
      return TreeBuilder::bstSaltTree(net_name, load_pins, skew_bound, guide_loc, TopoType::kGreedyDist);
    case ClockTreeRouterType::kCbs:
      return CBSRouter::route(net_name, load_pins, skew_bound, guide_loc, TopoType::kGreedyDist);
    case ClockTreeRouterType::kDefault:
      return TreeBuilder::defaultTree(net_name, load_pins, skew_bound, guide_loc, TopoType::kGreedyDist);
    default:
      CTS_LOG_FATAL << "Unsupported clock tree router type";
      return nullptr;
  }
}

}  // namespace icts
