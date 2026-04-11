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
 * @file LinearClusteringRealTechSetupScenario.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Setup-oriented real-tech linear clustering test scenarios.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/io/TestArtifactIO.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "common/types/TestDataTypes.hh"
#include "module/topology/config/TopologyConfig.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechInternal.hh"
#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechShared.hh"

namespace icts_test::linear_clustering::realtech {

namespace {
using namespace detail;
using common::io::EmitInfoReport;
using common::realtech::EnsureRealTechSetup;
using common::realtech::RealTechMode;

constexpr const char* kSetupStateCaseName = "setup_state_is_stable";
constexpr const char* kSetupStateReportTitle = "LinearClusteringTechTest.SetupStateIsStable";
constexpr const char* kActualLoadCountCaseName = "realtech_uses_actual_available_load_count";
constexpr const char* kActualLoadCountReportTitle = "LinearClusteringTechTest.RealTechUsesActualAvailableLoadCount";

auto PersistScenarioReport(const std::filesystem::path& output_dir, const std::string& case_name, const std::string& report) -> void
{
  const auto report_path = output_dir / "report.log";
  EXPECT_TRUE(WriteCaseLog(report_path, report)) << "Failed to write report: " << report_path.string();
  EmitInfoReport(InfoReport{.title = case_name, .content = report});
}

auto BuildUnavailableReport(const std::string& test_name, const common::realtech::RealTechSetupState& setup_state) -> std::string
{
  std::ostringstream report;
  report << "Test: " << test_name << "\n";
  report << "Mode: skipped\n";
  report << "Reason: real-tech assets are unavailable.\n";
  report << "Setup: " << setup_state.summary << "\n";
  report << "Artifacts: report.log\n";
  return report.str();
}

auto BuildSetupStateReport(const common::realtech::RealTechSetupState& state) -> std::string
{
  std::ostringstream report;
  report << "Test: " << kSetupStateReportTitle << "\n";
  report << "Mode: " << (state.mode == RealTechMode::kRealTech ? "real-tech" : "synthetic fallback") << "\n";
  report << "Output root: " << state.output_dir << "\n";
  report << "Assets available: " << (state.assets_available ? "true" : "false") << "\n";
  report << "Setup succeeded: " << (state.setup_succeeded ? "true" : "false") << "\n";
  report << "Summary: " << state.summary << "\n";
  report << "Artifacts: report.log\n";
  return report.str();
}

auto CheckSetupStateExpectations(const common::realtech::RealTechSetupState& state) -> void
{
  if (state.mode == RealTechMode::kRealTech) {
    EXPECT_TRUE(state.assets_available);
    EXPECT_TRUE(state.setup_succeeded);
    return;
  }
  EXPECT_FALSE(state.setup_succeeded);
}

auto BuildActualLoadCountReport(const RealClockLoads& real_clock_loads, std::size_t exact_cap_ready_pin_count,
                                const WireRcPerUm& wire_rc_per_um) -> std::string
{
  std::ostringstream summary;
  summary << "Test: " << kActualLoadCountReportTitle << "\n";
  summary << "Mode: real-tech metadata inspection\n";
  summary << "Clock: " << real_clock_loads.clock_name << "\n";
  summary << "Actual available real load count: " << real_clock_loads.loads.size() << "\n";
  summary << "Exact-cap ready pin count: " << exact_cap_ready_pin_count << "\n";
  summary << "DBU per micron: " << real_clock_loads.dbu_per_micron << "\n";
  summary << "Bounding box diameter: " << real_clock_loads.bounding_box_diameter << "\n";
  summary << "Default router kind: flute\n";
  summary << "Default routing layer: " << wire_rc_per_um.routing_layer << "\n";
  if (wire_rc_per_um.wire_width_um.has_value()) {
    summary << "Default wire width (um): " << *wire_rc_per_um.wire_width_um << "\n";
  } else {
    summary << "Default wire width (um): auto\n";
  }
  summary << "Wire resistance per um (ohm): " << wire_rc_per_um.resistance_per_um_ohm << "\n";
  summary << "Wire capacitance per um: " << wire_rc_per_um.capacitance_per_um << "\n";
  summary << "Artifacts: report.log\n";
  return summary.str();
}
}  // namespace

auto RunSetupStateIsStable() -> void
{
  const auto& state = EnsureRealTechSetup();
  const auto output_dir = PrepareOutputDir(kSetupStateCaseName);
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare setup-state output dir.";
  EXPECT_FALSE(state.output_dir.empty());
  EXPECT_FALSE(state.summary.empty());
  EXPECT_TRUE(std::filesystem::exists(state.output_dir));

  CheckSetupStateExpectations(state);
  PersistScenarioReport(output_dir, kSetupStateCaseName, BuildSetupStateReport(state));
}

auto RunRealTechUsesActualAvailableLoadCount() -> void
{
  const auto& setup_state = EnsureRealTechSetup();
  const auto output_dir = PrepareOutputDir(kActualLoadCountCaseName);
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare actual-load-count output dir.";
  if (setup_state.mode != RealTechMode::kRealTech) {
    PersistScenarioReport(output_dir, kActualLoadCountCaseName, BuildUnavailableReport(kActualLoadCountReportTitle, setup_state));
    GTEST_SKIP() << "Real-tech assets are unavailable: " << setup_state.summary;
  }

  const auto& real_clock_loads = EnsureLargestRealClockLoads();
  ASSERT_TRUE(real_clock_loads.available) << "Real-tech mode is active but no CTS clock loads are available.";
  ASSERT_FALSE(real_clock_loads.clock_name.empty());
  ASSERT_GT(real_clock_loads.loads.size(), 0U);

  const auto exact_cap_ready_pin_count = CountPinsWithExactCapContext(real_clock_loads.loads);
  EXPECT_EQ(exact_cap_ready_pin_count, real_clock_loads.loads.size())
      << "Real-tech loads must keep inst/net/name semantics for exact-cap regression.";

  const icts::LinearClusteringConfig default_config{};
  const auto wire_rc_per_um = QueryEffectiveWireRcPerUm(default_config);
  PersistScenarioReport(output_dir, kActualLoadCountCaseName,
                        BuildActualLoadCountReport(real_clock_loads, exact_cap_ready_pin_count, wire_rc_per_um));
}

}  // namespace icts_test::linear_clustering::realtech
