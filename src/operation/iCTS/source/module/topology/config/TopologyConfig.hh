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
 * @file TopologyConfig.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-24
 * @brief Shared configuration types for topology module algorithms.
 */

#pragma once

#include <cstddef>

namespace icts {

struct BiPartitionConfig
{
  double max_ratio = 0.6;
  int max_iter = 10;
  int converge_threshold = 1000;
  std::size_t kmeans_iter_count = 5;
};

}  // namespace icts
