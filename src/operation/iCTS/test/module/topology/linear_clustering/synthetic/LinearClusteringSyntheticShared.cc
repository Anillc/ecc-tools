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
 * @file LinearClusteringSyntheticShared.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Public synthetic linear clustering shared entry points.
 */

#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"

#include <vector>

#include "module/topology/linear_clustering/synthetic/support/LinearClusteringSyntheticInternal.hh"

namespace icts_test::linear_clustering::synthetic {
namespace {

auto DistNameImpl(DistKind kind) -> const char*
{
  switch (kind) {
    case DistKind::kNormal:
      return "normal";
    case DistKind::kMixture:
      return "mixture";
    case DistKind::kQuadrants:
    default:
      return "quadrants";
  }
}

}  // namespace

auto DistName(DistKind kind) -> const char*
{
  return DistNameImpl(kind);
}

auto BuildSyntheticSweepCases() -> std::vector<SyntheticSweepCase>
{
  return {
      SyntheticSweepCase{
          .name = "normal_small",
          .kind = DistKind::kNormal,
          .load_count = detail::kSyntheticSweepNormalSmallLoadCount,
          .seed = detail::kSyntheticSweepNormalSmallSeed,
      },
      SyntheticSweepCase{
          .name = "normal_large",
          .kind = DistKind::kNormal,
          .load_count = detail::kSyntheticSweepNormalLargeLoadCount,
          .seed = detail::kSyntheticSweepNormalLargeSeed,
      },
      SyntheticSweepCase{
          .name = "mixture",
          .kind = DistKind::kMixture,
          .load_count = detail::kSyntheticSweepMixtureLoadCount,
          .seed = detail::kSyntheticSweepMixtureSeed,
      },
      SyntheticSweepCase{
          .name = "quadrant_uneven",
          .kind = DistKind::kQuadrants,
          .load_count = detail::kSyntheticSweepQuadrantUnevenLoadCount,
          .seed = detail::kSyntheticSweepQuadrantUnevenSeed,
          .quadrant_weights = detail::kUnevenQuadrantWeights,
      },
  };
}

}  // namespace icts_test::linear_clustering::synthetic
