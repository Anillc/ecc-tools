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

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "module/characterization/Frontier.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/SegmentCharTable.hh"
#include "module/characterization/support/CharacterizationTestSupport.hh"

namespace icts_test {
namespace {

namespace support = characterization;

auto MakePatternCompositionState(unsigned source_strength_rank, unsigned sink_strength_rank,
                                 icts::TerminalSemantic terminal_semantic = icts::TerminalSemantic::kLeafUnbuffered)
    -> icts::PatternCompositionState
{
  return icts::PatternCompositionState{
      .terminal_semantic = terminal_semantic,
      .monotonic_boundary_state = icts::MonotonicBoundaryState{
          .source = icts::BoundaryBufferState{.has_buffer = source_strength_rank != 0U, .strength_rank = source_strength_rank},
          .sink = icts::BoundaryBufferState{.has_buffer = sink_strength_rank != 0U, .strength_rank = sink_strength_rank},
      },
  };
}

class MonotonicSegmentPatternCombiner
{
 public:
  explicit MonotonicSegmentPatternCombiner(std::unordered_map<icts::PatternId, icts::PatternCompositionState> pattern_states,
                                           unsigned start_id = 1000U)
      : _pattern_states(std::move(pattern_states)), _next_id(start_id)
  {
  }

  auto canCompose(icts::PatternId upstream, icts::PatternId downstream) const -> bool
  {
    return _pattern_states.at(upstream).monotonic_boundary_state.canComposeWith(_pattern_states.at(downstream).monotonic_boundary_state);
  }

  auto combine(icts::PatternId upstream, icts::PatternId downstream) const -> icts::PatternId
  {
    const auto upstream_state = _pattern_states.at(upstream);
    const auto downstream_state = _pattern_states.at(downstream);

    const auto merged_pattern_id = icts::PatternId::segment(_next_id++);
    _pattern_states[merged_pattern_id] = icts::PatternCompositionState{
        .terminal_semantic = downstream_state.terminal_semantic,
        .monotonic_boundary_state
        = icts::MonotonicBoundaryState::compose(upstream_state.monotonic_boundary_state, downstream_state.monotonic_boundary_state),
    };
    return merged_pattern_id;
  }

 private:
  mutable std::unordered_map<icts::PatternId, icts::PatternCompositionState> _pattern_states;
  mutable unsigned _next_id = 0U;
};

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

TEST(SegmentJoinTest, ExactJoinRejectsNonMonotonicBoundaryStrengthIncrease)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0,
                                            support::kPower0p5,
                                            support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}));

  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0,
                                              support::kPower0p3,
                                              support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}));
  downstream.addChar(support::MakeSegmentChar(support::kSlew100, support::kSlew110, support::kCap50, support::kCap60, support::kDelay1p5,
                                              support::kPower0p2,
                                              support::SegmentShape{.pattern_id = support::kPattern3, .length_idx = support::kLength2000}));

  MonotonicSegmentPatternCombiner combiner({
      {icts::PatternId::segment(support::kPattern1), MakePatternCompositionState(3U, 1U)},
      {icts::PatternId::segment(support::kPattern2), MakePatternCompositionState(2U, 2U)},
      {icts::PatternId::segment(support::kPattern3), MakePatternCompositionState(1U, 1U)},
  });
  const auto result = upstream.concatWith(downstream, combiner).get_chars();

  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result.front().get_output_slew_idx(), support::kSlew110);
  EXPECT_DOUBLE_EQ(result.front().get_delay(), 2.5);
  EXPECT_DOUBLE_EQ(result.front().get_power(), 0.7);
}

TEST(SegmentJoinTest, SegmentStateFrontierKeepsDistinctMonotonicBoundaryStates)
{
  const auto weaker_sink_boundary = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000});
  const auto stronger_sink_boundary = support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap60, support::kDelay1p0, support::kPower0p2,
      support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength1000});

  const auto frontier = icts::BuildSegmentStateFrontier(std::vector<icts::SegmentChar>{weaker_sink_boundary, stronger_sink_boundary},
                                                        [](const icts::SegmentChar& entry) -> icts::PatternCompositionState {
                                                          if (entry.get_pattern_id().local_id == support::kPattern2) {
                                                            return MakePatternCompositionState(3U, 2U);
                                                          }
                                                          return MakePatternCompositionState(3U, 1U);
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

TEST(SegmentJoinTest, JoinSubtractsDownstreamSourceBoundarySwitchPower)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(support::MakeSegmentChar(
      support::kSlew80, support::kSlew100, support::kCap40, support::kCap50, support::kDelay1p0, support::kPower0p5,
      support::SegmentShape{.pattern_id = support::kPattern1, .length_idx = support::kLength1000}, 0.05));
  downstream.addChar(support::MakeSegmentChar(
      support::kSlew100, support::kSlew120, support::kCap50, support::kCap60, support::kDelay2p0, support::kPower0p3,
      support::SegmentShape{.pattern_id = support::kPattern2, .length_idx = support::kLength2000}, 0.10));

  const icts::SegmentPatternCombiner combiner(support::kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);
  EXPECT_DOUBLE_EQ(result.get_chars().front().get_power(), 0.7);
  EXPECT_DOUBLE_EQ(result.get_chars().front().get_source_boundary_net_switch_power(), 0.05);
}

}  // namespace
}  // namespace icts_test
