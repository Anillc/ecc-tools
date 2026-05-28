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
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "logger/Schema.hh"
#include "timing_query/STAAdapterTimingQuery.hh"

namespace icts {

class Config;

namespace {

auto requireWireRcAdapter(const char* metric) -> ista::TimingIDBAdapter*
{
  auto* idb_adapter = sta_adapter_timing_query::GetStaEngine()->getIDBAdapter();
  LOG_FATAL_IF(idb_adapter == nullptr) << "STA IDB adapter is not ready for required wire " << metric << " query.";
  return idb_adapter;
}

auto requireWireRcQuery(const char* metric, int routing_layer, double length) -> void
{
  LOG_FATAL_IF(routing_layer <= 0) << "Required wire " << metric << " query needs a positive routing layer.";
  LOG_FATAL_IF(!std::isfinite(length) || length < 0.0)
      << "Required wire " << metric << " query needs a finite non-negative length, got " << length << " um.";
}

}  // namespace

auto STAAdapter::queryWireResistance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  observeQueryFacade();
  auto* idb_adapter = sta_adapter_timing_query::GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getResistance(routing_layer, length, wire_width);
}

auto STAAdapter::queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  observeQueryFacade();
  auto* idb_adapter = sta_adapter_timing_query::GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getCapacitance(routing_layer, length, wire_width);
}

auto STAAdapter::queryRequiredWireResistance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  observeQueryFacade();
  requireWireRcQuery("resistance", routing_layer, length);
  return requireWireRcAdapter("resistance")->getResistance(routing_layer, length, wire_width);
}

auto STAAdapter::queryRequiredWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  observeQueryFacade();
  requireWireRcQuery("capacitance", routing_layer, length);
  return requireWireRcAdapter("capacitance")->getCapacitance(routing_layer, length, wire_width);
}

auto STAAdapter::emitUnitWireRcReport(const std::string& title, int routing_layer, std::optional<double> wire_width) -> void
{
  const sta_adapter_timing_query::WireRcProbe probe = sta_adapter_timing_query::QueryWireRcProbe(*this, routing_layer, wire_width);
  if (probe.has_diagnostic) {
    const char* level = probe.diagnostic_level == DiagnosticLevel::kError ? "error" : "warning";
    LOG_WARNING << title << ": STAAdapter unit wire RC diagnostic (" << level << "): " << probe.diagnostic_summary;
  }
}

auto STAAdapter::emitConfiguredUnitWireRcReport(SchemaWriter& reporter, const Config& config, const std::string& title) -> void
{
  const sta_adapter_timing_query::WireRcProbe probe = sta_adapter_timing_query::QueryWireRcProbe(
      *this, sta_adapter_timing_query::ResolveConfiguredRoutingLayer(config), sta_adapter_timing_query::ResolveConfiguredWireWidth(config));
  sta_adapter_timing_query::EmitWireRcProbeDiagnostic(reporter, probe);
  EmitTable(reporter, title, {"Item", "Value", "Detail"}, sta_adapter_timing_query::BuildWireRcRows(probe));
}

}  // namespace icts
