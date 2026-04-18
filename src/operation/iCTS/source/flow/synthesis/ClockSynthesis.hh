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

class Clock;

class ClockSynthesis
{
 public:
  struct BuildOptions
  {
    std::optional<bool> enable_sink_clustering = std::nullopt;
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

  struct BuildResult
  {
    bool success = false;
    std::string failure_reason;
    bool sink_clustering_enabled = false;

    HTreeBuilder::BuildResult htree_result;
    std::optional<ClusterResult> cluster_result = std::nullopt;
    std::vector<ClusterBufferMeta> cluster_buffers;

    std::vector<std::unique_ptr<Inst>> inst_storage;
    std::vector<std::unique_ptr<Pin>> pin_storage;
    std::vector<std::unique_ptr<Net>> net_storage;

    std::vector<Inst*> inserted_insts;
    std::vector<Pin*> inserted_pins;
    std::vector<Net*> inserted_nets;
    Net* source_to_root_net = nullptr;
  };

  ClockSynthesis() = default;
  ~ClockSynthesis() = default;

  static auto build(Clock& clock) -> BuildResult;
  static auto build(Clock& clock, const BuildOptions& options) -> BuildResult;
  static auto build(Pin* clock_source, const std::vector<Pin*>& sinks) -> BuildResult;
  static auto build(Pin* clock_source, const std::vector<Pin*>& sinks, const BuildOptions& options) -> BuildResult;
};

}  // namespace icts
