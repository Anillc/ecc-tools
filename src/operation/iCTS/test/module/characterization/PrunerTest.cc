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
 * @brief Frontier helper tests for characterization composition states.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "module/characterization/Frontier.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/SegmentCharTable.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

TEST(PrunerTest, CostDominatesUsesDelayPowerOrdering)
{
  auto better_entry = support::MakeSegmentChar(support::kSlew80, support::kSlew90, support::kCap40, support::kCap60, support::kDelay1p0,
                                               support::kPower0p5,
                                               support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  auto worse_entry = support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay2p0,
                                              support::kPower0p6,
                                              support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});

  EXPECT_TRUE(icts::CostDominates(better_entry, worse_entry));
  EXPECT_FALSE(icts::CostDominates(worse_entry, better_entry));
}

TEST(PrunerTest, CostDominatesRejectsTradeoffs)
{
  auto lower_slew_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew90, support::kCap40, support::kCap50, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  auto higher_cap_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});

  EXPECT_FALSE(icts::CostDominates(lower_slew_entry, higher_cap_entry));
  EXPECT_FALSE(icts::CostDominates(higher_cap_entry, lower_slew_entry));
}

TEST(PrunerTest, SegmentStateFrontierPrunesDominatedSameGroupEntries)
{
  const auto cheaper_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  const auto dominated_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay2p0, support::kPower0p6,
      support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength1000});

  const auto frontier = icts::BuildSegmentStateFrontier(
      std::vector<icts::SegmentChar>{dominated_entry, cheaper_entry},
      [](const icts::SegmentChar&) -> icts::TerminalSemantic { return icts::TerminalSemantic::kLeafUnbuffered; });

  ASSERT_EQ(frontier.size(), 1U);
  EXPECT_EQ(frontier.front().get_pattern_id().local_id, support::kPattern1);
}

TEST(PrunerTest, SegmentStateFrontierPreservesDistinctExactJoinBoundaries)
{
  const auto stronger_but_different_boundary = support::MakeSegmentChar(
      support::kSlew80, support::kSlew90, support::kCap40, support::kCap70, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  const auto join_critical_boundary = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay2p0, support::kPower0p6,
      support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength1000});

  const auto frontier = icts::BuildSegmentStateFrontier(
      std::vector<icts::SegmentChar>{stronger_but_different_boundary, join_critical_boundary},
      [](const icts::SegmentChar&) -> icts::TerminalSemantic { return icts::TerminalSemantic::kLeafUnbuffered; });

  ASSERT_EQ(frontier.size(), 2U);

  icts::SegmentCharTable upstream;
  upstream.addChar(frontier.front());
  upstream.addChar(frontier.back());

  icts::SegmentCharTable downstream;
  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew110, support::kCap60, support::kCap70, support::kDelay1p5,
                                              support::kPower0p2,
                                              support::SegmentShape{.pattern_id = support::kPattern3, .length_idx = support::kLength2000}));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  const auto result = upstream.concatWith(downstream, combiner).get_chars();
  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result.front().get_input_slew_idx(), support::kSlew80);
  EXPECT_EQ(result.front().get_output_slew_idx(), support::kSlew110);
  EXPECT_EQ(result.front().get_load_cap_idx(), support::kCap70);
}

TEST(PrunerTest, SegmentStateFrontierDoesNotMergeTerminalSemantics)
{
  const auto leaf_unbuffered_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay2p0, support::kPower0p6,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  const auto branch_buffered_entry = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength1000});

  const auto frontier = icts::BuildSegmentStateFrontier(std::vector<icts::SegmentChar>{leaf_unbuffered_entry, branch_buffered_entry},
                                                        [](const icts::SegmentChar& entry) -> icts::TerminalSemantic {
                                                          return entry.get_pattern_id().local_id == support::kPattern2
                                                                     ? icts::TerminalSemantic::kBranchBuffered
                                                                     : icts::TerminalSemantic::kLeafUnbuffered;
                                                        });

  ASSERT_EQ(frontier.size(), 2U);
  std::vector<unsigned> pattern_ids;
  pattern_ids.reserve(frontier.size());
  for (const auto& entry : frontier) {
    pattern_ids.push_back(entry.get_pattern_id().local_id);
  }
  std::ranges::sort(pattern_ids);
  EXPECT_EQ(pattern_ids, (std::vector<unsigned>{support::kPattern1, support::kPattern2}));
}

}  // namespace
}  // namespace icts_test
