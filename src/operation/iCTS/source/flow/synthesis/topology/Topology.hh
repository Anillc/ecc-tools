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
 * @file Topology.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief CTS topology formation entry for sink branches and source trunk.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Point.hh"
#include "clustering/Clustering.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/HTreeSynthesisResult.hh"
#include "synthesis/trace/SynthesisTrace.hh"

namespace icts {

class CharacterizationLibrary;
class Clock;
class DomainStatusTable;
class ClockLayout;
struct ClockDistributionContext;

class Topology
{
 public:
  struct BuildOptions
  {
    std::optional<bool> enable_sink_clustering = std::nullopt;
    std::string object_name_prefix;
    CharacterizationLibrary* characterization_library = nullptr;
    std::vector<double> additional_characterization_lengths_um;
    double clock_period_ns = 0.0;
    std::string clock_period_source;
    HTree::LogContext log_context;
    bool htree_loads_are_local_buffers = false;
  };

  struct SourceTrunkBuildOptions
  {
    std::string object_name_prefix;
    CharacterizationLibrary* characterization_library = nullptr;
    double clock_period_ns = 0.0;
    std::string clock_period_source;
    HTree::LogContext log_context;
  };

  enum class SourceTrunkStage
  {
    kUnknown,
    kSegment,
    kHTree
  };

  struct ClusterBufferMeta
  {
    std::size_t cluster_index = 0;
    Point<int> location = Point<int>(0, 0);
    std::size_t sink_count = 0;
    Inst* inst = nullptr;
    Pin* input_pin = nullptr;
    Pin* output_pin = nullptr;
    Net* sink_net = nullptr;
  };

  struct ClusterLeafDistanceSummary
  {
    std::size_t count = 0;
    double min_distance_um = 0.0;
    double max_distance_um = 0.0;
    double mean_distance_um = 0.0;
    double median_distance_um = 0.0;
  };

  struct BuildResult
  {
    bool success = false;
    std::string failure_reason;
    bool sink_clustering_enabled = false;

    HTree::BuildResult htree_result;
    std::optional<ClusterResult> cluster_result = std::nullopt;
    std::vector<ClusterBufferMeta> cluster_buffers;
    std::optional<ClusterLeafDistanceSummary> cluster_leaf_distance_summary = std::nullopt;
    std::size_t selected_htree_level_count = 0;
    std::optional<unsigned> selected_htree_depth = std::nullopt;
    std::size_t htree_inserted_buffer_count = 0;
    std::size_t htree_inserted_net_count = 0;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;
    std::vector<HTree::InsertedInstLevel> inserted_inst_levels;
    std::vector<HTree::InsertedNetLevel> inserted_net_levels;
  };

  struct SourceTrunkBuildResult
  {
    bool success = false;
    std::string failure_reason;
    SourceTrunkStage stage = SourceTrunkStage::kUnknown;
    HTree::BuildResult htree_result;
    std::size_t inserted_buffer_count = 0U;
    std::size_t inserted_net_count = 0U;
    bool used_boundary_relaxation = false;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;
    std::vector<HTree::InsertedInstLevel> inserted_inst_levels;
    std::vector<HTree::InsertedNetLevel> inserted_net_levels;
  };

  Topology() = delete;

  static auto build(Net& root_net) -> BuildResult;
  static auto build(Net& root_net, const BuildOptions& options) -> BuildResult;
  static auto buildSourceTrunk(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                               const SourceTrunkBuildOptions& options) -> SourceTrunkBuildResult;
  static auto resetClockTopology(Clock& clock) -> void;
  static auto formClock(Clock& clock, std::size_t clock_index, ClockLayout& clock_layout, SynthesisTraceSummary& summary,
                        DomainStatusTable& status_table, CharacterizationLibrary& characterization_library, std::size_t valid_sinks,
                        const std::vector<ClockDistributionContext>& sink_domains) -> bool;
};

auto ToString(Topology::SourceTrunkStage stage) -> const char*;

}  // namespace icts
