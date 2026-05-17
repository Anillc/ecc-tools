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
 * @file TreeBufferSizingTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Unit tests for critical-branch buffer sizing solver.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "buffer_sizing/BufferSizingTypes.hh"
#include "buffer_sizing/CharTimingLookup.hh"
#include "buffer_sizing/TreeBufferSizing.hh"
#include "database/characterization/BufferingPattern.hh"
#include "database/characterization/CharCore.hh"
#include "database/characterization/PatternId.hh"
#include "database/characterization/SegmentChar.hh"
#include "database/characterization/ValueLattice.hh"

namespace icts_test {
namespace {

auto MakeChar(icts::PatternId pattern_id, unsigned load_cap_idx, double delay_ns, unsigned output_slew_idx) -> icts::SegmentChar
{
  return icts::SegmentChar(icts::CharCore(1U, output_slew_idx, load_cap_idx, load_cap_idx, delay_ns, 0.0, pattern_id, 0.0), 1U);
}

auto MakeSolverLookup() -> icts::buffer_sizing::CharTimingLookup
{
  const auto wire_pattern = icts::PatternId::segment(1U);
  const auto small_pattern = icts::PatternId::segment(2U);
  const auto large_pattern = icts::PatternId::segment(3U);
  const std::vector<icts::BufferingPattern> patterns{
      icts::BufferingPattern(1U, wire_pattern, {}, {}, false),
      icts::BufferingPattern(1U, small_pattern, {1.0}, {"BUF_X1"}, true),
      icts::BufferingPattern(1U, large_pattern, {1.0}, {"BUF_X2"}, true),
  };

  std::vector<icts::SegmentChar> chars;
  for (unsigned cap_idx = 1U; cap_idx <= 3U; ++cap_idx) {
    chars.push_back(MakeChar(wire_pattern, cap_idx, 0.04 + (0.04 * static_cast<double>(cap_idx)), 1U));
    chars.push_back(MakeChar(small_pattern, cap_idx, 0.60, 1U));
    chars.push_back(MakeChar(large_pattern, cap_idx, 0.20, 1U));
  }
  return icts::buffer_sizing::CharTimingLookup(chars, patterns, icts::UniformValueLattice(10.0, 1U), icts::UniformValueLattice(0.10, 1U),
                                               icts::UniformValueLattice(0.20, 3U));
}

auto SmallCandidate() -> icts::buffer_sizing::BufferCandidate
{
  return icts::buffer_sizing::BufferCandidate{
      .cell_master = "BUF_X1",
      .input_cap_pf = 0.20,
      .output_cap_limit_pf = 0.40,
      .area_um2 = 1.0,
      .drive_rank = 1U,
  };
}

auto LargeCandidate(double input_cap_pf = 0.30) -> icts::buffer_sizing::BufferCandidate
{
  return icts::buffer_sizing::BufferCandidate{
      .cell_master = "BUF_X2",
      .input_cap_pf = input_cap_pf,
      .output_cap_limit_pf = 0.60,
      .area_um2 = 2.0,
      .drive_rank = 2U,
  };
}

auto MakeProblem(double sink_b_pin_cap_pf = 0.40, double root_max_cap_pf = 0.90) -> icts::buffer_sizing::TreeSizingProblem
{
  icts::buffer_sizing::TreeSizingProblem problem;
  problem.clock_name = "clk";
  problem.root_node_id = 0U;
  problem.source_input_slew_ns = 0.10;
  problem.target_skew_ns = 0.01;
  problem.max_iterations = 4U;
  problem.max_trial_count = 16U;
  problem.nodes = {
      icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kSource,
                                    .name = "root",
                                    .parent_id = icts::buffer_sizing::kInvalidIndex,
                                    .output_net_id = 0U},
      icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kBuffer,
                                    .name = "buf_a",
                                    .parent_id = 0U,
                                    .incoming_net_id = 0U,
                                    .output_net_id = 1U,
                                    .buffer_id = 0U},
      icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kBuffer,
                                    .name = "buf_b",
                                    .parent_id = 0U,
                                    .incoming_net_id = 0U,
                                    .output_net_id = 2U,
                                    .buffer_id = 1U},
      icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kSink,
                                    .name = "sink_a",
                                    .parent_id = 1U,
                                    .incoming_net_id = 1U,
                                    .sink_pin_cap_pf = 0.20},
      icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kSink,
                                    .name = "sink_b",
                                    .parent_id = 2U,
                                    .incoming_net_id = 2U,
                                    .sink_pin_cap_pf = sink_b_pin_cap_pf},
  };
  problem.buffers = {
      icts::buffer_sizing::TreeBuffer{.node_id = 1U,
                                      .inst_name = "buf_a",
                                      .current_master = "BUF_X1",
                                      .candidates = {SmallCandidate(), LargeCandidate()},
                                      .current_candidate_index = 0U},
      icts::buffer_sizing::TreeBuffer{.node_id = 2U,
                                      .inst_name = "buf_b",
                                      .current_master = "BUF_X2",
                                      .candidates = {SmallCandidate(), LargeCandidate()},
                                      .current_candidate_index = 1U},
  };
  problem.nets = {
      icts::buffer_sizing::TreeNet{.name = "root_net",
                                   .driver_node_id = 0U,
                                   .arcs = {icts::buffer_sizing::TreeArc{.child_node_id = 1U, .length_um = 10.0},
                                            icts::buffer_sizing::TreeArc{.child_node_id = 2U, .length_um = 10.0}},
                                   .wire_cap_pf = 0.10,
                                   .baseline_load_cap_pf = 0.60,
                                   .max_cap_pf = root_max_cap_pf},
      icts::buffer_sizing::TreeNet{.name = "a_sink_net",
                                   .driver_node_id = 1U,
                                   .arcs = {icts::buffer_sizing::TreeArc{.child_node_id = 3U, .length_um = 10.0}},
                                   .baseline_load_cap_pf = 0.20,
                                   .max_cap_pf = 0.60},
      icts::buffer_sizing::TreeNet{.name = "b_sink_net",
                                   .driver_node_id = 2U,
                                   .arcs = {icts::buffer_sizing::TreeArc{.child_node_id = 4U, .length_um = 10.0}},
                                   .baseline_load_cap_pf = sink_b_pin_cap_pf,
                                   .max_cap_pf = 0.60},
  };
  return problem;
}

TEST(TreeBufferSizingTest, OrdersCriticalBranchFromLcaTowardMaxSink)
{
  const auto lookup = MakeSolverLookup();
  auto problem = MakeProblem();
  const auto inserted_node_id = problem.nodes.size();
  const auto inserted_buffer_id = problem.buffers.size();
  const auto inserted_net_id = problem.nets.size();
  problem.buffers.at(1U).current_master = "BUF_X1";
  problem.buffers.at(1U).current_candidate_index = 0U;
  problem.nodes.push_back(icts::buffer_sizing::TreeNode{.kind = icts::buffer_sizing::TreeNodeKind::kBuffer,
                                                        .name = "buf_b_child",
                                                        .parent_id = 2U,
                                                        .incoming_net_id = 2U,
                                                        .output_net_id = inserted_net_id,
                                                        .buffer_id = inserted_buffer_id});
  problem.nodes.at(4U).parent_id = inserted_node_id;
  problem.nodes.at(4U).incoming_net_id = inserted_net_id;
  problem.nets.at(2U).arcs = {icts::buffer_sizing::TreeArc{.child_node_id = inserted_node_id, .length_um = 10.0}};
  problem.nets.push_back(icts::buffer_sizing::TreeNet{
      .name = "b_child_sink_net",
      .driver_node_id = inserted_node_id,
      .arcs = {icts::buffer_sizing::TreeArc{.child_node_id = 4U, .length_um = 10.0}},
      .baseline_load_cap_pf = 0.40,
      .max_cap_pf = 0.60,
  });
  problem.buffers.push_back(icts::buffer_sizing::TreeBuffer{.node_id = inserted_node_id,
                                                            .inst_name = "buf_b_child",
                                                            .current_master = "BUF_X1",
                                                            .candidates = {SmallCandidate(), LargeCandidate()},
                                                            .current_candidate_index = 0U});
  std::vector<std::size_t> selection(problem.nodes.size(), 0U);
  for (const auto& buffer : problem.buffers) {
    selection.at(buffer.node_id) = buffer.current_candidate_index;
  }
  const auto evaluation = icts::buffer_sizing::TreeBufferSizing::evaluate(problem, lookup, selection);

  ASSERT_TRUE(evaluation.valid);
  EXPECT_EQ(evaluation.max_sink_node_id, 4U);
  EXPECT_EQ(icts::buffer_sizing::TreeBufferSizing::criticalBranchCandidates(problem, evaluation),
            (std::vector<std::size_t>{2U, inserted_node_id}));
}

TEST(TreeBufferSizingTest, AcceptsImprovementWhenMaxSinkChanges)
{
  const auto lookup = MakeSolverLookup();
  auto problem = MakeProblem(0.40);
  const auto result = icts::buffer_sizing::TreeBufferSizing::solve(problem, lookup);

  ASSERT_TRUE(result.valid);
  ASSERT_EQ(result.accepted_mutation_count, 1U);
  EXPECT_EQ(result.mutations.front().node_id, 1U);
  EXPECT_EQ(result.mutations.front().from_master, "BUF_X1");
  EXPECT_EQ(result.mutations.front().to_master, "BUF_X2");
  EXPECT_LT(result.after.skew_ns, result.before.skew_ns);
  EXPECT_EQ(result.selected_candidate_by_node.at(1U), 1U);
}

TEST(TreeBufferSizingTest, RejectsCandidateThatCreatesNewCapViolation)
{
  const auto lookup = MakeSolverLookup();
  auto problem = MakeProblem(0.40, 0.65);
  problem.buffers.at(0U).candidates.at(1U) = LargeCandidate(0.30);
  const auto result = icts::buffer_sizing::TreeBufferSizing::solve(problem, lookup);

  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.accepted_mutation_count, 0U);
  EXPECT_GT(result.cap_rejected_count, 0U);
  EXPECT_EQ(result.selected_candidate_by_node.at(1U), 0U);
}

TEST(TreeBufferSizingTest, RejectsNonImprovingCandidate)
{
  const auto lookup = MakeSolverLookup();
  auto problem = MakeProblem();
  problem.buffers.at(0U).candidates.at(1U).cell_master = "BUF_X1";
  problem.buffers.at(0U).candidates.at(1U).area_um2 = 2.0;
  const auto result = icts::buffer_sizing::TreeBufferSizing::solve(problem, lookup);

  ASSERT_TRUE(result.valid);
  EXPECT_EQ(result.accepted_mutation_count, 0U);
  EXPECT_GT(result.rejected_candidate_count, 0U);
  EXPECT_EQ(result.after.skew_ns, result.before.skew_ns);
}

}  // namespace
}  // namespace icts_test
