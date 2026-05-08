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
 * @file STAAdapterSdcClock.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-06
 * @brief SDC clock-only queries for the iCTS STA adapter.
 */

#include <glog/logging.h>

#include <filesystem>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "api/TimingEngine.hh"
#include "dm_config.h"
#include "idm.h"
#include "logger/Schema.hh"

namespace icts {
namespace {

auto configuredSdcPath() -> std::string
{
  return dmInst->get_config().get_sdc_path();
}

}  // namespace

auto STAAdapter::readSdcClockDeclarationsOnly(const std::string& sdc_path)
    -> std::vector<std::tuple<std::string, std::string, double, bool>>
{
  std::vector<std::tuple<std::string, std::string, double, bool>> declarations;
  if (sdc_path.empty()) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "STAAdapter", "SDC clock read skipped because SDC path is empty.",
                           {{"clock_source", "sdc"}});
    return declarations;
  }
  if (!std::filesystem::exists(sdc_path)) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "STAAdapter", "SDC clock read skipped because SDC file does not exist.",
                           {{"clock_source", "sdc"}, {"sdc_path", sdc_path}});
    LOG_ERROR << "STAAdapter: SDC clock-only file does not exist: " << sdc_path;
    return declarations;
  }

  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  if (timing_engine == nullptr) {
    LOG_ERROR << "STAAdapter: timing engine is null during SDC clock-only read.";
    return declarations;
  }

  const auto records = timing_engine->readSdcClockPeriodsOnly(sdc_path.c_str());
  declarations.reserve(records.size());
  for (const auto& [clock_name, source_expression, period_ns, period_resolved] : records) {
    if (!period_resolved) {
      schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "STAAdapter", "SDC clock period could not be resolved in clock-only mode.",
                             {{"clock", clock_name}});
    }
    declarations.emplace_back(clock_name, source_expression, period_ns, period_resolved);
  }
  return declarations;
}

auto STAAdapter::readConfiguredSdcClockDeclarationsOnly() -> std::vector<std::tuple<std::string, std::string, double, bool>>
{
  return readSdcClockDeclarationsOnly(configuredSdcPath());
}

}  // namespace icts
