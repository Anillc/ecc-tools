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
 * @file ClockSynthesisRealTechSmokeSupport.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Shared helpers for real-tech clock synthesis smoke scenarios.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "database/design/Inst.hh"
#include "database/design/Pin.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "flow/synthesis/ClockSynthesisVisualizationSupport.hh"
#include "spatial/Tree.hh"

namespace icts_test::synthesis_realtech_smoke {

inline constexpr std::size_t kClusteredMinLoadCount = 5U;
inline constexpr double kSynthesisSmokeMaxSlewNs = 0.05;
inline constexpr double kSynthesisSmokeMaxCapPf = 0.15;
inline constexpr unsigned kSynthesisTestDefaultMaxFanout = 32U;
inline constexpr double kArm9SynthesisRuntimeBudgetS = 600.0;
inline constexpr std::string_view kArm9ClockSynthesisScenario = "clock_synthesis_arm9_full_sink_matrix";
inline constexpr std::array<unsigned, 4> kArm9ExperimentIterations = {2U, 3U, 4U, 5U};
inline constexpr std::array<unsigned, 2> kArm9ExperimentSteps = {10U, 15U};

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

struct ClockSynthesisMatrixRunResult
{
  bool skipped = false;
  std::string skip_reason;
  RealClockSelection selection;
  std::vector<ClockSynthesisExperimentRecord> records;
  std::vector<std::string> failure_messages;
  bool report_written = false;
};

struct ClockSynthesisValidationResult
{
  std::vector<std::string> failure_messages;
};

auto FormatClockSynthesisExperimentReport(std::string_view scenario_name, const RealClockSelection& selection, bool omit_wire_length_unit,
                                          const std::vector<ClockSynthesisExperimentRecord>& records) -> std::string;
auto WriteClockSynthesisMatrixReport(std::string_view scenario_name, const std::string& file_name, const std::string& content) -> bool;
auto SelectLargestRealClock(std::size_t max_count, std::size_t min_required_load_count) -> std::optional<RealClockSelection>;
auto SetEnableSinkClustering(icts::ClockSynthesis::BuildOptions& options, bool enabled) -> void;
auto ResolveExpectedMinClusterBufferMaster() -> std::optional<std::string>;
auto CountNonEmptyClusters(const icts::ClusterResult& cluster_result) -> std::size_t;
auto CollectClusterBufferInsts(const icts::ClockSynthesis::BuildResult& result) -> std::unordered_set<icts::Inst*>;
auto ValidateClusteredSinkConnectivity(const std::vector<icts::Pin*>& sinks, const std::unordered_set<icts::Inst*>& cluster_buffer_insts)
    -> ClockSynthesisValidationResult;
auto ValidateClusterBufferMastersFollowLeafSemantics(const icts::ClockSynthesis::BuildResult& result, const std::string& min_cluster_master)
    -> ClockSynthesisValidationResult;
auto AssertClusteredSinkConnectivity(const std::vector<icts::Pin*>& sinks, const std::unordered_set<icts::Inst*>& cluster_buffer_insts)
    -> void;
auto AssertNoSingleLoadExternalLeafBuffer(const icts::HTreeBuilder::BuildResult& htree_result) -> void;
auto AssertClusterBufferMastersFollowLeafSemantics(const icts::ClockSynthesis::BuildResult& result, const std::string& min_cluster_master)
    -> void;
auto AssertUnrestrictedFrontierHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void;
auto AssertBranchBufferedHTree(const icts::HTreeBuilder::BuildResult& htree_result) -> void;
auto WriteAndAssertSynthesisArtifacts(const std::string& case_name, const std::string& scenario_name, const std::string& clock_name,
                                      const synthesis::ClockSynthesisArtifactPaths& artifact_paths, icts::Pin* source,
                                      const std::vector<icts::Pin*>& sinks, const icts::ClockSynthesis::BuildResult& result)
    -> synthesis::ClockSynthesisArtifactPaths;
auto AssertClusteredArtifacts(const synthesis::ClockSynthesisArtifactPaths& artifact_paths) -> void;
auto AssertNonClusteredArtifacts(const synthesis::ClockSynthesisArtifactPaths& artifact_paths) -> void;
auto CalcFloorPowerOfTwo(std::size_t value) -> std::size_t;
auto CountTopologyLeafNodes(const icts::Tree& topology) -> std::size_t;
auto FindSelectedDepthSummary(const icts::HTreeBuilder::BuildResult& result)
    -> const icts::HTreeBuilder::BuildResult::DepthCandidateSummary*;
auto AssertDepthCandidateCoverage(const icts::HTreeBuilder::BuildResult& result) -> void;
auto AssertSelectedHTreeLoadDistribution(const icts::HTreeBuilder::BuildResult& result) -> void;
auto EvaluateArm9FullSinkNonClusteredExperimentMatrix() -> ClockSynthesisMatrixRunResult;

}  // namespace icts_test::synthesis_realtech_smoke
