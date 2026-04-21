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
 * @file ClockSynthesisRealTechSmokeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Real-tech smoke coverage for clustered and non-clustered ClockSynthesis flows.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
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

#include "HTreeTopologyChar.hh"
#include "PatternId.hh"
#include "Point.hh"
#include "Tree.hh"
#include "clustering/Clustering.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/io/Wrapper.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "flow/synthesis/ClockSynthesisVisualizationSupport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;

constexpr std::size_t kClusteredMinLoadCount = 5U;
constexpr double kSynthesisSmokeMaxSlewNs = 0.05;
constexpr double kSynthesisSmokeMaxCapPf = 0.15;
constexpr unsigned kSynthesisTestDefaultMaxFanout = 32U;
constexpr double kArm9SynthesisRuntimeBudgetS = 600.0;
constexpr std::string_view kArm9ClockSynthesisScenario = "clock_synthesis_arm9_full_sink_matrix";
constexpr std::array<unsigned, 4> kArm9ExperimentIterations = {2U, 3U, 4U, 5U};
constexpr std::array<unsigned, 2> kArm9ExperimentSteps = {10U, 15U};

struct RealClockSelection
{
  std::string clock_name;
  std::string net_name;
  icts::Pin* source = nullptr;
  std::vector<icts::Pin*> sinks;
};

struct ClockSynthesisExperimentRecord
{
  unsigned wire_length_iterations = 0U;
  unsigned slew_cap_steps = 0U;
  double runtime_s = 0.0;
  bool success = false;
  std::size_t sink_count = 0U;
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

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return buffer.str();
}

auto FormatClockSynthesisExperimentReport(std::string_view scenario_name, const RealClockSelection& selection, bool omit_wire_length_unit,
                                          const std::vector<ClockSynthesisExperimentRecord>& records) -> std::string
{
  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(6);
  report_stream << "scenario=" << scenario_name << "\n";
  report_stream << "clock_name=" << selection.clock_name << "\n";
  report_stream << "clock_net=" << selection.net_name << "\n";
  report_stream << "sink_count=" << selection.sinks.size() << "\n";
  report_stream << "sink_clustering_enabled=false\n";
  report_stream << "omit_wire_length_unit=" << (omit_wire_length_unit ? "true" : "false") << "\n";
  report_stream << "runtime_budget_s=" << kArm9SynthesisRuntimeBudgetS << "\n";
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

auto WriteClockSynthesisMatrixReport(std::string_view scenario_name, const std::string& file_name, const std::string& content) -> bool
{
  const auto output_dir = common::io::PrepareCleanOutputDir(common::io::ResolveOutputDir() / "flow" / "synthesis"
                                                            / common::io::SanitizeOutputName(std::string(scenario_name)));
  if (output_dir.empty()) {
    return false;
  }
  return common::io::WriteTextLog(output_dir / file_name, content);
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

auto SelectLargestRealClock(std::size_t max_count, std::size_t min_required_load_count) -> std::optional<RealClockSelection>
{
  DESIGN_INST.reset();
  STA_ADAPTER_INST.updateTiming();

  for (const auto& [clock_name, net_name] : STA_ADAPTER_INST.collectClockNetPairs()) {
    auto clock = std::make_unique<icts::Clock>(clock_name, net_name);
    DESIGN_INST.add_clock(std::move(clock));
  }

  WRAPPER_INST.read();

  RealClockSelection best_selection;
  std::size_t best_source_load_count = 0U;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr || clock->get_clock_source() == nullptr || clock->get_loads().size() < min_required_load_count) {
      continue;
    }
    if (clock->get_loads().size() <= best_source_load_count) {
      continue;
    }

    best_selection.clock_name = clock->get_clock_name();
    best_selection.net_name = clock->get_clock_net_name();
    best_selection.source = clock->get_clock_source();
    best_selection.sinks = SampleLoadsForSmoke(clock->get_loads(), max_count);
    best_source_load_count = clock->get_loads().size();
  }

  if (best_selection.source == nullptr || best_selection.sinks.size() < min_required_load_count) {
    return std::nullopt;
  }
  return best_selection;
}

auto SetEnableSinkClustering(icts::ClockSynthesis::BuildOptions& options, bool enabled) -> void
{
  options.enable_sink_clustering = enabled;
}

auto ResolveBufferDriveCap(const std::string& cell_master) -> double
{
  double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (drive_cap_pf <= 0.0) {
    drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return drive_cap_pf;
}

auto ResolveExpectedMinClusterBufferMaster() -> std::optional<std::string>
{
  std::optional<std::string> expected_master = std::nullopt;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (cell_master.empty()) {
      continue;
    }

    const auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (input_pin.empty() || output_pin.empty()) {
      continue;
    }

    const double drive_cap_pf = ResolveBufferDriveCap(cell_master);
    if (drive_cap_pf <= 0.0) {
      continue;
    }

    if (!expected_master.has_value() || drive_cap_pf < best_drive_cap_pf
        || (drive_cap_pf == best_drive_cap_pf && cell_master < *expected_master)) {
      expected_master = cell_master;
      best_drive_cap_pf = drive_cap_pf;
    }
  }

  return expected_master;
}

auto CountNonEmptyClusters(const icts::ClusterResult& cluster_result) -> std::size_t
{
  return static_cast<std::size_t>(std::count_if(cluster_result.clusters.begin(), cluster_result.clusters.end(),
                                                [](const std::vector<icts::Pin*>& cluster) -> bool { return !cluster.empty(); }));
}

auto CollectClusterBufferInsts(const icts::ClockSynthesis::BuildResult& result) -> std::unordered_set<icts::Inst*>
{
  std::unordered_set<icts::Inst*> cluster_buffer_insts;
  cluster_buffer_insts.reserve(result.cluster_buffers.size());
  for (const auto& cluster_buffer : result.cluster_buffers) {
    if (cluster_buffer.inst != nullptr) {
      cluster_buffer_insts.insert(cluster_buffer.inst);
    }
  }
  return cluster_buffer_insts;
}

auto AssertClusteredSinkConnectivity(const std::vector<icts::Pin*>& sinks, const std::unordered_set<icts::Inst*>& cluster_buffer_insts)
    -> void
{
  ASSERT_FALSE(sinks.empty());
  ASSERT_FALSE(cluster_buffer_insts.empty());

  for (auto* sink : sinks) {
    ASSERT_NE(sink, nullptr);
    ASSERT_NE(sink->get_net(), nullptr);
    auto* driver = sink->get_net()->get_driver();
    ASSERT_NE(driver, nullptr);
    ASSERT_NE(driver->get_inst(), nullptr);
    EXPECT_TRUE(driver->get_inst()->is_buffer());
    EXPECT_TRUE(cluster_buffer_insts.contains(driver->get_inst()));
  }
}

auto AssertNoSingleLoadExternalLeafBuffer(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  const std::unordered_set<const icts::Inst*> inserted_insts(htree_result.inserted_insts.begin(), htree_result.inserted_insts.end());
  for (const auto* inst : htree_result.inserted_insts) {
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

auto FindLeafLevelPlan(const icts::HTreeBuilder::BuildResult& htree_result) -> const icts::HTreeBuilder::LevelPlan*
{
  const auto level_it
      = std::ranges::find_if(htree_result.levels, [](const icts::HTreeBuilder::LevelPlan& level) -> bool { return level.is_leaf_level; });
  if (level_it == htree_result.levels.end()) {
    return nullptr;
  }
  return &(*level_it);
}

auto AssertClusterBufferMastersFollowLeafSemantics(const icts::ClockSynthesis::BuildResult& result, const std::string& min_cluster_master)
    -> void
{
  if (!result.cluster_result.has_value()) {
    FAIL() << "Expected clustered synthesis result to include cluster metadata.";
    return;
  }
  const auto& cluster_result = *result.cluster_result;
  EXPECT_EQ(result.cluster_buffers.size(), CountNonEmptyClusters(cluster_result));
  ASSERT_FALSE(result.cluster_buffers.empty());

  for (const auto& cluster_buffer : result.cluster_buffers) {
    ASSERT_LT(cluster_buffer.cluster_index, cluster_result.clusters.size());
    const auto& cluster_sinks = cluster_result.clusters.at(cluster_buffer.cluster_index);
    ASSERT_FALSE(cluster_sinks.empty());
    ASSERT_NE(cluster_buffer.inst, nullptr);
    ASSERT_NE(cluster_buffer.input_pin, nullptr);
    ASSERT_NE(cluster_buffer.output_pin, nullptr);
    ASSERT_NE(cluster_buffer.sink_net, nullptr);
    ASSERT_EQ(cluster_buffer.sink_net->get_driver(), cluster_buffer.output_pin);
    EXPECT_EQ(cluster_buffer.sink_count, cluster_sinks.size());
    EXPECT_EQ(cluster_buffer.sink_net->get_loads().size(), cluster_sinks.size());
    EXPECT_EQ(cluster_buffer.output_pin->get_net(), cluster_buffer.sink_net);
    EXPECT_EQ(cluster_buffer.inst->get_location().get_x(), cluster_buffer.location.get_x());
    EXPECT_EQ(cluster_buffer.inst->get_location().get_y(), cluster_buffer.location.get_y());
    EXPECT_EQ(cluster_buffer.inst->get_cell_master(), min_cluster_master);

    auto* leaf_net = cluster_buffer.input_pin->get_net();
    ASSERT_NE(leaf_net, nullptr);
    EXPECT_NE(leaf_net, cluster_buffer.sink_net);
    auto* leaf_driver = leaf_net->get_driver();
    ASSERT_NE(leaf_driver, nullptr);
    EXPECT_NE(leaf_driver, cluster_buffer.output_pin);

    for (auto* sink : cluster_sinks) {
      ASSERT_NE(sink, nullptr);
      EXPECT_EQ(sink->get_net(), cluster_buffer.sink_net);
    }
  }
}

auto AssertUnrestrictedFrontierHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  ASSERT_TRUE(htree_result.success);
  EXPECT_FALSE(htree_result.force_branch_buffer);
  EXPECT_FALSE(htree_result.force_leaf_unbuffered);
  ASSERT_FALSE(htree_result.levels.empty());
}

auto AssertBranchBufferedHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  ASSERT_TRUE(htree_result.success);
  EXPECT_TRUE(htree_result.force_branch_buffer);
  EXPECT_FALSE(htree_result.force_leaf_unbuffered);
  ASSERT_FALSE(htree_result.levels.empty());

  const auto* leaf_level = FindLeafLevelPlan(htree_result);
  ASSERT_NE(leaf_level, nullptr);
  EXPECT_TRUE(leaf_level->selected_has_terminal_branch_buffer);
  EXPECT_FALSE(leaf_level->selected_terminal_cell_master.empty());
}

auto AssertLeafUnbufferedHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void
{
  ASSERT_TRUE(htree_result.success);
  EXPECT_TRUE(htree_result.force_leaf_unbuffered);
  EXPECT_FALSE(htree_result.force_branch_buffer);
  ASSERT_FALSE(htree_result.levels.empty());

  bool exercised_leaf = false;
  for (const auto& level : htree_result.levels) {
    if (!level.is_leaf_level) {
      continue;
    }
    exercised_leaf = true;
    EXPECT_FALSE(level.selected_has_terminal_branch_buffer);
  }
  EXPECT_TRUE(exercised_leaf);
}

auto WriteAndAssertSynthesisArtifacts(const std::string& case_name, const std::string& scenario_name, const std::string& clock_name,
                                      const synthesis::ClockSynthesisArtifactPaths& artifact_paths, icts::Pin* source,
                                      const std::vector<icts::Pin*>& sinks, const icts::ClockSynthesis::BuildResult& result)
    -> synthesis::ClockSynthesisArtifactPaths
{
  if (artifact_paths.output_dir.empty()) {
    ADD_FAILURE() << "Failed to prepare synthesis artifact output dir for case " << case_name;
    return artifact_paths;
  }
  EXPECT_TRUE(synthesis::WriteClockSynthesisArtifacts(artifact_paths, scenario_name, clock_name, source, sinks, result));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.cts_log));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.synthesis_svg));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.report_log));
  return artifact_paths;
}

auto AssertClusteredArtifacts(const synthesis::ClockSynthesisArtifactPaths& artifact_paths) -> void
{
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  ASSERT_FALSE(cts_log_content.empty());
  EXPECT_NE(cts_log_content.find("Cluster Center vs H-Tree Leaf Distance Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("Cluster Center vs H-Tree Leaf Distance Details"), std::string::npos);
  EXPECT_NE(cts_log_content.find("mean_distance_dbu"), std::string::npos);
  EXPECT_NE(cts_log_content.find("median_distance_dbu"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_group_count"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_min"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_max"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_mean"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_median"), std::string::npos);

  const auto svg_content = ReadTextFile(artifact_paths.synthesis_svg);
  ASSERT_FALSE(svg_content.empty());
  EXPECT_EQ(svg_content.find("cts_clock_source_to_htree_root"), std::string::npos);
  EXPECT_NE(svg_content.find("sink-level net"), std::string::npos);
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#2ca25f"[^>]*><title>net cts_htree_net_)")));
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#0f766e"[^>]*><title>sink-level net )")));
}

auto AssertNonClusteredArtifacts(const synthesis::ClockSynthesisArtifactPaths& artifact_paths) -> void
{
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  ASSERT_FALSE(cts_log_content.empty());
  EXPECT_EQ(cts_log_content.find("Cluster Center vs H-Tree Leaf Distance Summary"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Cluster Center vs H-Tree Leaf Distance Details"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_group_count"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_min"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_max"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_mean"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_load_cap_median"), std::string::npos);

  const auto svg_content = ReadTextFile(artifact_paths.synthesis_svg);
  ASSERT_FALSE(svg_content.empty());
  EXPECT_EQ(svg_content.find("cts_clock_source_to_htree_root"), std::string::npos);
  EXPECT_EQ(svg_content.find("sink-level net"), std::string::npos);
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#2ca25f"[^>]*><title>net cts_htree_net_)")));
}

auto CalcFloorPowerOfTwo(std::size_t value) -> std::size_t
{
  if (value == 0U) {
    return 0U;
  }

  std::size_t power = 1U;
  while ((power << 1U) <= value) {
    power <<= 1U;
  }
  return power;
}

auto CountTopologyLeafNodes(const icts::Tree& topology) -> std::size_t
{
  const auto levels = topology.levels();
  if (levels.empty()) {
    return 0U;
  }
  return levels.back().size();
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

TEST(ClockSynthesisRealTechSmokeTest, ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontier)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), kClusteredMinLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least five sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error
      = char_session.prepare("clock_synthesis_clustered_smoke", std::nullopt, kSynthesisSmokeMaxSlewNs, kSynthesisSmokeMaxCapPf, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  CONFIG_INST.set_max_fanout(kSynthesisTestDefaultMaxFanout);
  ASSERT_EQ(CONFIG_INST.get_max_fanout(), kSynthesisTestDefaultMaxFanout);

  const auto artifact_paths = synthesis::PrepareClockSynthesisArtifactPaths("clustered_mode_realtech_smoke");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "Clock Synthesis Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("Clock Synthesis Smoke Scenario",
                                       {
                                           {"scenario", "clustered_mode"},
                                           {"clock_name", selected_clock_data.clock_name},
                                           {"clock_net", selected_clock_data.net_name},
                                           {"load_count", std::to_string(selected_clock_data.sinks.size())},
                                           {"enable_sink_clustering", "true"},
                                           {"omit_wire_length_unit", "true"},
                                       });

  const auto expected_cluster_master = ResolveExpectedMinClusterBufferMaster();
  if (!expected_cluster_master.has_value()) {
    FAIL() << "Expected at least one legal cluster buffer master.";
    return;
  }
  const auto& expected_cluster_master_name = *expected_cluster_master;

  icts::Clock synthesized_clock(selected_clock_data.clock_name, selected_clock_data.net_name, selected_clock_data.source,
                                selected_clock_data.sinks);
  icts::ClockSynthesis::BuildOptions options;
  SetEnableSinkClustering(options, true);
  const auto result = icts::ClockSynthesis::build(synthesized_clock, options);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.sink_clustering_enabled);
  EXPECT_GT(result.htree_result.char_wire_length_unit_um, 0.0);
  EXPECT_TRUE(result.htree_result.char_grid_adapted
              || result.htree_result.char_wire_length_iterations == result.htree_result.char_unique_level_bins);
  if (!result.cluster_result.has_value()) {
    FAIL() << "Expected clustered synthesis result to include cluster metadata.";
    return;
  }
  const auto& cluster_result = *result.cluster_result;
  EXPECT_EQ(result.cluster_buffers.size(), CountNonEmptyClusters(cluster_result));
  EXPECT_EQ(synthesized_clock.get_inserted_insts().size(), result.inserted_insts.size());
  EXPECT_EQ(synthesized_clock.get_inserted_nets().size(), result.inserted_nets.size());

  auto* source_to_root_net = result.source_to_root_net;
  ASSERT_NE(source_to_root_net, nullptr);
  EXPECT_EQ(source_to_root_net->get_driver(), selected_clock_data.source);
  ASSERT_EQ(source_to_root_net->get_loads().size(), 1U);
  EXPECT_EQ(source_to_root_net->get_loads().front(), result.htree_result.root_input_pin);

  AssertUnrestrictedFrontierHTree(result.htree_result);
  AssertNoSingleLoadExternalLeafBuffer(result.htree_result);
  AssertDepthCandidateCoverage(result.htree_result);
  AssertSelectedLeafCapDistribution(result.htree_result);
  AssertSelectedHTreeLoadDistribution(result.htree_result);
  EXPECT_TRUE(result.htree_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(result.htree_result.min_top_input_slew_ns.value_or(0.0), kSynthesisSmokeMaxSlewNs * 0.5);
  EXPECT_TRUE(result.htree_result.min_leaf_driven_cap_pf.has_value());
  const auto* clustered_depth_summary = FindSelectedDepthSummary(result.htree_result);
  ASSERT_NE(clustered_depth_summary, nullptr);
  EXPECT_DOUBLE_EQ(clustered_depth_summary->requested_leaf_driven_cap_pf, result.htree_result.min_leaf_driven_cap_pf.value_or(0.0));
  EXPECT_FALSE(clustered_depth_summary->leaf_driven_cap_source.empty());

  AssertClusterBufferMastersFollowLeafSemantics(result, expected_cluster_master_name);
  const auto cluster_buffer_insts = CollectClusterBufferInsts(result);
  ASSERT_FALSE(cluster_buffer_insts.empty());
  AssertClusteredSinkConnectivity(selected_clock_data.sinks, cluster_buffer_insts);
  for (const auto& cluster_buffer : result.cluster_buffers) {
    ASSERT_NE(cluster_buffer.sink_net, nullptr);
    EXPECT_LE(cluster_buffer.sink_net->get_loads().size(), kSynthesisTestDefaultMaxFanout);
  }

  WriteAndAssertSynthesisArtifacts("clustered_mode_realtech_smoke", "clustered_mode", selected_clock_data.clock_name, artifact_paths,
                                   selected_clock_data.source, selected_clock_data.sinks, result);
  AssertClusteredArtifacts(artifact_paths);
}

TEST(ClockSynthesisRealTechSmokeTest, ClusteredModeForceBranchBufferedRealtechSmoke)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), kClusteredMinLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least five sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("clock_synthesis_clustered_force_branch_buffer", std::nullopt,
                                                      kSynthesisSmokeMaxSlewNs, kSynthesisSmokeMaxCapPf, true, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  CONFIG_INST.set_max_fanout(kSynthesisTestDefaultMaxFanout);
  ASSERT_EQ(CONFIG_INST.get_max_fanout(), kSynthesisTestDefaultMaxFanout);
  ASSERT_TRUE(CONFIG_INST.is_force_branch_buffer());

  const auto artifact_paths = synthesis::PrepareClockSynthesisArtifactPaths("clustered_mode_force_branch_buffered_realtech_smoke");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "Clock Synthesis Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("Clock Synthesis Smoke Scenario",
                                       {
                                           {"scenario", "clustered_mode_force_branch_buffered"},
                                           {"clock_name", selected_clock_data.clock_name},
                                           {"clock_net", selected_clock_data.net_name},
                                           {"load_count", std::to_string(selected_clock_data.sinks.size())},
                                           {"enable_sink_clustering", "true"},
                                           {"omit_wire_length_unit", "true"},
                                           {"force_branch_buffer", "true"},
                                       });

  const auto expected_cluster_master = ResolveExpectedMinClusterBufferMaster();
  if (!expected_cluster_master.has_value()) {
    FAIL() << "Expected at least one legal cluster buffer master.";
    return;
  }

  icts::Clock synthesized_clock(selected_clock_data.clock_name, selected_clock_data.net_name, selected_clock_data.source,
                                selected_clock_data.sinks);
  icts::ClockSynthesis::BuildOptions options;
  SetEnableSinkClustering(options, true);
  const auto result = icts::ClockSynthesis::build(synthesized_clock, options);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.sink_clustering_enabled);
  EXPECT_GT(result.htree_result.char_wire_length_unit_um, 0.0);
  EXPECT_TRUE(result.htree_result.char_grid_adapted
              || result.htree_result.char_wire_length_iterations == result.htree_result.char_unique_level_bins);
  AssertBranchBufferedHTree(result.htree_result);
  AssertNoSingleLoadExternalLeafBuffer(result.htree_result);
  AssertClusterBufferMastersFollowLeafSemantics(result, *expected_cluster_master);
  const auto cluster_buffer_insts = CollectClusterBufferInsts(result);
  ASSERT_FALSE(cluster_buffer_insts.empty());
  AssertClusteredSinkConnectivity(selected_clock_data.sinks, cluster_buffer_insts);

  WriteAndAssertSynthesisArtifacts("clustered_mode_force_branch_buffered_realtech_smoke", "clustered_mode_force_branch_buffered",
                                   selected_clock_data.clock_name, artifact_paths, selected_clock_data.source, selected_clock_data.sinks,
                                   result);
  AssertClusteredArtifacts(artifact_paths);
}

TEST(ClockSynthesisRealTechSmokeTest, NonClusteredModeSkipsClusterBuffersAndUsesLeafUnbufferedHTree)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), 2U);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least two sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error
      = char_session.prepare("clock_synthesis_non_clustered_smoke", std::nullopt, kSynthesisSmokeMaxSlewNs, kSynthesisSmokeMaxCapPf, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  const auto artifact_paths = synthesis::PrepareClockSynthesisArtifactPaths("non_clustered_mode_realtech_smoke");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  const common::logging::ScopedLogFile cts_log_guard(artifact_paths.cts_log, "Clock Synthesis Test Report");
  SCHEMA_WRITER_INST.emitKeyValueTable("Clock Synthesis Smoke Scenario",
                                       {
                                           {"scenario", "non_clustered_mode"},
                                           {"clock_name", selected_clock_data.clock_name},
                                           {"clock_net", selected_clock_data.net_name},
                                           {"load_count", std::to_string(selected_clock_data.sinks.size())},
                                           {"enable_sink_clustering", "false"},
                                           {"omit_wire_length_unit", "true"},
                                       });

  icts::ClockSynthesis::BuildOptions options;
  SetEnableSinkClustering(options, false);
  const auto result = icts::ClockSynthesis::build(selected_clock_data.source, selected_clock_data.sinks, options);

  ASSERT_TRUE(result.success);
  EXPECT_FALSE(result.sink_clustering_enabled);
  EXPECT_FALSE(result.cluster_result.has_value());
  EXPECT_GT(result.htree_result.char_wire_length_unit_um, 0.0);
  EXPECT_TRUE(result.htree_result.char_grid_adapted
              || result.htree_result.char_wire_length_iterations == result.htree_result.char_unique_level_bins);

  auto* source_to_root_net = result.source_to_root_net;
  ASSERT_NE(source_to_root_net, nullptr);
  EXPECT_EQ(source_to_root_net->get_driver(), selected_clock_data.source);
  ASSERT_EQ(source_to_root_net->get_loads().size(), 1U);
  EXPECT_EQ(source_to_root_net->get_loads().front(), result.htree_result.root_input_pin);

  AssertLeafUnbufferedHTree(result.htree_result);
  AssertNoSingleLoadExternalLeafBuffer(result.htree_result);
  AssertDepthCandidateCoverage(result.htree_result);
  AssertSelectedLeafCapDistribution(result.htree_result);
  AssertSelectedHTreeLoadDistribution(result.htree_result);
  EXPECT_TRUE(result.htree_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(result.htree_result.min_top_input_slew_ns.value_or(0.0), kSynthesisSmokeMaxSlewNs * 0.5);
  EXPECT_TRUE(result.htree_result.min_leaf_driven_cap_pf.has_value());
  const auto* non_clustered_depth_summary = FindSelectedDepthSummary(result.htree_result);
  ASSERT_NE(non_clustered_depth_summary, nullptr);
  EXPECT_DOUBLE_EQ(non_clustered_depth_summary->requested_leaf_driven_cap_pf, result.htree_result.min_leaf_driven_cap_pf.value_or(0.0));
  EXPECT_FALSE(non_clustered_depth_summary->leaf_driven_cap_source.empty());
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_EQ(CountTopologyLeafNodes(result.htree_result.topology), CalcFloorPowerOfTwo(selected_clock_data.sinks.size()));

  WriteAndAssertSynthesisArtifacts("non_clustered_mode_realtech_smoke", "non_clustered_mode", selected_clock_data.clock_name,
                                   artifact_paths, selected_clock_data.source, selected_clock_data.sinks, result);
  AssertNonClusteredArtifacts(artifact_paths);
}

TEST(ClockSynthesisRealTechSmokeTest, Arm9FullSinkNonClusteredExperimentMatrix)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), 2U);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least two sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  std::vector<ClockSynthesisExperimentRecord> records;
  records.reserve(kArm9ExperimentIterations.size() * kArm9ExperimentSteps.size());

  for (const unsigned wire_length_iterations : kArm9ExperimentIterations) {
    for (const unsigned slew_cap_steps : kArm9ExperimentSteps) {
      std::ostringstream scenario_name_stream;
      scenario_name_stream << "clock_synthesis_arm9_full_sink_iter" << wire_length_iterations << "_step" << slew_cap_steps;
      const std::string scenario_name = scenario_name_stream.str();

      realtech_support::RealTechCharSession char_session;
      if (const auto prepare_error
          = char_session.prepare(scenario_name, std::nullopt, kSynthesisSmokeMaxSlewNs, kSynthesisSmokeMaxCapPf, true);
          prepare_error.has_value()) {
        GTEST_SKIP() << *prepare_error;
        return;
      }

      CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
      CONFIG_INST.set_slew_steps(slew_cap_steps);
      CONFIG_INST.set_cap_steps(slew_cap_steps);

      icts::ClockSynthesis::BuildOptions options;
      SetEnableSinkClustering(options, false);

      const auto runtime_start = std::chrono::steady_clock::now();
      const auto result = icts::ClockSynthesis::build(selected_clock_data.source, selected_clock_data.sinks, options);
      const auto runtime_end = std::chrono::steady_clock::now();
      const double runtime_s = std::chrono::duration<double>(runtime_end - runtime_start).count();

      ClockSynthesisExperimentRecord record{
          .wire_length_iterations = wire_length_iterations,
          .slew_cap_steps = slew_cap_steps,
          .runtime_s = runtime_s,
          .success = result.success,
          .sink_count = selected_clock_data.sinks.size(),
          .char_wire_length_unit_um = result.htree_result.char_wire_length_unit_um,
          .char_wire_length_iterations = result.htree_result.char_wire_length_iterations,
          .char_grid_adapted = result.htree_result.char_grid_adapted,
          .used_boundary_fallback = result.htree_result.used_boundary_fallback,
          .failure_reason = result.htree_result.failure_reason,
      };

      const auto* selected_summary = FindSelectedDepthSummary(result.htree_result);
      if (selected_summary != nullptr) {
        record.final_frontier_count = selected_summary->final_frontier_count;
      }
      if (result.htree_result.selected_depth.has_value()) {
        record.selected_depth = *result.htree_result.selected_depth;
      }
      if (result.htree_result.best_char.has_value()) {
        record.best_pattern_id = result.htree_result.best_char->get_pattern_id().local_id;
        record.best_delay_ns = result.htree_result.best_char->get_delay();
        record.best_power_w = result.htree_result.best_char->get_power();
      }
      records.push_back(record);

      SCOPED_TRACE("iter=" + std::to_string(wire_length_iterations) + ", step=" + std::to_string(slew_cap_steps));
      EXPECT_TRUE(result.success) << "failure_reason=" << result.htree_result.failure_reason;
      EXPECT_FALSE(result.sink_clustering_enabled);
      EXPECT_TRUE(result.cluster_buffers.empty());
      EXPECT_LE(runtime_s, kArm9SynthesisRuntimeBudgetS);
      EXPECT_GT(record.final_frontier_count, 0U);
      EXPECT_TRUE(result.htree_result.best_char.has_value());
      EXPECT_GT(record.char_wire_length_unit_um, 0.0);
      EXPECT_LE(record.char_wire_length_iterations, wire_length_iterations);
    }
  }

  EXPECT_TRUE(WriteClockSynthesisMatrixReport(
      kArm9ClockSynthesisScenario, "matrix_report.txt",
      FormatClockSynthesisExperimentReport(kArm9ClockSynthesisScenario, selected_clock_data, true, records)));
}

}  // namespace
}  // namespace icts_test
