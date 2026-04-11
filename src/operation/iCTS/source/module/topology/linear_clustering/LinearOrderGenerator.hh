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
 * @file LinearOrderGenerator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-09
 * @brief Geometry-only sequence generation for linear clustering.
 */

#pragma once

#include <vector>

#include "LinearClusteringTypes.hh"

namespace icts {

struct LinearClusteringConfig;
class Pin;

class LinearOrderGenerator
{
 public:
  struct Bounds
  {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
  };

  LinearOrderGenerator() = delete;
  ~LinearOrderGenerator() = default;

  static auto generateOrder(const std::vector<Pin*>& loads, const LinearClusteringConfig& config) -> std::vector<OrderedLoad>;

 private:
  static auto calcBounds(const std::vector<Pin*>& loads) -> Bounds;
};

}  // namespace icts
