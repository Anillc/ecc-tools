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
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Tree.hh"
#include "common/io/TestArtifactIO.hh"
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

TEST(HTreeSchemaWriterTest, NestedScopedLogFilesRestoreOuterDestination)
{
  const auto output_dir = common::io::PrepareCleanOutputDir(common::io::ResolveOutputDir() / "flow" / "htree" / "schema_writer_nested");
  ASSERT_FALSE(output_dir.empty());

  const auto outer_log = output_dir / "outer_cts.log";
  const auto inner_log = output_dir / "inner_cts.log";
  {
    const common::logging::ScopedLogFile outer_guard(outer_log, "Outer CTS Report");
    icts::schema::EmitKeyValueTable("Outer Before", {
                                                        {"phase", "before"},
                                                    });
    {
      const common::logging::ScopedLogFile inner_guard(inner_log, "Inner CTS Report");
      icts::schema::EmitKeyValueTable("Inner Body", {
                                                        {"phase", "inside"},
                                                    });
    }
    icts::schema::EmitKeyValueTable("Outer After", {
                                                       {"phase", "after"},
                                                   });
  }

  const auto outer_content = ReadTextFile(outer_log);
  const auto inner_content = ReadTextFile(inner_log);
  ASSERT_FALSE(outer_content.empty());
  ASSERT_FALSE(inner_content.empty());
  EXPECT_NE(outer_content.find("Outer Before"), std::string::npos);
  EXPECT_NE(outer_content.find("Outer After"), std::string::npos);
  EXPECT_EQ(outer_content.find("Inner Body"), std::string::npos);
  EXPECT_NE(inner_content.find("Inner Body"), std::string::npos);
  EXPECT_EQ(inner_content.find("Outer After"), std::string::npos);
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
  EXPECT_EQ(CONFIG_INST.get_max_pattern_nodes(), realtech_support::kRealTechCharMaxPatternNodes);
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

}  // namespace
}  // namespace icts_test
