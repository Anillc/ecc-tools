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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ClockTreeViewSynthesisInput.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Narrow synthesis metadata consumed by clock-tree view construction.
 */

#pragma once

#include <vector>

namespace icts {

class Inst;
class Net;

struct ClockTreeViewInstTopology
{
  const Inst* inst = nullptr;
  int topology_level = -1;
};

struct ClockTreeViewNetTopology
{
  const Net* net = nullptr;
  int topology_level = -1;
};

struct ClockSinkDomainViewInput
{
  int selected_depth = -1;
  int topology_level_count = 0;
  std::vector<ClockTreeViewInstTopology> inserted_insts;
  std::vector<ClockTreeViewNetTopology> inserted_nets;
};

struct ClockSourceToRootViewInput
{
  int selected_depth = -1;
  int topology_level_count = 0;
  std::vector<ClockTreeViewInstTopology> inserted_insts;
  std::vector<ClockTreeViewNetTopology> inserted_nets;
};

}  // namespace icts
