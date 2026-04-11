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
 * @file LinearClusteringSyntheticDataset.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic dataset builders shared by linear clustering tests.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/data/TestDataGenerator.hh"
#include "common/types/TestDataTypes.hh"
#include "database/spatial/Point.hh"
#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"
#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic::detail {
namespace {

constexpr std::array<std::array<int, 2>, 4> kBalancedMarginalBinValues = {
    std::array<int, 2>{4, 20},
    std::array<int, 2>{36, 52},
    std::array<int, 2>{68, 84},
    std::array<int, 2>{100, 112},
};

constexpr std::array<std::array<int, 2>, 11> kMarginalDensitySkewPoints = {
    std::array<int, 2>{4, 4},     std::array<int, 2>{20, 12},   std::array<int, 2>{36, 20},   std::array<int, 2>{52, 28},
    std::array<int, 2>{68, 36},   std::array<int, 2>{84, 44},   std::array<int, 2>{100, 52},  std::array<int, 2>{116, 60},
    std::array<int, 2>{120, 100}, std::array<int, 2>{124, 120}, std::array<int, 2>{126, 124},
};

}  // namespace

auto GenerateSyntheticCase(const SyntheticSweepCase& test_case) -> GeneratedPins
{
  const CanvasSize canvas{.width = kCanvasWidth, .height = kCanvasHeight};
  switch (test_case.kind) {
    case DistKind::kNormal:
      return common::data::MakeNormal(test_case.load_count, canvas, test_case.seed);
    case DistKind::kMixture:
      return common::data::MakeGaussianMixture(test_case.load_count, canvas, test_case.seed);
    case DistKind::kQuadrants:
    default:
      return common::data::MakeWeightedQuadrants(test_case.load_count, canvas, test_case.seed, test_case.quadrant_weights);
  }
}

auto BuildBalancedMarginalJointSkewPoints() -> std::vector<icts::Point<int>>
{
  const std::array<std::pair<int, int>, 8> occupied_cells = {
      std::pair<int, int>{0, 0}, std::pair<int, int>{0, 3}, std::pair<int, int>{1, 1}, std::pair<int, int>{1, 2},
      std::pair<int, int>{2, 0}, std::pair<int, int>{2, 3}, std::pair<int, int>{3, 1}, std::pair<int, int>{3, 2},
  };
  std::vector<icts::Point<int>> points;
  points.reserve(occupied_cells.size() * 2U);
  for (const auto& [row, column] : occupied_cells) {
    const auto& x_values = kBalancedMarginalBinValues.at(static_cast<std::size_t>(column));
    const auto& y_values = kBalancedMarginalBinValues.at(static_cast<std::size_t>(row));
    points.emplace_back(x_values.at(0), y_values.at(0));
    points.emplace_back(x_values.at(1), y_values.at(1));
  }
  return points;
}

auto BuildMarginalDensitySkewPoints() -> std::vector<icts::Point<int>>
{
  std::vector<icts::Point<int>> points;
  points.reserve(kMarginalDensitySkewPoints.size());
  for (const auto& coords : kMarginalDensitySkewPoints) {
    points.emplace_back(coords.at(0), coords.at(1));
  }
  return points;
}

auto MakeLineLoads(std::size_t count, int start_x, int step_x, int y_coord) -> GeneratedPins
{
  std::vector<icts::Point<int>> points;
  points.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    points.emplace_back(start_x + (static_cast<int>(index) * step_x), y_coord);
  }
  const int width = count == 0 ? 0 : start_x + (static_cast<int>(count - 1) * step_x) + step_x;
  const int height = std::max(y_coord + step_x, step_x);
  return common::data::BuildPinsFromPoints(points, CanvasSize{.width = width, .height = height}, "line_load");
}

auto MakeSeparatedIslandLoads() -> GeneratedPins
{
  std::vector<icts::Point<int>> points;
  points.reserve(kIslandLoadCount);
  for (const int island_x : kIslandXOffsets) {
    for (int x_index = 0; x_index < kIslandGridSide; ++x_index) {
      for (int y_index = 0; y_index < kIslandGridSide; ++y_index) {
        points.emplace_back(island_x + (x_index * kIslandGridStep), kIslandBaseY + (y_index * kIslandGridStep));
      }
    }
  }
  return common::data::BuildPinsFromPoints(points, CanvasSize{.width = kIslandCanvasWidth, .height = kIslandCanvasHeight}, "island_load");
}

}  // namespace icts_test::linear_clustering::synthetic::detail
