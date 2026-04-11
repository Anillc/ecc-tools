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
 * @file LinearClusteringSyntheticConstraintTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Synthetic linear clustering constraint regression tests.
 */

#include <gtest/gtest.h>

#include <string>

#include "module/topology/linear_clustering/synthetic/LinearClusteringSyntheticShared.hh"

namespace icts_test::linear_clustering::synthetic {
namespace {

TEST(LinearClusteringTest, TightConstraintsAreLegalOrEmpty)
{
  RunTightConstraintsAreLegalOrEmpty();
}

TEST(LinearClusteringConstraintTest, FanoutLadderIsMonotonic)
{
  RunFanoutLadderIsMonotonic();
}

TEST(LinearClusteringConstraintTest, DiameterLadderIsMonotonic)
{
  RunDiameterLadderIsMonotonic();
}

TEST(LinearClusteringConstraintTest, TighterCombinedConstraintsIncreasePressure)
{
  RunTighterCombinedConstraintsIncreasePressure();
}

}  // namespace
}  // namespace icts_test::linear_clustering::synthetic
