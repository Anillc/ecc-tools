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
 * @file LinearClusteringRealTechRuntime.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Runtime and environment helpers for real-tech linear clustering tests.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"

namespace icts_test::linear_clustering::realtech::detail {

using common::io::PrepareCleanOutputDir;
using common::io::ResolveLinearClusteringOutputDir;
using common::io::SanitizeOutputName;
using common::io::WriteTextLog;
using common::realtech::EnsureRealTechSetup;
using common::realtech::RealTechMode;

namespace {

auto ClampStrictlyBelow(int upper_bound, int candidate) -> int
{
  if (upper_bound <= 1) {
    return 1;
  }
  return std::max(1, std::min(upper_bound - 1, candidate));
}

}  // namespace

auto PrepareOutputDir(const std::string& case_name) -> std::filesystem::path
{
  static const bool cleaned_legacy_realtech_dirs = []() -> bool {
    const auto current_run_dir = ResolveLinearClusteringOutputDir() / "realtech" / "current_run";
    std::error_code error_code;
    const std::array<std::string_view, 5> legacy_dirs = {
        "actual_load_count", "artifacts", "diameter_ladder", "exact_cap_regression", "exact_cap_root_collision",
    };
    for (const auto legacy_dir : legacy_dirs) {
      std::filesystem::remove_all(current_run_dir / legacy_dir, error_code);
      if (error_code) {
        return false;
      }
    }
    return true;
  }();
  (void) cleaned_legacy_realtech_dirs;

  return PrepareCleanOutputDir(ResolveLinearClusteringOutputDir() / "realtech" / "current_run" / SanitizeOutputName(case_name));
}

auto WriteCaseLog(const std::filesystem::path& path, const std::string& content) -> bool
{
  return WriteTextLog(path, content);
}

auto EnsureLargestRealClockLoads() -> const RealClockLoads&
{
  static const RealClockLoads real_clock_loads = []() -> RealClockLoads {
    RealClockLoads dataset;
    const auto& setup_state = EnsureRealTechSetup();
    if (setup_state.mode != RealTechMode::kRealTech || !setup_state.setup_succeeded) {
      return dataset;
    }

    DESIGN_INST.reset();
    STA_ADAPTER_INST.updateTiming();
    for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
      auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
      DESIGN_INST.add_clock(std::move(clock));
    }

    WRAPPER_INST.read();

    dataset.dbu_per_micron = std::max(1, WRAPPER_INST.queryDbUnit());
    for (auto* clock : DESIGN_INST.get_clocks()) {
      if (clock == nullptr || clock->get_loads().size() <= dataset.loads.size()) {
        continue;
      }
      dataset.available = true;
      dataset.clock_name = clock->get_clock_name() + ":" + clock->get_clock_net_name();
      dataset.loads = clock->get_loads();
      dataset.bounding_box_diameter = CalcClusterDiameter(dataset.loads);
    }
    return dataset;
  }();

  return real_clock_loads;
}

auto BuildResponsiveDiameterThresholds(const RealClockLoads& real_clock_loads) -> std::array<int, 3>
{
  const int span_diameter = std::max(1, real_clock_loads.bounding_box_diameter);
  const int dbu_per_micron = std::max(1, real_clock_loads.dbu_per_micron);

  const int loose_candidate = std::max(dbu_per_micron * 24, span_diameter);
  const int medium_candidate
      = std::max(dbu_per_micron * 12, static_cast<int>(std::lround(static_cast<double>(span_diameter) * kDiameterLadderMediumScale)));
  const int tight_candidate
      = std::max(dbu_per_micron * 6, static_cast<int>(std::lround(static_cast<double>(span_diameter) * kDiameterLadderTightScale)));

  std::array<int, 3> thresholds = {std::max(1, loose_candidate), 0, 0};
  thresholds.at(1) = ClampStrictlyBelow(thresholds.at(0), medium_candidate);
  thresholds.at(2) = ClampStrictlyBelow(thresholds.at(1), tight_candidate);
  return thresholds;
}

}  // namespace icts_test::linear_clustering::realtech::detail
