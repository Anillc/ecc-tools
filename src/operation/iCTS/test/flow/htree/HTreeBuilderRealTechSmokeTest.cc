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
 * @author OpenAI Codex
 * @date 2026-04-14
 * @brief Real-tech smoke coverage for HTreeBuilder on DEF-derived clock loads.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "CharBuilder.hh"
#include "Clock.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
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

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

constexpr std::size_t kMaxRealClockLoadCount = 64U;
constexpr double kHTreeSmokeMaxSlewNs = 0.05;
constexpr double kHTreeSmokeMaxCapPf = 0.15;

struct RealClockLoadSelection
{
  std::string clock_name;
  std::vector<icts::Pin*> loads;
};

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

auto AssertLeafBranchBufferMaterialization(const icts::HTreeBuilder::BuildResult& result) -> void
{
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.levels.empty());
  ASSERT_TRUE(result.force_leaf_branch_buffer);

  std::vector<icts::Point<int>> required_terminal_positions;
  const auto topology_levels = result.topology.levels();
  bool exercised_requirement = false;
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto& level = result.levels.at(level_index);
    if (!level.is_leaf_level) {
      continue;
    }
    exercised_requirement = true;
    EXPECT_TRUE(level.selected_has_terminal_branch_buffer);

    if (level_index + 1U >= topology_levels.size()) {
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

  EXPECT_TRUE(exercised_requirement);

  for (const auto& expected_position : required_terminal_positions) {
    const bool has_terminal_inst = std::ranges::any_of(result.inserted_insts, [&expected_position](const icts::Inst* inst) -> bool {
      return inst != nullptr && inst->get_location() == expected_position;
    });
    EXPECT_TRUE(has_terminal_inst);
  }
}

auto FilterSegmentFrontierByFloor(const std::vector<icts::SegmentChar>& entries, std::optional<unsigned> input_slew_floor_idx,
                                  std::optional<unsigned> driven_cap_floor_idx) -> std::vector<icts::SegmentChar>
{
  std::vector<icts::SegmentChar> filtered_entries;
  filtered_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    if (input_slew_floor_idx.has_value() && entry.get_input_slew_idx() < *input_slew_floor_idx) {
      continue;
    }
    if (driven_cap_floor_idx.has_value() && entry.get_driven_cap_idx() < *driven_cap_floor_idx) {
      continue;
    }
    filtered_entries.push_back(entry);
  }
  return filtered_entries;
}

auto CollectUniqueSegmentBoundaryIndices(const std::vector<icts::SegmentChar>& entries, bool use_input_slew_idx) -> std::vector<unsigned>
{
  std::vector<unsigned> indices;
  indices.reserve(entries.size());
  for (const auto& entry : entries) {
    indices.push_back(use_input_slew_idx ? entry.get_input_slew_idx() : entry.get_driven_cap_idx());
  }

  std::ranges::sort(indices);
  indices.erase(std::ranges::unique(indices).begin(), indices.end());
  std::ranges::reverse(indices);
  return indices;
}

auto BuildFeasibleBoundaryFrontier(const std::vector<unsigned>& level_length_indices,
                                   const std::unordered_map<unsigned, std::vector<icts::SegmentChar>>& frontier_by_length,
                                   std::optional<unsigned> top_input_slew_floor_idx, std::optional<unsigned> leaf_driven_cap_floor_idx)
    -> std::vector<icts::HTreeTopologyChar>
{
  std::vector<icts::HTreeTopologyChar> current_frontier;
  unsigned next_topology_pattern_id = 0U;
  for (std::size_t reverse_level = level_length_indices.size(); reverse_level > 0U; --reverse_level) {
    const bool is_top_level = (reverse_level == 1U);
    const bool is_leaf_level = (reverse_level == level_length_indices.size());
    const auto frontier_it = frontier_by_length.find(level_length_indices.at(reverse_level - 1U));
    if (frontier_it == frontier_by_length.end()) {
      return {};
    }

    auto segment_frontier = FilterSegmentFrontierByFloor(frontier_it->second, is_top_level ? top_input_slew_floor_idx : std::nullopt,
                                                         is_leaf_level ? leaf_driven_cap_floor_idx : std::nullopt);
    if (segment_frontier.empty()) {
      return {};
    }

    auto seed_entries = realtech_support::MakeHTreeSeedEntries(segment_frontier, next_topology_pattern_id);
    if (current_frontier.empty()) {
      current_frontier = realtech_support::BuildInputBoundaryFrontier(seed_entries);
      continue;
    }

    current_frontier = realtech_support::BuildInputBoundaryFrontier(
        realtech_support::ComposeHTreeEntriesExact(seed_entries, current_frontier, next_topology_pattern_id));
    if (current_frontier.empty()) {
      return {};
    }
  }

  return current_frontier;
}

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
  ASSERT_TRUE(result.best_char.has_value());
  ASSERT_TRUE(result.best_pattern.has_value());
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

  const auto leaf_loads = CollectLeafLoads(result.topology);
  EXPECT_EQ(leaf_loads.size(), original_loads.size());
  for (auto* load : real_loads) {
    ASSERT_NE(load, nullptr);
    EXPECT_TRUE(leaf_loads.contains(load));
    EXPECT_NE(load->get_inst(), nullptr);
    EXPECT_NE(load->get_net(), nullptr);
  }

  EXPECT_TRUE(htree::WriteHTreeArtifacts(artifact_paths, "htree_builder_realtech_smoke", selected_clock->clock_name, real_loads, result));

  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  ASSERT_FALSE(cts_log_content.empty());
  const auto first_line_break = cts_log_content.find('\n');
  ASSERT_NE(first_line_break, std::string::npos);
  EXPECT_EQ(cts_log_content.find("Generate the report at "), first_line_break + 1U);
  EXPECT_NE(cts_log_content.find("CharBuilder Runtime Configuration"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CharBuilder Routing / Wire RC"), std::string::npos);
  EXPECT_NE(cts_log_content.find("HTreeBuilder Build Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("Ohm/um"), std::string::npos);
  EXPECT_NE(cts_log_content.find("pF/um"), std::string::npos);
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*power\s*\|\s*[^|\n]*W\s*\|)")));
  EXPECT_NE(cts_log_content.find("CharBuilder Sweep Progress"), std::string::npos);
  EXPECT_NE(cts_log_content.find("report.log Details"), std::string::npos);
  EXPECT_NE(cts_log_content.find("details omitted from cts.log"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("CharBuilder build Running"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("pareto_solution pattern_id="), std::string::npos);
}

TEST(HTreeBuilderRealTechSmokeTest, ForceLeafBranchBufferSelectsTerminalBranchPatterns)
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
      = char_session.prepare("htree_builder_leaf_branch_buffer", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, false, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  ASSERT_TRUE(CONFIG_INST.is_force_leaf_branch_buffer());

  auto result = icts::HTreeBuilder::build(selected_clock->loads);

  AssertLeafBranchBufferMaterialization(result);
}

TEST(HTreeBuilderRealTechSmokeTest, CallerFacingLeafBranchBufferOptionOverridesConfigDefault)
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
  if (const auto prepare_error = char_session.prepare("htree_builder_leaf_branch_buffer_option_override", std::nullopt,
                                                      kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf, false, false);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  ASSERT_FALSE(CONFIG_INST.is_force_leaf_branch_buffer());

  const auto result = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.force_leaf_branch_buffer = true});

  AssertLeafBranchBufferMaterialization(result);
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
  ASSERT_GT(baseline_result.char_slew_steps, 0U);
  ASSERT_GT(baseline_result.char_cap_steps, 0U);
  ASSERT_GT(baseline_result.char_max_slew_ns, 0.0);
  ASSERT_GT(baseline_result.char_max_cap_pf, 0.0);

  icts::CharBuilder char_builder;
  char_builder.init();
  char_builder.build();
  ASSERT_FALSE(char_builder.get_segment_chars().empty());

  std::vector<unsigned> level_length_indices;
  level_length_indices.reserve(baseline_result.levels.size());
  unsigned max_target_length_idx = 0U;
  for (const auto& level : baseline_result.levels) {
    level_length_indices.push_back(level.aligned_length_idx);
    max_target_length_idx = std::max(max_target_length_idx, level.aligned_length_idx);
  }

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(char_builder.get_segment_chars());
  unsigned next_segment_pattern_id = realtech_support::FindNextSegmentPatternId(char_builder.get_segment_chars());
  ASSERT_TRUE(realtech_support::SynthesizeSegmentFrontierIfMissing(frontier_by_length, max_target_length_idx, next_segment_pattern_id));

  const auto top_frontier_it = frontier_by_length.find(level_length_indices.front());
  const auto leaf_frontier_it = frontier_by_length.find(level_length_indices.back());
  ASSERT_NE(top_frontier_it, frontier_by_length.end());
  ASSERT_NE(leaf_frontier_it, frontier_by_length.end());
  ASSERT_FALSE(top_frontier_it->second.empty());
  ASSERT_FALSE(leaf_frontier_it->second.empty());

  const auto top_input_slew_floor_candidates = CollectUniqueSegmentBoundaryIndices(top_frontier_it->second, true);
  const auto leaf_driven_cap_floor_candidates = CollectUniqueSegmentBoundaryIndices(leaf_frontier_it->second, false);
  ASSERT_FALSE(top_input_slew_floor_candidates.empty());
  ASSERT_FALSE(leaf_driven_cap_floor_candidates.empty());

  const auto top_input_slew_floor_idx = [&]() -> std::optional<unsigned> {
    for (const unsigned candidate_idx : top_input_slew_floor_candidates) {
      if (!BuildFeasibleBoundaryFrontier(level_length_indices, frontier_by_length, candidate_idx, std::nullopt).empty()) {
        return candidate_idx;
      }
    }
    return std::nullopt;
  }();
  const auto leaf_driven_cap_floor_idx = [&]() -> std::optional<unsigned> {
    for (const unsigned candidate_idx : leaf_driven_cap_floor_candidates) {
      if (!BuildFeasibleBoundaryFrontier(level_length_indices, frontier_by_length, std::nullopt, candidate_idx).empty()) {
        return candidate_idx;
      }
    }
    return std::nullopt;
  }();

  if (!top_input_slew_floor_idx.has_value() || !leaf_driven_cap_floor_idx.has_value()) {
    GTEST_SKIP() << "Real-tech assets cannot realize caller-facing HTree boundary constraints with the current frontier.";
    return;
  }

  const unsigned top_floor_idx = top_input_slew_floor_idx.value_or(0U);
  const unsigned leaf_floor_idx = leaf_driven_cap_floor_idx.value_or(0U);
  const double top_input_slew_ns = (static_cast<double>(top_floor_idx) - 0.5) * baseline_result.char_max_slew_ns
                                   / static_cast<double>(baseline_result.char_slew_steps);
  const double leaf_driven_cap_pf
      = (static_cast<double>(leaf_floor_idx) - 0.5) * baseline_result.char_max_cap_pf / static_cast<double>(baseline_result.char_cap_steps);

  auto top_boundary_result
      = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = top_input_slew_ns});
  ASSERT_TRUE(top_boundary_result.success);
  ASSERT_TRUE(top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(top_boundary_result.min_top_input_slew_ns.value_or(0.0), top_input_slew_ns);
  ASSERT_TRUE(top_boundary_result.top_input_slew_floor_idx.has_value());
  EXPECT_EQ(top_boundary_result.top_input_slew_floor_idx.value_or(0U), top_floor_idx);
  ASSERT_FALSE(top_boundary_result.feasible_chars.empty());
  EXPECT_TRUE(std::ranges::all_of(top_boundary_result.feasible_chars, [&](const icts::HTreeTopologyChar& entry) -> bool {
    return entry.get_input_slew_idx() >= top_floor_idx;
  }));
  EXPECT_FALSE(top_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_FALSE(top_boundary_result.leaf_driven_cap_floor_idx.has_value());

  auto leaf_boundary_result
      = icts::HTreeBuilder::build(selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_leaf_driven_cap_pf = leaf_driven_cap_pf});
  ASSERT_TRUE(leaf_boundary_result.success);
  ASSERT_TRUE(leaf_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(leaf_boundary_result.min_leaf_driven_cap_pf.value_or(0.0), leaf_driven_cap_pf);
  ASSERT_TRUE(leaf_boundary_result.leaf_driven_cap_floor_idx.has_value());
  EXPECT_EQ(leaf_boundary_result.leaf_driven_cap_floor_idx.value_or(0U), leaf_floor_idx);
  ASSERT_FALSE(leaf_boundary_result.feasible_chars.empty());
  EXPECT_TRUE(std::ranges::all_of(leaf_boundary_result.feasible_chars, [&](const icts::HTreeTopologyChar& entry) -> bool {
    return entry.get_leaf_driven_cap_idx() >= leaf_floor_idx;
  }));
  EXPECT_FALSE(leaf_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_FALSE(leaf_boundary_result.top_input_slew_floor_idx.has_value());

  const double impossible_top_input_slew_ns
      = baseline_result.char_max_slew_ns + (baseline_result.char_max_slew_ns / static_cast<double>(baseline_result.char_slew_steps));
  auto impossible_top_boundary_result = icts::HTreeBuilder::build(
      selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_top_input_slew_ns = impossible_top_input_slew_ns});
  ASSERT_TRUE(impossible_top_boundary_result.success);
  ASSERT_TRUE(impossible_top_boundary_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(impossible_top_boundary_result.min_top_input_slew_ns.value_or(0.0), impossible_top_input_slew_ns);
  ASSERT_TRUE(impossible_top_boundary_result.top_input_slew_floor_idx.has_value());
  EXPECT_EQ(impossible_top_boundary_result.top_input_slew_floor_idx.value_or(0U), baseline_result.char_slew_steps + 1U);
  EXPECT_TRUE(impossible_top_boundary_result.used_boundary_fallback);
  EXPECT_FALSE(impossible_top_boundary_result.boundary_fallback_reason.empty());
  ASSERT_TRUE(impossible_top_boundary_result.boundary_fallback_score.has_value());
  EXPECT_TRUE(impossible_top_boundary_result.feasible_chars.empty());
  ASSERT_FALSE(impossible_top_boundary_result.candidate_chars.empty());
  ASSERT_TRUE(impossible_top_boundary_result.best_char.has_value());
  const auto impossible_top_best_char = impossible_top_boundary_result.best_char.value_or(icts::HTreeTopologyChar{});
  const unsigned impossible_top_floor_idx = impossible_top_boundary_result.top_input_slew_floor_idx.value_or(0U);
  ASSERT_GT(impossible_top_floor_idx, 0U);
  const auto impossible_top_max_it
      = std::ranges::max_element(impossible_top_boundary_result.candidate_chars, {}, &icts::HTreeTopologyChar::get_input_slew_idx);
  ASSERT_NE(impossible_top_max_it, impossible_top_boundary_result.candidate_chars.end());
  EXPECT_LT(impossible_top_best_char.get_input_slew_idx(), impossible_top_floor_idx);
  EXPECT_EQ(impossible_top_best_char.get_input_slew_idx(), impossible_top_max_it->get_input_slew_idx());
  EXPECT_DOUBLE_EQ(
      impossible_top_boundary_result.boundary_fallback_score.value_or(0.0),
      static_cast<double>(impossible_top_best_char.get_input_slew_idx()) / static_cast<double>(baseline_result.char_slew_steps));

  const double impossible_leaf_driven_cap_pf
      = baseline_result.char_max_cap_pf + (baseline_result.char_max_cap_pf / static_cast<double>(baseline_result.char_cap_steps));
  auto impossible_leaf_boundary_result = icts::HTreeBuilder::build(
      selected_clock->loads, icts::HTreeBuilder::BuildOptions{.min_leaf_driven_cap_pf = impossible_leaf_driven_cap_pf});
  ASSERT_TRUE(impossible_leaf_boundary_result.success);
  ASSERT_TRUE(impossible_leaf_boundary_result.min_leaf_driven_cap_pf.has_value());
  EXPECT_DOUBLE_EQ(impossible_leaf_boundary_result.min_leaf_driven_cap_pf.value_or(0.0), impossible_leaf_driven_cap_pf);
  ASSERT_TRUE(impossible_leaf_boundary_result.leaf_driven_cap_floor_idx.has_value());
  EXPECT_EQ(impossible_leaf_boundary_result.leaf_driven_cap_floor_idx.value_or(0U), baseline_result.char_cap_steps + 1U);
  EXPECT_TRUE(impossible_leaf_boundary_result.used_boundary_fallback);
  EXPECT_FALSE(impossible_leaf_boundary_result.boundary_fallback_reason.empty());
  ASSERT_TRUE(impossible_leaf_boundary_result.boundary_fallback_score.has_value());
  EXPECT_TRUE(impossible_leaf_boundary_result.feasible_chars.empty());
  ASSERT_FALSE(impossible_leaf_boundary_result.candidate_chars.empty());
  ASSERT_TRUE(impossible_leaf_boundary_result.best_char.has_value());
  const auto impossible_leaf_best_char = impossible_leaf_boundary_result.best_char.value_or(icts::HTreeTopologyChar{});
  const unsigned impossible_leaf_floor_idx = impossible_leaf_boundary_result.leaf_driven_cap_floor_idx.value_or(0U);
  ASSERT_GT(impossible_leaf_floor_idx, 0U);
  const auto impossible_leaf_max_it
      = std::ranges::max_element(impossible_leaf_boundary_result.candidate_chars, {}, &icts::HTreeTopologyChar::get_leaf_driven_cap_idx);
  ASSERT_NE(impossible_leaf_max_it, impossible_leaf_boundary_result.candidate_chars.end());
  EXPECT_LT(impossible_leaf_best_char.get_leaf_driven_cap_idx(), impossible_leaf_floor_idx);
  EXPECT_EQ(impossible_leaf_best_char.get_leaf_driven_cap_idx(), impossible_leaf_max_it->get_leaf_driven_cap_idx());
  EXPECT_DOUBLE_EQ(
      impossible_leaf_boundary_result.boundary_fallback_score.value_or(0.0),
      static_cast<double>(impossible_leaf_best_char.get_leaf_driven_cap_idx()) / static_cast<double>(baseline_result.char_cap_steps));
}

}  // namespace
}  // namespace icts_test
