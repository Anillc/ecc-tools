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

#include <string>
#include <vector>

#include "database/config/Config.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "evaluation/ClockTreeEvaluator.hh"
#include "feature_icts.h"
#include "feature_ista.h"
#include "flow/FlowManager.hh"
#include "flow/session/CTSRunEnvironment.hh"
#include "utils/logger/Schema.hh"

namespace icts {
namespace {

auto buildFeatureSummary(const ClockTreeSummary& flow_summary) -> ieda_feature::CTSSummary
{
  ieda_feature::CTSSummary summary{};
  summary.buffer_num = flow_summary.buffer_num;
  summary.buffer_area = flow_summary.buffer_area;
  summary.clock_path_min_buffer = flow_summary.clock_path_min_buffer;
  summary.clock_path_max_buffer = flow_summary.clock_path_max_buffer;
  summary.max_level_of_clock_tree = flow_summary.max_level_of_clock_tree;
  summary.max_clock_wirelength = flow_summary.max_clock_wirelength;
  summary.total_clock_wirelength = flow_summary.total_clock_wirelength;
  summary.clocks_timing.reserve(flow_summary.clocks_timing.size());
  for (const auto& clock_timing : flow_summary.clocks_timing) {
    summary.clocks_timing.push_back(ieda_feature::ClockTiming{
        .clock_name = clock_timing.clock_name,
        .setup_tns = clock_timing.setup_tns,
        .setup_wns = clock_timing.setup_wns,
        .hold_tns = clock_timing.hold_tns,
        .hold_wns = clock_timing.hold_wns,
        .suggest_freq = clock_timing.suggest_freq,
    });
  }
  return summary;
}

}  // namespace

auto CTSAPI::runCTS() -> void
{
  FLOW_MANAGER_INST.runCTS();
}

auto CTSAPI::report(const std::string& save_dir) -> void
{
  FLOW_MANAGER_INST.report(save_dir);
}

auto CTSAPI::resetAPI() -> void
{
  CONFIG_INST.reset();
  DESIGN_INST.reset();
  WRAPPER_INST.reset();
  FLOW_MANAGER_INST.reset();
  SCHEMA_WRITER_INST.reset();
}

auto CTSAPI::init(const std::string& config_file, const std::string& work_dir) -> void
{
  resetAPI();
  CTSRunEnvironment::initialize(config_file, work_dir);
  FLOW_MANAGER_INST.outputRuntimeSetup();
}

auto CTSAPI::outputSummary() -> ieda_feature::CTSSummary
{
  return buildFeatureSummary(FLOW_MANAGER_INST.outputSummary());
}

}  // namespace icts
