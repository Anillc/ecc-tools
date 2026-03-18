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
 * @file BSTTypes.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-16
 * @brief Shared BST routing types for standalone router adaptation and core algorithm.
 */

#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "Point.hh"

namespace icts {

enum class RCPattern
{
  kSingle,
  kHV,
  kVH,
};

enum class TopoType
{
  kGreedyDist,
  kGreedyMerge,
  kBiPartition,
  kBiCluster,
  kInputTopo,
};

struct BSTParameters
{
  double skew_bound = 0.0;
  int db_unit = 0;
  double unit_h_cap = 0.0;
  double unit_h_res = 0.0;
  double unit_v_cap = 0.0;
  double unit_v_res = 0.0;
  TopoType topo_type = TopoType::kGreedyDist;
  RCPattern pattern = RCPattern::kHV;
  std::optional<Point<int>> root_guide = std::nullopt;
  std::unordered_map<std::string, double> init_delay_map;
  std::unordered_map<std::string, double> init_cap_map;
};

}  // namespace icts
