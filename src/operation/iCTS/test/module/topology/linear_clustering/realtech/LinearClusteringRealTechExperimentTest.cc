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
 * @file LinearClusteringRealTechExperimentTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Arm9 real-tech linear clustering experiment regression tests.
 */

#include <gtest/gtest.h>

#include <string>

#include "module/topology/linear_clustering/realtech/support/LinearClusteringRealTechShared.hh"

namespace icts_test::linear_clustering::realtech {
namespace {

TEST(LinearClusteringRealTechExperimentTest, Arm9StrategyRankingExperiment)
{
  RunRealTechArm9StrategyRankingExperiment();
}

}  // namespace
}  // namespace icts_test::linear_clustering::realtech
