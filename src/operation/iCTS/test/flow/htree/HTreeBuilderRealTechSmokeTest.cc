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
 * @file HTreeBuilderRealTechSmokeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief Real-tech smoke coverage for HTreeBuilder on DEF-derived clock loads.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "common/logging/LogText.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/config/Config.hh"
#include "flow/htree/HTreeBuildObservation.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/htree/HTreeBuilderRealTechSmokeSupport.hh"
#include "flow/htree/HTreeVisualizationSupport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

TEST(HTreeBuilderRealTechSmokeTest, SynthesizesMaterializedHTreeFromRealClockLoads)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClockLoads(kMaxRealClockLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes at least two CTS sink pins.";
    return;
  }

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("htree_builder_smoke", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  EXPECT_EQ(CONFIG_INST.get_wirelength_iterations(), realtech_support::kRealTechCharWirelengthIterations);
  EXPECT_EQ(CONFIG_INST.get_slew_steps(), realtech_support::kRealTechCharSlewSteps);
  EXPECT_EQ(CONFIG_INST.get_cap_steps(), realtech_support::kRealTechCharCapSteps);
  EXPECT_TRUE(CONFIG_INST.has_max_buf_tran());
  EXPECT_TRUE(CONFIG_INST.has_max_cap());
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_max_buf_tran(), kHTreeSmokeMaxSlewNs);
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_max_cap(), kHTreeSmokeMaxCapPf);

  const auto& real_loads = selected_clock->loads;
  ASSERT_GE(real_loads.size(), 2U);
  ASSERT_EQ(CountPinsWithRealContext(real_loads), real_loads.size())
      << "Selected clock loads do not carry complete DEF/CTS instance context: " << selected_clock->clock_name;

  std::unordered_set<icts::Pin*> original_loads(real_loads.begin(), real_loads.end());
  const auto artifact_paths = htree::PrepareHTreeArtifactPaths("realtech_smoke");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "HTree Flow Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("HTree Smoke Scenario", {
                                                                   {"scenario", "realtech_smoke"},
                                                                   {"clock_name", selected_clock->clock_name},
                                                                   {"load_count", std::to_string(real_loads.size())},
                                                               });

  icts::Pin root_driver("htree_smoke_root_out", icts::PinType::kOut);
  icts::Net root_net("htree_smoke_root_net");
  ConnectRootNetForHTreeTest(root_net, root_driver, real_loads);

  auto result = icts::HTreeBuilder::build(root_net);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.failure_reason.empty());
  ASSERT_TRUE(result.best_char.has_value());
  ASSERT_TRUE(result.best_pattern.has_value());
  const auto observation = htree::ObserveHTreeBuild(result);
  ASSERT_GT(observation.selected_feasible_solution_count, 0U);
  ASSERT_GT(observation.selected_feasible_frontier_entry_count, 0U);
  AssertDepthCandidateCoverage(result);
  AssertSelectedHTreeLoadDistribution(result);
  EXPECT_LE(observation.selected_feasible_frontier_entry_count, observation.selected_feasible_solution_count);
  const icts::HTreeTopologyPattern* best_pattern = nullptr;
  if (result.best_pattern.has_value()) {
    best_pattern = &result.best_pattern.value();
  }
  ASSERT_NE(best_pattern, nullptr);
  ASSERT_EQ(best_pattern->get_levels(), result.levels.size());
  ASSERT_EQ(best_pattern->get_level_segment_pattern_ids().size(), result.levels.size());
  ASSERT_EQ(result.root_net, &root_net);
  ASSERT_NE(result.root_output_pin, nullptr);
  EXPECT_EQ(result.root_output_pin, &root_driver);
  EXPECT_FALSE(result.inserted_pins.empty());
  EXPECT_FALSE(result.inserted_nets.empty());
  AssertNoSingleLoadExternalLeafBuffer(result);

  const auto leaf_loads = CollectLeafLoads(result.topology);
  EXPECT_EQ(leaf_loads.size(), original_loads.size());
  for (auto* load : real_loads) {
    ASSERT_NE(load, nullptr);
    EXPECT_TRUE(leaf_loads.contains(load));
    EXPECT_NE(load->get_inst(), nullptr);
    EXPECT_NE(load->get_net(), nullptr);
  }

  WriteAndAssertHTreeArtifacts(artifact_paths, "htree_builder_realtech_smoke", selected_clock->clock_name, real_loads, result);

  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  const auto report_log_content = ReadTextFile(artifact_paths.report_log);
  ASSERT_FALSE(cts_log_content.empty());
  ASSERT_FALSE(report_log_content.empty());
  const auto first_line_break = cts_log_content.find('\n');
  ASSERT_NE(first_line_break, std::string::npos);
  EXPECT_EQ(cts_log_content.find("Generate the report at "), first_line_break + 1U);
  EXPECT_NE(cts_log_content.find("CharBuilder Setup"), std::string::npos);
  const auto char_builder_setup = common::logging::ExtractTextBlock(cts_log_content, "CharBuilder Setup");
  ASSERT_FALSE(char_builder_setup.empty());
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*routing_layer\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*wire_width\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*max_slew\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*max_cap\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*wirelength_iterations\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*wirelength_points\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*slew_steps\s*\|)")));
  EXPECT_FALSE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*cap_steps\s*\|)")));
  EXPECT_TRUE(std::regex_search(char_builder_setup, std::regex(R"(\|\s*routing_rc_source\s*\|\s*Runtime Routing / Wire RC\s*\|)")));
  const auto htree_grid_plan = common::logging::ExtractTextBlock(cts_log_content, "HTreeBuilder Characterization Grid Plan");
  ASSERT_FALSE(htree_grid_plan.empty());
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*source\s*\|)")));
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*requested_level_lengths\s*\|)")));
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*required_covering_iterations\s*\|)")));
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*direct_characterization_bins\s*\|)")));
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*distinct_level_bins\s*\|)")));
  EXPECT_TRUE(std::regex_search(htree_grid_plan, std::regex(R"(\|\s*decision_flags\s*\|)")));
  EXPECT_EQ(cts_log_content.find("characterization grid was capped below direct topology coverage"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("wirelength unit is absent in runtime config; fallback to auto-derived topology grid unit"),
            std::string::npos);
  EXPECT_EQ(cts_log_content.find("configured wirelength unit collapses level bins to <=1"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("CharBuilder Runtime Configuration"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("CharBuilder Routing / Wire RC"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Notes"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Characterization setup lists the resolved limits"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("This section records the selected H-tree depth"), std::string::npos);
  EXPECT_NE(cts_log_content.find("HTreeBuilder Synthesis Summary"), std::string::npos);
  const auto htree_build_summary = common::logging::ExtractTextBlock(cts_log_content, "HTreeBuilder Synthesis Summary");
  ASSERT_FALSE(htree_build_summary.empty());
  EXPECT_EQ(htree_build_summary.find("force_branch_buffer"), std::string::npos);
  EXPECT_NE(htree_build_summary.find("selected_terminal_branch_buffered_levels"), std::string::npos);
  EXPECT_NE(cts_log_content.find("routing_rc_source"), std::string::npos);
  EXPECT_NE(cts_log_content.find("leaf_load_cap_idx"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_group_count"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_min"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_max"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_mean"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_median"), std::string::npos);
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*power\s*\|\s*[^|\n]*W\s*\|)")));
  EXPECT_NE(cts_log_content.find("CharBuilder Sweep Progress"), std::string::npos);
  EXPECT_NE(cts_log_content.find("report.log Details"), std::string::npos);
  EXPECT_NE(cts_log_content.find("details omitted from cts.log"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("CharBuilder build Running"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("pareto_solution pattern_id="), std::string::npos);
  EXPECT_NE(report_log_content.find("frontier_feasible_solution_count="), std::string::npos);
  EXPECT_NE(report_log_content.find("feasible_frontier_entry_count="), std::string::npos);
  EXPECT_NE(report_log_content.find("delay_power_selection_summary"), std::string::npos);
  EXPECT_NE(report_log_content.find("frontier_selection_pool_size="), std::string::npos);
  EXPECT_NE(report_log_content.find("selection_policy=global_frontier_pareto_power_median"), std::string::npos);
  EXPECT_NE(report_log_content.find("pareto_power_median_index="), std::string::npos);
  EXPECT_NE(report_log_content.find("selected_pareto_power_order_index="), std::string::npos);
}

}  // namespace
}  // namespace icts_test
