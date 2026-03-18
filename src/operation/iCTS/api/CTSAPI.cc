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
#include <cmath>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Type.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/io/Wrapper.hh"
#include "feature_icts.h"
#include "idm.h"
#include "liberty/Lib.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "netlist/Port.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "usage/usage.hh"
#include "utils/logger/Logger.hh"

namespace icts {
void CTSAPI::runCTS()
{
  ieda::Stats stats;
  readData();
  // ctsFlow();
  // evaluate();

  CTS_LOG_INFO << "**Flow memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Flow elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::readData()
{
  ieda::Stats stats;

  // Get clock netlist from Config / STA
  if (ConfigInst.is_use_netlist()) {
    auto& net_list = ConfigInst.get_net_list();
    for (auto& [clock_name, net_name] : net_list) {
      auto* clock = new icts::Clock(clock_name, net_name);
      DesignInst.add_clock(clock);
    }
  } else {
    STAAdapterInst.updateTiming();
    for (const auto& [clock_name, net_name] : STAAdapterInst.collectClockNetPairs()) {
      auto* clock = new icts::Clock(clock_name, net_name);
      DesignInst.add_clock(clock);
      CTS_LOG_INFO << "Clock [" << clock_name << "] have net \"" << net_name << "\"";
    }
  }
  // Get clock instance from DB
  WrapperInst.read();
  // Summary clock distribution
  summaryClockDistribution();

  CTS_LOG_INFO << "**Read Data memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Read Data elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::summaryClockDistribution()
{
  CTS_LOG_INFO << "======== Clock Distribution Summary ========";
  std::unordered_map<std::string, std::vector<Clock*>> clock_map;
  for (auto* clock : DesignInst.get_clocks()) {
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
      if (!inst) {
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

void CTSAPI::report(const std::string&)
{
  // TBD(clw): Reporting flow is not implemented yet.
}

void CTSAPI::resetAPI()
{
  ConfigInst.reset();
  DesignInst.reset();
  WrapperInst.reset();
  LogInst.close();
}

void CTSAPI::init(const std::string& config_file, const std::string& work_dir)
{
  resetAPI();
  // Config
  ConfigInst.init(config_file);
  auto dir_str = work_dir.empty() ? ConfigInst.get_work_dir() : work_dir;
  auto dir = std::filesystem::path(dir_str);
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }
  ConfigInst.set_work_dir(dir.string());

  const auto log_path = (dir / "cts.log").string();
  const auto gds_path = (dir / "cts.gds").string();
  const auto def_path = dir / "output";
  if (!std::filesystem::exists(def_path)) {
    std::filesystem::create_directories(def_path);
  }
  ConfigInst.set_log_file(log_path);
  ConfigInst.set_gds_file(gds_path);
  ConfigInst.set_output_def_path(def_path.string());

  // Logger
  LogInst.set_log_file(ConfigInst.get_log_file());
  CTS_LOG_INFO << "Generate the report at " << ieda::Time::getNowWallTime();

  // DB Wrapper
  auto* idb_builder = dmInst->get_idb_builder();
  CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
  WrapperInst.init(idb_builder);

  // STA
  STAAdapterInst.init();
}

ieda_feature::CTSSummary CTSAPI::outputSummary()
{
  // TBD(clw): Summary export is not implemented yet.
  return ieda_feature::CTSSummary{};
}

}  // namespace icts
