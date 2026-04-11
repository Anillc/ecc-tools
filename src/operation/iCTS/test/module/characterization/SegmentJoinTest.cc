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
 * @file SegmentJoinTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Segment characterization join tests.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/SegmentCharTable.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

TEST(SegmentJoinTest, BasicEqualJoin)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0,
                                            support::kPower0p5,
                                            support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}));

  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                              support::kPower0p3,
                                              support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}));
  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew130, support::kCap55, support::kCap70, support::kDelay3p0,
                                              support::kPower0p4,
                                              support::SegmentShape{.pattern_id = support::kPattern3, .length_idx = support::kLength3000}));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars().front();
  EXPECT_EQ(merged.get_input_slew_idx(), support::kSlew80);
  EXPECT_EQ(merged.get_output_slew_idx(), support::kSlew120);
  EXPECT_EQ(merged.get_driven_cap_idx(), support::kCap40);
  EXPECT_EQ(merged.get_load_cap_idx(), support::kCap60);
  EXPECT_DOUBLE_EQ(merged.get_delay(), support::kDelay3p0);
  EXPECT_DOUBLE_EQ(merged.get_power(), support::kMergedPower0p8);
  EXPECT_EQ(merged.get_length_idx(), support::kLengthSum3000);
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kSegmentPattern);
}

TEST(SegmentJoinTest, MultipleMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0,
                                            support::kPower0p5,
                                            support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}));
  upstream.addChar(support::MakeSegmentChar(support::kSlew90, support::kSlew100, support::kCap45, support::kCap50, support::kDelay1p5,
                                            support::kPower0p6,
                                            support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength1500}));

  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                              support::kPower0p3,
                                              support::SegmentShape{.pattern_id = support::kPattern3, .length_idx = support::kLength2000}));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 2U);
}

TEST(SegmentJoinTest, NoMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0,
                                            support::kPower0p5,
                                            support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}));
  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew120, support::kCap51, support::kCap60, support::kDelay2p0,
                                              support::kPower0p3,
                                              support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 0U);
}

}  // namespace
}  // namespace icts_test
