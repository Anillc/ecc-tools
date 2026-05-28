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
 * @file TopologyGen.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Topology generator for CTS.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Point.hh"
#include "Tree.hh"
#include "clustering/Clustering.hh"
#include "config/TopologyConfig.hh"

namespace icts {

class Pin;

class TopologyGen
{
 public:
  enum class LoadCountKind
  {
    kSink,
    kLocalBuffer
  };

  struct BuildOptions
  {
    BiPartitionConfig partition_config;
    std::optional<unsigned> target_depth = std::nullopt;
    std::optional<Point<int>> fixed_root_location = std::nullopt;
    int32_t dbu_per_um = 1;
    LoadCountKind load_count_kind = LoadCountKind::kSink;
    std::string clock_name;
    std::string clock_net_name;
    std::string sink_domain;
    std::string stage;
  };

  TopologyGen() = delete;
  ~TopologyGen() = default;

  static auto build(const std::vector<Pin*>& loads) -> Tree;
  static auto build(const std::vector<Pin*>& loads, const BuildOptions& options) -> Tree;
  static auto build(const std::vector<Pin*>& loads, const BiPartitionConfig& config) -> Tree;
  static auto buildFastClusteringElectricalConfig(std::size_t max_fanout, double max_cap) -> ClusterConfig;
  static auto fastClustering(const std::vector<Pin*>& loads) -> ClusterResult;
  static auto defaultFastClustering(const std::vector<Pin*>& loads, const ClusterConfig& base_config) -> ClusterResult;
  static auto fastClustering(const std::vector<Pin*>& loads, const ClusterConfig& config) -> ClusterResult;

 private:
  struct BuildCursor
  {
    std::size_t node_id = 0;
    int depth = 0;
  };

  static auto reportLoadDistribution(const std::vector<Pin*>& loads, LoadCountKind load_count_kind, int32_t dbu_per_um) -> void;
  static auto reportRootToLeafLengths(const Tree& tree, int32_t dbu_per_um) -> void;
  static auto calcMaxDepth(std::size_t load_count) -> unsigned;
  static auto calcLeafCount(std::size_t load_count) -> std::size_t;
  static auto build(const std::vector<Pin*>& loads, const BiPartitionConfig& config, std::optional<unsigned> target_depth,
                    std::optional<Point<int>> fixed_root_location, LoadCountKind load_count_kind, int32_t dbu_per_um,
                    const BuildOptions& options) -> Tree;
  static auto buildFullTree(Tree& tree, const BuildCursor& cursor, int height) -> void;
  static auto embedPositions(Tree& tree, std::size_t node, const std::vector<Pin*>& loads, std::size_t leaf_need,
                             const BiPartitionConfig& config) -> void;
  static auto balanceTopology(Tree& tree, int min_x, int min_y, int max_x, int max_y, double topology_tolerance) -> void;
};

}  // namespace icts
