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
 * @file EvaluatorData.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace icts {

struct CellStatsProperty
{
  int total_num = 0;
  double total_area = 0.0;
  double total_cap = 0.0;
};

struct PathInfo
{
  std::string root_name;
  int min_depth = 0;
  int max_depth = 0;
};

struct EvaluatorMetrics
{
  double top_wire_len = 0.0;
  double trunk_wire_len = 0.0;
  double leaf_wire_len = 0.0;
  double total_wire_len = 0.0;
  double max_net_len = 0.0;

  double hpwl_top_wire_len = 0.0;
  double hpwl_trunk_wire_len = 0.0;
  double hpwl_leaf_wire_len = 0.0;
  double hpwl_total_wire_len = 0.0;
  double hpwl_max_net_len = 0.0;

  std::map<std::string, int> cell_dist_map;
  std::map<std::string, CellStatsProperty> cell_stats_map;
  std::map<int, int> net_level_map;
  std::vector<PathInfo> path_infos;
};

}  // namespace icts
