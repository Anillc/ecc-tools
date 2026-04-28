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
 * @file HTreeBuilderRealTechBranchBufferRegressionTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Slow real-tech branch-buffer regression coverage for HTreeBuilder.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Net.hh"
#include "Pin.hh"
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

TEST(HTreeBuilderRealTechSmokeTest, ForceBranchBufferSelectsTerminalBranchPatternsOnEveryLevel)
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
  if (const auto prepare_error
      = char_session.prepare("htree_builder_branch_buffer", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, false, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  ASSERT_TRUE(CONFIG_INST.is_force_branch_buffer());
  EXPECT_EQ(CONFIG_INST.get_slew_steps(), realtech_support::kRealTechCharSlewSteps);
  EXPECT_EQ(CONFIG_INST.get_cap_steps(), realtech_support::kRealTechCharCapSteps);

  const auto artifact_paths = htree::PrepareHTreeArtifactPaths("realtech_branch_buffer");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "HTree Flow Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("HTree Smoke Scenario", {
                                                                   {"scenario", "htree_builder_branch_buffer"},
                                                                   {"clock_name", selected_clock->clock_name},
                                                                   {"load_count", std::to_string(selected_clock->loads.size())},
                                                               });

  icts::Pin root_driver("htree_branch_buffer_root_out", icts::PinType::kOut);
  icts::Net root_net("htree_branch_buffer_root_net");
  ConnectRootNetForHTreeTest(root_net, root_driver, selected_clock->loads);

  auto result = icts::HTreeBuilder::build(root_net);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.failure_reason.empty());
  const auto observation = htree::ObserveHTreeBuild(result);
  ASSERT_GT(observation.selected_feasible_solution_count, 0U);
  ASSERT_GT(observation.selected_feasible_frontier_entry_count, 0U);
  EXPECT_LE(observation.selected_feasible_frontier_entry_count, observation.selected_feasible_solution_count);
  ASSERT_TRUE(result.best_char.has_value());
  EXPECT_EQ(result.char_slew_steps, realtech_support::kRealTechCharSlewSteps);
  EXPECT_EQ(result.char_cap_steps, realtech_support::kRealTechCharCapSteps);
  AssertBranchBufferMaterialization(result);
  WriteAndAssertHTreeArtifacts(artifact_paths, "htree_builder_branch_buffer", selected_clock->clock_name, selected_clock->loads, result);
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  EXPECT_EQ(cts_log_content.find("force_branch_buffer"), std::string::npos);
  EXPECT_NE(cts_log_content.find("selected_terminal_branch_buffered_levels"), std::string::npos);
}

TEST(HTreeBuilderRealTechSmokeTest, CallerFacingBranchBufferOptionOverridesConfigDefault)
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
  if (const auto prepare_error = char_session.prepare("htree_builder_branch_buffer_option_override", std::nullopt, kHTreeSmokeMaxSlewNs,
                                                      kHTreeSmokeMaxCapPf, false, false);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  ASSERT_FALSE(CONFIG_INST.is_force_branch_buffer());

  icts::Pin root_driver("htree_branch_buffer_override_root_out", icts::PinType::kOut);
  icts::Net root_net("htree_branch_buffer_override_root_net");
  ConnectRootNetForHTreeTest(root_net, root_driver, selected_clock->loads);

  const auto result = icts::HTreeBuilder::build(root_net, icts::HTreeBuilder::BuildOptions{.force_branch_buffer = true});

  AssertBranchBufferMaterialization(result);
}

TEST(HTreeBuilderRealTechSmokeTest, CallerFacingTopBoundaryBuildOptionsPropagateWhenFeasible)
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
  if (const auto prepare_error
      = char_session.prepare("htree_builder_boundary_options", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::Pin baseline_root_driver("htree_boundary_baseline_root_out", icts::PinType::kOut);
  icts::Net baseline_root_net("htree_boundary_baseline_root_net");
  ConnectRootNetForHTreeTest(baseline_root_net, baseline_root_driver, selected_clock->loads);
  const auto baseline_result = icts::HTreeBuilder::build(baseline_root_net);
  ASSERT_TRUE(baseline_result.success);
  ASSERT_FALSE(baseline_result.levels.empty());
  ASSERT_GT(baseline_result.char_slew_steps, 0U);
  ASSERT_GT(baseline_result.char_cap_steps, 0U);
  ASSERT_GT(baseline_result.char_max_slew_ns, 0.0);
  ASSERT_GT(baseline_result.char_max_cap_pf, 0.0);
  if (!baseline_result.best_char.has_value()) {
    FAIL() << "baseline build should select an H-tree char";
    return;
  }

  const auto& baseline_best_char = baseline_result.best_char.value();
  const unsigned top_covering_idx = baseline_best_char.get_input_slew_idx();
  ASSERT_GT(top_covering_idx, 0U);
  const double top_input_slew_ns = (static_cast<double>(top_covering_idx) - 0.5) * baseline_result.char_max_slew_ns
                                   / static_cast<double>(baseline_result.char_slew_steps);

  icts::Pin top_boundary_root_driver("htree_boundary_root_out", icts::PinType::kOut);
  icts::Net top_boundary_root_net("htree_boundary_root_net");
  ConnectRootNetForHTreeTest(top_boundary_root_net, top_boundary_root_driver, selected_clock->loads);
  auto top_boundary_result
      = icts::HTreeBuilder::build(top_boundary_root_net, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = top_input_slew_ns});
  ASSERT_TRUE(top_boundary_result.success);
  ASSERT_TRUE(top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(top_boundary_result.min_top_input_slew_ns.value_or(0.0), top_input_slew_ns);
  ASSERT_TRUE(top_boundary_result.top_input_slew_covering_idx.has_value());
  EXPECT_EQ(top_boundary_result.top_input_slew_covering_idx.value_or(0U), top_covering_idx);
  const auto top_boundary_observation = htree::ObserveHTreeBuild(top_boundary_result);
  ASSERT_GT(top_boundary_observation.selected_feasible_frontier_entry_count, 0U);
  if (!top_boundary_result.best_char.has_value()) {
    FAIL() << "top-boundary build should select an H-tree char";
    return;
  }
  const auto& top_boundary_best_char = top_boundary_result.best_char.value();
  EXPECT_GE(top_boundary_best_char.get_input_slew_idx(), top_covering_idx);

  const double impossible_top_input_slew_ns
      = baseline_result.char_max_slew_ns + (baseline_result.char_max_slew_ns / static_cast<double>(baseline_result.char_slew_steps));
  icts::Pin impossible_root_driver("htree_boundary_impossible_root_out", icts::PinType::kOut);
  icts::Net impossible_root_net("htree_boundary_impossible_root_net");
  ConnectRootNetForHTreeTest(impossible_root_net, impossible_root_driver, selected_clock->loads);
  auto impossible_top_boundary_result = icts::HTreeBuilder::build(
      impossible_root_net, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = impossible_top_input_slew_ns});
  ASSERT_TRUE(impossible_top_boundary_result.success);
  ASSERT_TRUE(impossible_top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(impossible_top_boundary_result.min_top_input_slew_ns.value_or(0.0), impossible_top_input_slew_ns);
  ASSERT_TRUE(impossible_top_boundary_result.top_input_slew_covering_idx.has_value());
  EXPECT_EQ(impossible_top_boundary_result.top_input_slew_covering_idx.value_or(0U), baseline_result.char_slew_steps + 1U);
  const auto impossible_observation = htree::ObserveHTreeBuild(impossible_top_boundary_result);
  EXPECT_TRUE(impossible_observation.used_boundary_fallback);
  EXPECT_FALSE(impossible_observation.boundary_fallback_reason.empty());
  ASSERT_TRUE(impossible_observation.boundary_fallback_score.has_value());
  EXPECT_EQ(impossible_observation.selected_feasible_solution_count, 0U);
  ASSERT_GT(impossible_observation.selected_candidate_solution_count, 0U);
  ASSERT_GT(impossible_observation.selected_candidate_frontier_entry_count, 0U);
  EXPECT_LE(impossible_observation.selected_candidate_frontier_entry_count, impossible_observation.selected_candidate_solution_count);
  if (!impossible_top_boundary_result.best_char.has_value()) {
    FAIL() << "fallback build should keep the best candidate H-tree char";
    return;
  }
  const auto& impossible_top_best_char = impossible_top_boundary_result.best_char.value();
  const unsigned impossible_top_covering_idx = impossible_top_boundary_result.top_input_slew_covering_idx.value_or(0U);
  ASSERT_GT(impossible_top_covering_idx, 0U);
  EXPECT_LT(impossible_top_best_char.get_input_slew_idx(), impossible_top_covering_idx);
  const double expected_top_boundary_fallback_score
      = static_cast<double>(impossible_top_best_char.get_input_slew_idx()) / static_cast<double>(baseline_result.char_slew_steps);
  EXPECT_DOUBLE_EQ(impossible_observation.boundary_fallback_score.value_or(0.0), expected_top_boundary_fallback_score);
}

}  // namespace
}  // namespace icts_test
