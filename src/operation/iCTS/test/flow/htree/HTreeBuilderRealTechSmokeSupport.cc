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
 * @file HTreeBuilderRealTechSmokeSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Compiled helpers for real-tech H-tree smoke tests.
 */

#include "flow/htree/HTreeBuilderRealTechSmokeSupport.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

#include "Clock.hh"
#include "Inst.hh"
#include "Net.hh"
#include "Tree.hh"
#include "flow/htree/HTreeBuildObservation.hh"
#include "flow/htree/HTreeVisualizationSupport.hh"
#include "htree/HTreeBuilder.hh"
#if defined(ICTS_ENABLE_SLOW_REALTECH_REGRESSION) && ICTS_ENABLE_SLOW_REALTECH_REGRESSION
#include "Point.hh"
#endif
#include "database/config/Config.hh"
#include "database/design/Design.hh"
#include "database/io/Wrapper.hh"

namespace icts_test {

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

auto ConnectRootNetForHTreeTest(icts::Net& root_net, icts::Pin& root_driver, const std::vector<icts::Pin*>& loads) -> void
{
  root_net.set_driver(&root_driver);
  root_driver.set_net(&root_net);
  root_net.set_loads(loads);
  for (auto* load : loads) {
    if (load != nullptr) {
      load->set_net(&root_net);
    }
  }
}

auto SelectLargestRealClockLoads(std::size_t max_count) -> std::optional<RealClockLoadSelection>
{
  DESIGN_INST.reset();

  for (const auto& [clock_name, net_name] : WRAPPER_INST.collectClockNetPairs()) {
    auto* clock = DESIGN_INST.makeClock(clock_name, net_name);
    if (clock != nullptr) {
      clock->set_clock_name(clock_name);
      clock->set_clock_net_name(net_name);
    }
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
  std::unordered_set<const icts::Inst*> inserted_insts;
  inserted_insts.reserve(result.inserted_insts.size());
  for (const auto& inst_owner : result.inserted_insts) {
    inserted_insts.insert(inst_owner.get());
  }

  for (const auto& inst_owner : result.inserted_insts) {
    const auto* inst = inst_owner.get();
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

auto AssertDepthCandidateCoverage(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto observation = htree::ObserveHTreeBuild(result);
  ASSERT_GT(observation.depth_candidate_count, 0U);
  ASSERT_TRUE(observation.has_selected_depth);

  const auto topology_levels = result.topology.levels();
  ASSERT_GT(topology_levels.size(), 1U);
  const auto max_depth = static_cast<unsigned>(topology_levels.size() - 1U);
  EXPECT_EQ(observation.depth_candidate_count, std::min<std::size_t>(CONFIG_INST.get_htree_depth_explore_window(), max_depth));

  EXPECT_EQ(observation.selected_depth, result.selected_depth.value_or(0U));
  EXPECT_EQ(observation.selected_depth, observation.selected_level_count);
  EXPECT_TRUE(observation.success);
  EXPECT_GT(observation.selected_final_frontier_count, 0U);
}

auto AssertSelectedHTreeLoadDistribution(const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto observation = htree::ObserveHTreeBuild(result);
  ASSERT_GT(observation.htree_load_group_count, 0U);
  EXPECT_LE(observation.htree_load_cap_min_pf, observation.htree_load_cap_mean_pf);
  EXPECT_LE(observation.htree_load_cap_min_pf, observation.htree_load_cap_median_pf);
  EXPECT_LE(observation.htree_load_cap_mean_pf, observation.htree_load_cap_max_pf);
  EXPECT_LE(observation.htree_load_cap_median_pf, observation.htree_load_cap_max_pf);
}

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
    const bool has_terminal_inst = std::ranges::any_of(result.inserted_insts, [&expected_position](const auto& inst_owner) -> bool {
      const auto* inst = inst_owner.get();
      return inst != nullptr && inst->get_location() == expected_position;
    });
    EXPECT_TRUE(has_terminal_inst);
  }
}

#endif

}  // namespace icts_test
