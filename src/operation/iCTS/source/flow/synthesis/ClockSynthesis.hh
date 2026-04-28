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
 * @file ClockSynthesis.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Orchestrates optional sink clustering and H-tree synthesis for one clock distribution.
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
#include "htree/HTreeBuilder.hh"

namespace icts {

class CharacterizationLibrary;

class ClockSynthesis
{
 public:
  struct BuildOptions
  {
    std::optional<bool> enable_sink_clustering = std::nullopt;
    std::string object_name_prefix;
    CharacterizationLibrary* characterization_library = nullptr;
    std::vector<double> additional_characterization_lengths_um;
    HTreeBuilder::LogContext log_context;
  };

  struct SourceToRootBuildOptions
  {
    std::string object_name_prefix;
    CharacterizationLibrary* characterization_library = nullptr;
    HTreeBuilder::LogContext log_context;
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

    HTreeBuilder::BuildResult htree_result;
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
  };

  struct SourceToRootBuildResult
  {
    bool success = false;
    std::string failure_reason;
    std::string stage;
    HTreeBuilder::BuildResult htree_result;
    std::size_t inserted_buffer_count = 0U;
    std::size_t inserted_net_count = 0U;
    bool used_boundary_fallback = false;

    std::vector<std::unique_ptr<Inst>> inserted_insts;
    std::vector<std::unique_ptr<Pin>> inserted_pins;
    std::vector<std::unique_ptr<Net>> inserted_nets;
  };

  ClockSynthesis() = delete;

  static auto build(Net& root_net) -> BuildResult;
  static auto build(Net& root_net, const BuildOptions& options) -> BuildResult;
  static auto buildSourceToRoot(Net& source_net, Pin* clock_source, const std::vector<Pin*>& root_inputs,
                                const SourceToRootBuildOptions& options) -> SourceToRootBuildResult;
};

}  // namespace icts
