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
 * @file CTSAPI.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API implementation.
 */
#include "CTSAPI.hh"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Pin.hh"
#include "database/io/Wrapper.hh"
#include "feature_icts.h"
#include "idm.h"
#include "time/Time.hh"
#include "usage/usage.hh"
#include "utils/logger/Logger.hh"

namespace icts {
void CTSAPI::runCTS()
{
  const ieda::Stats stats;
  readData();
  // ctsFlow();
  // evaluate();

  CTS_LOG_INFO << "**Flow memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Flow elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::readData()
{
  const ieda::Stats stats;

  // Get clock netlist from Config / STA
  if (CONFIG_INST.is_use_netlist()) {
    const auto& net_list = CONFIG_INST.get_net_list();
    for (const auto& [clock_name, net_name] : net_list) {
      auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
      DESIGN_INST.add_clock(std::move(clock));
    }
  } else {
    STA_ADAPTER_INST.updateTiming();
    for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
      auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
      DESIGN_INST.add_clock(std::move(clock));
      CTS_LOG_INFO << "Clock [" << clock_name << "] have net \"" << net_name << "\"";
    }
  }
  // Get clock instance from DB
  WRAPPER_INST.read();
  // Summary clock distribution
  summaryClockDistribution();

  CTS_LOG_INFO << "**Read Data memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Read Data elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::summaryClockDistribution()
{
  CTS_LOG_INFO << "======== Clock Distribution Summary ========";
  std::unordered_map<std::string, std::vector<Clock*>> clock_map;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    clock_map[clock->get_clock_name()].push_back(clock);
  }

  for (const auto& [clock_name, clocks] : clock_map) {
    const std::size_t num_nets = clocks.size();

    std::size_t num_ff_sinks = 0;
    std::size_t num_total_sinks = 0;
    std::size_t num_buffer_sinks = 0;
    std::size_t num_none_inst_sinks = 0;

    std::vector<Pin*> pins;
    for (const auto* clock : clocks) {
      std::ranges::copy(clock->get_loads(), std::back_inserter(pins));
    }

    for (const Pin* pin : pins) {
      ++num_total_sinks;

      const auto* inst = pin->get_inst();
      if (inst == nullptr) {
        ++num_none_inst_sinks;
      } else if (inst->is_flipflop()) {
        ++num_ff_sinks;
      } else {
        ++num_buffer_sinks;
      }
    }

    CTS_LOG_INFO << "Clock: " << clock_name << ", #Net: " << num_nets << ", #Total Sinks: " << num_total_sinks
                 << ", #FlipFlop Sinks: " << num_ff_sinks << ", #Buffer Sinks: " << num_buffer_sinks
                 << ", #None Inst Sinks: " << num_none_inst_sinks;
  }
  CTS_LOG_INFO << "============================================";
}

void CTSAPI::report(const std::string& save_dir)
{
  // TBD(clw): Reporting flow is not implemented yet.
}

void CTSAPI::resetAPI()
{
  CONFIG_INST.reset();
  DESIGN_INST.reset();
  WRAPPER_INST.reset();
  LOG_INST.close();
}

void CTSAPI::init(const std::string& config_file, const InitOptions& options)
{
  resetAPI();
  // Config
  CONFIG_INST.init(config_file);
  auto dir_str = options.work_dir.path.empty() ? CONFIG_INST.get_work_dir() : options.work_dir.path;
  auto dir = std::filesystem::path(dir_str);
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }
  CONFIG_INST.set_work_dir(dir.string());

  const auto log_path = (dir / "cts.log").string();
  const auto gds_path = (dir / "cts.gds").string();
  const auto def_path = dir / "output";
  if (!std::filesystem::exists(def_path)) {
    std::filesystem::create_directories(def_path);
  }
  CONFIG_INST.set_log_file(log_path);
  CONFIG_INST.set_gds_file(gds_path);
  CONFIG_INST.set_output_def_path(def_path.string());

  // Logger
  LOG_INST.set_log_file(CONFIG_INST.get_log_file());
  CTS_LOG_INFO << "Generate the report at " << ieda::Time::getNowWallTime();

  // DB Wrapper
  auto* idb_builder = dmInst->get_idb_builder();
  CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
  WRAPPER_INST.init(idb_builder);

  // STA
  STA_ADAPTER_INST.init();
}

auto CTSAPI::outputSummary() -> ieda_feature::CTSSummary
{
  // TBD(clw): Summary export is not implemented yet.
  return ieda_feature::CTSSummary{};
}

}  // namespace icts
