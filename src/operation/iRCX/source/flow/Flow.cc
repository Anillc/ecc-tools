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
#include <omp.h>

#include "Extraction.hh"
#include "Flow.hh"
#include "PathUtils.hh"
#include "RCXConfig.hh"
#include "RCXData.hh"
#include "SpefDumper.hh"
#include "log/Log.hh"
#include "setup/Setup.hh"
namespace ircx {

void Flow::reset()
{
  RCX_DATA_INST.reset();
}

auto Flow::run() -> bool
{
  LOG_INFO << "RCX flow begin...";

  if (!RCX_CONFIG_INST.get_initialized()) {
    LOG_ERROR << "RCX flow failed: RCX config is not initialized.";
    LOG_INFO  << "RCX flow end.";
    return false;
  }

  if (!adaptDB()) {
    LOG_INFO << "RCX flow end.";
    return false;
  }

  omp_set_num_threads(RCX_CONFIG_INST.get_thread_num());

  if (!calculate()) {
    LOG_INFO << "RCX flow end.";
    return false;
  }

  LOG_INFO << "RCX flow end.";
  return true;
}

auto Flow::adaptDB() -> bool
{
  return Setup::adaptDB();
}

auto Flow::calculate() -> bool
{
  Extraction extraction;
  return extraction.run();
}

auto Flow::report() -> bool
{
  return dumpSpef();
}

auto Flow::dumpSpef() -> bool
{
  LOG_INFO << "report spef start";
  const Str& config_output_dir = RCX_CONFIG_INST.get_output_dir();
  const Str resolved_output_dir = config_output_dir.empty() ? "." : config_output_dir;
  if (!ensureDirectoryExists(resolved_output_dir, "output_dir")) {
    return false;
  }

  RCXData& data = RCX_DATA_INST;
  const auto process_corners = data.corners();
  if (process_corners.empty()) {
    LOG_ERROR << "report spef failed: process corners not loaded.";
    return false;
  }

  SpefDumper dumper;

  dumper.set_spef_context(&data.spef_context());
  dumper.set_layout_data(&data.layout());
  dumper.set_layer_table(&data.layer_table());
  dumper.set_topo_pool(&data.topo_pool());
  dumper.set_rc_table(&data.rc_table());
  dumper.set_corners(process_corners);

  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    dumper.dump(resolved_output_dir, corner_idx);
  }

  LOG_INFO << "report spef end";
  return true;
}

Flow::Flow()
{
  char config[] = "iRCX";
  char* argv[] = {config, nullptr};
  ieda::Log::init(argv);
}
Flow::~Flow() = default;

}  // namespace ircx
