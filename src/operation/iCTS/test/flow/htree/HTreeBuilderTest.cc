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
 * @file HTreeBuilderTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief Unit coverage for HTreeBuilder degenerate cases.
 */

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Tree.hh"
#include "flow/htree/HTreeBuilder.hh"

namespace icts_test {
namespace {

TEST(HTreeBuilderTest, EmptyLoadsReturnsEmptyResult)
{
  const auto result = icts::HTreeBuilder::build({});

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.topology.get_size(), 0U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_TRUE(result.candidate_chars.empty());
  EXPECT_TRUE(result.feasible_chars.empty());
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
}

TEST(HTreeBuilderTest, EmptyLoadsAcceptExplicitBuildOptions)
{
  const auto result = icts::HTreeBuilder::build({}, icts::HTreeBuilder::BuildOptions{
                                                        .force_branch_buffer = true,
                                                        .min_top_input_slew_ns = 0.05,
                                                    });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.topology.get_size(), 0U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_TRUE(result.candidate_chars.empty());
  EXPECT_TRUE(result.feasible_chars.empty());
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
}

TEST(HTreeBuilderTest, SingleLoadStopsBeforeCharacterization)
{
  auto load = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  std::vector<icts::Pin*> loads{load.get()};

  const auto result = icts::HTreeBuilder::build(loads);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.topology.get_size(), 1U);
  EXPECT_TRUE(result.levels.empty());
  EXPECT_FALSE(result.best_char.has_value());
  EXPECT_FALSE(result.best_pattern.has_value());
  EXPECT_TRUE(result.candidate_chars.empty());
  EXPECT_TRUE(result.feasible_chars.empty());
  EXPECT_TRUE(result.inserted_insts.empty());
  EXPECT_TRUE(result.inserted_nets.empty());
  EXPECT_EQ(load->get_net(), nullptr);
}

}  // namespace
}  // namespace icts_test
