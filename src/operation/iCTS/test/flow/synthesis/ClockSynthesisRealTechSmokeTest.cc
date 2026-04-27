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
 * @brief Real-tech smoke coverage for clustered ClockSynthesis flows.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ClockSynthesisRealTechSmokeSupport.hh"
#include "Clustering.hh"
#include "Net.hh"
#include "common/logging/ScopedLogFile.hh"
#include "common/realtech/support/RealTechSetupSupport.hh"
#include "database/config/Config.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "flow/synthesis/ClockSynthesisVisualizationSupport.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

namespace common_realtech = common::realtech;
namespace realtech_support = characterization::realtech;
namespace smoke = synthesis_realtech_smoke;

TEST(ClockSynthesisRealTechSmokeTest, ClusteredModeBuildsCentroidBuffersAndUsesUnrestrictedHtreeFrontier)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = smoke::SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), smoke::kClusteredMinLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least five sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("clock_synthesis_clustered_smoke", std::nullopt, smoke::kSynthesisSmokeMaxSlewNs,
                                                      smoke::kSynthesisSmokeMaxCapPf, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  CONFIG_INST.set_max_fanout(smoke::kSynthesisTestDefaultMaxFanout);
  CONFIG_INST.set_htree_topology_tolerance(0.1);
  ASSERT_EQ(CONFIG_INST.get_max_fanout(), smoke::kSynthesisTestDefaultMaxFanout);
  ASSERT_DOUBLE_EQ(CONFIG_INST.get_htree_topology_tolerance(), 0.1);

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

  const auto expected_cluster_master = smoke::ResolveExpectedMinClusterBufferMaster();
  if (!expected_cluster_master.has_value()) {
    FAIL() << "Expected at least one legal cluster buffer master.";
    return;
  }
  const auto& expected_cluster_master_name = *expected_cluster_master;

  icts::ClockSynthesis::BuildOptions options;
  smoke::SetEnableSinkClustering(options, true);
  icts::Net root_net(selected_clock_data.net_name + "_synthesis_root");
  smoke::ConnectRootNet(root_net, selected_clock_data.source, selected_clock_data.sinks);
  const auto result = icts::ClockSynthesis::build(root_net, options);

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
  EXPECT_EQ(result.cluster_buffers.size(), smoke::CountNonEmptyClusters(cluster_result));
  ASSERT_EQ(result.htree_result.root_net, &root_net);
  EXPECT_EQ(root_net.get_driver(), selected_clock_data.source);
  EXPECT_FALSE(root_net.get_loads().empty());

  smoke::AssertUnrestrictedFrontierHTree(result.htree_result);
  smoke::AssertNoSingleLoadExternalLeafBuffer(result.htree_result);
  smoke::AssertDepthCandidateCoverage(result.htree_result);
  smoke::AssertSelectedHTreeLoadDistribution(result.htree_result);
  EXPECT_TRUE(result.htree_result.min_top_input_slew_ns.has_value());
  EXPECT_DOUBLE_EQ(result.htree_result.min_top_input_slew_ns.value_or(0.0), smoke::kSynthesisSmokeMaxSlewNs * 0.5);

  smoke::AssertClusterBufferMastersFollowLeafSemantics(result, expected_cluster_master_name);
  const auto cluster_buffer_insts = smoke::CollectClusterBufferInsts(result);
  ASSERT_FALSE(cluster_buffer_insts.empty());
  smoke::AssertClusteredSinkConnectivity(selected_clock_data.sinks, cluster_buffer_insts);
  for (const auto& cluster_buffer : result.cluster_buffers) {
    ASSERT_NE(cluster_buffer.sink_net, nullptr);
    EXPECT_LE(cluster_buffer.sink_net->get_loads().size(), smoke::kSynthesisTestDefaultMaxFanout);
  }

  smoke::WriteAndAssertSynthesisArtifacts("clustered_mode_realtech_smoke", "clustered_mode", selected_clock_data.clock_name, artifact_paths,
                                          selected_clock_data.source, selected_clock_data.sinks, result);
  smoke::AssertClusteredArtifacts(artifact_paths);
}

TEST(ClockSynthesisRealTechSmokeTest, ClusteredModeForceBranchBufferedRealtechSmoke)
{
  const auto& setup_state = common_realtech::EnsureRealTechSetup();
  if (setup_state.mode != common_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = smoke::SelectLargestRealClock(std::numeric_limits<std::size_t>::max(), smoke::kClusteredMinLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes source plus at least five sinks.";
    return;
  }
  const auto& selected_clock_data = selected_clock.value();

  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("clock_synthesis_clustered_force_branch_buffer", std::nullopt,
                                                      smoke::kSynthesisSmokeMaxSlewNs, smoke::kSynthesisSmokeMaxCapPf, true, true);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  CONFIG_INST.set_max_fanout(smoke::kSynthesisTestDefaultMaxFanout);
  CONFIG_INST.set_htree_topology_tolerance(0.1);
  ASSERT_EQ(CONFIG_INST.get_max_fanout(), smoke::kSynthesisTestDefaultMaxFanout);
  ASSERT_DOUBLE_EQ(CONFIG_INST.get_htree_topology_tolerance(), 0.1);
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

  const auto expected_cluster_master = smoke::ResolveExpectedMinClusterBufferMaster();
  if (!expected_cluster_master.has_value()) {
    FAIL() << "Expected at least one legal cluster buffer master.";
    return;
  }

  icts::ClockSynthesis::BuildOptions options;
  smoke::SetEnableSinkClustering(options, true);
  icts::Net root_net(selected_clock_data.net_name + "_synthesis_root_force_branch_buffered");
  smoke::ConnectRootNet(root_net, selected_clock_data.source, selected_clock_data.sinks);
  const auto result = icts::ClockSynthesis::build(root_net, options);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.sink_clustering_enabled);
  EXPECT_GT(result.htree_result.char_wire_length_unit_um, 0.0);
  EXPECT_TRUE(result.htree_result.char_grid_adapted
              || result.htree_result.char_wire_length_iterations == result.htree_result.char_unique_level_bins);
  smoke::AssertBranchBufferedHTree(result.htree_result);
  smoke::AssertNoSingleLoadExternalLeafBuffer(result.htree_result);
  smoke::AssertClusterBufferMastersFollowLeafSemantics(result, *expected_cluster_master);
  const auto cluster_buffer_insts = smoke::CollectClusterBufferInsts(result);
  ASSERT_FALSE(cluster_buffer_insts.empty());
  smoke::AssertClusteredSinkConnectivity(selected_clock_data.sinks, cluster_buffer_insts);

  smoke::WriteAndAssertSynthesisArtifacts("clustered_mode_force_branch_buffered_realtech_smoke", "clustered_mode_force_branch_buffered",
                                          selected_clock_data.clock_name, artifact_paths, selected_clock_data.source,
                                          selected_clock_data.sinks, result);
  smoke::AssertClusteredArtifacts(artifact_paths);
}

}  // namespace
}  // namespace icts_test
