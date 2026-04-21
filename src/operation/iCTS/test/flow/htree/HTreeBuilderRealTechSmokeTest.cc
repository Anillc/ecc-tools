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

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#if defined(ICTS_ENABLE_SLOW_REALTECH_REGRESSION) && ICTS_ENABLE_SLOW_REALTECH_REGRESSION
#include <functional>

#include "Point.hh"
#endif
#include "Tree.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/htree/HTreeVisualizationSupport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

#ifndef ICTS_ENABLE_SLOW_REALTECH_REGRESSION
#define ICTS_ENABLE_SLOW_REALTECH_REGRESSION 0
#endif

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

constexpr std::size_t kMaxRealClockLoadCount = 64U;
constexpr double kHTreeSmokeMaxSlewNs = 0.05;
constexpr double kHTreeSmokeMaxCapPf = 0.15;
constexpr double kArm9ExperimentRuntimeBudgetS = 600.0;
constexpr std::string_view kRunArm9ExperimentEnv = "ICTS_RUN_ARM9_HTREE_MATRIX";
constexpr std::string_view kRunArm9ExperimentAutoUnitEnv = "ICTS_RUN_ARM9_HTREE_MATRIX_AUTO_UNIT";
constexpr std::string_view kArm9ExperimentScenario = "htree_builder_arm9_full_sink_matrix";
constexpr std::string_view kArm9ExperimentAutoUnitScenario = "htree_builder_arm9_full_sink_matrix_auto_unit";
constexpr std::array<unsigned, 4> kArm9ExperimentIterations = {2U, 3U, 4U, 5U};
constexpr std::array<unsigned, 2> kArm9ExperimentSteps = {10U, 15U};
#if ICTS_ENABLE_SLOW_REALTECH_REGRESSION
constexpr unsigned kLeafUnbufferedRealTechCharSteps = 10U;
#endif

struct RealClockLoadSelection
{
  std::string clock_name;
  std::vector<icts::Pin*> loads;
};

struct Arm9ExperimentRecord
{
  unsigned wire_length_iterations = 0U;
  unsigned slew_cap_steps = 0U;
  double runtime_s = 0.0;
  bool success = false;
  std::size_t load_count = 0U;
  std::size_t final_frontier_count = 0U;
  unsigned selected_depth = 0U;
  unsigned best_pattern_id = 0U;
  double best_delay_ns = 0.0;
  double best_power_w = 0.0;
  double char_wire_length_unit_um = 0.0;
  unsigned char_wire_length_iterations = 0U;
  bool char_grid_adapted = false;
  bool used_boundary_fallback = false;
  std::string failure_reason;
};

auto ReadEnvFlag(std::string_view env_name) -> bool
{
  const char* raw_value = std::getenv(std::string(env_name).c_str());
  if (raw_value == nullptr) {
    return false;
  }

  const std::string value = raw_value;
  return !(value.empty() || value == "0" || value == "false" || value == "FALSE" || value == "False");
}

auto FormatArm9ExperimentReport(std::string_view scenario_name, const std::string& clock_name, std::size_t load_count,
                                bool omit_wire_length_unit, const std::vector<Arm9ExperimentRecord>& records) -> std::string
{
  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(6);
  report_stream << "scenario=" << scenario_name << "\n";
  report_stream << "clock_name=" << clock_name << "\n";
  report_stream << "load_count=" << load_count << "\n";
  report_stream << "omit_wire_length_unit=" << (omit_wire_length_unit ? "true" : "false") << "\n";
  report_stream << "runtime_budget_s=" << kArm9ExperimentRuntimeBudgetS << "\n";
  report_stream << "columns=iter,step,runtime_s,success,frontier_count,selected_depth,best_pattern_id,best_delay_ns,best_power_w,"
                   "char_wire_length_unit_um,char_wire_length_iterations,char_grid_adapted,used_boundary_fallback,failure_reason\n";
  for (const auto& record : records) {
    report_stream << record.wire_length_iterations << "," << record.slew_cap_steps << "," << record.runtime_s << ","
                  << (record.success ? "true" : "false") << "," << record.final_frontier_count << "," << record.selected_depth << ","
                  << record.best_pattern_id << "," << record.best_delay_ns << "," << record.best_power_w << ","
                  << record.char_wire_length_unit_um << "," << record.char_wire_length_iterations << ","
                  << (record.char_grid_adapted ? "true" : "false") << "," << (record.used_boundary_fallback ? "true" : "false") << ","
                  << record.failure_reason << "\n";
  }
  return report_stream.str();
}

auto SampleLoadsForSmoke(const std::vector<icts::Pin*>& loads, std::size_t max_count) -> std::vector<icts::Pin*>
{
  if (loads.size() <= max_count) {
    return loads;
  }

  std::vector<icts::Pin*> sampled_loads;
  sampled_loads.reserve(max_count);
  for (std::size_t sample_index = 0; sample_index < max_count; ++sample_index) {
    const std::size_t source_index = sample_index * loads.size() / max_count;
    sampled_loads.push_back(loads.at(source_index));
  }
  return sampled_loads;
}

auto SelectLargestRealClockLoads(std::size_t max_count) -> std::optional<RealClockLoadSelection>
{
  DESIGN_INST.reset();
  STA_ADAPTER_INST.updateTiming();

  for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
    auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
    DESIGN_INST.add_clock(std::move(clock));
  }

  WRAPPER_INST.read();

  RealClockLoadSelection best_selection;
  std::size_t best_source_load_count = 0U;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr || clock->get_loads().size() < 2U) {
      continue;
    }
    if (clock->get_loads().size() <= best_source_load_count) {
      continue;
    }

    best_selection.clock_name = clock->get_clock_name() + ":" + clock->get_clock_net_name();
    best_selection.loads = SampleLoadsForSmoke(clock->get_loads(), max_count);
    best_source_load_count = clock->get_loads().size();
  }

  if (best_selection.loads.size() < 2U) {
    return std::nullopt;
  }

  return best_selection;
}

auto CountPinsWithRealContext(const std::vector<icts::Pin*>& loads) -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(loads, [](const icts::Pin* pin) -> bool {
    return pin != nullptr && pin->get_inst() != nullptr && pin->get_net() != nullptr && !pin->get_name().empty()
           && !pin->get_inst()->get_name().empty();
  }));
}

auto CollectLeafLoads(const icts::Tree& topology) -> std::unordered_set<icts::Pin*>
{
  std::unordered_set<icts::Pin*> leaf_loads;
  const auto levels = topology.levels();
  if (levels.empty()) {
    return leaf_loads;
  }

  for (const auto node_id : levels.back()) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr || !node->isLeaf()) {
      continue;
    }
    for (auto* load : node->get_loads()) {
      if (load != nullptr) {
        leaf_loads.insert(load);
      }
    }
  }

  return leaf_loads;
}

auto AssertNoSingleLoadExternalLeafBuffer(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const std::unordered_set<const icts::Inst*> inserted_insts(result.inserted_insts.begin(), result.inserted_insts.end());
  for (const auto* inst : result.inserted_insts) {
    if (inst == nullptr || !inst->is_buffer()) {
      continue;
    }

    const auto* output_pin = inst->findDriverPin();
    ASSERT_NE(output_pin, nullptr);
    const auto* output_net = output_pin->get_net();
    if (output_net == nullptr || output_net->get_loads().size() != 1U || output_net->get_loads().front() == nullptr) {
      continue;
    }

    const auto* downstream_load = output_net->get_loads().front();
    const auto* downstream_inst = downstream_load->get_inst();
    const bool drives_internal_htree_inst = downstream_inst != nullptr && inserted_insts.contains(downstream_inst);
    EXPECT_TRUE(drives_internal_htree_inst) << "Expected leaf single-load buffer pruning to remove " << inst->get_name()
                                            << " but it still drives " << downstream_load->get_name();
  }
}

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  if (!input_stream.is_open()) {
    return {};
  }

  std::ostringstream content_stream;
  content_stream << input_stream.rdbuf();
  return content_stream.str();
}

auto IsSameCharEntry(const icts::HTreeTopologyChar& lhs, const icts::HTreeTopologyChar& rhs) -> bool
{
  constexpr double metric_tolerance = 1e-12;
  return lhs.get_pattern_id() == rhs.get_pattern_id() && lhs.get_input_slew_idx() == rhs.get_input_slew_idx()
         && lhs.get_output_slew_idx() == rhs.get_output_slew_idx() && lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx()
         && lhs.get_leaf_driven_cap_idx() == rhs.get_leaf_driven_cap_idx() && lhs.get_load_cap_idx() == rhs.get_load_cap_idx()
         && std::abs(lhs.get_delay() - rhs.get_delay()) <= metric_tolerance
         && std::abs(lhs.get_power() - rhs.get_power()) <= metric_tolerance;
}

auto ContainsCharEntry(const std::vector<icts::HTreeTopologyChar>& entries, const icts::HTreeTopologyChar& target) -> bool
{
  return std::ranges::any_of(entries, [&target](const icts::HTreeTopologyChar& entry) -> bool { return IsSameCharEntry(entry, target); });
}

auto FindSelectedDepthSummary(const icts::HTreeBuilder::BuildResult& result)
    -> const icts::HTreeBuilder::BuildResult::DepthCandidateSummary*
{
  const auto summary_it = std::ranges::find_if(result.depth_candidates, [](const auto& summary) -> bool { return summary.selected; });
  if (summary_it == result.depth_candidates.end()) {
    return nullptr;
  }
  return &(*summary_it);
}

auto AssertDepthCandidateCoverage(const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_FALSE(result.depth_candidates.empty());
  ASSERT_TRUE(result.selected_depth.has_value());

  const auto topology_levels = result.topology.levels();
  ASSERT_GT(topology_levels.size(), 1U);
  const auto max_depth = static_cast<unsigned>(topology_levels.size() - 1U);
  EXPECT_EQ(result.depth_candidates.size(), std::min<std::size_t>(CONFIG_INST.get_htree_depth_explore_window(), max_depth));

  const auto* selected_summary = FindSelectedDepthSummary(result);
  ASSERT_NE(selected_summary, nullptr);
  EXPECT_EQ(selected_summary->depth, result.selected_depth.value_or(0U));
  EXPECT_EQ(selected_summary->depth, result.levels.size());
  EXPECT_TRUE(selected_summary->success);
}

auto AssertSelectedLeafCapDistribution(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto* selected_summary = FindSelectedDepthSummary(result);
  ASSERT_NE(selected_summary, nullptr);
  ASSERT_GT(selected_summary->evaluated_leaf_count, 0U);
  EXPECT_LE(selected_summary->leaf_cap_min_pf, selected_summary->leaf_cap_mean_pf);
  EXPECT_LE(selected_summary->leaf_cap_min_pf, selected_summary->leaf_cap_median_pf);
  EXPECT_LE(selected_summary->leaf_cap_mean_pf, selected_summary->leaf_cap_max_pf);
  EXPECT_LE(selected_summary->leaf_cap_median_pf, selected_summary->leaf_cap_max_pf);
  if (!selected_summary->used_explicit_leaf_driven_cap) {
    EXPECT_DOUBLE_EQ(selected_summary->requested_leaf_driven_cap_pf, selected_summary->leaf_cap_max_pf);
  }
}

auto AssertSelectedHTreeLoadDistribution(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto* selected_summary = FindSelectedDepthSummary(result);
  ASSERT_NE(selected_summary, nullptr);
  ASSERT_GT(selected_summary->htree_load_group_count, 0U);
  EXPECT_LE(selected_summary->htree_load_cap_min_pf, selected_summary->htree_load_cap_mean_pf);
  EXPECT_LE(selected_summary->htree_load_cap_min_pf, selected_summary->htree_load_cap_median_pf);
  EXPECT_LE(selected_summary->htree_load_cap_mean_pf, selected_summary->htree_load_cap_max_pf);
  EXPECT_LE(selected_summary->htree_load_cap_median_pf, selected_summary->htree_load_cap_max_pf);
}

#if ICTS_ENABLE_SLOW_REALTECH_REGRESSION
auto UseLeafUnbufferedRealTechCharSteps() -> void
{
  CONFIG_INST.set_slew_steps(kLeafUnbufferedRealTechCharSteps);
  CONFIG_INST.set_cap_steps(kLeafUnbufferedRealTechCharSteps);
}
#endif

auto WriteAndAssertHTreeArtifacts(const htree::HTreeArtifactPaths& artifact_paths, const std::string& scenario_name,
                                  const std::string& clock_name, const std::vector<icts::Pin*>& loads,
                                  const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  EXPECT_TRUE(htree::WriteHTreeArtifacts(artifact_paths, scenario_name, clock_name, loads, result));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.cts_log));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.topology_svg));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.materialized_svg));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.pareto_svg));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.report_log));
}

#if ICTS_ENABLE_SLOW_REALTECH_REGRESSION
auto AssertBranchBufferMaterialization(const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.levels.empty());
  ASSERT_TRUE(result.force_branch_buffer);

  std::vector<icts::Point<int>> required_terminal_positions;
  const auto topology_levels = result.topology.levels();
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto& level = result.levels.at(level_index);
    EXPECT_TRUE(level.selected_has_terminal_branch_buffer);

    if (!level.is_leaf_level || level_index + 1U >= topology_levels.size()) {
      continue;
    }
    for (const auto node_id : topology_levels.at(level_index + 1U)) {
      const auto* node = result.topology.get_node(node_id);
      if (node == nullptr || node->get_loads().empty()) {
        continue;
      }
      required_terminal_positions.push_back(node->get_position());
    }
  }

  for (const auto& expected_position : required_terminal_positions) {
    const bool has_terminal_inst = std::ranges::any_of(result.inserted_insts, [&expected_position](const icts::Inst* inst) -> bool {
      return inst != nullptr && inst->get_location() == expected_position;
    });
    EXPECT_TRUE(has_terminal_inst);
  }
}

auto AssertLeafUnbufferedMaterialization(const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.levels.empty());
  ASSERT_TRUE(result.force_leaf_unbuffered);
  ASSERT_FALSE(result.force_branch_buffer);

  std::vector<icts::Point<int>> terminal_positions;
  const auto topology_levels = result.topology.levels();
  bool exercised_requirement = false;
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto& level = result.levels.at(level_index);
    if (!level.is_leaf_level) {
      continue;
    }
    exercised_requirement = true;
    EXPECT_FALSE(level.selected_has_terminal_branch_buffer);

    if (level_index + 1U >= topology_levels.size()) {
      continue;
    }
    for (const auto node_id : topology_levels.at(level_index + 1U)) {
      const auto* node = result.topology.get_node(node_id);
      if (node == nullptr || node->get_loads().empty()) {
        continue;
      }
      terminal_positions.push_back(node->get_position());
    }
  }

  EXPECT_TRUE(exercised_requirement);

  for (const auto& terminal_position : terminal_positions) {
    const bool has_terminal_inst = std::ranges::any_of(result.inserted_insts, [&terminal_position](const icts::Inst* inst) -> bool {
      return inst != nullptr && inst->get_location() == terminal_position;
    });
    EXPECT_FALSE(has_terminal_inst);
  }
}
#endif

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

  EXPECT_EQ(CONFIG_INST.get_wire_length_iterations(), realtech_support::kRealTechCharWireLengthIterations);
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

  auto result = icts::HTreeBuilder::build(real_loads);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.failure_reason.empty());
  ASSERT_TRUE(result.best_char.has_value());
  ASSERT_TRUE(result.best_pattern.has_value());
  ASSERT_FALSE(result.feasible_chars.empty());
  ASSERT_FALSE(result.feasible_frontier_entries.empty());
  AssertDepthCandidateCoverage(result);
  AssertSelectedLeafCapDistribution(result);
  AssertSelectedHTreeLoadDistribution(result);
  EXPECT_TRUE(result.min_leaf_driven_cap_pf.has_value());
  EXPECT_LE(result.feasible_frontier_entries.size(), result.feasible_chars.size());
  const auto best_char = result.best_char.value_or(icts::HTreeTopologyChar{});
  EXPECT_TRUE(ContainsCharEntry(result.feasible_frontier_entries, best_char));
  const icts::HTreeTopologyPattern* best_pattern = nullptr;
  if (result.best_pattern.has_value()) {
    best_pattern = &result.best_pattern.value();
  }
  ASSERT_NE(best_pattern, nullptr);
  ASSERT_EQ(best_pattern->get_levels(), result.levels.size());
  ASSERT_EQ(best_pattern->get_level_segment_pattern_ids().size(), result.levels.size());
  ASSERT_NE(result.root_input_pin, nullptr);
  ASSERT_NE(result.root_output_pin, nullptr);
  EXPECT_NE(result.root_input_pin, result.root_output_pin);
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
  EXPECT_NE(cts_log_content.find("CharBuilder Runtime Configuration"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CharBuilder Routing / Wire RC"), std::string::npos);
  EXPECT_NE(cts_log_content.find("HTreeBuilder Build Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("Ohm/um"), std::string::npos);
  EXPECT_NE(cts_log_content.find("pF/um"), std::string::npos);
  EXPECT_NE(cts_log_content.find("leaf_load_cap_min"), std::string::npos);
  EXPECT_NE(cts_log_content.find("leaf_load_cap_max"), std::string::npos);
  EXPECT_NE(cts_log_content.find("leaf_load_cap_mean"), std::string::npos);
  EXPECT_NE(cts_log_content.find("leaf_load_cap_median"), std::string::npos);
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

TEST(HTreeBuilderRealTechSmokeTest, Arm9FullSinkExperimentMatrix)
{
  if (!ReadEnvFlag(kRunArm9ExperimentEnv)) {
    GTEST_SKIP() << "Set " << kRunArm9ExperimentEnv << "=1 to run the ARM9 full-sink H-tree experiment matrix.";
    return;
  }

  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClockLoads(std::numeric_limits<std::size_t>::max());
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes at least two CTS sink pins.";
    return;
  }

  ASSERT_GE(selected_clock->loads.size(), 2U);
  ASSERT_EQ(CountPinsWithRealContext(selected_clock->loads), selected_clock->loads.size())
      << "Selected clock loads do not carry complete DEF/CTS instance context: " << selected_clock->clock_name;

  std::vector<Arm9ExperimentRecord> records;
  records.reserve(kArm9ExperimentIterations.size() * kArm9ExperimentSteps.size());

  for (const unsigned wire_length_iterations : kArm9ExperimentIterations) {
    for (const unsigned slew_cap_steps : kArm9ExperimentSteps) {
      std::ostringstream scenario_name_stream;
      scenario_name_stream << "htree_builder_arm9_full_sink_iter" << wire_length_iterations << "_step" << slew_cap_steps;
      const std::string scenario_name = scenario_name_stream.str();

      realtech_support::RealTechCharSession char_session;
      if (const auto prepare_error = char_session.prepare(scenario_name, std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf);
          prepare_error.has_value()) {
        GTEST_SKIP() << *prepare_error;
        return;
      }

      CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
      CONFIG_INST.set_slew_steps(slew_cap_steps);
      CONFIG_INST.set_cap_steps(slew_cap_steps);

      const auto runtime_start = std::chrono::steady_clock::now();
      const auto result = icts::HTreeBuilder::build(selected_clock->loads);
      const auto runtime_end = std::chrono::steady_clock::now();
      const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();

      Arm9ExperimentRecord record{
          .wire_length_iterations = wire_length_iterations,
          .slew_cap_steps = slew_cap_steps,
          .runtime_s = runtime_s,
          .success = result.success,
          .load_count = selected_clock->loads.size(),
          .char_wire_length_unit_um = result.char_wire_length_unit_um,
          .char_wire_length_iterations = result.char_wire_length_iterations,
          .char_grid_adapted = result.char_grid_adapted,
          .used_boundary_fallback = result.used_boundary_fallback,
          .failure_reason = result.failure_reason,
      };

      if (const auto* selected_summary = FindSelectedDepthSummary(result); selected_summary != nullptr) {
        record.final_frontier_count = selected_summary->final_frontier_count;
      }
      if (result.selected_depth.has_value()) {
        record.selected_depth = *result.selected_depth;
      }
      if (result.best_char.has_value()) {
        record.best_pattern_id = result.best_char->get_pattern_id().local_id;
        record.best_delay_ns = result.best_char->get_delay();
        record.best_power_w = result.best_char->get_power();
      }
      records.push_back(record);

      SCOPED_TRACE("iter=" + std::to_string(wire_length_iterations) + ", step=" + std::to_string(slew_cap_steps));
      EXPECT_TRUE(result.success) << "failure_reason=" << result.failure_reason;
      EXPECT_LE(runtime_s, kArm9ExperimentRuntimeBudgetS);
      EXPECT_GT(record.final_frontier_count, 0U);
      EXPECT_TRUE(result.best_char.has_value());
    }
  }

  const auto report
      = FormatArm9ExperimentReport(kArm9ExperimentScenario, selected_clock->clock_name, selected_clock->loads.size(), false, records);
  EXPECT_TRUE(realtech_support::WriteScenarioLog(std::string(kArm9ExperimentScenario), "matrix_report.txt", report));
}

TEST(HTreeBuilderRealTechSmokeTest, Arm9FullSinkExperimentMatrixAutoWireLengthUnit)
{
  if (!ReadEnvFlag(kRunArm9ExperimentAutoUnitEnv)) {
    GTEST_SKIP() << "Set " << kRunArm9ExperimentAutoUnitEnv << "=1 to run the ARM9 full-sink H-tree experiment matrix with "
                 << "auto-derived wire length unit.";
    return;
  }

  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClockLoads(std::numeric_limits<std::size_t>::max());
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes at least two CTS sink pins.";
    return;
  }

  ASSERT_GE(selected_clock->loads.size(), 2U);
  ASSERT_EQ(CountPinsWithRealContext(selected_clock->loads), selected_clock->loads.size())
      << "Selected clock loads do not carry complete DEF/CTS instance context: " << selected_clock->clock_name;

  std::vector<Arm9ExperimentRecord> records;
  records.reserve(kArm9ExperimentIterations.size() * kArm9ExperimentSteps.size());

  for (const unsigned wire_length_iterations : kArm9ExperimentIterations) {
    for (const unsigned slew_cap_steps : kArm9ExperimentSteps) {
      std::ostringstream scenario_name_stream;
      scenario_name_stream << "htree_builder_arm9_full_sink_auto_unit_iter" << wire_length_iterations << "_step" << slew_cap_steps;
      const std::string scenario_name = scenario_name_stream.str();

      realtech_support::RealTechCharSession char_session;
      if (const auto prepare_error = char_session.prepare(scenario_name, std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, true);
          prepare_error.has_value()) {
        GTEST_SKIP() << *prepare_error;
        return;
      }

      CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
      CONFIG_INST.set_slew_steps(slew_cap_steps);
      CONFIG_INST.set_cap_steps(slew_cap_steps);

      const auto runtime_start = std::chrono::steady_clock::now();
      const auto result = icts::HTreeBuilder::build(selected_clock->loads);
      const auto runtime_end = std::chrono::steady_clock::now();
      const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();

      Arm9ExperimentRecord record{
          .wire_length_iterations = wire_length_iterations,
          .slew_cap_steps = slew_cap_steps,
          .runtime_s = runtime_s,
          .success = result.success,
          .load_count = selected_clock->loads.size(),
          .char_wire_length_unit_um = result.char_wire_length_unit_um,
          .char_wire_length_iterations = result.char_wire_length_iterations,
          .char_grid_adapted = result.char_grid_adapted,
          .used_boundary_fallback = result.used_boundary_fallback,
          .failure_reason = result.failure_reason,
      };

      if (const auto* selected_summary = FindSelectedDepthSummary(result); selected_summary != nullptr) {
        record.final_frontier_count = selected_summary->final_frontier_count;
      }
      if (result.selected_depth.has_value()) {
        record.selected_depth = *result.selected_depth;
      }
      if (result.best_char.has_value()) {
        record.best_pattern_id = result.best_char->get_pattern_id().local_id;
        record.best_delay_ns = result.best_char->get_delay();
        record.best_power_w = result.best_char->get_power();
      }
      records.push_back(record);

      SCOPED_TRACE("iter=" + std::to_string(wire_length_iterations) + ", step=" + std::to_string(slew_cap_steps));
      EXPECT_TRUE(result.success) << "failure_reason=" << result.failure_reason;
      EXPECT_LE(runtime_s, kArm9ExperimentRuntimeBudgetS);
      EXPECT_GT(record.final_frontier_count, 0U);
      EXPECT_TRUE(result.best_char.has_value());
      EXPECT_GT(record.char_wire_length_unit_um, 0.0);
      EXPECT_LE(record.char_wire_length_iterations, wire_length_iterations);
    }
  }

  const auto report = FormatArm9ExperimentReport(kArm9ExperimentAutoUnitScenario, selected_clock->clock_name, selected_clock->loads.size(),
                                                 true, records);
  EXPECT_TRUE(realtech_support::WriteScenarioLog(std::string(kArm9ExperimentAutoUnitScenario), "matrix_report.txt", report));
}

#if ICTS_ENABLE_SLOW_REALTECH_REGRESSION
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

  auto result = icts::HTreeBuilder::build(selected_clock->loads);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.failure_reason.empty());
  ASSERT_FALSE(result.feasible_chars.empty());
  ASSERT_FALSE(result.feasible_frontier_entries.empty());
  EXPECT_LE(result.feasible_frontier_entries.size(), result.feasible_chars.size());
  ASSERT_TRUE(result.best_char.has_value());
  const auto best_char = result.best_char.value_or(icts::HTreeTopologyChar{});
  EXPECT_TRUE(ContainsCharEntry(result.feasible_frontier_entries, best_char));
  EXPECT_EQ(result.char_slew_steps, realtech_support::kRealTechCharSlewSteps);
  EXPECT_EQ(result.char_cap_steps, realtech_support::kRealTechCharCapSteps);
  AssertBranchBufferMaterialization(result);
  WriteAndAssertHTreeArtifacts(artifact_paths, "htree_builder_branch_buffer", selected_clock->clock_name, selected_clock->loads, result);
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  EXPECT_NE(cts_log_content.find("force_branch_buffer"), std::string::npos);
  EXPECT_NE(cts_log_content.find("true"), std::string::npos);
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

  const auto result = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.force_branch_buffer = true});

  AssertBranchBufferMaterialization(result);
}

TEST(HTreeBuilderRealTechSmokeTest, CallerFacingLeafUnbufferedOptionSelectsUnbufferedLeafPatterns)
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
      = char_session.prepare("htree_builder_leaf_unbuffered_option", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, false, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }
  UseLeafUnbufferedRealTechCharSteps();

  ASSERT_TRUE(CONFIG_INST.is_force_branch_buffer());
  EXPECT_EQ(CONFIG_INST.get_slew_steps(), kLeafUnbufferedRealTechCharSteps);
  EXPECT_EQ(CONFIG_INST.get_cap_steps(), kLeafUnbufferedRealTechCharSteps);

  const auto artifact_paths = htree::PrepareHTreeArtifactPaths("realtech_leaf_unbuffered_option");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "HTree Flow Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("HTree Smoke Scenario", {
                                                                   {"scenario", "htree_builder_leaf_unbuffered_option"},
                                                                   {"clock_name", selected_clock->clock_name},
                                                                   {"load_count", std::to_string(selected_clock->loads.size())},
                                                               });

  const auto result = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{
                                                                           .force_branch_buffer = false,
                                                                           .force_leaf_unbuffered = true,
                                                                       });

  ASSERT_TRUE(result.force_leaf_unbuffered);
  ASSERT_FALSE(result.force_branch_buffer);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.failure_reason.empty());
  ASSERT_FALSE(result.feasible_chars.empty());
  ASSERT_FALSE(result.feasible_frontier_entries.empty());
  EXPECT_LE(result.feasible_frontier_entries.size(), result.feasible_chars.size());
  ASSERT_TRUE(result.best_char.has_value());
  const auto best_char = result.best_char.value_or(icts::HTreeTopologyChar{});
  EXPECT_TRUE(ContainsCharEntry(result.feasible_frontier_entries, best_char));
  EXPECT_EQ(result.char_slew_steps, kLeafUnbufferedRealTechCharSteps);
  EXPECT_EQ(result.char_cap_steps, kLeafUnbufferedRealTechCharSteps);
  AssertLeafUnbufferedMaterialization(result);
  WriteAndAssertHTreeArtifacts(artifact_paths, "htree_builder_leaf_unbuffered_option", selected_clock->clock_name, selected_clock->loads,
                               result);
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  EXPECT_NE(cts_log_content.find("force_leaf_unbuffered"), std::string::npos);
  EXPECT_NE(cts_log_content.find("true"), std::string::npos);
}

TEST(HTreeBuilderRealTechSmokeTest, CallerFacingBoundaryBuildOptionsPropagateWhenFeasible)
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

  const auto baseline_result = icts::HTreeBuilder::build(selected_clock->loads);
  ASSERT_TRUE(baseline_result.success);
  ASSERT_FALSE(baseline_result.levels.empty());
  ASSERT_FALSE(baseline_result.feasible_chars.empty());
  ASSERT_GT(baseline_result.char_slew_steps, 0U);
  ASSERT_GT(baseline_result.char_cap_steps, 0U);
  ASSERT_GT(baseline_result.char_max_slew_ns, 0.0);
  ASSERT_GT(baseline_result.char_max_cap_pf, 0.0);
  ASSERT_TRUE(baseline_result.min_leaf_driven_cap_pf.has_value());
  ASSERT_TRUE(baseline_result.leaf_driven_cap_covering_idx.has_value());

  const auto top_covering_it = std::ranges::max_element(baseline_result.feasible_chars, {}, &icts::HTreeTopologyChar::get_input_slew_idx);
  ASSERT_NE(top_covering_it, baseline_result.feasible_chars.end());
  const auto leaf_covering_it
      = std::ranges::max_element(baseline_result.feasible_chars, {}, &icts::HTreeTopologyChar::get_leaf_driven_cap_idx);
  ASSERT_NE(leaf_covering_it, baseline_result.feasible_chars.end());

  const unsigned top_covering_idx = top_covering_it->get_input_slew_idx();
  const unsigned leaf_covering_idx = leaf_covering_it->get_leaf_driven_cap_idx();
  ASSERT_GT(top_covering_idx, 0U);
  ASSERT_GT(leaf_covering_idx, 0U);
  const double top_input_slew_ns = (static_cast<double>(top_covering_idx) - 0.5) * baseline_result.char_max_slew_ns
                                   / static_cast<double>(baseline_result.char_slew_steps);
  const double leaf_driven_cap_pf = (static_cast<double>(leaf_covering_idx) - 0.5) * baseline_result.char_max_cap_pf
                                    / static_cast<double>(baseline_result.char_cap_steps);

  auto top_boundary_result
      = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = top_input_slew_ns});
  ASSERT_TRUE(top_boundary_result.success);
  ASSERT_TRUE(top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(top_boundary_result.min_top_input_slew_ns.value_or(0.0), top_input_slew_ns);
  ASSERT_TRUE(top_boundary_result.top_input_slew_covering_idx.has_value());
  EXPECT_EQ(top_boundary_result.top_input_slew_covering_idx.value_or(0U), top_covering_idx);
  ASSERT_FALSE(top_boundary_result.feasible_chars.empty());
  EXPECT_TRUE(std::ranges::all_of(top_boundary_result.feasible_chars, [&](const icts::HTreeTopologyChar& entry) -> bool {
    return entry.get_input_slew_idx() >= top_covering_idx;
  }));
  ASSERT_TRUE(top_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(top_boundary_result.min_leaf_driven_cap_pf.value_or(0.0), baseline_result.min_leaf_driven_cap_pf.value_or(0.0));
  ASSERT_TRUE(top_boundary_result.leaf_driven_cap_covering_idx.has_value());
  EXPECT_EQ(top_boundary_result.leaf_driven_cap_covering_idx.value_or(0U), baseline_result.leaf_driven_cap_covering_idx.value_or(0U));

  auto leaf_boundary_result
      = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_leaf_driven_cap_pf = leaf_driven_cap_pf});
  ASSERT_TRUE(leaf_boundary_result.success);
  ASSERT_TRUE(leaf_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(leaf_boundary_result.min_leaf_driven_cap_pf.value_or(0.0), leaf_driven_cap_pf);
  ASSERT_TRUE(leaf_boundary_result.leaf_driven_cap_covering_idx.has_value());
  EXPECT_EQ(leaf_boundary_result.leaf_driven_cap_covering_idx.value_or(0U), leaf_covering_idx);
  ASSERT_FALSE(leaf_boundary_result.feasible_chars.empty());
  EXPECT_TRUE(std::ranges::all_of(leaf_boundary_result.feasible_chars, [&](const icts::HTreeTopologyChar& entry) -> bool {
    return entry.get_leaf_driven_cap_idx() >= leaf_covering_idx;
  }));
  EXPECT_FALSE(leaf_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_FALSE(leaf_boundary_result.top_input_slew_covering_idx.has_value());

  const double impossible_top_input_slew_ns
      = baseline_result.char_max_slew_ns + (baseline_result.char_max_slew_ns / static_cast<double>(baseline_result.char_slew_steps));
  auto impossible_top_boundary_result = icts::HTreeBuilder::build(
      selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = impossible_top_input_slew_ns});
  ASSERT_TRUE(impossible_top_boundary_result.success);
  ASSERT_TRUE(impossible_top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(impossible_top_boundary_result.min_top_input_slew_ns.value_or(0.0), impossible_top_input_slew_ns);
  ASSERT_TRUE(impossible_top_boundary_result.top_input_slew_covering_idx.has_value());
  EXPECT_EQ(impossible_top_boundary_result.top_input_slew_covering_idx.value_or(0U), baseline_result.char_slew_steps + 1U);
  EXPECT_TRUE(impossible_top_boundary_result.used_boundary_fallback);
  EXPECT_FALSE(impossible_top_boundary_result.boundary_fallback_reason.empty());
  ASSERT_TRUE(impossible_top_boundary_result.boundary_fallback_score.has_value());
  EXPECT_TRUE(impossible_top_boundary_result.feasible_chars.empty());
  ASSERT_FALSE(impossible_top_boundary_result.candidate_chars.empty());
  ASSERT_FALSE(impossible_top_boundary_result.candidate_frontier_entries.empty());
  EXPECT_LE(impossible_top_boundary_result.candidate_frontier_entries.size(), impossible_top_boundary_result.candidate_chars.size());
  ASSERT_TRUE(impossible_top_boundary_result.best_char.has_value());
  const auto impossible_top_best_char = impossible_top_boundary_result.best_char.value_or(icts::HTreeTopologyChar{});
  EXPECT_TRUE(ContainsCharEntry(impossible_top_boundary_result.candidate_frontier_entries, impossible_top_best_char));
  const unsigned impossible_top_covering_idx = impossible_top_boundary_result.top_input_slew_covering_idx.value_or(0U);
  ASSERT_GT(impossible_top_covering_idx, 0U);
  EXPECT_LT(impossible_top_best_char.get_input_slew_idx(), impossible_top_covering_idx);
  double expected_top_boundary_fallback_score
      = static_cast<double>(impossible_top_best_char.get_input_slew_idx()) / static_cast<double>(baseline_result.char_slew_steps);
  if (impossible_top_boundary_result.leaf_driven_cap_covering_idx.has_value() && baseline_result.char_cap_steps > 0U) {
    expected_top_boundary_fallback_score
        += static_cast<double>(impossible_top_best_char.get_leaf_driven_cap_idx()) / static_cast<double>(baseline_result.char_cap_steps);
  }
  EXPECT_DOUBLE_EQ(impossible_top_boundary_result.boundary_fallback_score.value_or(0.0), expected_top_boundary_fallback_score);

  const double impossible_leaf_driven_cap_pf
      = baseline_result.char_max_cap_pf + (baseline_result.char_max_cap_pf / static_cast<double>(baseline_result.char_cap_steps));
  auto impossible_leaf_boundary_result = icts::HTreeBuilder::build(
      selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_leaf_driven_cap_pf = impossible_leaf_driven_cap_pf});
  ASSERT_TRUE(impossible_leaf_boundary_result.success);
  ASSERT_TRUE(impossible_leaf_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(impossible_leaf_boundary_result.min_leaf_driven_cap_pf.value_or(0.0), impossible_leaf_driven_cap_pf);
  ASSERT_TRUE(impossible_leaf_boundary_result.leaf_driven_cap_covering_idx.has_value());
  EXPECT_EQ(impossible_leaf_boundary_result.leaf_driven_cap_covering_idx.value_or(0U), baseline_result.char_cap_steps + 1U);
  EXPECT_TRUE(impossible_leaf_boundary_result.used_boundary_fallback);
  EXPECT_FALSE(impossible_leaf_boundary_result.boundary_fallback_reason.empty());
  ASSERT_TRUE(impossible_leaf_boundary_result.boundary_fallback_score.has_value());
  EXPECT_TRUE(impossible_leaf_boundary_result.feasible_chars.empty());
  ASSERT_FALSE(impossible_leaf_boundary_result.candidate_chars.empty());
  ASSERT_FALSE(impossible_leaf_boundary_result.candidate_frontier_entries.empty());
  EXPECT_LE(impossible_leaf_boundary_result.candidate_frontier_entries.size(), impossible_leaf_boundary_result.candidate_chars.size());
  ASSERT_TRUE(impossible_leaf_boundary_result.best_char.has_value());
  const auto impossible_leaf_best_char = impossible_leaf_boundary_result.best_char.value_or(icts::HTreeTopologyChar{});
  EXPECT_TRUE(ContainsCharEntry(impossible_leaf_boundary_result.candidate_frontier_entries, impossible_leaf_best_char));
  const unsigned impossible_leaf_covering_idx = impossible_leaf_boundary_result.leaf_driven_cap_covering_idx.value_or(0U);
  ASSERT_GT(impossible_leaf_covering_idx, 0U);
  EXPECT_LT(impossible_leaf_best_char.get_leaf_driven_cap_idx(), impossible_leaf_covering_idx);
  EXPECT_DOUBLE_EQ(
      impossible_leaf_boundary_result.boundary_fallback_score.value_or(0.0),
      static_cast<double>(impossible_leaf_best_char.get_leaf_driven_cap_idx()) / static_cast<double>(baseline_result.char_cap_steps));
}
#endif

}  // namespace
}  // namespace icts_test
