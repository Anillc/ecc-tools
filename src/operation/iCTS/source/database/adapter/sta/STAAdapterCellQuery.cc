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
 * @file STAAdapterCellQuery.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter liberty and pin query implementation.
 */

#include <glog/logging.h>

#include <cmath>
#include <optional>
#include <ostream>
#include <string>

#include "IdbCellMaster.h"
#include "IdbLayout.h"
#include "IdbUnits.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Type.hh"
#include "api/TimingEngine.hh"
#include "idm.h"
#include "liberty/Lib.hh"

namespace icts {

using namespace sta_adapter_internal;

auto STAAdapter::queryCellOutPinCapLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: liberty cell not found; caller may fallback to table-axis max.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (output == nullptr) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: output pin is unavailable; caller may fallback to table-axis max.";
    return 0.0;
  }

  auto cap_limit = output->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value()) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: max cap limit is not defined on output pin; caller may fallback to table-axis max.";
    return 0.0;
  }
  return ConvertLibCapToPf(lib_cell, *cap_limit);
}

auto STAAdapter::queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(cell_master, "output pin cap table-axis max",
                                 {ista::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE,
                                  ista::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE});
}

auto STAAdapter::queryCellInPinSlewLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: liberty cell not found; caller may fallback to table-axis max.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: input pin is unavailable; caller may fallback to table-axis max.";
    return 0.0;
  }

  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (!slew_limit.has_value()) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: max slew limit is not defined on input pin; caller may fallback to table-axis max.";
    return 0.0;
  }
  return ConvertLibTimeToNs(lib_cell, *slew_limit);
}

auto STAAdapter::queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(
      cell_master, "input pin slew table-axis max",
      {ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION, ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION,
       ista::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME, ista::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION});
}

auto STAAdapter::queryCellHeightUm(const std::string& cell_master) -> double
{
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr || idb_layout->get_units() == nullptr) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB layout metadata is not ready; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  auto* idb_master = idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB cell master is not found; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  const int dbu_per_micron = idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: invalid DBU-per-micron in iDB units; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  return static_cast<double>(idb_master->get_height()) / static_cast<double>(dbu_per_micron);
}

}  // namespace icts
