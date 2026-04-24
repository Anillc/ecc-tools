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
 * @file STAAdapterWireRc.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter wire RC query implementation.
 */

#include <glog/logging.h>

#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "logger/Schema.hh"

namespace icts {

using namespace sta_adapter_internal;

auto STAAdapter::queryWireResistance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getResistance(routing_layer, length, wire_width);
}

auto STAAdapter::queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getCapacitance(routing_layer, length, wire_width);
}

auto STAAdapter::emitUnitWireRcReport(const std::string& title, int routing_layer, std::optional<double> wire_width) -> void
{
  const WireRcProbe probe = QueryWireRcProbe(routing_layer, wire_width);
  EmitWireRcProbeDiagnostic(probe);
  schema::EmitTable(title, {"Item", "Value", "Detail"}, BuildWireRcRows(probe));
}

auto STAAdapter::emitConfiguredUnitWireRcReport(const std::string& title) -> void
{
  emitUnitWireRcReport(title, ResolveConfiguredRoutingLayer(), ResolveConfiguredWireWidth());
}

}  // namespace icts
