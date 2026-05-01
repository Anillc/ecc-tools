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
 * @file CandidateSelection.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief H-tree topology frontier composition and candidate selection.
 */

#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CharCore.hh"
#include "Frontier.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTraits.hh"
#include "HashJoinEngine.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"

namespace icts::htree {
namespace {

auto DelayPowerDominates(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  const bool not_worse = lhs.get_delay() <= rhs.get_delay() && lhs.get_power() <= rhs.get_power();
  const bool strictly_better = lhs.get_delay() < rhs.get_delay() || lhs.get_power() < rhs.get_power();
  return not_worse && strictly_better;
}

auto BuildDelayPowerParetoFront(const std::vector<HTreeTopologyChar>& entries) -> std::vector<const HTreeTopologyChar*>
{
  std::vector<const HTreeTopologyChar*> pareto_front;
  pareto_front.reserve(entries.size());

  for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
    bool dominated = false;
    for (std::size_t other_index = 0; other_index < entries.size(); ++other_index) {
      if (entry_index == other_index) {
        continue;
      }
      if (DelayPowerDominates(entries[other_index], entries[entry_index])) {
        dominated = true;
        break;
      }
    }

    if (!dominated) {
      pareto_front.push_back(&entries[entry_index]);
    }
  }

  std::ranges::sort(pareto_front, [](const HTreeTopologyChar* lhs, const HTreeTopologyChar* rhs) -> bool {
    if (lhs->get_delay() != rhs->get_delay()) {
      return lhs->get_delay() < rhs->get_delay();
    }
    if (lhs->get_power() != rhs->get_power()) {
      return lhs->get_power() < rhs->get_power();
    }
    return lhs->get_pattern_id().pack() < rhs->get_pattern_id().pack();
  });
  return pareto_front;
}

auto PreferDelayPowerTieBreak(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  return (lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() < rhs.get_output_slew_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() < rhs.get_delay())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() < rhs.get_power())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() < rhs.get_load_cap_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() == rhs.get_load_cap_idx()
             && lhs.get_input_slew_idx() > rhs.get_input_slew_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() == rhs.get_load_cap_idx()
             && lhs.get_input_slew_idx() == rhs.get_input_slew_idx() && lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack());
}

auto PreferPowerMedianOrder(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  if (lhs.get_power() != rhs.get_power()) {
    return lhs.get_power() < rhs.get_power();
  }
  if (lhs.get_delay() != rhs.get_delay()) {
    return lhs.get_delay() < rhs.get_delay();
  }
  return PreferDelayPowerTieBreak(lhs, rhs);
}

auto ResolveSegmentEntrySelection(const BoundaryConstraints& boundary_constraints) -> SegmentEntrySelection
{
  if (boundary_constraints.force_branch_buffer) {
    return SegmentEntrySelection::kBranchBuffered;
  }
  return SegmentEntrySelection::kAllFrontier;
}

auto SelectSegmentEntriesForLevel(const SegmentCandidateFrontierSet& entry_set, SegmentEntrySelection selection)
    -> const std::vector<SegmentChar>&
{
  switch (selection) {
    case SegmentEntrySelection::kBranchBuffered:
      return entry_set.branch_buffered_entries;
    case SegmentEntrySelection::kLeafUnbuffered:
      return entry_set.leaf_unbuffered_entries;
    case SegmentEntrySelection::kAllFrontier:
      return entry_set.all_frontier_entries;
  }
  return entry_set.all_frontier_entries;
}

auto DescribeMissingSegmentEntries(SegmentEntrySelection selection) -> std::string
{
  switch (selection) {
    case SegmentEntrySelection::kBranchBuffered:
      return "missing_branch_buffered_segment_frontier";
    case SegmentEntrySelection::kLeafUnbuffered:
      return "missing_leaf_unbuffered_segment_frontier";
    case SegmentEntrySelection::kAllFrontier:
      return "missing_segment_frontier";
  }
  return "missing_segment_frontier";
}

auto MakeHTreeSeedEntries(const std::vector<SegmentChar>& segment_frontier, const BufferPatternLibrary& segment_pattern_library,
                          TopologyPatternLibrary& topology_library, unsigned& next_pattern_id) -> std::vector<HTreeTopologyChar>
{
  std::vector<HTreeTopologyChar> seed_entries;
  seed_entries.reserve(segment_frontier.size());
  for (const auto& segment_entry : segment_frontier) {
    const auto topology_pattern_id = PatternId::topology(next_pattern_id++);
    topology_library.addSeed(topology_pattern_id, segment_entry.get_pattern_id(),
                             segment_pattern_library.getCompositionState(segment_entry.get_pattern_id()));
    const CharCore core(segment_entry.get_input_slew_idx(), segment_entry.get_output_slew_idx(), segment_entry.get_driven_cap_idx(),
                        segment_entry.get_load_cap_idx(), segment_entry.get_delay(), segment_entry.get_power(), topology_pattern_id,
                        segment_entry.get_source_boundary_net_switch_power());
    seed_entries.emplace_back(core, 1U);
  }
  return seed_entries;
}

auto ComposeHTreeFrontierEntries(const std::vector<HTreeTopologyChar>& upstream, const std::vector<HTreeTopologyChar>& downstream,
                                 TopologyPatternLibrary& topology_library, unsigned start_pattern_id)
    -> std::pair<std::vector<HTreeTopologyChar>, unsigned>
{
  if (upstream.empty() || downstream.empty()) {
    return {{}, start_pattern_id};
  }

  TopologyPatternLibraryCombiner combiner(topology_library, start_pattern_id);
  auto pruner = MakeHTreeStateFrontierPruner([&](const HTreeTopologyChar& entry) -> PatternCompositionState {
    return topology_library.getCompositionState(entry.get_pattern_id());
  });
  std::vector<HTreeTopologyChar> frontier_entries;
  detail::HashJoinConcat<HTreeTopologyChar, HTreeTraits>(upstream, downstream, combiner, frontier_entries, &pruner);
  SortHTreeFrontierEntries(frontier_entries);
  return {std::move(frontier_entries), combiner.get_next_id()};
}

auto BuildPatternSearch(const std::vector<HTree::LevelPlan>& levels,
                        const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                        const BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& boundary_constraints)
    -> PatternSearchResult
{
  PatternSearchResult result;
  unsigned next_topology_pattern_id = 0U;
  std::vector<HTreeTopologyChar> current_frontier_entries;

  for (std::size_t reverse_level = levels.size(); reverse_level > 0U; --reverse_level) {
    const auto& level = levels.at(reverse_level - 1U);
    result.failure_level = static_cast<unsigned>(reverse_level - 1U);
    result.failure_length_idx = level.aligned_length_idx;

    const auto entry_set_it = entry_sets_by_length.find(level.aligned_length_idx);
    if (entry_set_it == entry_sets_by_length.end()) {
      result.failure_reason = "missing_segment_frontier";
      return result;
    }

    const auto& entry_set = entry_set_it->second;
    const SegmentEntrySelection entry_selection = ResolveSegmentEntrySelection(boundary_constraints);
    const auto& base_segment_frontier = SelectSegmentEntriesForLevel(entry_set, entry_selection);
    if (base_segment_frontier.empty()) {
      result.failure_reason = DescribeMissingSegmentEntries(entry_selection);
      return result;
    }

    auto seed_entries
        = MakeHTreeSeedEntries(base_segment_frontier, segment_pattern_library, result.topology_pattern_library, next_topology_pattern_id);
    if (current_frontier_entries.empty()) {
      current_frontier_entries = std::move(seed_entries);
      continue;
    }

    auto [composed_frontier_entries, updated_next_pattern_id]
        = ComposeHTreeFrontierEntries(seed_entries, current_frontier_entries, result.topology_pattern_library, next_topology_pattern_id);
    next_topology_pattern_id = updated_next_pattern_id;
    current_frontier_entries = std::move(composed_frontier_entries);
    if (current_frontier_entries.empty()) {
      result.failure_reason = "empty_frontier";
      return result;
    }
  }

  result.success = !current_frontier_entries.empty();
  result.frontier = std::move(current_frontier_entries);
  return result;
}

auto FilterBoundaryFeasibleHTreeChars(const std::vector<HTreeTopologyChar>& entries, const BoundaryConstraints& boundary_constraints)
    -> std::vector<HTreeTopologyChar>
{
  if (!HasBoundaryConstraints(boundary_constraints)) {
    return entries;
  }

  std::vector<HTreeTopologyChar> filtered_entries;
  filtered_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    if (boundary_constraints.top_input_slew_covering_idx.has_value()
        && entry.get_input_slew_idx() < *boundary_constraints.top_input_slew_covering_idx) {
      continue;
    }
    filtered_entries.push_back(entry);
  }
  return filtered_entries;
}

auto SelectBestHTreeChar(const std::vector<HTreeTopologyChar>& entries) -> std::optional<HTreeTopologyChar>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  auto pareto_front = BuildDelayPowerParetoFront(entries);
  if (pareto_front.empty()) {
    return entries.front();
  }

  std::ranges::sort(pareto_front, [](const HTreeTopologyChar* lhs, const HTreeTopologyChar* rhs) -> bool {
    LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTree: null pareto entry encountered during final selection.";
    return PreferPowerMedianOrder(*lhs, *rhs);
  });

  const std::size_t median_index = (pareto_front.size() - 1U) / 2U;
  return *pareto_front.at(median_index);
}

auto BuildDelayPowerParetoFront(const std::vector<CandidateCharRef>& entries) -> std::vector<CandidateCharRef>
{
  std::vector<CandidateCharRef> pareto_front;
  pareto_front.reserve(entries.size());

  for (std::size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
    if (entries.at(entry_index).entry == nullptr) {
      continue;
    }

    bool dominated = false;
    for (std::size_t other_index = 0; other_index < entries.size(); ++other_index) {
      if (entry_index == other_index || entries.at(other_index).entry == nullptr) {
        continue;
      }
      if (DelayPowerDominates(*entries.at(other_index).entry, *entries.at(entry_index).entry)) {
        dominated = true;
        break;
      }
    }

    if (!dominated) {
      pareto_front.push_back(entries.at(entry_index));
    }
  }

  std::ranges::sort(pareto_front, [](const CandidateCharRef& lhs, const CandidateCharRef& rhs) -> bool {
    LOG_FATAL_IF(lhs.entry == nullptr || rhs.entry == nullptr) << "HTree: null global candidate frontier entry encountered.";
    return PreferPowerMedianOrder(*lhs.entry, *rhs.entry);
  });
  return pareto_front;
}

}  // namespace

auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const BoundaryConstraints& boundary_constraints, unsigned slew_steps)
    -> double
{
  double score = 0.0;
  if (boundary_constraints.top_input_slew_covering_idx.has_value() && slew_steps > 0U) {
    score += static_cast<double>(entry.get_input_slew_idx()) / static_cast<double>(slew_steps);
  }
  return score;
}

auto SelectBestGlobalEntry(const std::vector<CandidateCharRef>& entries) -> std::optional<CandidateCharRef>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  auto pareto_front = BuildDelayPowerParetoFront(entries);
  if (pareto_front.empty()) {
    return std::nullopt;
  }

  const std::size_t median_index = (pareto_front.size() - 1U) / 2U;
  return pareto_front.at(median_index);
}

auto EvaluateCandidateBuild(const std::vector<HTree::LevelPlan>& levels,
                            const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                            const BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& boundary_constraints,
                            const Tree& topology, SinkLoadRegionLegalityContext& sink_load_region_legality_context, std::size_t leaf_count,
                            unsigned depth, unsigned char_slew_steps) -> CandidateBuildEvaluation
{
  CandidateBuildEvaluation result;
  result.depth = depth;
  result.leaf_count = leaf_count;
  result.boundary_constraints = boundary_constraints;
  result.levels = levels;

  const bool has_boundary_constraints = HasBoundaryConstraints(boundary_constraints);
  const auto topology_assembly = BuildPatternSearch(levels, entry_sets_by_length, segment_pattern_library, boundary_constraints);
  if (!topology_assembly.success) {
    result.failure_reason = topology_assembly.failure_reason.empty() ? std::string{"empty_frontier"} : topology_assembly.failure_reason;
    result.failure_level = topology_assembly.failure_level;
    result.failure_length_idx = topology_assembly.failure_length_idx;
    return result;
  }

  result.topology_pattern_library = topology_assembly.topology_pattern_library;
  result.final_frontier_count = topology_assembly.frontier.size();
  if (has_boundary_constraints) {
    result.candidate_chars = topology_assembly.frontier;
    auto candidate_sink_load_region_filter = FilterSinkLoadRegionLegalEntries(
        result.candidate_chars, topology, result.topology_pattern_library, segment_pattern_library, sink_load_region_legality_context);
    result.candidate_frontier_entries = std::move(candidate_sink_load_region_filter.entries);
    result.feasible_chars = FilterBoundaryFeasibleHTreeChars(result.candidate_chars, boundary_constraints);
    auto feasible_sink_load_region_filter = FilterSinkLoadRegionLegalEntries(
        result.feasible_chars, topology, result.topology_pattern_library, segment_pattern_library, sink_load_region_legality_context);
    result.feasible_frontier_entries = std::move(feasible_sink_load_region_filter.entries);
    if (result.candidate_frontier_entries.empty() && !candidate_sink_load_region_filter.first_failure_reason.empty()) {
      result.failure_reason = candidate_sink_load_region_filter.first_failure_reason;
    }
    if (result.feasible_frontier_entries.empty() && result.failure_reason.empty()
        && !feasible_sink_load_region_filter.first_failure_reason.empty()) {
      result.failure_reason = feasible_sink_load_region_filter.first_failure_reason;
    }
  } else {
    result.feasible_chars = topology_assembly.frontier;
    auto feasible_sink_load_region_filter = FilterSinkLoadRegionLegalEntries(
        result.feasible_chars, topology, result.topology_pattern_library, segment_pattern_library, sink_load_region_legality_context);
    result.feasible_frontier_entries = std::move(feasible_sink_load_region_filter.entries);
    if (result.feasible_frontier_entries.empty() && !feasible_sink_load_region_filter.first_failure_reason.empty()) {
      result.failure_reason = feasible_sink_load_region_filter.first_failure_reason;
    }
  }

  if (!result.feasible_frontier_entries.empty()) {
    result.best_char = SelectBestHTreeChar(result.feasible_frontier_entries);
  } else if (has_boundary_constraints) {
    result.best_char = SelectBestHTreeChar(result.candidate_frontier_entries);
    if (result.best_char.has_value()) {
      result.used_boundary_fallback = true;
      result.boundary_fallback_reason = "no_strict_boundary_feasible_solution";
      result.boundary_fallback_score = CalcBoundaryFallbackScore(*result.best_char, boundary_constraints, char_slew_steps);
    }
  }

  result.success = result.best_char.has_value();
  if (!result.success && result.failure_reason.empty()) {
    result.failure_reason = "no_sink_load_region_legal_frontier_entries";
  }
  return result;
}

auto FilterGlobalEntriesBySinkLoadRegionCoverage(const std::vector<CandidateCharRef>& entries,
                                                 const std::vector<CandidateBuildEvaluation>& evaluations, const Tree& topology,
                                                 const BufferPatternLibrary& segment_pattern_library,
                                                 SinkLoadRegionLegalityContext& legality_context) -> CandidateCharRefFilterResult
{
  CandidateCharRefFilterResult result;
  result.entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr || entry_ref.candidate_index >= evaluations.size()) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = "invalid_global_candidate_ref";
      }
      continue;
    }

    const auto& evaluation = evaluations[entry_ref.candidate_index];
    const auto legality = ResolveSinkLoadRegionLegality(topology, entry_ref.entry->get_pattern_id(), evaluation.topology_pattern_library,
                                                        segment_pattern_library, legality_context);
    if (!legality.legal) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = legality.failure_reason;
      }
      continue;
    }

    if (legality.required_leaf_load_cap_covering_idx.has_value()
        && entry_ref.entry->get_leaf_load_cap_idx() < *legality.required_leaf_load_cap_covering_idx) {
      if (result.first_failure_reason.empty()) {
        std::ostringstream detail;
        detail << "sink_load_region_boundary_load_coverage_violation required_leaf_load_cap_idx="
               << *legality.required_leaf_load_cap_covering_idx << ", entry_leaf_load_cap_idx=" << entry_ref.entry->get_leaf_load_cap_idx()
               << ", max_real_load_cap_pf=" << legality.required_leaf_load_cap_pf;
        result.first_failure_reason = detail.str();
      }
      continue;
    }

    result.entries.push_back(entry_ref);
  }
  return result;
}

}  // namespace icts::htree
