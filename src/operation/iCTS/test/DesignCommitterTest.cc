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
 * @file DesignCommitterTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include <new>

#include "CtsDesign.hh"
#include "DesignCommitter.hh"
#include "gtest/gtest.h"

namespace {

using icts::CtsDBWrapper;
using icts::CtsDesign;
using icts::DesignCommitter;
using icts::Point;

TEST(DesignCommitterTest, RuntimeContextValidation)
{
  DesignCommitter::RuntimeContext context;
  EXPECT_FALSE(context.isValid());

  CtsDesign design;
  context._design = &design;
  context._is_in_die = [](const Point&) { return true; };
  context._sync_timing = []() {};
  EXPECT_FALSE(context.isValid());

  void* raw_wrapper = ::operator new(1);
  context._db_wrapper = static_cast<CtsDBWrapper*>(raw_wrapper);
  context._register_synthesis_net = [](icts::Net*) {};
  EXPECT_TRUE(context.isValid());
  ::operator delete(raw_wrapper);
}

TEST(DesignCommitterTest, BuildSnakeGuidePointsUsePrimaryBranchWhenInsideDie)
{
  const Point parent_loc(100, 50);
  const Point current_loc(200, 180);
  const int64_t require_snake = 60;

  auto [snake_p1, snake_p2]
      = DesignCommitter::buildSnakeGuidePoints(parent_loc, current_loc, require_snake, [](const Point&) { return true; });

  EXPECT_EQ(snake_p1.x(), 230);
  EXPECT_EQ(snake_p1.y(), parent_loc.y());
  EXPECT_EQ(snake_p2.x(), 230);
  EXPECT_EQ(snake_p2.y(), current_loc.y());
}

TEST(DesignCommitterTest, BuildSnakeGuidePointsFallbackWhenPrimaryBranchOutOfDie)
{
  const Point parent_loc(100, 50);
  const Point current_loc(200, 180);
  const int64_t require_snake = 60;

  auto [snake_p1, snake_p2]
      = DesignCommitter::buildSnakeGuidePoints(parent_loc, current_loc, require_snake, [](const Point& point) { return point.x() != 230; });

  EXPECT_EQ(snake_p1.x(), 70);
  EXPECT_EQ(snake_p1.y(), parent_loc.y());
  EXPECT_EQ(snake_p2.x(), 70);
  EXPECT_EQ(snake_p2.y(), current_loc.y());
}

}  // namespace
