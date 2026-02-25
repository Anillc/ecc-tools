// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
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
#include <vector>

#include "database/design/Pin.hh"
#include "database/spatial/Tree.hh"
#include "module/topology/clustering/Clustering.hh"

namespace icts {

class TopologyGen
{
 public:
  using Config = Clustering::Config;

  TopologyGen();
  explicit TopologyGen(const Config& config);
  ~TopologyGen() = default;

  Tree build(const std::vector<Pin*>& loads);
  void set_config(const Config& config);

 private:
  void reportLoadDistribution(const std::vector<Pin*>& loads) const;
  void reportRootToLeafLengths(const Tree& tree) const;
  std::size_t calcLeafCount(std::size_t load_count) const;
  void buildFullTree(Tree& tree, std::size_t node, int depth, int height) const;
  void embedPositions(Tree& tree, std::size_t node, const std::vector<Pin*>& loads, std::size_t leaf_need);
  void balanceTopology(Tree& tree, int min_x, int min_y, int max_x, int max_y) const;

  Config _config;
  Clustering _clustering;
};

}  // namespace icts
