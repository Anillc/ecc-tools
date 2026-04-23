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
#include <cmath>
#include <cstddef>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
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
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
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

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(frontier_by_length.contains(length_25_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_25_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  auto exact_segment_100_raw = realtech_support::ComposeSegmentEntriesExact(frontier_by_length.at(length_50_idx),
                                                                            frontier_by_length.at(length_50_idx), segment_context);
  ASSERT_FALSE(exact_segment_100_raw.empty());
  auto exact_segment_100_frontier = realtech_support::BuildSegmentStateFrontier(exact_segment_100_raw, segment_context);
  ASSERT_FALSE(exact_segment_100_frontier.empty());
  EXPECT_TRUE(std::ranges::all_of(exact_segment_100_frontier, [length_100_idx](const icts::SegmentChar& entry) -> bool {
    return entry.get_length_idx() == length_100_idx;
  }));

  realtech_support::HTreeFrontierContext htree_context;

  realtech_support::HTreeStageSummary leaf_stage;
  leaf_stage.label = "leaf_25um";
  leaf_stage.raw_entries = realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_25_idx), segment_context, htree_context);
  leaf_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(leaf_stage.raw_entries, htree_context);
  ASSERT_FALSE(leaf_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary mid_stage;
  mid_stage.label = "mid_50um_to_25um";
  mid_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_50_idx), segment_context, htree_context),
      leaf_stage.frontier_entries, htree_context);
  mid_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(mid_stage.raw_entries, htree_context);
  ASSERT_FALSE(mid_stage.frontier_entries.empty());

  realtech_support::HTreeStageSummary root_stage;
  root_stage.label = "root_100um_to_50um_to_25um";
  root_stage.raw_entries = realtech_support::ComposeHTreeEntriesExact(
      realtech_support::MakeHTreeSeedEntries(frontier_by_length.at(length_100_idx), segment_context, htree_context),
      mid_stage.frontier_entries, htree_context);
  root_stage.frontier_entries = realtech_support::BuildHTreeStateFrontier(root_stage.raw_entries, htree_context);
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

TEST(CharacterizationRealTechExactRegressionTest, ExactComposePowerAccountingProducesComparableDirectReport)
{
  realtech_support::RealTechCharSession char_session;
  if (const auto prepare_error = char_session.prepare("exact_compose_power_accounting", std::nullopt, 0.0, 0.0);
      prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  icts::CharBuilder builder;
  builder.init();
  builder.build();

  ASSERT_FALSE(builder.get_segment_chars().empty());
  auto segment_context = realtech_support::BuildSegmentFrontierContext(builder.get_buffering_patterns());
  const auto grid = realtech_support::CalcCharGrid(builder);
  ASSERT_GT(grid.length_step_um, 0.0);

  const unsigned length_50_idx = realtech_support::MakeLengthIndex(realtech_support::kExactMidLevelLengthUm, grid.length_step_um);
  const unsigned length_100_idx = realtech_support::MakeLengthIndex(realtech_support::kExactRootLevelLengthUm, grid.length_step_um);

  auto frontier_by_length = realtech_support::BuildSegmentLengthFrontiers(builder.get_segment_chars(), segment_context);
  ASSERT_TRUE(frontier_by_length.contains(length_50_idx));
  ASSERT_TRUE(frontier_by_length.contains(length_100_idx));
  ASSERT_FALSE(frontier_by_length.at(length_50_idx).empty());
  ASSERT_FALSE(frontier_by_length.at(length_100_idx).empty());

  auto exact_segment_100_raw = realtech_support::ComposeSegmentEntriesExact(frontier_by_length.at(length_50_idx),
                                                                            frontier_by_length.at(length_50_idx), segment_context);
  ASSERT_FALSE(exact_segment_100_raw.empty());
  auto exact_segment_100_frontier = realtech_support::BuildSegmentStateFrontier(exact_segment_100_raw, segment_context);
  ASSERT_FALSE(exact_segment_100_frontier.empty());

  struct CompareKey
  {
    std::string pattern_signature;
    unsigned input_slew_idx = 0U;
    unsigned output_slew_idx = 0U;
    unsigned driven_cap_idx = 0U;
    unsigned load_cap_idx = 0U;

    auto operator==(const CompareKey& rhs) const -> bool = default;
  };

  struct CompareKeyHash
  {
    auto operator()(const CompareKey& key) const -> std::size_t
    {
      std::size_t seed = std::hash<std::string>{}(key.pattern_signature);
      seed ^= std::hash<unsigned>{}(key.input_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.output_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      seed ^= std::hash<unsigned>{}(key.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      return seed;
    }
  };

  auto make_pattern_signature = [&segment_context](icts::PatternId pattern_id) -> std::string {
    const auto pattern_it = segment_context.patterns.find(pattern_id);
    if (pattern_it == segment_context.patterns.end()) {
      return {};
    }

    const auto& pattern = pattern_it->second;
    std::ostringstream stream;
    stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
    stream << std::setprecision(6);
    stream << "len=" << pattern.get_length_idx();
    stream << ";terminal=" << (pattern.hasTerminalBranchBuffer() ? 1 : 0);
    stream << ";src_has=" << (pattern.get_monotonic_boundary_state().source.has_buffer ? 1 : 0);
    stream << ";src_rank=" << pattern.get_monotonic_boundary_state().source.strength_rank;
    stream << ";snk_has=" << (pattern.get_monotonic_boundary_state().sink.has_buffer ? 1 : 0);
    stream << ";snk_rank=" << pattern.get_monotonic_boundary_state().sink.strength_rank;
    stream << ";pos=";
    for (double pos : pattern.get_buffer_positions()) {
      stream << pos << ",";
    }
    stream << ";masters=";
    for (const auto& cell_master : pattern.get_cell_masters()) {
      stream << cell_master << ",";
    }
    return stream.str();
  };

  auto make_compare_key = [&make_pattern_signature](const icts::SegmentChar& entry) -> CompareKey {
    return CompareKey{
        .pattern_signature = make_pattern_signature(entry.get_pattern_id()),
        .input_slew_idx = entry.get_input_slew_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
    };
  };

  std::unordered_map<CompareKey, icts::SegmentChar, CompareKeyHash> direct_entries;
  direct_entries.reserve(frontier_by_length.at(length_100_idx).size());
  for (const auto& entry : frontier_by_length.at(length_100_idx)) {
    direct_entries.emplace(make_compare_key(entry), entry);
  }

  std::size_t matched_entry_count = 0U;
  std::size_t exact_lower_power_count = 0U;
  std::size_t exact_higher_power_count = 0U;
  std::size_t exact_lower_delay_count = 0U;
  std::size_t exact_higher_delay_count = 0U;
  double sum_direct_power_w = 0.0;
  double sum_exact_power_w = 0.0;
  double sum_direct_delay_ns = 0.0;
  double sum_exact_delay_ns = 0.0;
  double worst_power_underestimate_w = 0.0;
  double worst_delay_underestimate_ns = 0.0;
  std::string worst_power_example;
  std::string worst_delay_example;

  for (const auto& exact_entry : exact_segment_100_frontier) {
    const auto direct_it = direct_entries.find(make_compare_key(exact_entry));
    if (direct_it == direct_entries.end()) {
      continue;
    }

    const auto& direct_entry = direct_it->second;
    ++matched_entry_count;
    sum_direct_power_w += direct_entry.get_power();
    sum_exact_power_w += exact_entry.get_power();
    sum_direct_delay_ns += direct_entry.get_delay();
    sum_exact_delay_ns += exact_entry.get_delay();

    const double power_delta_w = exact_entry.get_power() - direct_entry.get_power();
    if (power_delta_w < -1e-15) {
      ++exact_lower_power_count;
      const double power_underestimate_w = -power_delta_w;
      if (power_underestimate_w > worst_power_underestimate_w) {
        worst_power_underestimate_w = power_underestimate_w;
        std::ostringstream example;
        example << "key{input_slew_idx=" << exact_entry.get_input_slew_idx() << ",output_slew_idx=" << exact_entry.get_output_slew_idx()
                << ",driven_cap_idx=" << exact_entry.get_driven_cap_idx() << ",load_cap_idx=" << exact_entry.get_load_cap_idx()
                << "} direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
                << " exact=" << realtech_support::FormatSegmentChar(exact_entry, grid);
        worst_power_example = example.str();
      }
    } else if (power_delta_w > 1e-15) {
      ++exact_higher_power_count;
    }

    const double delay_delta_ns = exact_entry.get_delay() - direct_entry.get_delay();
    if (delay_delta_ns < -1e-15) {
      ++exact_lower_delay_count;
      const double delay_underestimate_ns = -delay_delta_ns;
      if (delay_underestimate_ns > worst_delay_underestimate_ns) {
        worst_delay_underestimate_ns = delay_underestimate_ns;
        std::ostringstream example;
        example << "key{input_slew_idx=" << exact_entry.get_input_slew_idx() << ",output_slew_idx=" << exact_entry.get_output_slew_idx()
                << ",driven_cap_idx=" << exact_entry.get_driven_cap_idx() << ",load_cap_idx=" << exact_entry.get_load_cap_idx()
                << "} direct=" << realtech_support::FormatSegmentChar(direct_entry, grid)
                << " exact=" << realtech_support::FormatSegmentChar(exact_entry, grid);
        worst_delay_example = example.str();
      }
    } else if (delay_delta_ns > 1e-15) {
      ++exact_higher_delay_count;
    }
  }

  ASSERT_GT(matched_entry_count, 0U);

  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::scientific, std::ostringstream::floatfield);
  report_stream << std::setprecision(12);
  report_stream << "scenario=exact_compose_power_accounting\n";
  report_stream << "direct_length_100_frontier_count=" << frontier_by_length.at(length_100_idx).size() << "\n";
  report_stream << "exact_compose_length_100_frontier_count=" << exact_segment_100_frontier.size() << "\n";
  report_stream << "matched_entry_count=" << matched_entry_count << "\n";
  report_stream << "exact_lower_power_count=" << exact_lower_power_count << "\n";
  report_stream << "exact_higher_power_count=" << exact_higher_power_count << "\n";
  report_stream << "exact_lower_delay_count=" << exact_lower_delay_count << "\n";
  report_stream << "exact_higher_delay_count=" << exact_higher_delay_count << "\n";
  report_stream << "sum_direct_power_w=" << sum_direct_power_w << "\n";
  report_stream << "sum_exact_power_w=" << sum_exact_power_w << "\n";
  report_stream << "sum_power_delta_w=" << (sum_exact_power_w - sum_direct_power_w) << "\n";
  report_stream << "power_ratio_exact_over_direct=" << (sum_direct_power_w > 0.0 ? (sum_exact_power_w / sum_direct_power_w) : 0.0) << "\n";
  report_stream << "sum_direct_delay_ns=" << sum_direct_delay_ns << "\n";
  report_stream << "sum_exact_delay_ns=" << sum_exact_delay_ns << "\n";
  report_stream << "sum_delay_delta_ns=" << (sum_exact_delay_ns - sum_direct_delay_ns) << "\n";
  report_stream << "delay_ratio_exact_over_direct=" << (sum_direct_delay_ns > 0.0 ? (sum_exact_delay_ns / sum_direct_delay_ns) : 0.0)
                << "\n";
  report_stream << "worst_power_underestimate_w=" << worst_power_underestimate_w << "\n";
  report_stream << "worst_power_underestimate_example=" << worst_power_example << "\n";
  report_stream << "worst_delay_underestimate_ns=" << worst_delay_underestimate_ns << "\n";
  report_stream << "worst_delay_underestimate_example=" << worst_delay_example << "\n";

  ASSERT_TRUE(realtech_support::WriteScenarioLog("exact_compose_power_accounting", "exact_compose_power_accounting_report.txt",
                                                 report_stream.str()));
}

}  // namespace
}  // namespace icts_test
