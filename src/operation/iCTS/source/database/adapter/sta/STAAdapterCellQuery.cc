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

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbLayout.h"
#include "IdbUnits.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Type.hh"
#include "api/TimingEngine.hh"
#include "config/Config.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "idm.h"
#include "liberty/Lib.hh"
#include "sta/Sta.hh"

namespace icts {
namespace {

auto ResolvePositiveMin(const std::vector<std::optional<double>>& values) -> double
{
  double min_value = std::numeric_limits<double>::infinity();
  bool found_value = false;
  for (const auto& value : values) {
    if (!value.has_value() || *value <= 0.0) {
      continue;
    }
    min_value = std::min(min_value, *value);
    found_value = true;
  }
  return found_value ? min_value : 0.0;
}

auto ResolvePositiveMax(const std::vector<std::optional<double>>& values) -> double
{
  double max_value = 0.0;
  for (const auto& value : values) {
    if (!value.has_value() || *value <= 0.0) {
      continue;
    }
    max_value = std::max(max_value, *value);
  }
  return max_value;
}

auto queryLibOutputPinCapLimitPf(const Pin* pin) -> double
{
  if (pin == nullptr || pin->get_inst() == nullptr) {
    return 0.0;
  }

  const auto* inst = pin->get_inst();
  const auto& cell_master = inst->get_cell_master();
  const auto pin_full_name = Design::getPinFullName(pin);
  if (cell_master.empty()) {
    LOG_WARNING << "Clock-source drive-cap query skipped: CTS inst has no cell master for " << pin_full_name << ".";
    return 0.0;
  }

  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Clock-source drive-cap query skipped: liberty cell \"" << cell_master << "\" is not found for " << pin_full_name << ".";
    return 0.0;
  }

  const auto port_name = sta_adapter_internal::NormalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    LOG_WARNING << "Clock-source drive-cap query skipped: liberty port \"" << port_name << "\" is not found on cell " << cell_master << ".";
    return 0.0;
  }
  if (lib_port->isOutput() == 0U) {
    LOG_WARNING << "Clock-source drive-cap query skipped: liberty port \"" << port_name << "\" on cell " << cell_master
                << " is not an output/inout port.";
    return 0.0;
  }

  const auto cap_limit = lib_port->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value() || *cap_limit <= 0.0) {
    LOG_WARNING << "Clock-source drive-cap query found no max-cap limit on liberty port \"" << port_name << "\" of cell " << cell_master
                << "; caller may use configured max_cap policy.";
    return 0.0;
  }
  return sta_adapter_internal::ConvertLibCapToPf(lib_cell, *cap_limit);
}

auto queryBufferOutputTableAxisMaxPf(const Pin* pin) -> double
{
  const auto* inst = pin != nullptr ? pin->get_inst() : nullptr;
  if (inst == nullptr || inst->get_cell_master().empty()) {
    return 0.0;
  }
  return STAAdapter::queryCellOutPinCapTableAxisMax(inst->get_cell_master());
}

auto queryStaVertexCapLimitPf(const std::string& pin_full_name, bool allow_refresh_full_context) -> double
{
  if (pin_full_name.empty()) {
    return 0.0;
  }

  if (!sta_adapter_internal::HasFullDesignTimingContext() && allow_refresh_full_context && sta_adapter_internal::HasStaBaseContext()) {
    STA_ADAPTER_INST.refreshFullDesignTimingContext();
  }
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    return 0.0;
  }

  auto* vertex = sta_adapter_internal::FindStaVertex(pin_full_name);
  if (vertex == nullptr) {
    LOG_WARNING << "Clock-source drive-cap query skipped: STA vertex \"" << pin_full_name << "\" is not found.";
    return 0.0;
  }

  auto* ista = sta_adapter_internal::GetStaEngine()->get_ista();
  if (ista == nullptr) {
    LOG_WARNING << "Clock-source drive-cap query skipped: iSTA context is null.";
    return 0.0;
  }

  return ResolvePositiveMin({
      ista->getVertexCapacitanceLimit(vertex, ista::AnalysisMode::kMax, ista::TransType::kRise),
      ista->getVertexCapacitanceLimit(vertex, ista::AnalysisMode::kMax, ista::TransType::kFall),
  });
}

auto queryConfiguredMaxCapBoundaryPf(const Pin* clock_source) -> double
{
  if (CONFIG_INST.has_max_cap() && CONFIG_INST.get_max_cap() > 0.0) {
    LOG_WARNING << "Clock-source drive-cap query uses runtime max_cap=" << CONFIG_INST.get_max_cap()
                << " pF as the hard source boundary for " << Design::getPinFullName(clock_source)
                << " because source-specific STA/liberty cap limit is unavailable.";
    return CONFIG_INST.get_max_cap();
  }
  return 0.0;
}

}  // namespace

auto STAAdapter::queryCellOutPinCapLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: liberty cell not found; caller may use table-axis max policy.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (output == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: output pin is unavailable; caller may use table-axis max policy.";
    return 0.0;
  }

  auto cap_limit = output->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value()) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: max cap limit is not defined on output pin; caller may use table-axis max policy.";
    return 0.0;
  }
  return sta_adapter_internal::ConvertLibCapToPf(lib_cell, *cap_limit);
}

auto STAAdapter::queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double
{
  return sta_adapter_internal::QueryBufferTableAxisMax(cell_master, "output pin cap table-axis max",
                                                       {ista::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE,
                                                        ista::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE});
}

auto STAAdapter::queryClockSourceDriveCapLimit(const Pin* clock_source) -> double
{
  if (clock_source == nullptr) {
    LOG_WARNING << "Clock-source drive-cap query skipped: clock source pin is null.";
    return 0.0;
  }

  const auto pin_full_name = Design::getPinFullName(clock_source);
  if (clock_source->get_inst() != nullptr) {
    const double lib_cap_limit_pf = queryLibOutputPinCapLimitPf(clock_source);
    if (lib_cap_limit_pf > 0.0) {
      return lib_cap_limit_pf;
    }

    const double table_axis_cap_limit_pf = queryBufferOutputTableAxisMaxPf(clock_source);
    if (table_axis_cap_limit_pf > 0.0) {
      return table_axis_cap_limit_pf;
    }

    double sta_cap_limit_pf = queryStaVertexCapLimitPf(pin_full_name, false);
    if (sta_cap_limit_pf > 0.0) {
      return sta_cap_limit_pf;
    }
    const double configured_cap_limit_pf = queryConfiguredMaxCapBoundaryPf(clock_source);
    if (configured_cap_limit_pf > 0.0) {
      return configured_cap_limit_pf;
    }
    sta_cap_limit_pf = queryStaVertexCapLimitPf(pin_full_name, true);
    return sta_cap_limit_pf > 0.0 ? sta_cap_limit_pf : 0.0;
  }

  double sta_cap_limit_pf = queryStaVertexCapLimitPf(pin_full_name, false);
  if (sta_cap_limit_pf > 0.0) {
    return sta_cap_limit_pf;
  }

  const double configured_cap_limit_pf = queryConfiguredMaxCapBoundaryPf(clock_source);
  if (configured_cap_limit_pf > 0.0) {
    return configured_cap_limit_pf;
  }

  sta_cap_limit_pf = queryStaVertexCapLimitPf(pin_full_name, true);
  if (sta_cap_limit_pf > 0.0) {
    return sta_cap_limit_pf;
  }

  if (!clock_source->is_io()) {
    LOG_WARNING << "Clock-source drive-cap query skipped: CTS pin \"" << pin_full_name
                << "\" has no owning inst and is not marked as top-level IO.";
  }
  return 0.0;
}

auto STAAdapter::queryCellInPinSlewLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: liberty cell not found; caller may use table-axis max policy.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: input pin is unavailable; caller may use table-axis max policy.";
    return 0.0;
  }

  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (!slew_limit.has_value()) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: max slew limit is not defined on input pin; caller may use table-axis max policy.";
    return 0.0;
  }
  return sta_adapter_internal::ConvertLibTimeToNs(lib_cell, *slew_limit);
}

auto STAAdapter::queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double
{
  return sta_adapter_internal::QueryBufferTableAxisMax(
      cell_master, "input pin slew table-axis max",
      {ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION, ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION,
       ista::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME, ista::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION});
}

auto STAAdapter::queryPinSlewLimit(const Pin* pin) -> double
{
  if (pin == nullptr) {
    LOG_WARNING << "Pin-slew-limit query skipped: CTS pin is null.";
    return 0.0;
  }

  const auto pin_full_name = Design::getPinFullName(pin);
  if (sta_adapter_internal::HasFullDesignTimingContext()) {
    auto* vertex = sta_adapter_internal::FindStaVertex(pin_full_name);
    auto* ista = sta_adapter_internal::GetStaEngine()->get_ista();
    if (vertex != nullptr && ista != nullptr) {
      const double sta_slew_limit_ns
          = ResolvePositiveMin({ista->getVertexSlewLimit(vertex, ista::AnalysisMode::kMax, ista::TransType::kRise),
                                ista->getVertexSlewLimit(vertex, ista::AnalysisMode::kMax, ista::TransType::kFall)});
      if (sta_slew_limit_ns > 0.0) {
        return sta_slew_limit_ns;
      }
    }
  }

  auto* inst = pin->get_inst();
  if (inst == nullptr) {
    const double configured_limit_ns = CONFIG_INST.get_max_sink_tran();
    return configured_limit_ns > 0.0 ? configured_limit_ns : 0.0;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    const double configured_limit_ns = CONFIG_INST.get_max_sink_tran();
    return configured_limit_ns > 0.0 ? configured_limit_ns : 0.0;
  }

  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Pin-slew-limit query skipped: liberty cell \"" << cell_master << "\" is not found for " << pin_full_name << ".";
    const double configured_limit_ns = CONFIG_INST.get_max_sink_tran();
    return configured_limit_ns > 0.0 ? configured_limit_ns : 0.0;
  }

  const auto port_name = sta_adapter_internal::NormalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port != nullptr && lib_port->isInput() != 0U) {
    if (auto slew_limit_ns = lib_port->get_port_slew_limit(ista::AnalysisMode::kMax); slew_limit_ns.has_value() && *slew_limit_ns > 0.0) {
      return sta_adapter_internal::ConvertLibTimeToNs(lib_cell, *slew_limit_ns);
    }
  }

  auto* owner_lib = lib_cell->get_owner_lib();
  if (owner_lib != nullptr) {
    const double default_max_transition_ns = ResolvePositiveMax({owner_lib->get_default_max_transition()});
    if (default_max_transition_ns > 0.0) {
      return sta_adapter_internal::ConvertLibTimeToNs(lib_cell, default_max_transition_ns);
    }
  }

  const double configured_limit_ns = CONFIG_INST.get_max_sink_tran();
  return configured_limit_ns > 0.0 ? configured_limit_ns : 0.0;
}

auto STAAdapter::queryCellHeightUm(const std::string& cell_master) -> double
{
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr || idb_layout->get_units() == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB layout metadata is not ready; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  auto* idb_master = idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB cell master is not found; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  const int dbu_per_micron = idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell height", cell_master)
                << " failed: invalid DBU-per-micron in iDB units; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  return static_cast<double>(idb_master->get_height()) / static_cast<double>(dbu_per_micron);
}

auto STAAdapter::queryCellAreaUm2(const std::string& cell_master) -> double
{
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr || idb_layout->get_units() == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell area", cell_master) << " failed: iDB layout metadata is not ready.";
    return 0.0;
  }

  auto* idb_master = idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell area", cell_master) << " failed: iDB cell master is not found.";
    return 0.0;
  }

  const int dbu_per_micron = idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    LOG_WARNING << sta_adapter_internal::MakeCharQueryContext("cell area", cell_master) << " failed: invalid DBU-per-micron in iDB units.";
    return 0.0;
  }

  const auto dbu_per_micron_double = static_cast<double>(dbu_per_micron);
  return (static_cast<double>(idb_master->get_width()) * static_cast<double>(idb_master->get_height()))
         / (dbu_per_micron_double * dbu_per_micron_double);
}

}  // namespace icts
