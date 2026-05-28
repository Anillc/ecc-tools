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
#include <memory>
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
#include "Vector.hh"
#include "api/TimingEngine.hh"
#include "design/Inst.hh"
#include "liberty/Lib.hh"
#include "timing_query/STAAdapterTimingQuery.hh"

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

auto TermName(idb::IdbPin* idb_pin) -> std::string
{
  auto* term = idb_pin == nullptr ? nullptr : idb_pin->get_term();
  return term == nullptr ? std::string{} : term->get_name();
}

auto FindLibPort(ista::LibCell* lib_cell, idb::IdbPin* idb_pin) -> ista::LibPort*
{
  if (lib_cell == nullptr || idb_pin == nullptr) {
    return nullptr;
  }
  const auto port_name = TermName(idb_pin);
  if (port_name.empty()) {
    return nullptr;
  }
  return lib_cell->get_cell_port_or_port_bus(port_name.c_str());
}

auto CollectClockInputPins(idb::IdbPins* pin_list) -> std::vector<idb::IdbPin*>
{
  std::vector<idb::IdbPin*> clock_input_pins;
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
      clock_input_pins.push_back(idb_pin);
    }
  }
  return clock_input_pins;
}

auto IsCombinationalTimingType(ista::LibArc::TimingType timing_type) -> bool
{
  return timing_type == ista::LibArc::TimingType::kComb || timing_type == ista::LibArc::TimingType::kCombRise
         || timing_type == ista::LibArc::TimingType::kCombFall || timing_type == ista::LibArc::TimingType::kDefault;
}

auto HasTransparentDataArcForClockPin(ista::LibCell* lib_cell, idb::IdbPin* clock_pin) -> bool
{
  if (lib_cell == nullptr || clock_pin == nullptr) {
    return false;
  }
  const auto clock_pin_name = TermName(clock_pin);
  if (clock_pin_name.empty()) {
    return false;
  }
  for (auto& arc_set : lib_cell->get_cell_arcs()) {
    if (arc_set == nullptr) {
      continue;
    }
    for (auto& arc : arc_set->get_arcs()) {
      auto* lib_arc = arc.get();
      if (lib_arc == nullptr || !IsCombinationalTimingType(lib_arc->get_timing_type())) {
        continue;
      }
      if (clock_pin_name == lib_arc->get_src_port()) {
        continue;
      }
      auto* src_port = lib_cell->get_cell_port_or_port_bus(lib_arc->get_src_port());
      auto* snk_port = lib_cell->get_cell_port_or_port_bus(lib_arc->get_snk_port());
      if (src_port != nullptr && src_port->isInput() != 0U && snk_port != nullptr && snk_port->isOutput() != 0U) {
        return true;
      }
    }
  }
  return false;
}

auto IsLibertySequentialSinkPin(ista::LibCell* lib_cell, idb::IdbPin* idb_pin) -> bool
{
  if (lib_cell == nullptr || idb_pin == nullptr) {
    return false;
  }
  auto* lib_port = FindLibPort(lib_cell, idb_pin);
  if (lib_port != nullptr && (lib_port->isClock() || lib_port->get_is_clock_pin())) {
    return true;
  }
  const auto pin_name = TermName(idb_pin);
  if (pin_name.empty()) {
    return false;
  }
  for (auto& arc_set : lib_cell->get_cell_arcs()) {
    if (arc_set == nullptr) {
      continue;
    }
    for (auto& arc : arc_set->get_arcs()) {
      auto* lib_arc = arc.get();
      if (lib_arc == nullptr || pin_name != lib_arc->get_src_port()) {
        continue;
      }
      const auto timing_type = lib_arc->get_timing_type();
      if (lib_arc->isCheckArc() != 0U || timing_type == ista::LibArc::TimingType::kRisingEdge
          || timing_type == ista::LibArc::TimingType::kFallingEdge) {
        return true;
      }
    }
  }
  return idb_pin->is_flip_flop_clk();
}

auto ClassifySequentialInst(ista::LibCell* lib_cell, idb::IdbPins* pin_list) -> icts::InstType
{
  const auto clock_input_pins = CollectClockInputPins(pin_list);
  auto* primary_clock_pin = clock_input_pins.empty() ? nullptr : clock_input_pins.front();
  if (primary_clock_pin != nullptr && HasTransparentDataArcForClockPin(lib_cell, primary_clock_pin)) {
    return icts::InstType::kLatch;
  }
  if (primary_clock_pin == nullptr || IsLibertySequentialSinkPin(lib_cell, primary_clock_pin)) {
    return icts::InstType::kFlipFlop;
  }
  return icts::InstType::kFlipFlop;
}

}  // namespace

auto STAAdapter::queryInstType(const std::string& inst_name) -> icts::InstType
{
  observeQueryFacade();
  auto inst_type = icts::InstType::kUnknown;
  const auto name = sta_adapter_timing_query::NormalizeInstName(inst_name);

  auto* idb_inst = sta_adapter_timing_query::FindIdbInstance(name);
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

  auto* lib_cell = sta_adapter_timing_query::GetStaEngine()->findLibertyCell(cell_master_name.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Instance " << name << " liberty cell \"" << cell_master_name << "\" is not found; using iDB-only type heuristics.";
  } else if (lib_cell->isSequentialCell()) {
    inst_type = ClassifySequentialInst(lib_cell, pin_list);
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

  if (idb_inst->is_clock_instance()) {
    inst_type = icts::InstType::kBoundaryLoad;
    return inst_type;
  }

  LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master_name;
  return inst_type;
}

auto STAAdapter::isFlipFlop(const std::string& inst_name) -> bool
{
  const auto inst_type = queryInstType(inst_name);
  return inst_type == icts::InstType::kFlipFlop || inst_type == icts::InstType::kLatch;
}

}  // namespace icts
