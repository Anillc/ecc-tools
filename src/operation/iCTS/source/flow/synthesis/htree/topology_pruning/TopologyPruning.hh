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
 * @file CandidateSelection.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief H-tree pattern candidate assembly and global selection contracts.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree {

class RootDriverCompensationPass;
struct SinkLoadRegionLegalityContext;

struct CandidateBuildEvaluation
{
  unsigned depth = 0U;
  std::size_t leaf_count = 0U;
  BoundaryConstraints boundary_constraints;
  std::vector<HTree::LevelPlan> levels;
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> failure_level = std::nullopt;
  std::optional<unsigned> failure_length_idx = std::nullopt;
  std::size_t final_frontier_count = 0U;
  std::size_t candidate_solution_count = 0U;
  std::vector<HTreeTopologyChar> candidate_frontier_entries;
  std::size_t feasible_solution_count = 0U;
  std::vector<HTreeTopologyChar> feasible_frontier_entries;
  std::optional<HTreeTopologyChar> best_char = std::nullopt;
  bool used_boundary_fallback = false;
  std::optional<double> boundary_fallback_score = std::nullopt;
  std::string boundary_fallback_reason;
  TopologyPatternLibrary topology_pattern_library;
};

struct CandidateCharRef
{
  std::size_t candidate_index = 0U;
  const HTreeTopologyChar* entry = nullptr;
};

struct CandidateCharRefFilterResult
{
  std::vector<CandidateCharRef> entries;
  std::string first_failure_reason;
};

auto EvaluateCandidateBuild(const std::vector<HTree::LevelPlan>& levels,
                            const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                            const BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& boundary_constraints,
                            const Tree& topology, SinkLoadRegionLegalityContext& sink_load_region_legality_context, std::size_t leaf_count,
                            unsigned depth, unsigned char_slew_steps, RootDriverCompensationPass& compensation_pass)
    -> CandidateBuildEvaluation;
auto FilterGlobalEntriesBySinkLoadRegionCoverage(const std::vector<CandidateCharRef>& entries,
                                                 const std::vector<CandidateBuildEvaluation>& evaluations, const Tree& topology,
                                                 const BufferPatternLibrary& segment_pattern_library,
                                                 SinkLoadRegionLegalityContext& legality_context) -> CandidateCharRefFilterResult;
auto BuildPerDepthDelayPowerParetoRefs(const std::vector<CandidateCharRef>& entries) -> std::vector<CandidateCharRef>;
auto SelectBestGlobalEntry(const std::vector<CandidateCharRef>& entries) -> std::optional<CandidateCharRef>;
auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const BoundaryConstraints& boundary_constraints, unsigned slew_steps)
    -> double;

}  // namespace icts::htree
