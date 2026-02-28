// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
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

// Helper to create SegmentChar with given parameters
icts::SegmentChar makeSegmentChar(unsigned input_slew, unsigned output_slew, unsigned driven_cap, unsigned load_cap, double delay,
                                  double power, unsigned pid, unsigned length_idx)
{
  icts::CharCore core(input_slew, output_slew, driven_cap, load_cap, delay, power, icts::PatternId::segment(pid));
  return icts::SegmentChar(core, length_idx);
}

// Helper to create HTreeTopologyChar with given parameters
icts::HTreeTopologyChar makeHTreeChar(unsigned input_slew, unsigned output_slew, unsigned driven_cap, unsigned load_cap, double delay,
                                      double power, unsigned pid, unsigned levels)
{
  icts::CharCore core(input_slew, output_slew, driven_cap, load_cap, delay, power, icts::PatternId::topology(pid));
  return icts::HTreeTopologyChar(core, levels);
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
  upstream.addChar(makeSegmentChar(80, 100, 40, 50, 1.0, 0.5, 1, 1000));

  // Downstream: input_slew=100, driven_cap=50 (should match)
  downstream.addChar(makeSegmentChar(100, 120, 50, 60, 2.0, 0.3, 2, 2000));

  // Downstream: input_slew=100, driven_cap=55 (should NOT match)
  downstream.addChar(makeSegmentChar(100, 130, 55, 70, 3.0, 0.4, 3, 3000));

  icts::SegmentPatternCombiner combiner(100);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars()[0];
  // Verify composition rules
  EXPECT_EQ(merged.get_input_slew_idx(), 80);    // from upstream
  EXPECT_EQ(merged.get_output_slew_idx(), 120);  // from downstream
  EXPECT_EQ(merged.get_driven_cap_idx(), 40);    // from upstream
  EXPECT_EQ(merged.get_load_cap_idx(), 60);      // from downstream
  EXPECT_DOUBLE_EQ(merged.get_delay(), 3.0);     // 1.0 + 2.0
  EXPECT_DOUBLE_EQ(merged.get_power(), 0.8);     // 0.5 + 0.3
  EXPECT_EQ(merged.get_length_idx(), 3000U);     // 1000 + 2000
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kSegmentPattern);
}

TEST(SegmentJoinTest, MultipleMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  // Two upstream entries with same output boundary
  upstream.addChar(makeSegmentChar(80, 100, 40, 50, 1.0, 0.5, 1, 1000));
  upstream.addChar(makeSegmentChar(90, 100, 45, 50, 1.5, 0.6, 2, 1500));

  // One downstream that matches both (input_slew=100, driven_cap=50)
  downstream.addChar(makeSegmentChar(100, 120, 50, 60, 2.0, 0.3, 3, 2000));

  icts::SegmentPatternCombiner combiner(100);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 2U);  // Both upstream entries match
}

TEST(SegmentJoinTest, NoMatches)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  upstream.addChar(makeSegmentChar(80, 100, 40, 50, 1.0, 0.5, 1, 1000));
  // Downstream has different driven_cap (51 != 50)
  downstream.addChar(makeSegmentChar(100, 120, 51, 60, 2.0, 0.3, 2, 2000));

  icts::SegmentPatternCombiner combiner(100);
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
  upstream.addChar(makeHTreeChar(80, 100, 40, 100, 1.0, 0.5, 1, 1));

  // Downstream: input_slew=100, driven_cap=50 (should match: 100/2 == 50)
  downstream.addChar(makeHTreeChar(100, 120, 50, 60, 2.0, 0.3, 2, 1));

  // Downstream: input_slew=100, driven_cap=51 (should NOT match)
  downstream.addChar(makeHTreeChar(100, 130, 51, 70, 3.0, 0.4, 3, 1));

  icts::TopologyPatternCombiner combiner(100);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);

  const auto& merged = result.get_chars()[0];
  // Verify H-tree composition rules
  EXPECT_EQ(merged.get_input_slew_idx(), 80);
  EXPECT_EQ(merged.get_output_slew_idx(), 120);
  EXPECT_EQ(merged.get_driven_cap_idx(), 40);
  EXPECT_EQ(merged.get_load_cap_idx(), 60);
  EXPECT_DOUBLE_EQ(merged.get_delay(), 3.0);  // 1.0 + 2.0
  // Power: upstream + 2 * downstream = 0.5 + 2*0.3 = 1.1
  EXPECT_DOUBLE_EQ(merged.get_power(), 1.1);
  EXPECT_EQ(merged.get_levels(), 2U);  // 1 + 1
  EXPECT_EQ(merged.get_pattern_id().domain, icts::PatternDomain::kTopologyPattern);
}

TEST(HTreeJoinTest, OddCapHalving)
{
  // Test floor division: 101/2 = 50
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  // Upstream: load_cap=101 -> probe_key cap = 50 (floor division)
  upstream.addChar(makeHTreeChar(80, 100, 40, 101, 1.0, 0.5, 1, 1));

  // Downstream: driven_cap=50 (should match: 101/2 = 50)
  downstream.addChar(makeHTreeChar(100, 120, 50, 60, 2.0, 0.3, 2, 1));

  icts::TopologyPatternCombiner combiner(100);
  auto result = upstream.concatWith(downstream, combiner);

  EXPECT_EQ(result.size(), 1U);
}

TEST(HTreeJoinTest, PowerDoubling)
{
  // Verify power = up.power + 2 * down.power
  icts::HTreeTopologyCharTable upstream;
  icts::HTreeTopologyCharTable downstream;

  upstream.addChar(makeHTreeChar(80, 100, 40, 100, 1.0, 10.0, 1, 1));
  downstream.addChar(makeHTreeChar(100, 120, 50, 60, 2.0, 5.0, 2, 1));

  icts::TopologyPatternCombiner combiner(100);
  auto result = upstream.concatWith(downstream, combiner);

  ASSERT_EQ(result.size(), 1U);
  // Power = 10.0 + 2 * 5.0 = 20.0
  EXPECT_DOUBLE_EQ(result.get_chars()[0].get_power(), 20.0);
}

// ============================================================================
// Pareto Pruner Tests
// ============================================================================

TEST(PrunerTest, DominationCheck)
{
  icts::ParetoPruner<icts::SegmentChar> pruner;

  // Entry a: better in all metrics
  auto a = makeSegmentChar(80, 90, 40, 60, 1.0, 0.5, 1, 1000);
  // Entry b: worse in all metrics
  auto b = makeSegmentChar(80, 100, 40, 50, 2.0, 0.6, 1, 1000);

  EXPECT_TRUE(pruner.dominates(a, b));
  EXPECT_FALSE(pruner.dominates(b, a));
}

TEST(PrunerTest, NonDomination)
{
  icts::ParetoPruner<icts::SegmentChar> pruner;

  // Entry a: better slew, worse cap
  auto a = makeSegmentChar(80, 90, 40, 50, 1.0, 0.5, 1, 1000);
  // Entry b: worse slew, better cap
  auto b = makeSegmentChar(80, 100, 40, 60, 1.0, 0.5, 1, 1000);

  // Neither dominates the other (Pareto non-comparable)
  EXPECT_FALSE(pruner.dominates(a, b));
  EXPECT_FALSE(pruner.dominates(b, a));
}

TEST(PrunerTest, WithPruning)
{
  icts::SegmentCharTable upstream;
  icts::SegmentCharTable downstream;

  // Upstream with same output boundary
  upstream.addChar(makeSegmentChar(80, 100, 40, 50, 1.0, 0.5, 1, 1000));

  // Two downstream - one dominates the other
  // Better: lower output_slew, higher load_cap, lower delay, lower power
  downstream.addChar(makeSegmentChar(100, 110, 50, 70, 1.5, 0.2, 2, 2000));
  // Worse: higher output_slew, lower load_cap, higher delay, higher power
  downstream.addChar(makeSegmentChar(100, 120, 50, 60, 2.0, 0.3, 2, 2000));

  icts::SegmentPatternCombiner combiner(100);
  // Use InputBoundaryPruner to group by (input_slew, driven_cap)
  // since merged entries have different pattern IDs from combiner
  icts::InputBoundaryPruner<icts::SegmentChar> pruner;

  auto result = upstream.concatWith(downstream, combiner, &pruner);

  // After pruning, only the non-dominated result should remain
  // Both merged entries have same input_slew=80 and driven_cap=40 (from upstream)
  // The first downstream produces a better result than the second
  EXPECT_EQ(result.size(), 1U);
  // The surviving entry should have the better output_slew
  EXPECT_EQ(result.get_chars()[0].get_output_slew_idx(), 110);
}

// ============================================================================
// Pattern ID Tests
// ============================================================================

TEST(PatternIdTest, DomainTagging)
{
  auto seg_id = icts::PatternId::segment(42);
  auto topo_id = icts::PatternId::topology(42);

  EXPECT_EQ(seg_id.domain, icts::PatternDomain::kSegmentPattern);
  EXPECT_EQ(topo_id.domain, icts::PatternDomain::kTopologyPattern);
  EXPECT_EQ(seg_id.local_id, 42U);
  EXPECT_EQ(topo_id.local_id, 42U);

  // Same local_id but different domain should not be equal
  EXPECT_NE(seg_id, topo_id);

  // Same domain and local_id should be equal
  auto seg_id2 = icts::PatternId::segment(42);
  EXPECT_EQ(seg_id, seg_id2);
}

TEST(PatternIdTest, PackUnique)
{
  auto seg_id = icts::PatternId::segment(42);
  auto topo_id = icts::PatternId::topology(42);

  // Packed values should be different due to domain
  EXPECT_NE(seg_id.pack(), topo_id.pack());
}

// ============================================================================
// Hash-Join Engine Pack Function Test
// ============================================================================

TEST(HashJoinEngineTest, PackFunction)
{
  unsigned key = icts::detail::pack(0x1234, 0x5678);
  EXPECT_EQ(key, 0x12345678U);

  key = icts::detail::pack(0xFFFF, 0x0000);
  EXPECT_EQ(key, 0xFFFF0000U);

  key = icts::detail::pack(0x0000, 0xFFFF);
  EXPECT_EQ(key, 0x0000FFFFU);
}

}  // namespace icts_test
