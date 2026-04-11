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
 * @file LinearClusteringSyntheticSweepScoringTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic linear clustering sweep and scoring regression tests.
 */

#include <gtest/gtest.h>

#include <string>

#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"

namespace icts_test::linear_clustering::synthetic {
namespace {

TEST(LinearClusteringSweepTest, PrefixSweepResolvesSinkStyleOffsets)
{
  RunPrefixSweepResolvesSinkStyleOffsets();
}

TEST(LinearClusteringSweepTest, StridedSweepSamplesDistinctRingStarts)
{
  RunStridedSweepSamplesDistinctRingStarts();
}

TEST(LinearClusteringSweepTest, CombinedSweepNormalizesToFullSequenceWhenAnchorWindowsCoverRing)
{
  RunCombinedSweepNormalizesToFullSequenceWhenAnchorWindowsCoverRing();
}

TEST(LinearClusteringScoringTest, MaxDiameterScoringPenalizesZeroDiameterSingletons)
{
  RunMaxDiameterScoringPenalizesZeroDiameterSingletons();
}

}  // namespace
}  // namespace icts_test::linear_clustering::synthetic
