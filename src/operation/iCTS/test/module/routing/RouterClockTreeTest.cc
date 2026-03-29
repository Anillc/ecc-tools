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
 * @file RouterClockTreeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-29
 * @brief Unit tests for clock routing tree specialization.
 */

#include <gtest/gtest.h>

#include <string>

#include "SteinerTree.hh"
#include "database/spatial/Point.hh"
#include "module/routing/Router.hh"

namespace icts_test {
namespace {

using Router = icts::Router;
using Point = icts::Point<int>;

constexpr int kTerminalX = 100;
constexpr int kTerminalY = 200;
constexpr int kSteinerX = 80;
constexpr int kSteinerY = 180;
constexpr int kRootX = 10;
constexpr int kRootY = 20;
constexpr double kTerminalPinCap = 0.18;
constexpr double kTerminalInsertionDelay = 0.9;
constexpr double kRootPinCap = 0.23;

TEST(RouterClockTreeTest, ClockSteinerNodeDefaultsAndExplicitMetadata)
{
  Router::ClockSteinerTreeType clock_tree;

  auto terminal_id = clock_tree.addNode("sink0", Point(kTerminalX, kTerminalY), true, kTerminalPinCap, kTerminalInsertionDelay);
  auto steiner_id = clock_tree.addNode("steiner0", Point(kSteinerX, kSteinerY), false);

  const auto* terminal = clock_tree.get_node(terminal_id);
  const auto* steiner = clock_tree.get_node(steiner_id);
  ASSERT_NE(terminal, nullptr);
  ASSERT_NE(steiner, nullptr);

  EXPECT_DOUBLE_EQ(terminal->pin_cap, kTerminalPinCap);
  EXPECT_DOUBLE_EQ(terminal->insertion_delay, kTerminalInsertionDelay);
  EXPECT_DOUBLE_EQ(steiner->pin_cap, 0.0);
  EXPECT_DOUBLE_EQ(steiner->insertion_delay, 0.0);
}

TEST(RouterClockTreeTest, BuildRCTreeUsesClockNodePinCap)
{
  Router::ClockSteinerTreeType clock_tree;
  auto clock_root_id = clock_tree.addNode("clock_root", Point(kRootX, kRootY), true, kRootPinCap, 0.0);
  clock_tree.setRoot(clock_root_id);

  auto clock_rc_tree = Router::buildRCTree(clock_tree);
  const auto* clock_vertex = clock_rc_tree.findVertex("clock_root");
  ASSERT_NE(clock_vertex, nullptr);
  EXPECT_DOUBLE_EQ(clock_vertex->lumped_cap, kRootPinCap);

  Router::SteinerTreeType steiner_tree;
  auto steiner_root_id = steiner_tree.addNode("generic_root", Point(kRootX, kRootY), true);
  steiner_tree.setRoot(steiner_root_id);

  auto generic_rc_tree = Router::buildRCTree(steiner_tree);
  const auto* generic_vertex = generic_rc_tree.findVertex("generic_root");
  ASSERT_NE(generic_vertex, nullptr);
  EXPECT_DOUBLE_EQ(generic_vertex->lumped_cap, 0.0);
}

}  // namespace
}  // namespace icts_test
