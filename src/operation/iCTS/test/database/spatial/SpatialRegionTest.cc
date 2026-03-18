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
 * @file SpatialRegionTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-17
 * @brief Unit tests for spatial Rect and Region helpers.
 */

#include <gtest/gtest.h>

#include "database/spatial/Rect.hh"
#include "database/spatial/Region.hh"

namespace icts_test {
namespace {

using Point = icts::Point<int>;
using Rect = icts::Rect<int>;
using Region = icts::Region<int>;

TEST(SpatialRegionTest, RectContainsAndClamp)
{
  Rect rect(0, 0, 10, 5);
  EXPECT_TRUE(rect.contains(Point(0, 0)));
  EXPECT_TRUE(rect.contains(Point(10, 5)));
  EXPECT_FALSE(rect.contains(Point(11, 5)));
  EXPECT_EQ(rect.clamp(Point(-3, 9)), Point(0, 5));
}

TEST(SpatialRegionTest, RegionSubtractRectKeepsExpectedPieces)
{
  Region region(Rect(0, 0, 10, 10));
  region.subtract(Rect(3, 3, 7, 7));

  EXPECT_FALSE(region.empty());
  EXPECT_FALSE(region.contains(Point(5, 5)));
  EXPECT_TRUE(region.contains(Point(1, 1)));
  EXPECT_TRUE(region.contains(Point(9, 9)));
  EXPECT_TRUE(region.contains(Point(2, 5)));
  EXPECT_EQ(region.rects().size(), 4U);
}

TEST(SpatialRegionTest, RegionNormalizationMergesTouchingSpansOnly)
{
  Region region;
  region.add_rect(Rect(0, 0, 4, 2));
  region.add_rect(Rect(0, 3, 4, 5));
  EXPECT_EQ(region.rects().size(), 1U);
  EXPECT_TRUE(region.contains(Point(2, 4)));

  Region l_shape({Rect(0, 0, 4, 1), Rect(3, 0, 5, 4)});
  EXPECT_EQ(l_shape.rects().size(), 2U);
  EXPECT_FALSE(l_shape.contains(Point(1, 3)));
}

TEST(SpatialRegionTest, RegionProjectNearestSelectsClosestLegalPoint)
{
  Region region({Rect(0, 0, 2, 2), Rect(10, 0, 12, 2)});
  auto projected = region.project_nearest(Point(7, 1));
  ASSERT_TRUE(projected.has_value());
  EXPECT_EQ(projected.value(), Point(10, 1));

  auto inside = region.project_nearest(Point(1, 2));
  ASSERT_TRUE(inside.has_value());
  EXPECT_EQ(inside.value(), Point(1, 2));
}

}  // namespace
}  // namespace icts_test
