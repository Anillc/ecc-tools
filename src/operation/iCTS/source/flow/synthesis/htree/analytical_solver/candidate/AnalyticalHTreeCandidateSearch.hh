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
 * @file AnalyticalHTreeCandidateSearch.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree candidate search contracts.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "SegmentChar.hh"
#include "analytical_characterization/AnalyticalModel.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree::analytical_solver {

using icts::analytical::AnalyticalMetric;
using icts::analytical::AnalyticalModelKey;
using icts::analytical::AnalyticalModelSet;

struct ScoredSegment
{
  PatternId pattern_id = PatternId::segment(0U);
  unsigned length_idx = 0U;
  double score = 0.0;
  double input_slew_ns = 0.0;
  double downstream_load_cap_pf = 0.0;
  double output_slew_ns = 0.0;
  double source_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_power_w = 0.0;
  double slew_upper_ns = 0.0;
  double delay_upper_ns = 0.0;
  double power_upper_w = 0.0;
  std::vector<PatternId> unit_pattern_ids;
};

struct ScoredSegmentCacheKey
{
  unsigned length_idx = 0U;
  bool leaf_level = false;
  bool force_branch_buffer = false;
  double input_slew_ns = 0.0;
  double downstream_cap_pf = 0.0;

  auto operator==(const ScoredSegmentCacheKey& rhs) const -> bool = default;
};

struct ScoredSegmentCacheKeyHash
{
  auto operator()(const ScoredSegmentCacheKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = std::hash<unsigned>{}(key.length_idx);
    hash_value ^= std::hash<bool>{}(key.leaf_level) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<bool>{}(key.force_branch_buffer) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<double>{}(key.input_slew_ns) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    hash_value ^= std::hash<double>{}(key.downstream_cap_pf) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    return hash_value;
  }
};

struct PartialAnalyticalCandidate
{
  double current_slew_ns = 0.0;
  double upstream_load_cap_pf = 0.0;
  double root_source_cap_pf = 0.0;
  double accumulated_delay_ns = 0.0;
  double accumulated_power_w = 0.0;
  double conservative_delay_ns = 0.0;
  double conservative_power_w = 0.0;
  bool has_composition_state = false;
  PatternCompositionState composition_state;
  std::vector<PatternId> level_segment_pattern_ids;
  std::vector<std::vector<PatternId>> level_unit_pattern_ids;
  std::vector<double> level_load_caps_pf;
  std::vector<AnalyticalSegmentChoice> trace;
};

struct UnitModelRef
{
  PatternId pattern_id = PatternId::segment(0U);
  const AnalyticalModelSet* model_set = nullptr;
  PatternCompositionState composition_state;
};

struct PatternSequenceKey
{
  std::vector<unsigned> pattern_ids;

  auto operator==(const PatternSequenceKey& rhs) const -> bool = default;
};

struct PatternSequenceKeyHash
{
  auto operator()(const PatternSequenceKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = 0U;
    for (const unsigned pattern_id : key.pattern_ids) {
      hash_value ^= std::hash<unsigned>{}(pattern_id) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    }
    return hash_value;
  }
};

struct FunctionalComposeContext
{
  std::vector<UnitModelRef> unit_models;
  std::unordered_map<std::string, PatternId> unit_pattern_by_cell_master_and_terminal_semantic;
  std::unordered_map<PatternId, std::vector<PatternId>> decomposed_patterns;
  std::unordered_map<ScoredSegmentCacheKey, std::vector<ScoredSegment>, ScoredSegmentCacheKeyHash> scored_segments_by_key;
  std::unordered_map<PatternSequenceKey, PatternId, PatternSequenceKeyHash> materialized_patterns;
  unsigned next_segment_pattern_id = 0U;
};

struct AnalyticalModelProbe
{
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  bool slew_floored = false;
  bool cap_floored = false;
};

enum class DiagnosticPatternStage
{
  kFrontier,
  kDecomposed,
  kScored,
  kShortlisted,
};

auto MakeFailure(std::string reason) -> AnalyticalSolverResult;
auto ValidateSolveProblem(const AnalyticalHTreeSolveProblem& solve_problem) -> std::string;
auto ResolveNextSegmentPatternId(const BufferPatternLibrary& segment_pattern_library) -> unsigned;
auto MakePatternSequenceKey(const std::vector<PatternId>& pattern_ids) -> PatternSequenceKey;
auto ResolveSegmentPatternLibrary(const AnalyticalHTreeSolveProblem& solve_problem) -> const BufferPatternLibrary*;
auto DiagnosticPatternIds(const AnalyticalHTreeSolveProblem& solve_problem, std::size_t level_index) -> std::span<const PatternId>;
auto RecordDiagnosticPatternStage(std::span<const PatternId> diagnostic_pattern_ids, PatternId pattern_id, DiagnosticPatternStage stage,
                                  AnalyticalSolverResult& result) -> void;
auto RecordDiagnosticLibraryHits(const AnalyticalHTreeSolveProblem& solve_problem, AnalyticalSolverResult& result) -> void;
auto MaterializeFunctionalSegmentPattern(const std::vector<PatternId>& unit_pattern_ids, FunctionalComposeContext& context,
                                         BufferPatternLibrary& segment_pattern_library) -> std::optional<PatternId>;
auto ResolveAnalyticalRootProbeSlewNs(const AnalyticalHTreeSolveProblem& solve_problem) -> double;
auto ScoreModelSet(PatternId pattern_id, unsigned length_idx, const AnalyticalModelSet& model_set, double input_slew_ns,
                   double downstream_cap_pf, bool conservative) -> std::optional<ScoredSegment>;
auto ScoreSegment(const SegmentChar& segment_char, const AnalyticalModelSet& model_set, double input_slew_ns, double downstream_cap_pf,
                  bool conservative) -> std::optional<ScoredSegment>;
auto CollectUnitModelRefs(const AnalyticalHTreeSolveProblem& solve_problem) -> std::vector<UnitModelRef>;
auto BuildUnitPatternByCellMaster(const AnalyticalHTreeSolveProblem& solve_problem, const std::vector<UnitModelRef>& unit_models)
    -> std::unordered_map<std::string, PatternId>;
auto DecomposePatternToUnitSequence(PatternId pattern_id, const AnalyticalHTreeSolveProblem& solve_problem,
                                    FunctionalComposeContext& context) -> std::vector<PatternId>;
auto ScoreFunctionalUnitSequence(const AnalyticalHTreeSolveProblem& solve_problem, const std::vector<PatternId>& unit_pattern_ids,
                                 PatternId materialized_pattern_id, unsigned length_idx, double input_slew_ns, double downstream_cap_pf,
                                 bool conservative, AnalyticalSolverResult& result) -> std::optional<ScoredSegment>;
auto PreferScoredSegment(const ScoredSegment& lhs, const ScoredSegment& rhs) -> bool;
auto TrimScoredSegments(std::vector<ScoredSegment> scored_segments, std::size_t max_size) -> std::vector<ScoredSegment>;
auto TrimPartialCandidates(std::vector<PartialAnalyticalCandidate> candidates, std::size_t max_size)
    -> std::vector<PartialAnalyticalCandidate>;
auto SegmentHasAnyBuffer(const AnalyticalHTreeSolveProblem& solve_problem, PatternId pattern_id) -> bool;
auto MakeSegmentChoice(std::size_t level_index, const ScoredSegment& selected) -> AnalyticalSegmentChoice;
auto AccumulateHTreePower(double accumulated_power_w, std::size_t level_index, const ScoredSegment& selected) -> double;
auto TryPrependCompositionState(const AnalyticalHTreeSolveProblem& solve_problem, const PartialAnalyticalCandidate& downstream,
                                PatternId upstream_segment_pattern_id) -> std::optional<PatternCompositionState>;
auto CanAppendUnitPattern(const BufferPatternLibrary& segment_pattern_library, const std::vector<PatternId>& unit_pattern_ids,
                          PatternId next_pattern_id) -> bool;
auto IsFunctionalSequenceAllowedForLevel(const AnalyticalHTreeSolveProblem& solve_problem, const HTree::LevelPlan& level,
                                         PatternId segment_pattern_id) -> bool;
auto ShortlistSegmentsForLevel(const AnalyticalHTreeSolveProblem& solve_problem, const HTree::LevelPlan& level, double input_slew_ns,
                               std::size_t level_index, double downstream_cap_pf, FunctionalComposeContext* functional_context,
                               AnalyticalSolverResult& result) -> std::vector<ScoredSegment>;
auto BuildBeamCandidates(const AnalyticalHTreeSolveProblem& solve_problem, AnalyticalSolverResult& result)
    -> std::vector<AnalyticalCandidate>;

}  // namespace icts::htree::analytical_solver
