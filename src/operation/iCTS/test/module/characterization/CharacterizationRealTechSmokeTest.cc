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
 * @file CharacterizationRealTechSmokeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Smoke coverage for manual H-tree composition on real-tech assets.
 */

#include <gtest/gtest.h>

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(ICTS_ENABLE_SLOW_REALTECH_REGRESSION) && ICTS_ENABLE_SLOW_REALTECH_REGRESSION
#include "BufferingPattern.hh"
#endif
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/config/Config.hh"
#include "module/characterization/CharBuilder.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

#ifndef ICTS_ENABLE_SLOW_REALTECH_REGRESSION
#define ICTS_ENABLE_SLOW_REALTECH_REGRESSION 0
#endif

namespace realtech_support = characterization::realtech;

TEST(CharacterizationRealTechSmokeTest, ManualHTreeCompositionProducesInspectableReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("smoke_manual_htree", std::nullopt, 0.0, 0.0); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  const auto buffer_infos = realtech_support::CollectConfiguredBufferLimitInfo();
  const auto usable_buffers = realtech_support::CollectUsableBufferMasters(buffer_infos);
  if (usable_buffers.empty()) {
    GTEST_SKIP() << "No configured buffer has both slew and cap support via port or table limits.";
  }

  const double expected_max_slew = realtech_support::MinPositiveResolvedLimit(buffer_infos, usable_buffers, true);
  const double expected_max_cap = realtech_support::MinPositiveResolvedLimit(buffer_infos, usable_buffers, false);
  ASSERT_GT(expected_max_slew, 0.0);
  ASSERT_GT(expected_max_cap, 0.0);

  icts::CharBuilder builder;
  builder.init(realtech_support::MakeRuntimeCharBuilderInitOptions());

  EXPECT_DOUBLE_EQ(builder.get_max_slew(), expected_max_slew);
  EXPECT_DOUBLE_EQ(builder.get_max_cap(), expected_max_cap);
  EXPECT_DOUBLE_EQ(builder.get_wirelength_unit_um(), realtech_support::kRealTechCharWirelengthUnitUm);
  EXPECT_EQ(builder.get_wirelength_iterations(), realtech_support::kRealTechCharWirelengthIterations);

  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  EXPECT_GT(realtech_support::CountPositivePower(builder.get_segment_chars()), 0U);
  const auto lattice_summary = realtech_support::SummarizeSegmentCharLattice(builder.get_segment_chars(), builder);
  EXPECT_EQ(lattice_summary.out_of_range_entries, 0U) << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder);
  EXPECT_LE(lattice_summary.max_length_idx, builder.get_wirelength_iterations());
  EXPECT_LE(lattice_summary.max_input_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_output_slew_idx, builder.get_slew_steps());
  EXPECT_LE(lattice_summary.max_driven_cap_idx, builder.get_cap_steps());
  EXPECT_LE(lattice_summary.max_load_cap_idx, builder.get_cap_steps());

  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kLeafLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kMidLevelLengthUm, grid.length_step_um);
  const unsigned length_200_idx = realtech_support::MakeLengthIndex(realtech_support::kRootLevelLengthUm, grid.length_step_um);
  ASSERT_GT(length_50_idx, 0U);
  ASSERT_GT(length_100_idx, 0U);
  ASSERT_GT(length_200_idx, 0U);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  std::string synthesized_200_mode = "characterized";
  if (!frontier_by_length.contains(length_200_idx) || frontier_by_length.at(length_200_idx).empty()) {
    synthesized_200_mode = "exact_compose";
    ASSERT_TRUE(realtech_support::SynthesizeSegmentFrontierExactOnly(frontier_by_length, length_200_idx, segment_context));
  }

  struct HTreeFlowResult
  {
    std::vector<icts::SegmentChar> leaf_candidates;
    std::vector<icts::SegmentChar> mid_candidates;
    std::vector<icts::SegmentChar> root_candidates;
    realtech_support::HTreeStageSummary leaf_stage;
    realtech_support::HTreeStageSummary mid_stage;
    realtech_support::HTreeStageSummary root_stage;
    std::optional<icts::HTreeTopologyChar> best_char;
  };

  const auto run_htree_flow
      = [](const std::vector<icts::SegmentChar>& leaf_candidates, const std::vector<icts::SegmentChar>& mid_candidates,
           const std::vector<icts::SegmentChar>& root_candidates,
           const realtech_support::SegmentFrontierContext& segment_context) -> HTreeFlowResult {
    HTreeFlowResult result;
    result.leaf_candidates = leaf_candidates;
    result.mid_candidates = mid_candidates;
    result.root_candidates = root_candidates;

    realtech_support::HTreeFrontierContext htree_context;

    result.leaf_stage.label = "leaf_50um";
    result.leaf_stage.raw_entries = realtech_support::MakeHTreeSeedEntries(result.leaf_candidates, segment_context, htree_context);
    result.leaf_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(result.leaf_stage.raw_entries, htree_context);

    result.mid_stage.label = "mid_100um_to_50um";
    result.mid_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
        realtech_support::MakeHTreeSeedEntries(result.mid_candidates, segment_context, htree_context), result.leaf_stage.frontier_entries,
        htree_context);
    result.mid_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(result.mid_stage.raw_entries, htree_context);

    result.root_stage.label = "root_200um_to_100um_to_50um";
    result.root_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
        realtech_support::MakeHTreeSeedEntries(result.root_candidates, segment_context, htree_context), result.mid_stage.frontier_entries,
        htree_context);
    result.root_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(result.root_stage.raw_entries, htree_context);
    result.best_char = realtech_support::SelectBestHTreeChar(result.root_stage.frontier_entries);
    return result;
  };

  const auto leaf_candidates = frontier_by_length.at(length_50_idx);
  const auto mid_candidates = frontier_by_length.at(length_100_idx);
  const auto root_candidates = frontier_by_length.at(length_200_idx);
  ASSERT_FALSE(leaf_candidates.empty());
  ASSERT_FALSE(mid_candidates.empty());
  ASSERT_FALSE(root_candidates.empty());

  const auto exact_flow = run_htree_flow(leaf_candidates, mid_candidates, root_candidates, segment_context);
  ASSERT_FALSE(exact_flow.leaf_stage.frontier_entries.empty());
  ASSERT_FALSE(exact_flow.mid_stage.frontier_entries.empty());
  ASSERT_FALSE(exact_flow.root_stage.frontier_entries.empty());

  if (!exact_flow.best_char.has_value()) {
    GTEST_FAIL() << "Failed to select exact H-tree characterization entry.";
    return;
  }

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(3);
  report_stream << "scenario=smoke_manual_htree\n";
  report_stream << "configured_buffers=" << realtech_support::JoinStrings(CONFIG_INST.get_buffer_types()) << "\n";
  report_stream << "usable_buffers=" << realtech_support::JoinStrings(usable_buffers) << "\n";
  report_stream << "resolved_max_slew_ns=" << builder.get_max_slew() << "\n";
  report_stream << "resolved_max_cap_pf=" << builder.get_max_cap() << "\n";
  report_stream << "wirelength_unit_um=" << builder.get_wirelength_unit_um() << "\n";
  report_stream << "wirelength_iterations=" << builder.get_wirelength_iterations() << "\n";
  report_stream << "segment_char_lattice=" << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder) << "\n";
  report_stream << "observed_sample_bounds{output_slew_overflow_samples=" << builder.get_output_slew_overflow_samples()
                << ",max_observed_output_slew_ns=" << builder.get_max_observed_output_slew_ns()
                << ",max_observed_output_slew_idx=" << builder.get_max_observed_output_slew_idx()
                << ",driven_cap_overflow_samples=" << builder.get_driven_cap_overflow_samples()
                << ",driven_cap_overflow_load_points=" << builder.get_driven_cap_overflow_load_points()
                << ",max_observed_driven_cap_pf=" << builder.get_max_observed_driven_cap_pf()
                << ",max_observed_driven_cap_idx=" << builder.get_max_observed_driven_cap_idx() << "}\n";
  report_stream << "segment_200_source=" << synthesized_200_mode << "\n";
  report_stream << "positive_power_segment_chars=" << realtech_support::CountPositivePower(builder.get_segment_chars()) << "/"
                << builder.get_segment_chars().size() << "\n";
  report_stream << "segment_frontier_counts{50um=" << frontier_by_length.at(length_50_idx).size()
                << ",100um=" << frontier_by_length.at(length_100_idx).size() << ",200um=" << frontier_by_length.at(length_200_idx).size()
                << "}\n";
  report_stream << "htree_exact_counts{leaf_raw=" << exact_flow.leaf_stage.raw_entries.size()
                << ",leaf_frontier=" << exact_flow.leaf_stage.frontier_entries.size()
                << ",mid_raw=" << exact_flow.mid_stage.raw_entries.size()
                << ",mid_frontier=" << exact_flow.mid_stage.frontier_entries.size()
                << ",root_raw=" << exact_flow.root_stage.raw_entries.size()
                << ",root_frontier=" << exact_flow.root_stage.frontier_entries.size() << "}\n";
  realtech_support::AppendExamples(
      report_stream, "segment_50_example=", frontier_by_length.at(length_50_idx),
      [&](const icts::SegmentChar& entry) -> std::string { return realtech_support::FormatSegmentChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "segment_100_example=", frontier_by_length.at(length_100_idx),
      [&](const icts::SegmentChar& entry) -> std::string { return realtech_support::FormatSegmentChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "segment_200_example=", frontier_by_length.at(length_200_idx),
      [&](const icts::SegmentChar& entry) -> std::string { return realtech_support::FormatSegmentChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_exact_leaf_example=", exact_flow.leaf_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_exact_mid_example=", exact_flow.mid_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_exact_root_example=", exact_flow.root_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  report_stream << "best_exact_htree_char=" << realtech_support::FormatHTreeChar(exact_flow.best_char.value(), grid) << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("smoke_manual_htree", "smoke_manual_htree_report.txt", report_stream.str()));
}

#if ICTS_ENABLE_SLOW_REALTECH_REGRESSION
TEST(CharacterizationRealTechSmokeTest, TerminalBranchBufferedPatternsRemainAvailableIndependentOfBuildPolicy)
{
  auto collect_terminal_pattern_count = [](bool force_branch_buffer) -> std::optional<unsigned> {
    realtech_support::RealTechCharSession char_session;
    if (const auto prepare_error = char_session.prepare(force_branch_buffer ? "branch_buffer_on" : "branch_buffer_off", std::nullopt, 0.0,
                                                        0.0, false, force_branch_buffer);
        prepare_error.has_value()) {
      return std::nullopt;
    }

    icts::CharBuilder builder;
    builder.init(realtech_support::MakeRuntimeCharBuilderInitOptions());
    builder.build();
    if (builder.get_segment_chars().empty()) {
      return std::nullopt;
    }
    const auto lattice_summary = realtech_support::SummarizeSegmentCharLattice(builder.get_segment_chars(), builder);
    EXPECT_EQ(lattice_summary.out_of_range_entries, 0U) << realtech_support::FormatSegmentCharLatticeSummary(lattice_summary, builder);
    if (lattice_summary.out_of_range_entries != 0U) {
      return std::nullopt;
    }

    const unsigned target_length_idx = builder.get_wirelength_iterations();
    unsigned terminal_pattern_count = 0U;
    for (const auto& pattern : builder.get_buffering_patterns()) {
      if (pattern.get_length_idx() != target_length_idx || !pattern.hasTerminalBranchBuffer()) {
        continue;
      }
      if (pattern.get_buffer_positions().empty() || pattern.get_buffer_positions().back() != 1.0) {
        return std::nullopt;
      }
      ++terminal_pattern_count;
    }

    return terminal_pattern_count;
  };

  const auto disabled_count = collect_terminal_pattern_count(false);
  const auto enabled_count = collect_terminal_pattern_count(true);
  if (!disabled_count.has_value() || !enabled_count.has_value()) {
    GTEST_SKIP() << "Real-tech assets cannot exercise terminal branch-buffered characterization.";
    return;
  }

  EXPECT_GT(*disabled_count, 0U);
  EXPECT_EQ(*disabled_count, *enabled_count);
  EXPECT_GT(*enabled_count, 0U);
}
#endif

}  // namespace
}  // namespace icts_test
