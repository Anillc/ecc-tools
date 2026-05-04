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
 * @file STAAdapterRootDriverQuery.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-03
 * @brief Root driver delay/power and routed timing query helpers.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "Log.hh"
#include "PwrConfig.hh"
#include "PwrType.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Type.hh"
#include "api/TimingEngine.hh"
#include "design/Design.hh"
#include "design/Pin.hh"
#include "liberty/Lib.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sta/StaVertex.hh"

namespace icts {
namespace {

constexpr double kMilliwattToWatt = 1.0 / 1000.0;

auto flipTrans(ista::TransType trans_type) -> ista::TransType
{
  return trans_type == ista::TransType::kRise ? ista::TransType::kFall : ista::TransType::kRise;
}

auto makeInvalidCost(const std::string& method, const std::string& cell_master, double input_slew_ns, double output_load_pf)
    -> STAAdapter::RootDriverCost
{
  STAAdapter::RootDriverCost cost;
  cost.method = method;
  cost.cell_master = cell_master;
  cost.input_slew_ns = input_slew_ns;
  cost.output_load_pf = output_load_pf;
  return cost;
}

auto lookupRootCellDelayNs(ista::LibCell* lib_cell, ista::LibArcSet* timing_arc_set, double input_slew_ns, double output_load_pf) -> double
{
  if (lib_cell == nullptr || timing_arc_set == nullptr || input_slew_ns <= 0.0 || output_load_pf < 0.0) {
    return 0.0;
  }

  const double output_load = sta_adapter_internal::ConvertPfLoadToLibUnit(lib_cell, output_load_pf);
  double worst_delay_ns = 0.0;
  bool has_delay = false;
  for (const auto input_trans : {ista::TransType::kRise, ista::TransType::kFall}) {
    const auto output_trans = timing_arc_set->isNegativeArc() != 0U ? flipTrans(input_trans) : input_trans;
    if (!timing_arc_set->isMatchTimingType(output_trans)) {
      continue;
    }
    const auto delay_values = timing_arc_set->getDelayOrConstrainCheckNs(input_trans, output_trans, input_slew_ns, output_load);
    for (const double delay_ns : delay_values) {
      if (!std::isfinite(delay_ns)) {
        continue;
      }
      worst_delay_ns = has_delay ? std::max(worst_delay_ns, delay_ns) : delay_ns;
      has_delay = true;
    }
  }
  return has_delay ? worst_delay_ns : 0.0;
}

auto lookupInternalPowerW(ista::LibCell* lib_cell, ista::LibPowerArcSet* power_arc_set, double input_slew_ns, double output_load_pf,
                          double clock_period_ns) -> double
{
  if (lib_cell == nullptr || power_arc_set == nullptr || input_slew_ns <= 0.0 || output_load_pf < 0.0 || clock_period_ns <= 0.0) {
    return 0.0;
  }

  const double output_load = sta_adapter_internal::ConvertPfLoadToLibUnit(lib_cell, output_load_pf);
  const double output_toggle_per_ns = c_default_clock_toggle / clock_period_ns;
  double power_mw = 0.0;

  for (const auto& power_arc_ptr : power_arc_set->get_power_arcs()) {
    auto* power_arc = power_arc_ptr.get();
    if (power_arc == nullptr || power_arc->get_internal_power_info() == nullptr) {
      continue;
    }
    auto* internal_power_info = power_arc->get_internal_power_info().get();
    const double rise_energy_mw_ns
        = lib_cell->convertInternalPowerTableToMwNs(internal_power_info->gatePower(ista::TransType::kRise, input_slew_ns, output_load));
    const double fall_energy_mw_ns
        = lib_cell->convertInternalPowerTableToMwNs(internal_power_info->gatePower(ista::TransType::kFall, input_slew_ns, output_load));
    const double average_energy_mw_ns = ipower::CalcAveragePower(rise_energy_mw_ns, fall_energy_mw_ns);
    if (std::isfinite(average_energy_mw_ns)) {
      power_mw += output_toggle_per_ns * average_energy_mw_ns;
    }
  }

  return power_mw * kMilliwattToWatt;
}

auto lookupLeakagePowerW(ista::LibCell* lib_cell) -> double
{
  if (lib_cell == nullptr) {
    return 0.0;
  }

  const double default_leakage_power_w = lib_cell->get_cell_leakage_power();
  if (std::isfinite(default_leakage_power_w) && default_leakage_power_w > 0.0) {
    return default_leakage_power_w;
  }

  double unconditional_leakage_power_w = 0.0;
  for (auto* leakage_power : lib_cell->getLeakagePowerList()) {
    if (leakage_power != nullptr && leakage_power->get_when().empty() && std::isfinite(leakage_power->get_value())) {
      unconditional_leakage_power_w += leakage_power->get_value();
    }
  }
  return unconditional_leakage_power_w;
}

auto maxArrivalNs(ista::StaVertex* vertex, const std::string& clock_name) -> std::optional<double>
{
  if (vertex == nullptr) {
    return std::nullopt;
  }

  std::vector<double> arrivals_ns;
  arrivals_ns.reserve(2U);
  for (const auto trans : {ista::TransType::kRise, ista::TransType::kFall}) {
    auto clock_arrival = clock_name.empty() ? vertex->getClockArriveTime(ista::AnalysisMode::kMax, trans)
                                            : vertex->getClockArriveTime(ista::AnalysisMode::kMax, trans, clock_name);
    if (!clock_arrival.has_value() && !clock_name.empty()) {
      clock_arrival = vertex->getClockArriveTime(ista::AnalysisMode::kMax, trans);
    }
    if (clock_arrival.has_value()) {
      arrivals_ns.push_back(FS_TO_NS(*clock_arrival));
      continue;
    }
    auto data_arrival = vertex->getArriveTimeNs(ista::AnalysisMode::kMax, trans);
    if (data_arrival.has_value()) {
      arrivals_ns.push_back(*data_arrival);
    }
  }

  if (arrivals_ns.empty()) {
    return std::nullopt;
  }
  return *std::ranges::max_element(arrivals_ns);
}

}  // namespace

auto STAAdapter::queryPinClockArrival(const Pin* pin, const std::string& clock_name) -> std::optional<double>
{
  if (pin == nullptr || clock_name.empty()) {
    return std::nullopt;
  }
  if (!sta_adapter_internal::HasFullDesignTimingContext()) {
    return std::nullopt;
  }

  auto* vertex = sta_adapter_internal::FindStaVertex(Design::getPinFullName(pin));
  return maxArrivalNs(vertex, clock_name);
}

auto STAAdapter::queryRootDriverCostDirect(const std::string& cell_master, double input_slew_ns, double output_load_pf,
                                           double clock_period_ns) -> RootDriverCost
{
  auto cost = makeInvalidCost("direct", cell_master, input_slew_ns, output_load_pf);
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Direct root-driver cost query skipped: liberty cell is not found: " << cell_master << ".";
    return cost;
  }

  ista::LibPort* input_port = nullptr;
  ista::LibPort* output_port = nullptr;
  lib_cell->bufferPorts(input_port, output_port);
  if (input_port == nullptr || output_port == nullptr) {
    LOG_WARNING << "Direct root-driver cost query skipped: cell is not a single-input/single-output buffer or inverter: " << cell_master
                << ".";
    return cost;
  }

  const auto timing_arc_set = sta_adapter_internal::FindBufferArcSet(lib_cell);
  auto power_arc_set = lib_cell->findLibertyPowerArcSet(input_port->get_port_name(), output_port->get_port_name());
  cost.cell_delay_ns = lookupRootCellDelayNs(lib_cell, timing_arc_set.value_or(nullptr), input_slew_ns, output_load_pf);
  cost.internal_power_w = lookupInternalPowerW(lib_cell, power_arc_set.value_or(nullptr), input_slew_ns, output_load_pf, clock_period_ns);
  cost.leakage_power_w = lookupLeakagePowerW(lib_cell);
  cost.cell_power_w = cost.internal_power_w + cost.leakage_power_w;
  cost.valid = cost.cell_delay_ns > 0.0 || cost.cell_power_w > 0.0;
  return cost;
}

}  // namespace icts
