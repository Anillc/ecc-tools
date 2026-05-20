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
 * @file AnalyticalSolverTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Unit tests for analytical H-tree solver candidate ranking and label compression.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "analytical_characterization/AnalyticalModel.hh"
#include "characterization/Characterization.hh"
#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/CharCore.hh"
#include "database/characterization/HTreeTopologyChar.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/characterization/ValueLattice.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts_test {
namespace {

auto MakeAcceptedModel(icts::analytical::AnalyticalMetric metric, double value) -> icts::analytical::AnalyticalSurfaceModel
{
  icts::analytical::AnalyticalSurfaceModel model;
  model.metric = metric;
  model.basis = icts::analytical::AnalyticalModelBasis::kAffine;
  model.domain = icts::analytical::AnalyticalDomain{.slew_min_ns = 0.01, .slew_max_ns = 0.20, .cap_min_pf = 0.01, .cap_max_pf = 0.50};
  model.coefficients = {value, 0.0, 0.0};
  model.quality.accepted = true;
  model.quality.sample_count = 9U;
  return model;
}

auto MakeModelSet(icts::PatternId pattern_id, unsigned length_idx, double delay, double power) -> icts::analytical::AnalyticalModelSet
{
  return icts::analytical::AnalyticalModelSet{
      .key = icts::analytical::AnalyticalModelKey{.pattern_id = pattern_id, .length_idx = length_idx},
      .output_slew_model = MakeAcceptedModel(icts::analytical::AnalyticalMetric::kOutputSlew, 0.04),
      .delay_model = MakeAcceptedModel(icts::analytical::AnalyticalMetric::kDelay, delay),
      .power_model = MakeAcceptedModel(icts::analytical::AnalyticalMetric::kPower, power),
      .source_boundary_power_model = MakeAcceptedModel(icts::analytical::AnalyticalMetric::kSourceBoundaryNetSwitchPower, power * 0.25),
      .source_cap_operator = icts::analytical::StructuralCapOperator::wire(0.01),
  };
}

auto MakeSegmentChar(icts::PatternId pattern_id, unsigned length_idx, double delay, double power) -> icts::SegmentChar
{
  return icts::SegmentChar(icts::CharCore(1U, 2U, 2U, 1U, delay, power, pattern_id, power * 0.25), length_idx);
}

auto MakeBoundaryState(bool source_has_buffer, bool sink_has_buffer) -> icts::MonotonicBoundaryState
{
  return icts::MonotonicBoundaryState{
      .source = icts::BoundaryBufferState{.has_buffer = source_has_buffer, .strength_rank = source_has_buffer ? 1U : 0U},
      .sink = icts::BoundaryBufferState{.has_buffer = sink_has_buffer, .strength_rank = sink_has_buffer ? 1U : 0U},
  };
}

auto MakeLevelPlan(unsigned length_idx) -> icts::HTree::LevelPlan
{
  return icts::HTree::LevelPlan{
      .requested_length_dbu = 0,
      .requested_length_um = 0.0,
      .aligned_length_idx = length_idx,
      .aligned_length_um = 0.0,
      .is_leaf_level = false,
      .selected_has_any_buffer = false,
      .selected_has_terminal_branch_buffer = false,
      .selected_leaf_buffer_cell_master = {},
      .selected_terminal_cell_master = {},
      .selected_buffer_count = 0U,
      .selected_buffer_area_um2 = 0.0,
      .selected_weighted_buffer_count = 0U,
      .selected_weighted_buffer_area_um2 = 0.0,
      .segment_pattern_id = icts::PatternId::segment(0U),
  };
}

TEST(AnalyticalSolverTest, ProducesDeterministicCandidateFromShortlist)
{
  const auto pattern_fast = icts::PatternId::segment(1U);
  const auto pattern_slow = icts::PatternId::segment(2U);
  const unsigned length_idx = 1U;

  icts::htree::SegmentCandidateFrontierSet frontier_set;
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kAll)
      = {MakeSegmentChar(pattern_slow, length_idx, 0.30, 2e-6), MakeSegmentChar(pattern_fast, length_idx, 0.10, 1e-6)};
  icts::htree::SegmentFrontierCatalog frontier_catalog({{length_idx, frontier_set}});

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_fast, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_slow, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(pattern_fast, length_idx, 0.10, 1e-6));
  catalog.addModelSet(MakeModelSet(pattern_slow, length_idx, 0.30, 2e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_frontier_catalog = &frontier_catalog;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.top_k_per_depth = 1U;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 1U);
  EXPECT_EQ(result.candidates.front().level_segment_pattern_ids.front(), pattern_fast);
  EXPECT_TRUE(result.candidates.front().materialized_char.has_value());
}

TEST(AnalyticalSolverTest, AcceptsIdealRootInputSlew)
{
  const auto pattern_id = icts::PatternId::segment(1U);
  const unsigned length_idx = 1U;

  icts::htree::SegmentCandidateFrontierSet frontier_set;
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kAll) = {MakeSegmentChar(pattern_id, length_idx, 0.10, 1e-6)};
  icts::htree::SegmentFrontierCatalog frontier_catalog({{length_idx, frontier_set}});

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_id, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(pattern_id, length_idx, 0.10, 1e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_frontier_catalog = &frontier_catalog;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.0;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 1U);
  EXPECT_DOUBLE_EQ(result.candidates.front().root_input_slew_ns, 0.0);
  ASSERT_FALSE(result.candidates.front().trace.empty());
  EXPECT_NEAR(result.candidates.front().trace.front().input_slew_ns, 0.01, 1e-12);
}

TEST(AnalyticalSolverTest, KeepsTopKBeamCandidates)
{
  const auto pattern_fast = icts::PatternId::segment(1U);
  const auto pattern_medium = icts::PatternId::segment(2U);
  const auto pattern_slow = icts::PatternId::segment(3U);
  const unsigned length_idx = 1U;

  icts::htree::SegmentCandidateFrontierSet frontier_set;
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kAll)
      = {MakeSegmentChar(pattern_slow, length_idx, 0.30, 1e-6), MakeSegmentChar(pattern_medium, length_idx, 0.20, 2e-6),
         MakeSegmentChar(pattern_fast, length_idx, 0.10, 3e-6)};
  icts::htree::SegmentFrontierCatalog frontier_catalog({{length_idx, frontier_set}});

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_fast, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_medium, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_slow, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(pattern_fast, length_idx, 0.10, 3e-6));
  catalog.addModelSet(MakeModelSet(pattern_medium, length_idx, 0.20, 2e-6));
  catalog.addModelSet(MakeModelSet(pattern_slow, length_idx, 0.30, 1e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(length_idx),
      MakeLevelPlan(length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_frontier_catalog = &frontier_catalog;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.per_level_shortlist_size = 3U;
  solve_problem.options.top_k_per_depth = 2U;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 2U);
  EXPECT_EQ(result.candidates.at(0).level_segment_pattern_ids, std::vector<icts::PatternId>({pattern_fast, pattern_fast}));
  EXPECT_NE(result.candidates.at(1).level_segment_pattern_ids, result.candidates.at(0).level_segment_pattern_ids);
  EXPECT_LE(result.candidates.at(0).raw_delay_ns, result.candidates.at(1).raw_delay_ns);
}

TEST(AnalyticalSolverTest, ComposesUnitModelsWithoutSegmentFrontierCatalog)
{
  const auto unit_pattern_id = icts::PatternId::segment(1U);
  const unsigned unit_length_idx = 1U;
  const unsigned composed_length_idx = 2U;

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(unit_length_idx, unit_pattern_id, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(unit_pattern_id, unit_length_idx, 0.10, 1e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(composed_length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.mutable_segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.use_functional_unit_compose = true;
  solve_problem.options.unit_length_idx = unit_length_idx;
  solve_problem.options.unit_compose_beam_size = 2U;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 1U);
  const auto composed_pattern_id = result.candidates.front().level_segment_pattern_ids.front();
  EXPECT_NE(composed_pattern_id, unit_pattern_id);
  const auto* composed_pattern = segment_patterns.find(composed_pattern_id);
  ASSERT_NE(composed_pattern, nullptr);
  EXPECT_EQ(composed_pattern->get_length_idx(), composed_length_idx);
  EXPECT_NEAR(result.candidates.front().raw_delay_ns, 0.20, 1e-12);
  EXPECT_TRUE(result.candidates.front().materialized_char.has_value());
}

TEST(AnalyticalSolverTest, FunctionalUnitComposeMaterializesNativeSegmentPattern)
{
  const auto unit_pattern = icts::PatternId::segment(1U);
  const unsigned unit_length_idx = 1U;
  const unsigned level_length_idx = 2U;

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(unit_length_idx, unit_pattern, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(unit_pattern, unit_length_idx, 0.10, 1e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(level_length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.mutable_segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.use_functional_unit_compose = true;
  solve_problem.options.unit_length_idx = unit_length_idx;
  solve_problem.options.top_k_per_depth = 1U;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 1U);
  const auto materialized_pattern_id = result.candidates.front().level_segment_pattern_ids.front();
  EXPECT_NE(materialized_pattern_id, unit_pattern);
  const auto* materialized_pattern = segment_patterns.find(materialized_pattern_id);
  ASSERT_NE(materialized_pattern, nullptr);
  EXPECT_EQ(materialized_pattern->get_length_idx(), level_length_idx);
  ASSERT_FALSE(result.candidates.front().trace.empty());
  EXPECT_EQ(result.candidates.front().trace.front().length_idx, level_length_idx);
  EXPECT_TRUE(result.candidates.front().materialized_char.has_value());
}

TEST(AnalyticalSolverTest, DiagnosticSequenceCountersTrackFrontierDecompositionAndShortlist)
{
  const auto unit_pattern = icts::PatternId::segment(1U);
  const auto native_pattern = icts::PatternId::segment(251617U);
  const unsigned unit_length_idx = 1U;
  const unsigned level_length_idx = 2U;

  icts::htree::SegmentCandidateFrontierSet frontier_set;
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kAll) = {MakeSegmentChar(native_pattern, level_length_idx, 0.20, 2e-6)};
  icts::htree::SegmentFrontierCatalog frontier_catalog({{level_length_idx, frontier_set}});

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(unit_length_idx, unit_pattern, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(level_length_idx, native_pattern, {}, {}, false));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(unit_pattern, unit_length_idx, 0.10, 1e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(level_length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_frontier_catalog = &frontier_catalog;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.mutable_segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.use_functional_unit_compose = true;
  solve_problem.options.unit_length_idx = unit_length_idx;
  solve_problem.options.diagnostic_segment_pattern_ids = {native_pattern};

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  EXPECT_EQ(result.diagnostic_library_hit_count, 1U);
  EXPECT_EQ(result.diagnostic_frontier_hit_count, 1U);
  EXPECT_EQ(result.diagnostic_decomposed_count, 1U);
  EXPECT_EQ(result.diagnostic_scored_count, 1U);
  EXPECT_EQ(result.diagnostic_shortlisted_count, 1U);
  EXPECT_EQ(result.diagnostic_generated_candidate_count, 1U);
}

TEST(AnalyticalSolverTest, AnalyticalTopologyPatternRejectsInternalFanoutHiddenByRootBuffer)
{
  const auto plain_pattern = icts::PatternId::segment(1U);
  const auto root_buffer_pattern = icts::PatternId::segment(2U);
  const unsigned length_idx = 1U;

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(length_idx, plain_pattern, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(length_idx, root_buffer_pattern, {0.1}, {"BUF_X1"}, false, MakeBoundaryState(true, true)));

  const auto topology_pattern = icts::htree::analytical_solver::BuildAnalyticalTopologyPattern(
      {root_buffer_pattern, plain_pattern, plain_pattern}, segment_patterns, 2U);

  EXPECT_FALSE(topology_pattern.has_value());
}

TEST(AnalyticalSolverTest, PrependsDpSegmentWithStructuralCapBranchAndTrace)
{
  const auto pattern_id = icts::PatternId::segment(8U);
  const unsigned length_idx = 1U;
  const auto model_set = MakeModelSet(pattern_id, length_idx, 0.10, 4e-6);

  icts::htree::analytical_solver::AnalyticalDpLabel suffix;
  suffix.cap_operator = icts::analytical::StructuralCapOperator::wire(0.02);
  suffix.input_slew_min_ns = 0.01;
  suffix.input_slew_max_ns = 0.20;
  suffix.delay_lower_ns = 0.20;
  suffix.delay_upper_ns = 0.25;
  suffix.power_lower_w = 1e-6;
  suffix.power_upper_w = 2e-6;
  suffix.source_exposed_load_count = 1U;
  suffix.trace_segment_pattern_ids = {icts::PatternId::segment(9U)};

  const auto label = icts::htree::analytical_solver::PrependAnalyticalDpSegment(
      suffix, model_set, icts::NormalizePatternCompositionState(icts::TerminalSemantic::kLeafUnbuffered),
      icts::htree::analytical_solver::AnalyticalDpTransitionOptions{
          .leaf_load_cap_pf = 0.05,
          .input_slew_probe_ns = 0.02,
          .branch_fanout = 2.0,
          .branch_junction_cap_pf = 0.01,
          .source_boundary_power_weight = 0.5,
          .use_conservative_metrics = false,
      });

  if (!label.has_value()) {
    ADD_FAILURE() << "Expected prepended label.";
    return;
  }
  const auto& label_value = label.value();
  EXPECT_NEAR(label_value.cap_operator.apply(0.05), 0.16, 1e-12);
  EXPECT_NEAR(label_value.delay_lower_ns, 0.30, 1e-12);
  EXPECT_NEAR(label_value.delay_upper_ns, 0.35, 1e-12);
  EXPECT_NEAR(label_value.power_lower_w, 4.5e-6, 1e-18);
  ASSERT_EQ(label_value.trace_segment_pattern_ids.size(), 2U);
  EXPECT_EQ(label_value.trace_segment_pattern_ids.front(), pattern_id);
  EXPECT_EQ(label_value.source_exposed_load_count, 2U);
}

TEST(AnalyticalSolverTest, RespectsBranchBufferAndRootFanoutConstraints)
{
  const auto pattern_plain = icts::PatternId::segment(1U);
  const auto pattern_branch = icts::PatternId::segment(2U);
  const unsigned length_idx = 1U;

  icts::htree::SegmentCandidateFrontierSet frontier_set;
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kAll)
      = {MakeSegmentChar(pattern_plain, length_idx, 0.05, 1e-6), MakeSegmentChar(pattern_branch, length_idx, 0.20, 2e-6)};
  frontier_set.mutableEntries(icts::htree::SegmentFrontierKind::kTerminalBranchBuffered)
      = {MakeSegmentChar(pattern_branch, length_idx, 0.20, 2e-6)};
  icts::htree::SegmentFrontierCatalog frontier_catalog({{length_idx, frontier_set}});

  icts::htree::BufferPatternLibrary segment_patterns;
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_plain, {}, {}, false));
  segment_patterns.add(icts::BufferingPattern(length_idx, pattern_branch, {1.0}, {"BUF_X1"}, true));

  icts::analytical::AnalyticalModelCatalog catalog;
  catalog.addModelSet(MakeModelSet(pattern_plain, length_idx, 0.05, 1e-6));
  catalog.addModelSet(MakeModelSet(pattern_branch, length_idx, 0.20, 2e-6));

  std::vector<icts::HTree::LevelPlan> levels = {
      MakeLevelPlan(length_idx),
      MakeLevelPlan(length_idx),
  };
  icts::htree::analytical_solver::AnalyticalHTreeSolveProblem solve_problem;
  solve_problem.levels = &levels;
  solve_problem.segment_frontier_catalog = &frontier_catalog;
  solve_problem.segment_pattern_library = &segment_patterns;
  solve_problem.model_catalog = &catalog;
  solve_problem.boundary_constraints.force_branch_buffer = true;
  solve_problem.fanout_options.max_fanout = 2U;
  solve_problem.slew_lattice = icts::UniformValueLattice(0.01, 20U);
  solve_problem.cap_lattice = icts::UniformValueLattice(0.01, 50U);
  solve_problem.options.root_input_slew_ns = 0.02;
  solve_problem.options.representative_leaf_load_cap_pf = 0.05;
  solve_problem.options.top_k_per_depth = 1U;

  const auto result = icts::htree::analytical_solver::SolveAnalyticalHTreeCandidates(solve_problem);

  ASSERT_TRUE(result.success) << result.failure_reason;
  ASSERT_EQ(result.candidates.size(), 1U);
  EXPECT_EQ(result.candidates.front().level_segment_pattern_ids.front(), pattern_branch);
  EXPECT_TRUE(result.candidates.front().fanout_legal);
}

TEST(AnalyticalSolverTest, CompressParetoLabelsKeepsIntervalOverlaps)
{
  icts::htree::analytical_solver::AnalyticalDpLabel left;
  left.cap_operator = icts::analytical::StructuralCapOperator::wire(0.01);
  left.delay_lower_ns = 1.0;
  left.delay_upper_ns = 1.2;
  left.power_lower_w = 1.0;
  left.power_upper_w = 1.2;
  left.trace_segment_pattern_ids = {icts::PatternId::segment(1U)};

  icts::htree::analytical_solver::AnalyticalDpLabel overlapped;
  overlapped.cap_operator = icts::analytical::StructuralCapOperator::wire(0.02);
  overlapped.delay_lower_ns = 1.1;
  overlapped.delay_upper_ns = 1.3;
  overlapped.power_lower_w = 1.1;
  overlapped.power_upper_w = 1.3;
  overlapped.trace_segment_pattern_ids = {icts::PatternId::segment(2U)};

  icts::htree::analytical_solver::AnalyticalDpLabel dominated;
  dominated.cap_operator = icts::analytical::StructuralCapOperator::wire(0.05);
  dominated.delay_lower_ns = 1.5;
  dominated.delay_upper_ns = 1.6;
  dominated.power_lower_w = 1.5;
  dominated.power_upper_w = 1.6;
  dominated.trace_segment_pattern_ids = {icts::PatternId::segment(3U)};

  const auto compressed = icts::htree::analytical_solver::CompressParetoLabels(
      {left, overlapped, dominated}, icts::htree::analytical_solver::AnalyticalDominanceOptions{});

  ASSERT_EQ(compressed.size(), 2U);
  EXPECT_EQ(compressed.at(0).trace_segment_pattern_ids.front(), icts::PatternId::segment(1U));
  EXPECT_EQ(compressed.at(1).trace_segment_pattern_ids.front(), icts::PatternId::segment(2U));
}

}  // namespace
}  // namespace icts_test
