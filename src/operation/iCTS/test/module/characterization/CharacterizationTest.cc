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
 * @file CharacterizationTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Unit tests for CTS Characterization module.
 */

#include <gtest/gtest.h>

#include <vector>

#include "database/characterization/CharCore.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "module/characterization/HTreeTopologyCharTable.hh"
#include "module/characterization/HashJoinEngine.hh"
#include "module/characterization/PatternCombiner.hh"
#include "module/characterization/Pruner.hh"
#include "module/characterization/SegmentCharTable.hh"

namespace icts_test {
namespace {

constexpr unsigned kSlew80 = 80;
constexpr unsigned kSlew90 = 90;
constexpr unsigned kSlew100 = 100;
constexpr unsigned kSlew110 = 110;
constexpr unsigned kSlew120 = 120;
constexpr unsigned kSlew130 = 130;

constexpr unsigned kCap40 = 40;
constexpr unsigned kCap45 = 45;
constexpr unsigned kCap50 = 50;
constexpr unsigned kCap51 = 51;
constexpr unsigned kCap55 = 55;
constexpr unsigned kCap60 = 60;
constexpr unsigned kCap70 = 70;
constexpr unsigned kCap100 = 100;
constexpr unsigned kCap101 = 101;

constexpr double kDelay1p0 = 1.0;
constexpr double kDelay1p5 = 1.5;
constexpr double kDelay2p0 = 2.0;
constexpr double kDelay3p0 = 3.0;

constexpr double kPower0p2 = 0.2;
constexpr double kPower0p3 = 0.3;
constexpr double kPower0p4 = 0.4;
constexpr double kPower0p5 = 0.5;
constexpr double kPower0p6 = 0.6;
constexpr double kPower5p0 = 5.0;
constexpr double kPower10p0 = 10.0;
constexpr double kMergedPower0p8 = 0.8;
constexpr double kMergedPower1p1 = 1.1;
constexpr double kMergedPower20p0 = 20.0;

constexpr unsigned kPattern1 = 1;
constexpr unsigned kPattern2 = 2;
constexpr unsigned kPattern3 = 3;
constexpr unsigned kLength1000 = 1000;
constexpr unsigned kLength1500 = 1500;
constexpr unsigned kLength2000 = 2000;
constexpr unsigned kLength3000 = 3000;
constexpr unsigned kLengthSum3000 = 3000;
constexpr unsigned kBoundaryKey = 100;
constexpr unsigned kPatternId42 = 42;
constexpr unsigned kPackedKey12345678 = 0x12345678U;
constexpr unsigned kPackedKeyFFFF0000 = 0xFFFF0000U;
constexpr unsigned kPackedKey0000FFFF = 0x0000FFFFU;
constexpr unsigned kPackHigh1234 = 0x1234U;
constexpr unsigned kPackLow5678 = 0x5678U;
constexpr unsigned kPackWordFFFF = 0xFFFFU;
constexpr unsigned kPackWordZero = 0x0000U;

struct SegmentShape
{
  unsigned pattern_id = 0;
  unsigned length_idx = 0;
};

// Helper to create SegmentChar with given parameters
auto MakeSegmentChar(unsigned input_slew, unsigned output_slew, unsigned driven_cap, unsigned load_cap, double delay, double power,
                     SegmentShape shape) -> icts::SegmentChar
{
  const icts::CharCore core(input_slew, output_slew, driven_cap, load_cap, delay, power, icts::PatternId::segment(shape.pattern_id));
  return {core, shape.length_idx};
}

struct HTreeShape
{
  unsigned pattern_id = 0;
  unsigned levels = 0;
};

// Helper to create HTreeTopologyChar with given parameters
auto MakeHTreeChar(unsigned input_slew, unsigned output_slew, unsigned driven_cap, unsigned load_cap, double delay, double power,
                   HTreeShape shape) -> icts::HTreeTopologyChar
{
  const icts::CharCore core(input_slew, output_slew, driven_cap, load_cap, delay, power, icts::PatternId::topology(shape.pattern_id));
  return {core, shape.levels};
}

}  // namespace

// ============================================================================
// Segment Equal Join Tests
// ============================================================================

TEST(SegmentJoinTest, BasicEqualJoin)
{
  // Test: up.output_slew == down.input_slew AND up.load_cap == down.driven_cap
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  // Upstream: output_slew=100, load_cap=50
  upstream.addChar(MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap50, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000}));

  // Downstream: input_slew=100, driven_cap=50 (should match)
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower0p3, SegmentShape{kPattern2, kLength2000}));

  // Downstream: input_slew=100, driven_cap=55 (should NOT match)
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew130, kCap55, kCap70, kDelay3p0, kPower0p4, SegmentShape{kPattern3, kLength3000}));

  const icts::SegmentPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars().front();
  // Verify composition rules
  EXPECT_EQ(merged.get_input_slew_idx(), kSlew80);        // from upstream
  EXPECT_EQ(merged.get_output_slew_idx(), kSlew120);      // from downstream
  EXPECT_EQ(merged.get_driven_cap_idx(), kCap40);         // from upstream
  EXPECT_EQ(merged.get_load_cap_idx(), kCap60);           // from downstream
  EXPECT_DOUBLE_EQ(merged.get_delay(), kDelay3p0);        // 1.0 + 2.0
  EXPECT_DOUBLE_EQ(merged.get_power(), kMergedPower0p8);  // 0.5 + 0.3
  EXPECT_EQ(merged.get_length_idx(), kLengthSum3000);     // 1000 + 2000
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kSegmentPattern);
}

TEST(SegmentJoinTest, MultipleMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  // Two upstream entries with same output boundary
  upstream.addChar(MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap50, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000}));
  upstream.addChar(MakeSegmentChar(kSlew90, kSlew100, kCap45, kCap50, kDelay1p5, kPower0p6, SegmentShape{kPattern2, kLength1500}));

  // One downstream that matches both (input_slew=100, driven_cap=50)
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower0p3, SegmentShape{kPattern3, kLength2000}));

  const icts::SegmentPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 2U);  // Both upstream entries match
}

TEST(SegmentJoinTest, NoMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap50, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000}));
  // Downstream has different driven_cap (51 != 50)
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew120, kCap51, kCap60, kDelay2p0, kPower0p3, SegmentShape{kPattern2, kLength2000}));

  const icts::SegmentPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 0U);
}

// ============================================================================
// HTree /2 Cap Join Tests
// ============================================================================

TEST(HTreeJoinTest, HalfCapJoin)
{
  // Test: up.output_slew == down.input_slew AND floor(up.load_cap/2) == down.driven_cap
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  // Upstream: output_slew=100, load_cap=100 -> probe_key cap = 50
  upstream.addChar(MakeHTreeChar(kSlew80, kSlew100, kCap40, kCap100, kDelay1p0, kPower0p5, HTreeShape{kPattern1, 1}));

  // Downstream: input_slew=100, driven_cap=50 (should match: 100/2 == 50)
  downstream.addChar(MakeHTreeChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower0p3, HTreeShape{kPattern2, 1}));

  // Downstream: input_slew=100, driven_cap=51 (should NOT match)
  downstream.addChar(MakeHTreeChar(kSlew100, kSlew130, kCap51, kCap70, kDelay3p0, kPower0p4, HTreeShape{kPattern3, 1}));

  const icts::TopologyPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars().front();
  // Verify H-tree composition rules
  EXPECT_EQ(merged.get_input_slew_idx(), kSlew80);
  EXPECT_EQ(merged.get_output_slew_idx(), kSlew120);
  EXPECT_EQ(merged.get_driven_cap_idx(), kCap40);
  EXPECT_EQ(merged.get_load_cap_idx(), kCap60);
  EXPECT_DOUBLE_EQ(merged.get_delay(), kDelay3p0);  // 1.0 + 2.0
  // Power: upstream + 2 * downstream = 0.5 + 2*0.3 = 1.1
  EXPECT_DOUBLE_EQ(merged.get_power(), kMergedPower1p1);
  EXPECT_EQ(merged.get_levels(), 2U);  // 1 + 1
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kTopologyPattern);
}

TEST(HTreeJoinTest, OddCapHalving)
{
  // Test floor division: 101/2 = 50
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  // Upstream: load_cap=101 -> probe_key cap = 50 (floor division)
  upstream.addChar(MakeHTreeChar(kSlew80, kSlew100, kCap40, kCap101, kDelay1p0, kPower0p5, HTreeShape{kPattern1, 1}));

  // Downstream: driven_cap=50 (should match: 101/2 = 50)
  downstream.addChar(MakeHTreeChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower0p3, HTreeShape{kPattern2, 1}));

  const icts::TopologyPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 1U);
}

TEST(HTreeJoinTest, PowerDoubling)
{
  // Verify power = up.power + 2 * down.power
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  upstream.addChar(MakeHTreeChar(kSlew80, kSlew100, kCap40, kCap100, kDelay1p0, kPower10p0, HTreeShape{kPattern1, 1}));
  downstream.addChar(MakeHTreeChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower5p0, HTreeShape{kPattern2, 1}));

  const icts::TopologyPatternCombiner combiner(kBoundaryKey);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);
  // Power = 10.0 + 2 * 5.0 = 20.0
  EXPECT_DOUBLE_EQ(result.get_chars().front().get_power(), kMergedPower20p0);
}

// ============================================================================
// Pareto Pruner Tests
// ============================================================================

TEST(PrunerTest, DominationCheck)
{
  const icts::ParetoPruner<icts::SegmentChar> pruner;

  // Entry a: better in all metrics
  auto better_entry = MakeSegmentChar(kSlew80, kSlew90, kCap40, kCap60, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000});
  // Entry b: worse in all metrics
  auto worse_entry = MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap50, kDelay2p0, kPower0p6, SegmentShape{kPattern1, kLength1000});

  EXPECT_TRUE(pruner.dominates(better_entry, worse_entry));
  EXPECT_FALSE(pruner.dominates(worse_entry, better_entry));
}

TEST(PrunerTest, NonDomination)
{
  const icts::ParetoPruner<icts::SegmentChar> pruner;

  // Entry a: better slew, worse cap
  auto lower_slew_entry = MakeSegmentChar(kSlew80, kSlew90, kCap40, kCap50, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000});
  // Entry b: worse slew, better cap
  auto higher_cap_entry = MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap60, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000});

  // Neither dominates the other (Pareto non-comparable)
  EXPECT_FALSE(pruner.dominates(lower_slew_entry, higher_cap_entry));
  EXPECT_FALSE(pruner.dominates(higher_cap_entry, lower_slew_entry));
}

TEST(PrunerTest, WithPruning)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  // Upstream with same output boundary
  upstream.addChar(MakeSegmentChar(kSlew80, kSlew100, kCap40, kCap50, kDelay1p0, kPower0p5, SegmentShape{kPattern1, kLength1000}));

  // Two downstream - one dominates the other
  // Better: lower output_slew, higher load_cap, lower delay, lower power
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew110, kCap50, kCap70, kDelay1p5, kPower0p2, SegmentShape{kPattern2, kLength2000}));
  // Worse: higher output_slew, lower load_cap, higher delay, higher power
  downstream.addChar(MakeSegmentChar(kSlew100, kSlew120, kCap50, kCap60, kDelay2p0, kPower0p3, SegmentShape{kPattern2, kLength2000}));

  const icts::SegmentPatternCombiner combiner(kBoundaryKey);
  // Use InputBoundaryPruner to group by (input_slew, driven_cap)
  // since merged entries have different pattern IDs from combiner
  const icts::InputBoundaryPruner<icts::SegmentChar> pruner;

  auto result = upstream.concatWith(downstream, combiner, &pruner);

  // After pruning, only the non-dominated result should remain
  // Both merged entries have same input_slew=80 and driven_cap=40 (from upstream)
  // The first downstream produces a better result than the second
  EXPECT_EQ(result.size(), 1U);
  // The surviving entry should have the better output_slew
  EXPECT_EQ(result.get_chars().front().get_output_slew_idx(), kSlew110);
}

// ============================================================================
// Pattern ID Tests
// ============================================================================

TEST(PatternIdTest, DomainTagging)
{
  auto seg_id = icts::PatternId::segment(kPatternId42);
  auto topo_id = icts::PatternId::topology(kPatternId42);

  EXPECT_EQ(seg_id.domain, icts::PatternDomain::kSegmentPattern);
  EXPECT_EQ(topo_id.domain, icts::PatternDomain::kTopologyPattern);
  EXPECT_EQ(seg_id.local_id, kPatternId42);
  EXPECT_EQ(topo_id.local_id, kPatternId42);

  // Same local_id but different domain should not be equal
  EXPECT_NE(seg_id, topo_id);

  // Same domain and local_id should be equal
  auto seg_id_copy = icts::PatternId::segment(kPatternId42);
  EXPECT_EQ(seg_id, seg_id_copy);
}

TEST(PatternIdTest, PackUnique)
{
  auto seg_id = icts::PatternId::segment(kPatternId42);
  auto topo_id = icts::PatternId::topology(kPatternId42);

  // Packed values should be different due to domain
  EXPECT_NE(seg_id.pack(), topo_id.pack());
}

// ============================================================================
// Hash-Join Engine Pack Function Test
// ============================================================================

TEST(HashJoinEngineTest, PackFunction)
{
  unsigned key = icts::detail::Pack(kPackHigh1234, kPackLow5678);
  EXPECT_EQ(key, kPackedKey12345678);

  key = icts::detail::Pack(kPackWordFFFF, kPackWordZero);
  EXPECT_EQ(key, kPackedKeyFFFF0000);

  key = icts::detail::Pack(kPackWordZero, kPackWordFFFF);
  EXPECT_EQ(key, kPackedKey0000FFFF);
}

}  // namespace icts_test
