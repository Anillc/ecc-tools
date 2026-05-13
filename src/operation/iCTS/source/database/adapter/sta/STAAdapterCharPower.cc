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
 * @file STAAdapterCharPower.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter characterization power bridge implementation.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "Log.hh"
#include "PwrConfig.hh"
#include "PwrType.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Vector.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "core/PwrAnalysisData.hh"
#include "core/PwrCell.hh"
#include "core/PwrGraph.hh"
#include "core/PwrVertex.hh"
#include "netlist/DesignObject.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "sta/Sta.hh"
#include "sta/StaGraph.hh"
#include "sta/StaVertex.hh"

namespace icts {
namespace {

auto calcSelectedNetSwitchPower(ipower::Power* power, const std::unordered_set<std::string>& net_names) -> double
{
  if (power == nullptr || net_names.empty()) {
    return 0.0;
  }

  auto& power_graph = power->get_power_graph();
  auto* sta_graph = power_graph.get_sta_graph();
  if (sta_graph == nullptr) {
    return 0.0;
  }

  auto* netlist = sta_graph->get_nl();
  if (netlist == nullptr) {
    return 0.0;
  }

  double switch_power_w = 0.0;
  for (const auto& net_name : net_names) {
    auto* net = netlist->findNet(net_name.c_str());
    if (net == nullptr || net->getLoads().empty()) {
      continue;
    }

    auto* driver_obj = net->getDriver();
    if (driver_obj == nullptr) {
      continue;
    }
    if (driver_obj->isPort() != 0U && net->getLoads().size() == 1U && net->getLoads().front()->isPort() != 0U) {
      continue;
    }

    auto driver_sta_vertex = sta_graph->findVertex(driver_obj);
    if (!driver_sta_vertex.has_value()) {
      continue;
    }

    auto* driver_pwr_vertex = power_graph.staToPwrVertex(*driver_sta_vertex);
    if (driver_pwr_vertex == nullptr) {
      continue;
    }

    const auto driver_voltage = driver_pwr_vertex->getDriveVoltage();
    if (!driver_voltage.has_value()) {
      continue;
    }

    const double toggle = driver_pwr_vertex->getToggleData(std::nullopt);
    const double net_cap = (*driver_sta_vertex)->getNetLoad();
    const double switch_power_mw = c_switch_power_K * toggle * net_cap * driver_voltage.value() * driver_voltage.value();
    switch_power_w += switch_power_mw / static_cast<double>(ipower::g_mw2w);
  }

  return switch_power_w;
}

auto filterPowerCells(ipower::Power* power, const std::unordered_set<std::string>& inst_names) -> void
{
  if (power == nullptr || inst_names.empty()) {
    return;
  }

  auto& power_cells = power->get_power_graph().get_cells();
  auto erase_result = std::ranges::remove_if(power_cells, [&inst_names](const std::unique_ptr<ipower::PwrCell>& cell) -> bool {
    return cell == nullptr || !inst_names.contains(cell->get_design_inst()->get_name());
  });
  power_cells.erase(erase_result.begin(), erase_result.end());
}

}  // namespace

auto STAAdapter::prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                                  const std::optional<std::string>& source_input_pin_full_name) -> bool
{
  resetCharPowerState();
  auto& runtime = _char_power_state;
  runtime.inst_names = inst_names;
  runtime.net_names = net_names;
  runtime.inst_name_set = std::unordered_set<std::string>(inst_names.begin(), inst_names.end());
  runtime.net_name_set = std::unordered_set<std::string>(net_names.begin(), net_names.end());
  runtime.source_input_pin_full_name = source_input_pin_full_name;

  if (runtime.inst_names.empty()) {
    LOG_WARNING << "Characterization power setup skipped: no selected instances are provided.";
    return false;
  }

  auto clocks = sta_adapter_internal::GetStaEngine()->get_ista()->getClocks();
  if (clocks.empty()) {
    LOG_WARNING << "Characterization power setup skipped: char-only STA has no clock objects.";
    return false;
  }

  return true;
}

auto STAAdapter::refreshCharPowerLoad() -> bool
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_power_state;
  if (!runtime.is_runtime_ready) {
    runtime.is_switch_power_cached = false;
    return true;
  }

  auto* power = runtime.char_power.get();
  if (!sta_adapter_internal::PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power load refresh skipped: iPA graph is not ready.";
    return false;
  }

  runtime.cached_switch_power_w = calcSelectedNetSwitchPower(power, runtime.net_name_set);
  runtime.is_switch_power_cached = true;
  return true;
}

auto STAAdapter::updateCharPower() -> bool
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_power_state;
  auto* power = runtime.char_power.get();
  if (!runtime.is_runtime_ready) {
    runtime.char_power = sta_adapter_internal::BuildCharPower();
    power = runtime.char_power.get();
    if (!sta_adapter_internal::PrimeCharPower(power)) {
      LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
      return false;
    }

    sta_adapter_internal::AnnotateCharSourceInputPower(power, runtime.source_input_pin_full_name);
    filterPowerCells(power, runtime.inst_name_set);
    if (power->calcLeakagePower() == 0U) {
      LOG_WARNING << "Characterization power update skipped: leakage calculation failed.";
      return false;
    }

    runtime.cached_leakage_power_w = sta_adapter_internal::SumInstPowerData(power->get_leakage_powers(), runtime.inst_name_set);
    runtime.is_runtime_ready = true;
    runtime.is_switch_power_cached = false;
  } else if (!sta_adapter_internal::PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
    return false;
  }

  if (!runtime.is_switch_power_cached) {
    runtime.cached_switch_power_w = calcSelectedNetSwitchPower(power, runtime.net_name_set);
    runtime.is_switch_power_cached = true;
  }

  if (power->calcInternalPower() == 0U) {
    LOG_WARNING << "Characterization power update skipped: internal-power calculation failed.";
    return false;
  }
  const double internal_power_w = sta_adapter_internal::SumInstPowerData(power->get_internal_powers(), runtime.inst_name_set);

  runtime.last_total_power_w = runtime.cached_leakage_power_w + internal_power_w + runtime.cached_switch_power_w;
  return true;
}

auto STAAdapter::queryCharPower() -> double
{
  return getInst()._char_power_state.last_total_power_w;
}

auto STAAdapter::queryCharNetSwitchPower(const std::string& net_name) -> double
{
  if (net_name.empty()) {
    return 0.0;
  }

  auto& runtime = getInst()._char_power_state;
  auto* power = runtime.char_power.get();
  if (!runtime.is_runtime_ready || !sta_adapter_internal::PrimeCharPower(power)) {
    return 0.0;
  }

  return calcSelectedNetSwitchPower(power, std::unordered_set<std::string>{net_name});
}

auto STAAdapter::destroyCharPower() -> void
{
  auto& adapter = getInst();
  adapter.resetCharPowerState();
}

auto STAAdapter::finishCharOnly() -> void
{
  auto& adapter = getInst();
  if (!adapter._is_char_only_active) {
    return;
  }

  resetCharContext();
  adapter._is_char_only_active = false;
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  timing_engine->set_num_threads(sta_adapter_internal::kStaThreadCount);
  sta_adapter_internal::ConfigureStaWorkspace(timing_engine, "sta");
}

}  // namespace icts
