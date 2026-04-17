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
 * @brief Smoke coverage for manual H-tree composition on real-tech assets.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/config/Config.hh"
#include "module/characterization/CharBuilder.hh"
#include "module/characterization/support/CharacterizationRealTechTestSupport.hh"

namespace icts_test {
namespace {

namespace realtech_support = characterization::realtech;

constexpr std::size_t kManualCappedCandidatesPerBoundaryGroup = 2U;

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
  builder.init();

  EXPECT_DOUBLE_EQ(builder.get_max_slew(), expected_max_slew);
  EXPECT_DOUBLE_EQ(builder.get_max_cap(), expected_max_cap);
  EXPECT_DOUBLE_EQ(builder.get_wire_length_unit_um(), realtech_support::kRealTechCharWireLengthUnitUm);
  EXPECT_EQ(builder.get_wire_length_iterations(), realtech_support::kRealTechCharWireLengthIterations);

  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  EXPECT_GT(realtech_support::CountPositivePower(builder.get_segment_chars()), 0U);

  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kLeafLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kMidLevelLengthUm, grid.length_step_um);
  const unsigned length_200_idx = realtech_support::MakeLengthIndex(realtech_support::kRootLevelLengthUm, grid.length_step_um);
  ASSERT_GT(length_50_idx, 0U);
  ASSERT_GT(length_100_idx, 0U);
  ASSERT_GT(length_200_idx, 0U);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars());
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  std::string synthesized_200_mode = "characterized";
  unsigned next_segment_pattern_id = realtech_support::FindNextSegmentPatternId(builder.get_segment_chars());
  if (!frontier_by_length.contains(length_200_idx) || frontier_by_length.at(length_200_idx).empty()) {
    synthesized_200_mode = "exact_compose";
    if (!realtech_support::SynthesizeSegmentFrontierExactOnly(frontier_by_length, length_200_idx, next_segment_pattern_id)) {
      frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars());
      next_segment_pattern_id = realtech_support::FindNextSegmentPatternId(builder.get_segment_chars());
      ASSERT_TRUE(realtech_support::SynthesizeSegmentFrontierIfMissing(frontier_by_length, length_200_idx, next_segment_pattern_id));
      synthesized_200_mode = "relaxed_compose";
    }
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
           const std::vector<icts::SegmentChar>& root_candidates) -> HTreeFlowResult {
    HTreeFlowResult result;
    result.leaf_candidates = leaf_candidates;
    result.mid_candidates = mid_candidates;
    result.root_candidates = root_candidates;

    unsigned next_topology_pattern_id = 0U;

    result.leaf_stage.label = "leaf_50um";
    result.leaf_stage.raw_entries = realtech_support::MakeHTreeSeedEntries(result.leaf_candidates, next_topology_pattern_id);
    result.leaf_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(result.leaf_stage.raw_entries);

    result.mid_stage.label = "mid_100um_to_50um";
    result.mid_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
        realtech_support::MakeHTreeSeedEntries(result.mid_candidates, next_topology_pattern_id), result.leaf_stage.frontier_entries,
        next_topology_pattern_id);
    result.mid_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(result.mid_stage.raw_entries);

    result.root_stage.label = "root_200um_to_100um_to_50um";
    result.root_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
        realtech_support::MakeHTreeSeedEntries(result.root_candidates, next_topology_pattern_id), result.mid_stage.frontier_entries,
        next_topology_pattern_id);
    result.root_stage.frontier_entries = realtech_support::BuildInputBoundaryFrontier(result.root_stage.raw_entries);
    result.best_char = realtech_support::SelectBestHTreeChar(result.root_stage.frontier_entries);
    return result;
  };

  const auto leaf_candidates_uncapped = frontier_by_length.at(length_50_idx);
  const auto mid_candidates_uncapped = frontier_by_length.at(length_100_idx);
  const auto root_candidates_uncapped = frontier_by_length.at(length_200_idx);
  ASSERT_FALSE(leaf_candidates_uncapped.empty());
  ASSERT_FALSE(mid_candidates_uncapped.empty());
  ASSERT_FALSE(root_candidates_uncapped.empty());

  const auto leaf_candidates_capped
      = realtech_support::SelectCompositionCandidates(frontier_by_length.at(length_50_idx), kManualCappedCandidatesPerBoundaryGroup);
  const auto mid_candidates_capped
      = realtech_support::SelectCompositionCandidates(frontier_by_length.at(length_100_idx), kManualCappedCandidatesPerBoundaryGroup);
  const auto root_candidates_capped
      = realtech_support::SelectCompositionCandidates(frontier_by_length.at(length_200_idx), kManualCappedCandidatesPerBoundaryGroup);
  ASSERT_FALSE(leaf_candidates_capped.empty());
  ASSERT_FALSE(mid_candidates_capped.empty());
  ASSERT_FALSE(root_candidates_capped.empty());

  const auto uncapped_flow = run_htree_flow(leaf_candidates_uncapped, mid_candidates_uncapped, root_candidates_uncapped);
  const auto capped_flow = run_htree_flow(leaf_candidates_capped, mid_candidates_capped, root_candidates_capped);
  ASSERT_FALSE(uncapped_flow.leaf_stage.frontier_entries.empty());
  ASSERT_FALSE(uncapped_flow.mid_stage.frontier_entries.empty());
  ASSERT_FALSE(uncapped_flow.root_stage.frontier_entries.empty());
  ASSERT_FALSE(capped_flow.leaf_stage.frontier_entries.empty());
  ASSERT_FALSE(capped_flow.mid_stage.frontier_entries.empty());
  ASSERT_FALSE(capped_flow.root_stage.frontier_entries.empty());

  if (!uncapped_flow.best_char.has_value()) {
    GTEST_FAIL() << "Failed to select uncapped H-tree characterization entry.";
    return;
  }
  if (!capped_flow.best_char.has_value()) {
    GTEST_FAIL() << "Failed to select capped H-tree characterization entry.";
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
  report_stream << "wire_length_unit_um=" << builder.get_wire_length_unit_um() << "\n";
  report_stream << "wire_length_iterations=" << builder.get_wire_length_iterations() << "\n";
  report_stream << "configured_relaxed_candidates_per_boundary_group=" << CONFIG_INST.get_relaxed_candidates_per_boundary_group() << "\n";
  report_stream << "manual_capped_candidates_per_boundary_group=" << kManualCappedCandidatesPerBoundaryGroup << "\n";
  report_stream << "segment_200_source=" << synthesized_200_mode << "\n";
  report_stream << "positive_power_segment_chars=" << realtech_support::CountPositivePower(builder.get_segment_chars()) << "/"
                << builder.get_segment_chars().size() << "\n";
  report_stream << "segment_frontier_counts{50um=" << frontier_by_length.at(length_50_idx).size()
                << ",100um=" << frontier_by_length.at(length_100_idx).size() << ",200um=" << frontier_by_length.at(length_200_idx).size()
                << "}\n";
  report_stream << "htree_candidate_counts{leaf_uncapped=" << uncapped_flow.leaf_candidates.size()
                << ",leaf_capped=" << capped_flow.leaf_candidates.size() << ",mid_uncapped=" << uncapped_flow.mid_candidates.size()
                << ",mid_capped=" << capped_flow.mid_candidates.size() << ",root_uncapped=" << uncapped_flow.root_candidates.size()
                << ",root_capped=" << capped_flow.root_candidates.size() << "}\n";
  report_stream << "htree_uncapped_counts{leaf_raw=" << uncapped_flow.leaf_stage.raw_entries.size()
                << ",leaf_frontier=" << uncapped_flow.leaf_stage.frontier_entries.size()
                << ",mid_raw=" << uncapped_flow.mid_stage.raw_entries.size()
                << ",mid_frontier=" << uncapped_flow.mid_stage.frontier_entries.size()
                << ",root_raw=" << uncapped_flow.root_stage.raw_entries.size()
                << ",root_frontier=" << uncapped_flow.root_stage.frontier_entries.size() << "}\n";
  report_stream << "htree_capped_counts{leaf_raw=" << capped_flow.leaf_stage.raw_entries.size()
                << ",leaf_frontier=" << capped_flow.leaf_stage.frontier_entries.size()
                << ",mid_raw=" << capped_flow.mid_stage.raw_entries.size()
                << ",mid_frontier=" << capped_flow.mid_stage.frontier_entries.size()
                << ",root_raw=" << capped_flow.root_stage.raw_entries.size()
                << ",root_frontier=" << capped_flow.root_stage.frontier_entries.size() << "}\n";
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
      report_stream, "htree_uncapped_leaf_example=", uncapped_flow.leaf_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_uncapped_mid_example=", uncapped_flow.mid_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_uncapped_root_example=", uncapped_flow.root_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_capped_leaf_example=", capped_flow.leaf_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_capped_mid_example=", capped_flow.mid_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  realtech_support::AppendExamples(
      report_stream, "htree_capped_root_example=", capped_flow.root_stage.frontier_entries,
      [&](const icts::HTreeTopologyChar& entry) -> std::string { return realtech_support::FormatHTreeChar(entry, grid); });
  report_stream << "best_uncapped_htree_char=" << realtech_support::FormatHTreeChar(uncapped_flow.best_char.value(), grid) << "\n";
  report_stream << "best_capped_htree_char=" << realtech_support::FormatHTreeChar(capped_flow.best_char.value(), grid) << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("smoke_manual_htree", "smoke_manual_htree_report.txt", report_stream.str()));
}

TEST(CharacterizationRealTechSmokeTest, TerminalBranchBufferedPatternsRemainAvailableIndependentOfBuildPolicy)
{
  auto collect_terminal_pattern_count = [](bool force_branch_buffer) -> std::optional<std::size_t> {
    realtech_support::RealTechCharSession char_session;
    if (const auto prepare_error = char_session.prepare(force_branch_buffer ? "branch_buffer_on" : "branch_buffer_off", std::nullopt, 0.0,
                                                        0.0, false, force_branch_buffer);
        prepare_error.has_value()) {
      return std::nullopt;
    }

    icts::CharBuilder builder;
    builder.init();
    builder.build();
    if (builder.get_segment_chars().empty()) {
      return std::nullopt;
    }

    const unsigned target_length_idx = builder.get_wire_length_iterations();
    std::size_t terminal_pattern_count = 0U;
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

}  // namespace
}  // namespace icts_test
