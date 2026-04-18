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
 * @file CharacterizationRealTechExactRegressionTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Exact compose and exact join regression coverage on real-tech assets.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/SegmentChar.hh"
#include "module/characterization/CharBuilder.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

namespace realtech_support = characterization::realtech;

TEST(CharacterizationRealTechExactRegressionTest, ExactComposeAndExactJoinRemainUsable)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("exact_regression", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  const auto usable_buffers = realtech_support::CollectUsableBufferMasters(realtech_support::CollectConfiguredBufferLimitInfo());
  if (usable_buffers.empty()) {
    GTEST_SKIP() << "No configured buffer has both slew and cap support via port or table limits.";
  }

  icts::CharBuilder builder;
  builder.init();
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  const auto lattice_summary = realtech_support::SummarizeSegmentCharLattice(builder.get_segment_chars(), builder);
  EXPECT_EQ(lattice_summary.out_of_range_entries, 0U) << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder);
  EXPECT_LE(lattice_summary.max_length_idx, builder.get_wire_length_iterations());
  EXPECT_LE(lattice_summary.max_input_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_output_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_driven_cap_idx, builder.get_cap_steps());
  EXPECT_LE(lattice_summary.max_load_cap_idx, builder.get_cap_steps());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_25_idx = realtech_support::MakeLengthIndex(realtech_support::kExactLeafLevelLengthUm, grid.length_step_um);
  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kExactMidLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kExactRootLevelLengthUm, grid.length_step_um);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars());
  ASSERT_TRUE(frontier_by_length.contains(length_25_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_25_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  unsigned next_segment_pattern_id = realtech_support::FindNextSegmentPatternId(builder.get_segment_chars());
  auto exact_segment_100_raw = realtech_support::ComposeSegmentEntriesExact(frontier_by_length.at(length_50_idx),
                                                                            frontier_by_length.at(length_50_idx), next_segment_pattern_id);
  ASSERT_FALSE(exact_segment_100_raw.empty());
  auto exact_segment_100_frontier = realtech_support::BuildInputBoundaryFrontier(exact_segment_100_raw);
  ASSERT_FALSE(exact_segment_100_frontier.empty());
  EXPECT_TRUE(std::ranges::all_of(exact_segment_100_frontier, [length_100_idx](const icts::SegmentChar& entry) -> bool {
    return entry.get_length_idx() == length_100_idx;
  }));

  unsigned next_topology_pattern_id = 0U;

  realtech_support::HTreeStageSummary leaf_stage;
  leaf_stage.label = "leaf_25um";
  leaf_stage.raw_entries = realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_25_idx), next_topology_pattern_id);
  leaf_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(leaf_stage.raw_entries);
  ASSERT_FALSE(leaf_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary mid_stage;
  mid_stage.label = "mid_50um_to_25um";
  mid_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_50_idx), next_topology_pattern_id), leaf_stage.frontier_entries,
      next_topology_pattern_id);
  mid_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(mid_stage.raw_entries);
  ASSERT_FALSE(mid_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary root_stage;
  root_stage.label = "root_100um_to_50um_to_25um";
  root_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_100_idx), next_topology_pattern_id), mid_stage.frontier_entries,
      next_topology_pattern_id);
  root_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(root_stage.raw_entries);
  ASSERT_FALSE(root_stage.frontier_entries.empty());

  EXPECT_GE(exact_segment_100_raw.size(), exact_segment_100_frontier.size());
  EXPECT_GE(mid_stage.raw_entries.size(), mid_stage.frontier_entries.size());
  EXPECT_GE(root_stage.raw_entries.size(), root_stage.frontier_entries.size());

  const auto best_exact_htree = realtech_support::SelectBestHTreeChar(root_stage.frontier_entries);
  if (!best_exact_htree.has_value()) {
    GTEST_FAIL() << "Failed to select exact H-tree characterization entry.";
    return;
  }

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(3);
  report_stream << "scenario=exact_regression\n";
  report_stream << "usable_buffers=" << realtech_support::JoinStrings(usable_buffers) << "\n";
  report_stream << "segment_char_lattice=" << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder) << "\n";
  report_stream << "observed_sample_bounds{output_slew_overflow_samples=" << builder.get_output_slew_overflow_samples()
                << ",max_observed_output_slew_ns=" << builder.get_max_observed_output_slew_ns()
                << ",max_observed_output_slew_idx=" << builder.get_max_observed_output_slew_idx()
                << ",driven_cap_overflow_samples=" << builder.get_driven_cap_overflow_samples()
                << ",max_observed_driven_cap_pf=" << builder.get_max_observed_driven_cap_pf()
                << ",max_observed_driven_cap_idx=" << builder.get_max_observed_driven_cap_idx() << "}\n";
  report_stream << "exact_segment_compose{lhs=50um,rhs=50um,target=100um,raw_count=" << exact_segment_100_raw.size()
                << ",frontier_count=" << exact_segment_100_frontier.size() << "}\n";
  report_stream << "exact_htree_frontier_counts{leaf=" << leaf_stage.frontier_entries.size() << ",mid=" << mid_stage.frontier_entries.size()
                << ",root=" << root_stage.frontier_entries.size() << "}\n";
  realtech_support::AppendExamples(
      report_stream, "exact_segment_100_example=", exact_segment_100_frontier,
      [&](const icts::SegmentChar& entry) -> std::string { return realtech_support::FormatSegmentChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_leaf_example=", leaf_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_mid_example=", mid_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "exact_htree_root_example=", root_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  report_stream << "best_exact_htree_char=" << realtech_support::FormatHTreeChar(best_exact_htree.value(), grid) << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("exact_regression", "exact_regression_report.txt", report_stream.str()));
}

}  // namespace
}  // namespace icts_test
