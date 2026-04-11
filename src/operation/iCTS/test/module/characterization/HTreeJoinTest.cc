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
 * @file HTreeJoinTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief H-tree characterization join tests.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
#include "module/characterization/HTreeTopologyCharTable.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

TEST(HTreeJoinTest, HalfCapJoin)
{
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  upstream.addChar(support::MakeHTreeChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap100, support::kDelay1p0,
                                          support::kPower0p5, support::HTreeShape{.pattern_id = support::kPattern1, .levels = 1}));

  downstream.addChar(support::MakeHTreeChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                            support::kPower0p3, support::HTreeShape{.pattern_id = support::kPattern2, .levels = 1}));
  downstream.addChar(support::MakeHTreeChar(support::kSlew100, support::kSlew130, support::kCap51, support::kCap70, support::kDelay3p0,
                                            support::kPower0p4, support::HTreeShape{.pattern_id = support::kPattern3, .levels = 1}));

  const icts::TopologyPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars().front();
  EXPECT_EQ(merged.get_input_slew_idx(), support::kSlew80);
  EXPECT_EQ(merged.get_output_slew_idx(), support::kSlew120);
  EXPECT_EQ(merged.get_driven_cap_idx(), support::kCap40);
  EXPECT_EQ(merged.get_load_cap_idx(), support::kCap60);
  EXPECT_DOUBLE_EQ(merged.get_delay(), support::kDelay3p0);
  EXPECT_DOUBLE_EQ(merged.get_power(), support::kMergedPower1p1);
  EXPECT_EQ(merged.get_levels(), 2U);
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kTopologyPattern);
}

TEST(HTreeJoinTest, OddCapHalving)
{
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  upstream.addChar(support::MakeHTreeChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap101, support::kDelay1p0,
                                          support::kPower0p5, support::HTreeShape{.pattern_id = support::kPattern1, .levels = 1}));
  downstream.addChar(support::MakeHTreeChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                            support::kPower0p3, support::HTreeShape{.pattern_id = support::kPattern2, .levels = 1}));

  const icts::TopologyPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 1U);
}

TEST(HTreeJoinTest, PowerDoubling)
{
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  upstream.addChar(support::MakeHTreeChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap100, support::kDelay1p0,
                                          support::kPower10p0, support::HTreeShape{.pattern_id = support::kPattern1, .levels = 1}));
  downstream.addChar(support::MakeHTreeChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                            support::kPower5p0, support::HTreeShape{.pattern_id = support::kPattern2, .levels = 1}));

  const icts::TopologyPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);
  EXPECT_DOUBLE_EQ(result.get_chars().front().get_power(), support::kMergedPower20p0);
}

}  // namespace
}  // namespace icts_test
