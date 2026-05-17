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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CharTimingLookupTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Unit tests for characterization timing lookup interpolation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "buffer_sizing/CharTimingLookup.hh"
#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/CharCore.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/characterization/ValueLattice.hh"

namespace icts_test {
namespace {

auto MakeChar(icts::PatternId pattern_id, unsigned slew_idx, unsigned cap_idx, double delay_ns, unsigned output_slew_idx,
              unsigned length_idx = 1U) -> icts::SegmentChar
{
  return icts::SegmentChar(icts::CharCore(slew_idx, output_slew_idx, cap_idx, cap_idx, delay_ns, 0.0, pattern_id, 0.0), length_idx);
}

auto MakeLookup() -> icts::buffer_sizing::CharTimingLookup
{
  const auto wire_pattern = icts::PatternId::segment(1U);
  const auto buffer_pattern = icts::PatternId::segment(2U);
  const std::vector<icts::BufferingPattern> patterns{
      icts::BufferingPattern(1U, wire_pattern, {}, {}, false),
      icts::BufferingPattern(1U, buffer_pattern, {1.0}, {"BUF_X2"}, true),
  };
  std::vector<icts::SegmentChar> chars;
  for (unsigned slew_idx = 1U; slew_idx <= 2U; ++slew_idx) {
    for (unsigned cap_idx = 1U; cap_idx <= 2U; ++cap_idx) {
      chars.push_back(MakeChar(wire_pattern, slew_idx, cap_idx, 0.10 * static_cast<double>(slew_idx) + 0.20 * cap_idx, slew_idx));
      chars.push_back(MakeChar(wire_pattern, slew_idx, cap_idx, 0.10 * static_cast<double>(slew_idx) + 0.20 * cap_idx, slew_idx, 2U));
      chars.push_back(MakeChar(buffer_pattern, slew_idx, cap_idx, 0.30 * static_cast<double>(slew_idx) + 0.40 * cap_idx,
                               std::min(slew_idx + cap_idx - 1U, 2U)));
      chars.push_back(MakeChar(buffer_pattern, slew_idx, cap_idx, 0.30 * static_cast<double>(slew_idx) + 0.40 * cap_idx,
                               std::min(slew_idx + cap_idx - 1U, 2U), 2U));
    }
  }
  return icts::buffer_sizing::CharTimingLookup(chars, patterns, icts::UniformValueLattice(10.0, 2U), icts::UniformValueLattice(0.10, 2U),
                                               icts::UniformValueLattice(0.20, 2U));
}

TEST(CharTimingLookupTest, BilinearInterpolatesWireDelayAndSlew)
{
  const auto lookup = MakeLookup();
  const auto result = lookup.lookup(icts::buffer_sizing::CharTimingQuery{
      .arc_kind = icts::buffer_sizing::CharArcKind::kWire,
      .terminal_buffer_master = "",
      .length_um = 10.0,
      .input_slew_ns = 0.15,
      .load_cap_pf = 0.30,
  });

  ASSERT_TRUE(result.success);
  EXPECT_FALSE(result.clamped);
  EXPECT_DOUBLE_EQ(result.delay_ns, 0.45);
  EXPECT_DOUBLE_EQ(result.output_slew_ns, 0.15);
}

TEST(CharTimingLookupTest, SelectsTerminalBufferPatternByMaster)
{
  const auto lookup = MakeLookup();
  const auto result = lookup.lookup(icts::buffer_sizing::CharTimingQuery{
      .arc_kind = icts::buffer_sizing::CharArcKind::kTerminalBuffer,
      .terminal_buffer_master = "BUF_X2",
      .length_um = 10.0,
      .input_slew_ns = 0.15,
      .load_cap_pf = 0.30,
  });

  ASSERT_TRUE(result.success);
  EXPECT_DOUBLE_EQ(result.delay_ns, 1.05);
  EXPECT_DOUBLE_EQ(result.output_slew_ns, 0.175);
}

TEST(CharTimingLookupTest, ReportsMissingTerminalBufferCoverage)
{
  const auto lookup = MakeLookup();
  const auto result = lookup.lookup(icts::buffer_sizing::CharTimingQuery{
      .arc_kind = icts::buffer_sizing::CharArcKind::kTerminalBuffer,
      .terminal_buffer_master = "BUF_X8",
      .length_um = 10.0,
      .input_slew_ns = 0.15,
      .load_cap_pf = 0.30,
  });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.failure_reason, "missing_lattice_sample");
}

TEST(CharTimingLookupTest, ClampsLookupBoundaryButKeepsResultTagged)
{
  const auto lookup = MakeLookup();
  const auto result = lookup.lookup(icts::buffer_sizing::CharTimingQuery{
      .arc_kind = icts::buffer_sizing::CharArcKind::kWire,
      .terminal_buffer_master = "",
      .length_um = 20.0,
      .input_slew_ns = 1.0,
      .load_cap_pf = 1.0,
  });

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.clamped);
  EXPECT_EQ(result.length_idx, 2U);
  EXPECT_DOUBLE_EQ(result.input_slew_ns, 0.20);
  EXPECT_DOUBLE_EQ(result.load_cap_pf, 0.40);
}

TEST(CharTimingLookupTest, ComposesRequestsBeyondLengthLatticeWithUnitChar)
{
  const auto lookup = MakeLookup();
  const auto result = lookup.lookup(icts::buffer_sizing::CharTimingQuery{
      .arc_kind = icts::buffer_sizing::CharArcKind::kWire,
      .terminal_buffer_master = "",
      .length_um = 30.0,
      .input_slew_ns = 0.10,
      .load_cap_pf = 0.20,
  });

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.clamped);
  EXPECT_GT(result.delay_ns, 0.60);
  EXPECT_EQ(result.length_idx, std::numeric_limits<unsigned>::max());
}

}  // namespace
}  // namespace icts_test
