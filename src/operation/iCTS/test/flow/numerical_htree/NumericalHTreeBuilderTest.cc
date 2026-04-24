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
 * @file NumericalHTreeBuilderTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Synthetic unit coverage for numerical H-tree pattern selection.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "PatternId.hh"
#include "flow/numerical_htree/NumericalHTreeBuilder.hh"

namespace icts_test {
namespace {

auto MakeConstantModel(unsigned pattern_id, double delay_ns, double output_slew_ns, double driven_cap_pf, double power_w,
                       double source_boundary_switch_power_w, std::string model_name = {}) -> icts::NumericalHTreePatternModel
{
  return icts::NumericalHTreePatternModel{
      .pattern_id = icts::PatternId::segment(pattern_id),
      .model_name = std::move(model_name),
      .delay_ns = icts::NumericalHTreeResponseSurface::constant(delay_ns),
      .output_slew_ns = icts::NumericalHTreeResponseSurface::constant(output_slew_ns),
      .driven_cap_pf = icts::NumericalHTreeResponseSurface::constant(driven_cap_pf),
      .power_w = icts::NumericalHTreeResponseSurface::constant(power_w),
      .source_boundary_switch_power_w = icts::NumericalHTreeResponseSurface::constant(source_boundary_switch_power_w),
  };
}

auto MakeInput(std::vector<icts::NumericalHTreeLevelInput> levels, std::size_t top_k_per_level) -> icts::NumericalHTreeBuildInput
{
  return icts::NumericalHTreeBuildInput{
      .options = icts::NumericalHTreeOptions{
          .top_input_slew_ns = 0.10,
          .leaf_load_cap_pf = 0.03,
          .top_k_per_level = top_k_per_level,
          .delay_weight = 1.0,
          .power_weight = 1.0,
      },
      .levels = std::move(levels),
  };
}

TEST(NumericalHTreeBuilderTest, SelectsKnownBestPattern)
{
  auto root_fast_expensive = MakeConstantModel(1U, 0.20, 0.16, 0.08, 1.00, 0.05, "root_fast_expensive");
  root_fast_expensive.delay_metrics = icts::NumericalHTreeFitMetrics{
      .sample_count = 9U,
      .rank = 3U,
      .rmse = 0.001,
      .r2 = 0.999,
      .max_abs_error = 0.002,
  };
  const auto root_balanced = MakeConstantModel(2U, 0.30, 0.12, 0.07, 0.20, 0.03, "root_balanced");
  const auto leaf_best = MakeConstantModel(3U, 0.10, 0.11, 0.04, 0.10, 0.01, "leaf_best");
  const auto leaf_slow = MakeConstantModel(4U, 0.70, 0.13, 0.04, 0.60, 0.01, "leaf_slow");

  const auto result = icts::NumericalHTreeBuilder::build(MakeInput(
      {
          icts::NumericalHTreeLevelInput{
              .representative_load_cap_pf = 0.06,
              .pattern_models = {root_fast_expensive, root_balanced},
          },
          icts::NumericalHTreeLevelInput{
              .representative_load_cap_pf = 0.03,
              .pattern_models = {leaf_best, leaf_slow},
          },
      },
      8U));

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.selected_segment_pattern_ids.size(), 2U);
  EXPECT_EQ(result.selected_segment_pattern_ids[0U], icts::PatternId::segment(2U));
  EXPECT_EQ(result.selected_segment_pattern_ids[1U], icts::PatternId::segment(3U));
  EXPECT_EQ(result.selected_depth.value_or(0U), 2U);
  EXPECT_EQ(result.selected_levels, 2U);
  EXPECT_NEAR(result.delay_ns, 0.40, 1e-12);
  EXPECT_NEAR(result.power_w, 0.38, 1e-12);
  ASSERT_EQ(result.levels.size(), 2U);
  EXPECT_NEAR(result.levels[1U].input_slew_ns, 0.12, 1e-12);
  EXPECT_TRUE(result.model_quality_summary.available);
  EXPECT_EQ(result.model_quality_summary.model_count, 4U);
  EXPECT_EQ(result.model_quality_summary.metric_count, 1U);
  ASSERT_EQ(result.model_metrics.size(), 1U);
  EXPECT_EQ(result.model_metrics[0U].label, "root_fast_expensive.delay");
}

TEST(NumericalHTreeBuilderTest, ComposesBinaryFanoutPower)
{
  const auto root = MakeConstantModel(10U, 0.10, 0.10, 0.08, 10.0, 1.0, "root");
  const auto middle = MakeConstantModel(11U, 0.20, 0.10, 0.06, 7.0, 2.0, "middle");
  const auto leaf = MakeConstantModel(12U, 0.30, 0.10, 0.03, 5.0, 1.0, "leaf");

  const auto result = icts::NumericalHTreeBuilder::build(MakeInput(
      {
          icts::NumericalHTreeLevelInput{
              .representative_load_cap_pf = 0.12,
              .pattern_models = {root},
          },
          icts::NumericalHTreeLevelInput{
              .representative_load_cap_pf = 0.06,
              .pattern_models = {middle},
          },
          icts::NumericalHTreeLevelInput{
              .representative_load_cap_pf = 0.03,
              .pattern_models = {leaf},
          },
      },
      1U));

  ASSERT_TRUE(result.success) << result.failure_reason;
  EXPECT_NEAR(result.delay_ns, 0.60, 1e-12);
  EXPECT_NEAR(result.power_w, 36.0, 1e-12);
  ASSERT_EQ(result.levels.size(), 3U);
  EXPECT_NEAR(result.levels[0U].composed_power_contribution_w, 10.0, 1e-12);
  EXPECT_NEAR(result.levels[1U].composed_power_contribution_w, 10.0, 1e-12);
  EXPECT_NEAR(result.levels[2U].composed_power_contribution_w, 16.0, 1e-12);
}

TEST(NumericalHTreeBuilderTest, TopKPruningControlsBeamSearch)
{
  const auto root_greedy = MakeConstantModel(20U, 1.0, 0.50, 0.08, 1.0, 0.0, "root_greedy");
  const auto root_global = MakeConstantModel(21U, 3.0, 0.10, 0.08, 1.0, 0.0, "root_global");
  auto input_sensitive_leaf = MakeConstantModel(22U, 0.0, 0.10, 0.03, 0.0, 0.0, "input_sensitive_leaf");
  input_sensitive_leaf.power_w = icts::NumericalHTreeResponseSurface::affine(0.0, 20.0, 0.0);

  const std::vector<icts::NumericalHTreeLevelInput> levels{
      icts::NumericalHTreeLevelInput{
          .representative_load_cap_pf = 0.06,
          .pattern_models = {root_greedy, root_global},
      },
      icts::NumericalHTreeLevelInput{
          .representative_load_cap_pf = 0.03,
          .pattern_models = {input_sensitive_leaf},
      },
  };

  const auto greedy_result = icts::NumericalHTreeBuilder::build(MakeInput(levels, 1U));
  ASSERT_TRUE(greedy_result.success) << greedy_result.failure_reason;
  ASSERT_EQ(greedy_result.selected_segment_pattern_ids.size(), 2U);
  EXPECT_EQ(greedy_result.selected_segment_pattern_ids[0U], icts::PatternId::segment(20U));
  EXPECT_EQ(greedy_result.selected_segment_pattern_ids[1U], icts::PatternId::segment(22U));
  EXPECT_EQ(greedy_result.pruned_candidate_count, 1U);
  ASSERT_EQ(greedy_result.level_surviving_state_counts.size(), 2U);
  EXPECT_EQ(greedy_result.level_surviving_state_counts[0U], 1U);

  const auto beam_result = icts::NumericalHTreeBuilder::build(MakeInput(levels, 2U));
  ASSERT_TRUE(beam_result.success) << beam_result.failure_reason;
  ASSERT_EQ(beam_result.selected_segment_pattern_ids.size(), 2U);
  EXPECT_EQ(beam_result.selected_segment_pattern_ids[0U], icts::PatternId::segment(21U));
  EXPECT_EQ(beam_result.selected_segment_pattern_ids[1U], icts::PatternId::segment(22U));
  EXPECT_NEAR(beam_result.power_w, 5.0, 1e-12);
  EXPECT_LT(beam_result.selected_score, greedy_result.selected_score);
  EXPECT_EQ(beam_result.pruned_candidate_count, 0U);
}

}  // namespace
}  // namespace icts_test
