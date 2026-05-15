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
 * @file AnalyticalSolver.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-14
 * @brief Analytical H-tree shortlist solver entry point and result contracts.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "PatternId.hh"
#include "ValueLattice.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/AnalyticalCandidate.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::analytical {
class AnalyticalModelCatalog;
}  // namespace icts::analytical

namespace icts::htree {
class SegmentFrontierCatalog;
struct BufferPatternLibrary;
}  // namespace icts::htree

namespace icts::htree::analytical_solver {

struct AnalyticalSolverOptions
{
  std::size_t per_level_shortlist_size = 8U;
  std::size_t top_k_per_depth = 16U;
  std::size_t unit_compose_beam_size = 32U;
  double root_input_slew_ns = 0.0;
  double representative_leaf_load_cap_pf = 0.0;
  bool use_conservative_scoring = true;
  bool use_functional_unit_compose = false;
  unsigned unit_length_idx = 1U;
  std::vector<PatternId> diagnostic_segment_pattern_ids;
};

struct AnalyticalSolverRequest
{
  const std::vector<HTree::LevelPlan>* levels = nullptr;
  const SegmentFrontierCatalog* segment_frontier_catalog = nullptr;
  const BufferPatternLibrary* segment_pattern_library = nullptr;
  BufferPatternLibrary* mutable_segment_pattern_library = nullptr;
  const icts::analytical::AnalyticalModelCatalog* model_catalog = nullptr;
  BoundaryConstraints boundary_constraints;
  HTreeFanoutPruningOptions fanout_options;
  UniformValueLattice slew_lattice;
  UniformValueLattice cap_lattice;
  AnalyticalSolverOptions options;
};

struct AnalyticalSolverResult
{
  bool success = false;
  std::string failure_reason;
  std::size_t evaluated_segment_count = 0U;
  std::size_t generated_candidate_count = 0U;
  std::size_t materialization_attempt_count = 0U;
  std::size_t scored_segment_count = 0U;
  std::size_t missing_model_count = 0U;
  std::size_t decomposition_rejected_count = 0U;
  std::size_t metric_evaluation_rejected_count = 0U;
  std::size_t domain_slew_rejected_count = 0U;
  std::size_t domain_cap_rejected_count = 0U;
  std::size_t domain_slew_floor_count = 0U;
  std::size_t domain_cap_floor_count = 0U;
  double max_domain_rejected_cap_pf = 0.0;
  std::size_t empty_shortlist_count = 0U;
  std::size_t root_fanout_rejected_count = 0U;
  std::size_t lattice_rejected_count = 0U;
  std::size_t diagnostic_library_hit_count = 0U;
  std::size_t diagnostic_frontier_hit_count = 0U;
  std::size_t diagnostic_decomposed_count = 0U;
  std::size_t diagnostic_scored_count = 0U;
  std::size_t diagnostic_shortlisted_count = 0U;
  std::size_t diagnostic_generated_candidate_count = 0U;
  std::size_t diagnostic_direct_candidate_count = 0U;
  double diagnostic_direct_delay_ns = 0.0;
  double diagnostic_direct_power_w = 0.0;
  double diagnostic_direct_root_cap_pf = 0.0;
  unsigned diagnostic_direct_input_slew_idx = 0U;
  unsigned diagnostic_direct_output_slew_idx = 0U;
  unsigned diagnostic_direct_driven_cap_idx = 0U;
  unsigned first_empty_level_index = 0U;
  unsigned first_empty_length_idx = 0U;
  std::string first_empty_reason;
  std::vector<AnalyticalCandidate> candidates;
};

auto SolveAnalyticalHTreeCandidates(const AnalyticalSolverRequest& request) -> AnalyticalSolverResult;

}  // namespace icts::htree::analytical_solver
