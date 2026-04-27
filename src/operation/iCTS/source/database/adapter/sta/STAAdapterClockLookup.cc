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

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "api/TimingEngine.hh"
#include "design/Inst.hh"
#include "liberty/Lib.hh"

namespace icts {

namespace {

auto CountClockInputPins(idb::IdbPins* pin_list) -> std::size_t
{
  std::size_t clock_input_pins = 0U;
  if (pin_list == nullptr) {
    return clock_input_pins;
  }

  for (auto* idb_pin : pin_list->get_pin_list()) {
    if (idb_pin == nullptr || idb_pin->get_term() == nullptr) {
      continue;
    }
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      continue;
    }
    auto* idb_net = idb_pin->get_net();
    if (idb_net != nullptr && idb_net->is_clock()) {
      ++clock_input_pins;
    }
  }
  return clock_input_pins;
}

}  // namespace

auto STAAdapter::queryInstType(const std::string& inst_name) -> icts::InstType
{
  auto inst_type = icts::InstType::kUnknown;
  const auto name = sta_adapter_internal::NormalizeInstName(inst_name);

  auto* idb_inst = sta_adapter_internal::FindIdbInstance(name);
  LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name << " is not found in iDB when querying instance type.";
  auto* pin_list = idb_inst->get_pin_list();
  LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name << " type is unknown (none pin in iDB) which cell is unknown";
  auto* cell_master = idb_inst->get_cell_master();
  LOG_FATAL_IF(cell_master == nullptr) << "Instance " << name << " has no iDB cell master when querying instance type.";
  const std::string cell_master_name = cell_master->get_name();

  if (!idb_inst->is_clock_instance()) {
    LOG_WARNING << "Instance " << name << " type is unknown because iDB does not mark it as a clock instance.";
    return inst_type;
  }

  if (cell_master->is_block()) {
    return icts::InstType::kMacroBlock;
  }

  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master_name.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Instance " << name << " liberty cell \"" << cell_master_name
                << "\" is not found; fallback to iDB-only type heuristics.";
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

  constexpr std::size_t clock_input_pin_limit = 1U;
  if (CountClockInputPins(pin_list) > clock_input_pin_limit) {
    inst_type = icts::InstType::kMux;
    return inst_type;
  }

  LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master_name;
  return inst_type;
}

auto STAAdapter::isFlipFlop(const std::string& inst_name) -> bool
{
  return queryInstType(inst_name) == icts::InstType::kFlipFlop;
}

}  // namespace icts
