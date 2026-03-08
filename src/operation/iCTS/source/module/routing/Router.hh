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
 * @brief Unified stage-1 routing dispatch facade
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "legacy_module/CtsPoint.hh"

namespace icts {

class Inst;
class Pin;

enum class SteinerRouterType
{
  kFlute,
  kSalt,
};

enum class ClockTreeRouterType
{
  kBst,
  kBstSalt,
  kCbs,
  kDefault,
};

class Router
{
 public:
  Router() = delete;
  ~Router() = default;

  static void routeSteiner(SteinerRouterType router_type, const std::string& net_name, Pin* driver_pin, const std::vector<Pin*>& load_pins);

  static Inst* routeClockTree(ClockTreeRouterType router_type, const std::string& net_name, const std::vector<Pin*>& load_pins,
                              const std::optional<double>& skew_bound = std::nullopt, const std::optional<Point>& guide_loc = std::nullopt);
};

}  // namespace icts
