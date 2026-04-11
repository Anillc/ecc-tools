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
 * @file PrunerTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Pareto pruner tests for characterization.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "database/characterization/SegmentChar.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/Pruner.hh"
#include "module/characterization/SegmentCharTable.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

TEST(PrunerTest, DominationCheck)
{
  const icts::ParetoPruner<icts::SegmentChar> pruner;

  auto better_entry = support::MakeSegmentChar(support::kSlew80, support::kSlew90, support::kCap40, support::kCap60, support::kDelay1p0,
                                               support::kPower0p5,
                                               support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  auto worse_entry = support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay2p0,
                                              support::kPower0p6,
                                              support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});

  EXPECT_TRUE(pruner.dominates(better_entry, worse_entry));
  EXPECT_FALSE(pruner.dominates(worse_entry, better_entry));
}

TEST(PrunerTest, NonDomination)
{
  const icts::ParetoPruner<icts::SegmentChar> pruner;

  auto lower_slew_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew90, support::kCap40, support::kCap50, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  auto higher_cap_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});

  EXPECT_FALSE(pruner.dominates(lower_slew_entry, higher_cap_entry));
  EXPECT_FALSE(pruner.dominates(higher_cap_entry, lower_slew_entry));
}

TEST(PrunerTest, WithPruning)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0,
                                            support::kPower0p5,
                                            support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}));

  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew110, support::kCap50, support::kCap70, support::kDelay1p5,
                                              support::kPower0p2,
                                              support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}));
  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                              support::kPower0p3,
                                              support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  const icts::InputBoundaryPruner<icts::SegmentChar> pruner;
  auto result = upstream.concatWith(downstream, combiner, &pruner);

  EXPECT_EQ(result.size(), 1U);
  EXPECT_EQ(result.get_chars().front().get_output_slew_idx(), support::kSlew110);
}

}  // namespace
}  // namespace icts_test
