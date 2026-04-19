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
 * @file LinearClustering.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Linear clustering orchestration facade.
 */

#pragma once

#include <cstddef>
#include <vector>

namespace icts {

struct ClusterResult;
struct LinearClusteringConfig;
class Pin;

class LinearClustering
{
 public:
  LinearClustering() = delete;
  ~LinearClustering() = default;

  static auto buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> LinearClusteringConfig;
  static auto runDefault(const std::vector<Pin*>& loads, const LinearClusteringConfig& base_config) -> ClusterResult;
  static auto run(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> ClusterResult;
};

}  // namespace icts
