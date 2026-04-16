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

#include <glog/logging.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Log.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "feature_icts.h"
#include "idm.h"
#include "time/Time.hh"
#include "usage/usage.hh"
#include "utils/logger/Schema.hh"

namespace icts {
auto CTSAPI::runCTS() -> void
{
  const ieda::Stats stats;
  schema::ScopedStage run_stage("CTS", "Clock tree synthesis API flow");
  readData();
  // ctsFlow();
  // evaluate();

  run_stage.markRunning("Main CTS flow is not enabled yet (ctsFlow/evaluate TODO)");
  schema::EmitKeyValueTable("CTS Flow Summary", {
                                                    {"elapsed_time_s", std::to_string(stats.elapsedRunTime())},
                                                    {"memory_delta_mb", std::to_string(stats.memoryDelta())},
                                                });
  run_stage.finish({
      {"main_flow", "not_enabled"},
      {"memory_delta_mb", std::to_string(stats.memoryDelta())},
  });
}

auto CTSAPI::readData() -> void
{
  const ieda::Stats stats;
  schema::ScopedStage read_stage("ReadData", "Load clocks from config/STA and sync DB wrapper");
  std::size_t added_clock_nets = 0U;
  std::string clock_source = "Config::net_list";

  // Get clock netlist from Config / STA
  if (CONFIG_INST.is_use_netlist()) {
    const auto& net_list = CONFIG_INST.get_net_list();
    for (const auto& [clock_name, net_name] : net_list) {
      auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
      DESIGN_INST.add_clock(std::move(clock));
      ++added_clock_nets;
    }
  } else {
    clock_source = "STAAdapter::collectClockNetPairs";
    STA_ADAPTER_INST.updateTiming();
    for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
      auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
      DESIGN_INST.add_clock(std::move(clock));
      ++added_clock_nets;
    }
  }
  // Get clock instance from DB
  WRAPPER_INST.read();
  // Summary clock distribution
  DESIGN_INST.emitClockDistributionSummary();

  std::unordered_map<std::string, std::size_t> clock_domain_counter;
  for (const auto* clock : DESIGN_INST.get_clocks()) {
    ++clock_domain_counter[clock->get_clock_name()];
  }

  schema::EmitKeyValueTable("ReadData Summary", {
                                                    {"clock_source", clock_source},
                                                    {"added_clock_nets", std::to_string(added_clock_nets)},
                                                    {"unique_clock_domains", std::to_string(clock_domain_counter.size())},
                                                    {"total_clock_nets", std::to_string(DESIGN_INST.get_clocks().size())},
                                                    {"elapsed_time_s", std::to_string(stats.elapsedRunTime())},
                                                    {"memory_delta_mb", std::to_string(stats.memoryDelta())},
                                                });
  read_stage.finish({
      {"clock_source", clock_source},
      {"added_clock_nets", std::to_string(added_clock_nets)},
      {"total_clock_nets", std::to_string(DESIGN_INST.get_clocks().size())},
  });
}

auto CTSAPI::report(const std::string& /*save_dir*/) -> void
{
  // TBD(clw): Reporting flow is not implemented yet.
}

auto CTSAPI::resetAPI() -> void
{
  CONFIG_INST.reset();
  DESIGN_INST.reset();
  WRAPPER_INST.reset();
  SCHEMA_WRITER_INST.close();
}

auto CTSAPI::init(const std::string& config_file, const InitOptions& options) -> void
{
  resetAPI();
  const std::string generated_on = ieda::Time::getNowWallTime();
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

  SCHEMA_WRITER_INST.open(CONFIG_INST.get_log_file(), "iCTS Run",
                          {
                              {"config_file", config_file},
                              {"work_dir", dir.string()},
                          });
  LOG_INFO << "Generate the report at " << generated_on;
  schema::EmitKeyValueTable("Runtime Paths", {
                                                 {"cts_log", CONFIG_INST.get_log_file()},
                                                 {"work_dir", CONFIG_INST.get_work_dir()},
                                                 {"output_def_dir", CONFIG_INST.get_output_def_path()},
                                                 {"gds_file", CONFIG_INST.get_gds_file()},
                                             });

  // DB Wrapper
  auto* idb_builder = dmInst->get_idb_builder();
  LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
  WRAPPER_INST.init(idb_builder);

  // STA
  STA_ADAPTER_INST.init();

  CONFIG_INST.emitRuntimeConfigReport("Runtime Configuration");
  STA_ADAPTER_INST.emitConfiguredUnitWireRcReport("Runtime Routing / Wire RC");
}

auto CTSAPI::outputSummary() -> ieda_feature::CTSSummary
{
  // TBD(clw): Summary export is not implemented yet.
  return ieda_feature::CTSSummary{};
}

}  // namespace icts
