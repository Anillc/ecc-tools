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
 * @file AnalyticalSelection.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree candidate selection contracts.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/candidate/AnalyticalCandidate.hh"
#include "synthesis/htree/analytical_solver/selection/AnalyticalValidation.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts {

class CharBuilder;
class Tree;

namespace htree {
struct BoundaryConstraints;
struct BufferPatternLibrary;
class SegmentFrontierCatalog;
}  // namespace htree

namespace htree::analytical_selection {

inline constexpr std::size_t kAnalyticalPerLevelShortlistSize = 128U;
inline constexpr std::size_t kAnalyticalTopKPerDepth = 128U;
inline constexpr std::size_t kAnalyticalUnitComposeBeamSize = 128U;
inline constexpr unsigned kAnalyticalUnitLengthIdx = 1U;
inline constexpr double kAnalyticalParetoPowerSlackRatio = 0.20;

struct AnalyticalHTreeAttempt
{
  bool selected = false;
  std::string failure_reason;
  CandidateBuildEvaluation selected_evaluation;
  DepthSummary selected_summary;
  RootDriverCompensationDetail selected_compensation_detail;
  SinkLoadRegionLegalityResult selected_sink_load_region_legality;
  RootDriverCompensationStats root_driver_compensation_stats;
  std::size_t model_set_count = 0U;
  std::size_t rejected_fit_count = 0U;
  std::size_t structural_cap_operator_count = 0U;
  std::size_t evaluated_segment_count = 0U;
  std::size_t generated_candidate_count = 0U;
  std::size_t validated_candidate_count = 0U;
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
  std::size_t materialization_attempt_count = 0U;
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
  std::size_t validated_pareto_count = 0U;
  std::size_t selected_pareto_power_rank = 0U;
  double validated_delay_min_ns = 0.0;
  double validated_delay_median_ns = 0.0;
  double validated_delay_max_ns = 0.0;
  double validated_power_min_w = 0.0;
  double validated_power_median_w = 0.0;
  double validated_power_max_w = 0.0;
  unsigned first_empty_level_index = 0U;
  unsigned first_empty_length_idx = 0U;
  std::string first_empty_reason;
};

struct AnalyticalValidatedCandidate
{
  analytical_solver::AnalyticalCandidate candidate;
  analytical_solver::AnalyticalValidationResult validation;
  std::size_t original_index = 0U;
};

auto TrySolveAnalyticalHTree(const Tree& topology, const std::vector<HTree::LevelPlan>& full_level_plans,
                             const std::vector<unsigned>& depth_candidates, const SegmentFrontierCatalog& segment_frontier_catalog,
                             BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& search_boundary_constraints,
                             const HTreeFanoutPruningOptions& fanout_pruning_options,
                             const RootDriverCompensationOptions& root_driver_compensation_options, const CharBuilder& char_builder,
                             unsigned char_slew_steps) -> AnalyticalHTreeAttempt;
auto ApplyAnalyticalRootDriverStats(DepthSearchResult& exploration, const AnalyticalHTreeAttempt& attempt,
                                    const RootDriverCompensationOptions& compensation_options) -> void;

}  // namespace htree::analytical_selection
}  // namespace icts
