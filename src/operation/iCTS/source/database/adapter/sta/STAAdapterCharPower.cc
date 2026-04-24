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

#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Vector.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "core/PwrAnalysisData.hh"
#include "sta/Sta.hh"

namespace icts {

using namespace sta_adapter_internal;

auto STAAdapter::prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                                  std::optional<std::string> source_input_pin_full_name) -> bool
{
  auto& adapter = getInst();
  adapter.resetCharPowerState();
  auto& runtime = adapter._char_power_state;
  runtime.inst_names = inst_names;
  runtime.net_names = net_names;
  runtime.inst_name_set = std::unordered_set<std::string>(inst_names.begin(), inst_names.end());
  runtime.net_name_set = std::unordered_set<std::string>(net_names.begin(), net_names.end());
  runtime.source_input_pin_full_name = std::move(source_input_pin_full_name);

  if (runtime.inst_names.empty()) {
    LOG_WARNING << "Characterization power setup skipped: no selected instances are provided.";
    return false;
  }

  auto clocks = GetStaEngine()->get_ista()->getClocks();
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
  if (!PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power load refresh skipped: iPA graph is not ready.";
    return false;
  }

  runtime.cached_switch_power_w = CalcSelectedNetSwitchPower(power, runtime.net_name_set);
  runtime.is_switch_power_cached = true;
  return true;
}

auto STAAdapter::updateCharPower() -> bool
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_power_state;
  auto* power = runtime.char_power.get();
  if (!runtime.is_runtime_ready) {
    runtime.char_power = BuildCharPower();
    power = runtime.char_power.get();
    if (!PrimeCharPower(power)) {
      LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
      return false;
    }

    AnnotateCharSourceInputPower(power, runtime.source_input_pin_full_name);
    FilterPowerCells(power, runtime.inst_name_set);
    if (power->calcLeakagePower() == 0U) {
      LOG_WARNING << "Characterization power update skipped: leakage calculation failed.";
      return false;
    }

    runtime.cached_leakage_power_w = SumInstPowerData(power->get_leakage_powers(), runtime.inst_name_set);
    runtime.is_runtime_ready = true;
    runtime.is_switch_power_cached = false;
  } else if (!PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
    return false;
  }

  if (!runtime.is_switch_power_cached) {
    runtime.cached_switch_power_w = CalcSelectedNetSwitchPower(power, runtime.net_name_set);
    runtime.is_switch_power_cached = true;
  }

  if (power->calcInternalPower() == 0U) {
    LOG_WARNING << "Characterization power update skipped: internal-power calculation failed.";
    return false;
  }
  const double internal_power_w = SumInstPowerData(power->get_internal_powers(), runtime.inst_name_set);

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
  if (!runtime.is_runtime_ready || !PrimeCharPower(power)) {
    return 0.0;
  }

  return CalcSelectedNetSwitchPower(power, std::unordered_set<std::string>{net_name});
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
  auto* timing_engine = GetStaEngine();
  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
}

}  // namespace icts
