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
 * @file AnalyticalSolverRequest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree solver request validation and diagnostics.
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AnalyticalModel.hh"
#include "BufferingPattern.hh"
#include "PatternId.hh"
#include "ValueLattice.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolver.hh"
#include "synthesis/htree/analytical_solver/AnalyticalSolverInternal.hh"
#include "synthesis/htree/segment_pruning/SegmentLibrary.hh"

namespace icts::htree::analytical_solver {

auto MakeFailure(std::string reason) -> AnalyticalSolverResult
{
  AnalyticalSolverResult result;
  result.success = false;
  result.failure_reason = std::move(reason);
  return result;
}

auto ValidateRequest(const AnalyticalSolverRequest& request) -> std::string
{
  if (request.levels == nullptr || request.levels->empty()) {
    return "missing_levels";
  }
  if (!request.options.use_functional_unit_compose && request.segment_frontier_catalog == nullptr) {
    return "missing_segment_frontier_catalog";
  }
  if (request.segment_pattern_library == nullptr) {
    return "missing_segment_pattern_library";
  }
  if (request.options.use_functional_unit_compose && request.mutable_segment_pattern_library == nullptr) {
    return "missing_mutable_segment_pattern_library";
  }
  if (request.model_catalog == nullptr || request.model_catalog->empty()) {
    return "missing_analytical_model_catalog";
  }
  if (!request.slew_lattice.isValid() || !request.cap_lattice.isValid()) {
    return "invalid_lattice";
  }
  if (request.options.root_input_slew_ns < 0.0) {
    return "invalid_root_input_slew";
  }
  if (request.options.representative_leaf_load_cap_pf <= 0.0) {
    return "invalid_representative_leaf_load_cap";
  }
  return {};
}

auto ResolveNextSegmentPatternId(const BufferPatternLibrary& segment_pattern_library) -> unsigned
{
  unsigned next_id = 0U;
  for (const auto& [pattern_id, pattern] : segment_pattern_library.patterns) {
    (void) pattern;
    if (pattern_id.domain == PatternDomain::kSegmentPattern) {
      next_id = std::max(next_id, pattern_id.local_id + 1U);
    }
  }
  return next_id;
}

auto MakePatternSequenceKey(const std::vector<PatternId>& pattern_ids) -> PatternSequenceKey
{
  PatternSequenceKey key;
  key.pattern_ids.reserve(pattern_ids.size());
  for (const auto pattern_id : pattern_ids) {
    key.pattern_ids.push_back(pattern_id.pack());
  }
  return key;
}

auto ResolveSegmentPatternLibrary(const AnalyticalSolverRequest& request) -> const BufferPatternLibrary*
{
  return request.segment_pattern_library != nullptr ? request.segment_pattern_library : request.mutable_segment_pattern_library;
}

auto DiagnosticPatternIds(const AnalyticalSolverRequest& request, std::size_t level_index) -> std::span<const PatternId>
{
  if (request.options.diagnostic_segment_pattern_ids.empty() || request.levels == nullptr || request.levels->empty()) {
    return {};
  }
  if (request.options.diagnostic_segment_pattern_ids.size() != request.levels->size() || level_index >= request.levels->size()) {
    return {};
  }
  return std::span<const PatternId>(&request.options.diagnostic_segment_pattern_ids.at(level_index), 1U);
}

namespace {

auto ContainsDiagnosticPattern(std::span<const PatternId> diagnostic_pattern_ids, PatternId pattern_id) -> bool
{
  return std::ranges::find(diagnostic_pattern_ids, pattern_id) != diagnostic_pattern_ids.end();
}

}  // namespace

auto RecordDiagnosticPatternStage(std::span<const PatternId> diagnostic_pattern_ids, PatternId pattern_id, DiagnosticPatternStage stage,
                                  AnalyticalSolverResult& result) -> void
{
  if (!ContainsDiagnosticPattern(diagnostic_pattern_ids, pattern_id)) {
    return;
  }
  switch (stage) {
    case DiagnosticPatternStage::kFrontier:
      ++result.diagnostic_frontier_hit_count;
      break;
    case DiagnosticPatternStage::kDecomposed:
      ++result.diagnostic_decomposed_count;
      break;
    case DiagnosticPatternStage::kScored:
      ++result.diagnostic_scored_count;
      break;
    case DiagnosticPatternStage::kShortlisted:
      ++result.diagnostic_shortlisted_count;
      break;
  }
}

auto RecordDiagnosticLibraryHits(const AnalyticalSolverRequest& request, AnalyticalSolverResult& result) -> void
{
  const auto* segment_pattern_library = ResolveSegmentPatternLibrary(request);
  if (segment_pattern_library == nullptr) {
    return;
  }
  for (const auto pattern_id : request.options.diagnostic_segment_pattern_ids) {
    if (segment_pattern_library->find(pattern_id) != nullptr) {
      ++result.diagnostic_library_hit_count;
    }
  }
}

auto MaterializeFunctionalSegmentPattern(const std::vector<PatternId>& unit_pattern_ids, FunctionalComposeContext& context,
                                         BufferPatternLibrary& segment_pattern_library) -> std::optional<PatternId>
{
  if (unit_pattern_ids.empty()) {
    return std::nullopt;
  }
  if (unit_pattern_ids.size() == 1U) {
    return unit_pattern_ids.front();
  }

  const auto key = MakePatternSequenceKey(unit_pattern_ids);
  if (const auto it = context.materialized_patterns.find(key); it != context.materialized_patterns.end()) {
    return it->second;
  }

  if (segment_pattern_library.find(unit_pattern_ids.front()) == nullptr) {
    return std::nullopt;
  }
  SegmentPatternLibraryCombiner combiner(segment_pattern_library, context.next_segment_pattern_id);
  PatternId merged_pattern_id = unit_pattern_ids.front();
  for (std::size_t index = 1U; index < unit_pattern_ids.size(); ++index) {
    const PatternId next_pattern_id = unit_pattern_ids.at(index);
    if (segment_pattern_library.find(next_pattern_id) == nullptr || !combiner.canCompose(merged_pattern_id, next_pattern_id)) {
      return std::nullopt;
    }
    merged_pattern_id = combiner.combine(merged_pattern_id, next_pattern_id);
  }

  context.next_segment_pattern_id = combiner.get_next_id();
  context.materialized_patterns.emplace(key, merged_pattern_id);
  return merged_pattern_id;
}

}  // namespace icts::htree::analytical_solver
