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
 * @file TestUtils.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-19
 * @brief Test utilities for iCTS module testing.
 */

#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "database/spatial/Tree.hh"

namespace icts_test {

struct GeneratedPins
{
  std::vector<std::unique_ptr<icts::Pin>> storage;
  std::vector<icts::Pin*> loads;
  int width = 0;
  int height = 0;
};

GeneratedPins MakeNormal(std::size_t count, int width, int height, unsigned seed);
GeneratedPins MakeGaussianMixture(std::size_t count, int width, int height, unsigned seed);
GeneratedPins MakeWeightedQuadrants(std::size_t count, int width, int height, unsigned seed, const std::array<double, 4>& weights);

struct TopologyStats
{
  std::size_t tree_size = 0;
  std::size_t leaf_count = 0;
  std::size_t empty_leaf_count = 0;
  std::size_t min_leaf_load = 0;
  std::size_t max_leaf_load = 0;
  double avg_leaf_load = 0.0;
};

bool AnalyzeTopology(const icts::Tree& tree, const std::vector<icts::Pin*>& loads, TopologyStats& stats,
                     std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                     std::string& error);

// Analyze only first-level clusters (biPartition result)
// This produces cleaner visualization with only 2 clusters
bool AnalyzeFirstLevelClusters(const icts::Tree& tree, const std::vector<icts::Pin*>& loads,
                               std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, std::vector<icts::Point<int>>& centers,
                               std::string& error);

bool WriteClusterSvg(const std::string& path, const std::vector<icts::Pin*>& loads,
                     const std::unordered_map<const icts::Pin*, std::size_t>& cluster_map, const std::vector<icts::Point<int>>& centers);
bool WriteTopologySvg(const std::string& path, const icts::Tree& tree, const std::vector<icts::Pin*>& loads);

std::filesystem::path ResolveOutputDir();

}  // namespace icts_test
