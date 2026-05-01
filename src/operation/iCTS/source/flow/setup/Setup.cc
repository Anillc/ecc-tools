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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file Setup.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS setup entry facade implementation.
 */

#include "setup/Setup.hh"

#include <glog/logging.h>

#include <filesystem>
#include <ostream>
#include <string>
#include <system_error>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "idm.h"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "time/Time.hh"

namespace icts {
namespace {

auto ensureDirectory(const std::filesystem::path& dir) -> void
{
  std::error_code error_code;
  std::filesystem::create_directories(dir, error_code);
  LOG_FATAL_IF(error_code) << "failed to create CTS runtime directory " << dir.string() << ": " << error_code.message();
}

}  // namespace

auto Setup::initialize(const std::string& config_file, const std::string& work_dir) -> void
{
  const std::string generated_on = ieda::Time::getNowWallTime();

  CONFIG_INST.init(config_file);
  const auto dir_str = work_dir.empty() ? CONFIG_INST.get_work_dir() : work_dir;
  auto dir = std::filesystem::path(dir_str);
  ensureDirectory(dir);
  CONFIG_INST.set_work_dir(dir.string());

  const auto log_path = (dir / "cts.log").string();
  const auto visualization_dir = dir / "visualization";
  const auto statistics_dir = dir / "statistics";
  ensureDirectory(visualization_dir);
  ensureDirectory(statistics_dir);
  CONFIG_INST.set_log_file(log_path);
  CONFIG_INST.set_visualization_dir(visualization_dir.string());
  CONFIG_INST.set_statistics_dir(statistics_dir.string());

  SCHEMA_WRITER_INST.open(CONFIG_INST.get_log_file(), "iCTS Run",
                          {
                              {"config_file", config_file},
                              {"work_dir", dir.string()},
                          });
  LOG_INFO << "Generate the report at " << generated_on;

  auto* idb_builder = dmInst->get_idb_builder();
  LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
  WRAPPER_INST.init(idb_builder);

  STA_ADAPTER_INST.init();
}

auto Setup::emitRuntimeSetup() -> void
{
  SCHEMA_WRITER_INST.emitSection("## Runtime Setup");
  schema::EmitKeyValueTable("Runtime Paths", {
                                                 {"cts_log", CONFIG_INST.get_log_file()},
                                                 {"work_dir", CONFIG_INST.get_work_dir()},
                                                 {"visualization_dir", CONFIG_INST.get_visualization_dir()},
                                                 {"statistics_dir", CONFIG_INST.get_statistics_dir()},
                                             });
  SCHEMA_WRITER_INST.emitSection("### Runtime Configuration");
  CONFIG_INST.emitRuntimeConfigReport("Runtime Configuration");
  SCHEMA_WRITER_INST.emitSection("### Runtime Routing / Wire RC");
  STA_ADAPTER_INST.emitConfiguredUnitWireRcReport("Runtime Routing / Wire RC");
}

}  // namespace icts
