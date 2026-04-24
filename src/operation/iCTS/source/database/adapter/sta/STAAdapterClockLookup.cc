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
 * @file STAAdapterClockLookup.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter clock and instance lookup implementation.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbPins.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "api/TimingEngine.hh"
#include "design/Inst.hh"
#include "liberty/Lib.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "sta/StaClock.hh"

namespace icts {

using namespace sta_adapter_internal;

auto STAAdapter::queryInstType(const std::string& inst_name) -> icts::InstType
{
  auto inst_type = icts::InstType::kUnknown;
  const auto name = NormalizeInstName(inst_name);
  std::string cell_master = "unknown";
  auto* idb_inst = FindIdbInstance(name);
  LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name << " is not found in iDB when querying instance type.";
  auto* pin_list = idb_inst->get_pin_list();
  LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name << " type is unknown (none pin in iDB) which cell is " << cell_master;

  auto* lib_cell = FindLibertyCellForInstName(name, cell_master);
  if (lib_cell == nullptr) {
    LOG_WARNING << "Instance " << name << " liberty cell \"" << cell_master << "\" is not found; fallback to iDB-only type heuristics.";
  } else if (lib_cell->isSequentialCell()) {
    inst_type = icts::InstType::kFlipFlop;
  } else if (lib_cell->isBuffer()) {
    inst_type = icts::InstType::kBuffer;
  } else if (lib_cell->isInverter()) {
    inst_type = icts::InstType::kInverter;
  } else if (lib_cell->isICG()) {
    inst_type = icts::InstType::kClockGate;
  }
  if (inst_type != icts::InstType::kUnknown) {
    return inst_type;
  }

  constexpr std::ptrdiff_t clock_input_pin_limit = 1;
  const auto clock_input_pins = std::ranges::count_if(pin_list->get_pin_list(), [&](auto* idb_pin) -> bool {
    if (idb_pin == nullptr || idb_pin->get_term() == nullptr) {
      return false;
    }
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      return false;
    }
    auto* idb_net = idb_pin->get_net();
    LOG_FATAL_IF(idb_net == nullptr) << "Instance " << name << " pin " << idb_pin->get_pin_name()
                                     << " is not connected to any net, error in inst type judgement, which cell is " << cell_master;

    if (idb_net->is_clock() == 0U) {
      LOG_WARNING << "Instance " << name << " pin " << idb_pin->get_pin_name() << " connected net " << idb_net->get_net_name()
                  << " is not clock net, warning in inst type judgement, which cell is " << cell_master;
      return false;
    }
    return true;
  });
  if (clock_input_pins > clock_input_pin_limit) {
    inst_type = icts::InstType::kMux;
    return inst_type;
  }

  LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master;
  return inst_type;
}

auto STAAdapter::isFlipFlop(const std::string& inst_name) -> bool
{
  return queryInstType(inst_name) == icts::InstType::kFlipFlop;
}

auto STAAdapter::isClockNet(const std::string& net_name) -> bool
{
  getInst().prepareFullDesignTiming();
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    LOG_ERROR << "STA netlist is not ready.";
    return false;
  }
  auto* sta_net = sta_netlist->findNet(net_name.c_str());
  if (sta_net == nullptr) {
    LOG_ERROR << "Net " << net_name << " is not found in the STA netlist.";
    return false;
  }
  return sta_net->isClockNet();
}

auto STAAdapter::collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>
{
  getInst().prepareFullDesignTiming();
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    LOG_ERROR << "STA netlist is null when collecting clock nets.";
    return clock_net_pairs;
  }

  ista::Net* sta_net = nullptr;
  FOREACH_NET(sta_netlist, sta_net)
  {
    if (!sta_net->isClockNet()) {
      continue;
    }
    auto* sta_clock = GetStaEngine()->getPropClockOfNet(sta_net);
    if (sta_clock == nullptr) {
      LOG_WARNING << "Clock net \"" << sta_net->get_name() << "\" has no propagated clock in STA.";
      continue;
    }
    clock_net_pairs.emplace_back(sta_clock->get_clock_name(), sta_net->get_name());
  }
  return clock_net_pairs;
}

}  // namespace icts
