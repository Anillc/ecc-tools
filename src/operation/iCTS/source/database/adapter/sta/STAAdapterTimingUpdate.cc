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
 * @file STAAdapterTimingUpdate.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter timing update and state reset implementation.
 */

#include <glog/logging.h>

#include <cmath>
#include <ostream>
#include <string>
#include <utility>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "TimingDBAdapter.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "liberty/Lib.hh"
#include "sta/Sta.hh"

namespace icts {

using namespace sta_adapter_internal;

auto STAAdapter::updateTiming() -> void
{
  getInst().prepareFullDesignTiming();
  GetStaEngine()->updateTiming();
}

auto STAAdapter::queryCharClockAT(const std::string& clock_name) -> double
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before clock-arrival query.";
  return QueryCharClockATFromVertex(runtime.sink_vertex, clock_name);
}

auto STAAdapter::queryCharSlew() -> double
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before slew query.";
  return QueryCharSlewFromVertex(runtime.sink_vertex);
}

auto STAAdapter::queryCharInputPinCap(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin cap", cell_master) << " failed: liberty cell not found; return 0.0.";
    return 0.0;
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    return 0.0;
  }
  return ConvertLibCapToPf(lib_cell, input->get_port_cap());
}

auto STAAdapter::queryPinCapacitance(const Pin* pin) -> double
{
  if (pin == nullptr) {
    LOG_WARNING << "Null pin provided when querying pin capacitance.";
    return 0.0;
  }

  auto* inst = pin->get_inst();
  const std::string pin_full_name = inst != nullptr ? (inst->get_name() + "/" + pin->get_name()) : pin->get_name();
  if (inst == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: CTS pin has no owning instance for " << pin_full_name << ".";
    return 0.0;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    LOG_WARNING << "Pin-cap query skipped: CTS instance has no cell master for " << pin_full_name << ".";
    return 0.0;
  }

  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty cell \"" << cell_master << "\" is not found for " << pin_full_name << ".";
    return 0.0;
  }

  const auto port_name = NormalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty port \"" << port_name << "\" is not found on cell " << cell_master << ".";
    return 0.0;
  }
  if (lib_port->isInput() == 0U) {
    return 0.0;
  }

  return QueryLibPortCapacitancePf(lib_cell, lib_port);
}

auto STAAdapter::queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("buffer ports", cell_master) << " failed: liberty cell not found; return empty port names.";
    return {"", ""};
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  const std::string in_name = input != nullptr ? input->get_port_name() : "";
  const std::string out_name = output != nullptr ? output->get_port_name() : "";
  return {in_name, out_name};
}

auto STAAdapter::destroyCharInstance(const std::string& inst_name) -> void
{
  (void) inst_name;
  resetCharContext();
}

auto STAAdapter::destroyCharNet(const std::string& net_name) -> void
{
  (void) net_name;
  resetCharContext();
}

auto STAAdapter::resetStaTransientState() -> void
{
  resetCharTimingState();
  resetCharPowerState();
  ipower::Power::destroyPower();

  auto* timing_engine = GetStaEngine();
  auto* ista = timing_engine->get_ista();
  if (ista != nullptr) {
    ista->resetAllRcNet();
    ista->resetSdcConstrain();
  }
  timing_engine->resetNetlist();
  timing_engine->resetGraph();
}

auto STAAdapter::prepareFullDesignTiming() -> void
{
  auto* timing_engine = GetStaEngine();
  LOG_FATAL_IF(timing_engine->get_db_adapter() == nullptr) << "STA full-design preparation requires STAAdapter::init() first.";

  if (_is_char_only_active) {
    resetStaTransientState();
    _is_char_only_active = false;
  }
  if (HasFullDesignTimingContext()) {
    return;
  }

  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
  resetStaTransientState();
  timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  LoadConfiguredSdcIfPresent(timing_engine);
  timing_engine->buildGraph();
  timing_engine->initRcTree();
  timing_engine->get_ista()->set_n_worst_path_per_clock(kWorstPathPerClock);
}

auto STAAdapter::resetCharTimingState() -> void
{
  _char_timing_state = CharTimingState{};
}

auto STAAdapter::resetCharPowerState() -> void
{
  _char_power_state = CharPowerState{};
}

}  // namespace icts
