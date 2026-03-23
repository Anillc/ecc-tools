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
 * @file Clustering.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-16
 * @brief Clustering for topology embedding.
 */

#pragma once

#include <cstddef>
#include <vector>

#include "Point.hh"

namespace icts {

class Pin;

struct ClusterResult
{
  std::vector<std::vector<Pin*>> clusters;
  std::vector<Point<int>> centers;
};

class Clustering
{
 public:
  struct Config
  {
    double max_ratio = 0.6;
    int max_iter = 10;
    int converge_threshold = 1000;
  };

  Clustering() = default;
  explicit Clustering(const Config& config);
  ~Clustering() = default;

  ClusterResult biPartition(const std::vector<Pin*>& loads, std::size_t min_cluster_size) const;

 private:
  Config _config;
};

}  // namespace icts
