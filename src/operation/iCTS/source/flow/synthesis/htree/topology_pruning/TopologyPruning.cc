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
 * @file TopologyPruning.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief H-tree topology frontier composition and candidate selection.
 */

#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
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
#include "logger/Schema.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"

namespace icts::htree {
namespace {

auto BuildDelayPowerParetoFront(const std::vector<HTreeTopologyChar>& entries) -> std::vector<const HTreeTopologyChar*>
{
  std::vector<const HTreeTopologyChar*> pareto_front;
  pareto_front.reserve(entries.size());

  std::vector<const HTreeTopologyChar*> sorted_entries;
  sorted_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    sorted_entries.push_back(&entry);
  }

  std::ranges::sort(sorted_entries, [](const HTreeTopologyChar* lhs, const HTreeTopologyChar* rhs) -> bool {
    LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTree: null pareto entry encountered during delay-power sorting.";
    if (lhs->get_delay() != rhs->get_delay()) {
      return lhs->get_delay() < rhs->get_delay();
    }
    if (lhs->get_power() != rhs->get_power()) {
      return lhs->get_power() < rhs->get_power();
    }
    return lhs->get_pattern_id().pack() < rhs->get_pattern_id().pack();
  });

  double best_power_before_delay = std::numeric_limits<double>::infinity();
  std::size_t delay_group_begin = 0U;
  while (delay_group_begin < sorted_entries.size()) {
    std::size_t delay_group_end = delay_group_begin + 1U;
    while (delay_group_end < sorted_entries.size()
           && sorted_entries.at(delay_group_end)->get_delay() == sorted_entries.at(delay_group_begin)->get_delay()) {
      ++delay_group_end;
    }

    double delay_group_min_power = std::numeric_limits<double>::infinity();
    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      delay_group_min_power = std::min(delay_group_min_power, sorted_entries.at(index)->get_power());
    }

    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      const auto* entry = sorted_entries.at(index);
      if (best_power_before_delay <= entry->get_power() || delay_group_min_power < entry->get_power()) {
        continue;
      }
      pareto_front.push_back(entry);
    }

    best_power_before_delay = std::min(best_power_before_delay, delay_group_min_power);
    delay_group_begin = delay_group_end;
  }
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

auto ResolveSegmentFrontierKind(const BoundaryConstraints& boundary_constraints) -> SegmentFrontierKind
{
  if (boundary_constraints.force_branch_buffer) {
    return SegmentFrontierKind::kTerminalBranchBuffered;
  }
  return SegmentFrontierKind::kAll;
}

auto SelectSegmentEntriesForLevel(const SegmentFrontierCatalog& segment_frontier_catalog, unsigned length_idx,
                                  SegmentFrontierKind frontier_kind) -> const std::vector<SegmentChar>*
{
  return segment_frontier_catalog.find(length_idx, frontier_kind);
}

auto DescribeMissingSegmentEntries(SegmentFrontierKind frontier_kind) -> std::string
{
  switch (frontier_kind) {
    case SegmentFrontierKind::kTerminalBranchBuffered:
      return "missing_branch_buffered_segment_frontier";
    case SegmentFrontierKind::kTerminalLeafUnbuffered:
      return "missing_leaf_unbuffered_segment_frontier";
    case SegmentFrontierKind::kAll:
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

auto CountBoundaryFeasibleHTreeChars(const std::vector<HTreeTopologyChar>& entries, const BoundaryConstraints& boundary_constraints)
    -> std::size_t
{
  if (!HasBoundaryConstraints(boundary_constraints) || !boundary_constraints.top_input_slew_covering_idx.has_value()) {
    return entries.size();
  }

  return static_cast<std::size_t>(std::ranges::count_if(entries, [&](const HTreeTopologyChar& entry) -> bool {
    return entry.get_input_slew_idx() >= *boundary_constraints.top_input_slew_covering_idx;
  }));
}

auto BuildPatternSearch(const std::vector<HTree::LevelPlan>& levels, const SegmentFrontierCatalog& segment_frontier_catalog,
                        const BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& boundary_constraints,
                        const Tree& topology, RootDriverCompensationPass& compensation_pass) -> PatternSearchResult
{
  auto pattern_search_stage
      = SCHEMA_WRITER_INST.beginStage("HTreeDepth", "Build pattern frontier",
                                      {
                                          {"levels", std::to_string(levels.size())},
                                          {"segment_frontier_length_sets", std::to_string(segment_frontier_catalog.lengthCount())},
                                      });
  PatternSearchResult result;
  unsigned next_topology_pattern_id = 0U;
  std::vector<HTreeTopologyChar> current_frontier_entries;

  for (std::size_t reverse_level = levels.size(); reverse_level > 0U; --reverse_level) {
    const auto& level = levels.at(reverse_level - 1U);
    result.failure_level = static_cast<unsigned>(reverse_level - 1U);
    result.failure_length_idx = level.aligned_length_idx;

    if (segment_frontier_catalog.find(level.aligned_length_idx, SegmentFrontierKind::kAll) == nullptr) {
      result.failure_reason = "missing_segment_frontier";
      pattern_search_stage.failed({
          {"reason", result.failure_reason},
          {"failure_level", std::to_string(result.failure_level)},
          {"failure_length_idx", std::to_string(result.failure_length_idx)},
      });
      return result;
    }

    const SegmentFrontierKind frontier_kind = ResolveSegmentFrontierKind(boundary_constraints);
    const auto* base_segment_frontier = SelectSegmentEntriesForLevel(segment_frontier_catalog, level.aligned_length_idx, frontier_kind);
    if (base_segment_frontier == nullptr || base_segment_frontier->empty()) {
      result.failure_reason = DescribeMissingSegmentEntries(frontier_kind);
      pattern_search_stage.failed({
          {"reason", result.failure_reason},
          {"failure_level", std::to_string(result.failure_level)},
          {"failure_length_idx", std::to_string(result.failure_length_idx)},
      });
      return result;
    }

    auto seed_entries
        = MakeHTreeSeedEntries(*base_segment_frontier, segment_pattern_library, result.topology_pattern_library, next_topology_pattern_id);
    if (current_frontier_entries.empty()) {
      current_frontier_entries = std::move(seed_entries);
      continue;
    }

    auto [composed_entries, updated_next_pattern_id]
        = ComposeHTreeFrontierEntries(seed_entries, current_frontier_entries, result.topology_pattern_library, next_topology_pattern_id);
    next_topology_pattern_id = updated_next_pattern_id;
    current_frontier_entries = std::move(composed_entries);
    if (current_frontier_entries.empty()) {
      result.failure_reason = "empty_frontier";
      pattern_search_stage.failed({
          {"reason", result.failure_reason},
          {"failure_level", std::to_string(result.failure_level)},
          {"failure_length_idx", std::to_string(result.failure_length_idx)},
      });
      return result;
    }
  }

  result.success = !current_frontier_entries.empty();
  compensation_pass.apply(current_frontier_entries, result.topology_pattern_library, segment_pattern_library, topology);
  current_frontier_entries
      = BuildHTreeStateFrontier(current_frontier_entries, [&](const HTreeTopologyChar& entry) -> PatternCompositionState {
          return result.topology_pattern_library.getCompositionState(entry.get_pattern_id());
        });
  result.success = !current_frontier_entries.empty();
  result.frontier = std::move(current_frontier_entries);
  if (result.success) {
    pattern_search_stage.finished({
        {"frontier_entries", std::to_string(result.frontier.size())},
        {"topology_patterns", std::to_string(next_topology_pattern_id)},
    });
  } else {
    pattern_search_stage.failed({{"reason", "empty_frontier_after_compensation"}});
  }
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
  std::vector<CandidateCharRef> sorted_entries;
  sorted_entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr) {
      continue;
    }
    sorted_entries.push_back(entry_ref);
  }

  std::ranges::sort(sorted_entries, [](const CandidateCharRef& lhs, const CandidateCharRef& rhs) -> bool {
    LOG_FATAL_IF(lhs.entry == nullptr || rhs.entry == nullptr) << "HTree: null global candidate frontier entry encountered.";
    if (lhs.entry->get_delay() != rhs.entry->get_delay()) {
      return lhs.entry->get_delay() < rhs.entry->get_delay();
    }
    if (lhs.entry->get_power() != rhs.entry->get_power()) {
      return lhs.entry->get_power() < rhs.entry->get_power();
    }
    return PreferPowerMedianOrder(*lhs.entry, *rhs.entry);
  });

  std::vector<CandidateCharRef> pareto_front;
  pareto_front.reserve(sorted_entries.size());
  double best_power_before_delay = std::numeric_limits<double>::infinity();
  std::size_t delay_group_begin = 0U;
  while (delay_group_begin < sorted_entries.size()) {
    std::size_t delay_group_end = delay_group_begin + 1U;
    while (delay_group_end < sorted_entries.size()
           && sorted_entries.at(delay_group_end).entry->get_delay() == sorted_entries.at(delay_group_begin).entry->get_delay()) {
      ++delay_group_end;
    }

    double delay_group_min_power = std::numeric_limits<double>::infinity();
    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      delay_group_min_power = std::min(delay_group_min_power, sorted_entries.at(index).entry->get_power());
    }

    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      const auto& entry_ref = sorted_entries.at(index);
      if (best_power_before_delay <= entry_ref.entry->get_power() || delay_group_min_power < entry_ref.entry->get_power()) {
        continue;
      }
      pareto_front.push_back(entry_ref);
    }

    best_power_before_delay = std::min(best_power_before_delay, delay_group_min_power);
    delay_group_begin = delay_group_end;
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

auto BuildPerDepthDelayPowerParetoRefs(const std::vector<CandidateCharRef>& entries) -> std::vector<CandidateCharRef>
{
  std::vector<CandidateCharRef> pareto_entries;
  pareto_entries.reserve(entries.size());

  std::unordered_map<std::size_t, std::vector<CandidateCharRef>> entries_by_candidate;
  std::vector<std::size_t> candidate_indices;
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr) {
      continue;
    }
    if (!entries_by_candidate.contains(entry_ref.candidate_index)) {
      candidate_indices.push_back(entry_ref.candidate_index);
    }
    entries_by_candidate[entry_ref.candidate_index].push_back(entry_ref);
  }

  for (const auto candidate_index : candidate_indices) {
    const auto group_it = entries_by_candidate.find(candidate_index);
    LOG_FATAL_IF(group_it == entries_by_candidate.end()) << "HTree: missing per-depth candidate group during Pareto compression.";
    auto local_pareto = BuildDelayPowerParetoFront(group_it->second);
    pareto_entries.insert(pareto_entries.end(), local_pareto.begin(), local_pareto.end());
  }
  return pareto_entries;
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

auto EvaluateCandidateBuild(const std::vector<HTree::LevelPlan>& levels, const SegmentFrontierCatalog& segment_frontier_catalog,
                            const BufferPatternLibrary& segment_pattern_library, const BoundaryConstraints& boundary_constraints,
                            const Tree& topology, SinkLoadRegionLegalityContext& sink_load_region_legality_context, std::size_t leaf_count,
                            unsigned depth, unsigned char_slew_steps, RootDriverCompensationPass& compensation_pass)
    -> CandidateBuildEvaluation
{
  CandidateBuildEvaluation result;
  result.depth = depth;
  result.leaf_count = leaf_count;
  result.boundary_constraints = boundary_constraints;
  result.levels = levels;
  compensation_pass.beginCandidateBuild();

  const bool has_boundary_constraints = HasBoundaryConstraints(boundary_constraints);
  const auto topology_assembly
      = BuildPatternSearch(levels, segment_frontier_catalog, segment_pattern_library, boundary_constraints, topology, compensation_pass);
  if (!topology_assembly.success) {
    result.failure_reason = topology_assembly.failure_reason.empty() ? std::string{"empty_frontier"} : topology_assembly.failure_reason;
    result.failure_level = topology_assembly.failure_level;
    result.failure_length_idx = topology_assembly.failure_length_idx;
    return result;
  }

  result.topology_pattern_library = topology_assembly.topology_pattern_library;
  result.final_frontier_count = topology_assembly.frontier.size();
  result.candidate_solution_count = topology_assembly.frontier.size();
  if (has_boundary_constraints) {
    SinkLoadRegionEntryFilterResult candidate_sink_load_region_filter;
    SinkLoadRegionEntryFilterResult feasible_sink_load_region_filter;
    std::vector<HTreeTopologyChar> feasible_raw_frontier;
    {
      auto filter_stage = SCHEMA_WRITER_INST.beginStage("HTreeDepth", "Filter sink-load region",
                                                        {
                                                            {"depth", std::to_string(depth)},
                                                            {"raw_frontier_entries", std::to_string(topology_assembly.frontier.size())},
                                                            {"has_boundary_constraints", "true"},
                                                        });
      candidate_sink_load_region_filter
          = FilterSinkLoadRegionLegalEntries(topology_assembly.frontier, topology, result.topology_pattern_library, segment_pattern_library,
                                             sink_load_region_legality_context);
      result.feasible_solution_count = CountBoundaryFeasibleHTreeChars(topology_assembly.frontier, boundary_constraints);
      feasible_raw_frontier = FilterBoundaryFeasibleHTreeChars(topology_assembly.frontier, boundary_constraints);
      feasible_sink_load_region_filter = FilterSinkLoadRegionLegalEntries(feasible_raw_frontier, topology, result.topology_pattern_library,
                                                                          segment_pattern_library, sink_load_region_legality_context);
      filter_stage.finished({
          {"candidate_frontier_entries", std::to_string(candidate_sink_load_region_filter.entries.size())},
          {"feasible_raw_entries", std::to_string(feasible_raw_frontier.size())},
          {"feasible_frontier_entries", std::to_string(feasible_sink_load_region_filter.entries.size())},
      });
    }
    result.candidate_frontier_entries = std::move(candidate_sink_load_region_filter.entries);
    result.feasible_frontier_entries = std::move(feasible_sink_load_region_filter.entries);
    if (result.candidate_frontier_entries.empty() && !candidate_sink_load_region_filter.first_failure_reason.empty()) {
      result.failure_reason = candidate_sink_load_region_filter.first_failure_reason;
    }
    if (result.feasible_frontier_entries.empty() && result.failure_reason.empty()
        && !feasible_sink_load_region_filter.first_failure_reason.empty()) {
      result.failure_reason = feasible_sink_load_region_filter.first_failure_reason;
    }
  } else {
    result.feasible_solution_count = result.candidate_solution_count;
    SinkLoadRegionEntryFilterResult feasible_sink_load_region_filter;
    {
      auto filter_stage = SCHEMA_WRITER_INST.beginStage("HTreeDepth", "Filter sink-load region",
                                                        {
                                                            {"depth", std::to_string(depth)},
                                                            {"raw_frontier_entries", std::to_string(topology_assembly.frontier.size())},
                                                            {"has_boundary_constraints", "false"},
                                                        });
      feasible_sink_load_region_filter
          = FilterSinkLoadRegionLegalEntries(topology_assembly.frontier, topology, result.topology_pattern_library, segment_pattern_library,
                                             sink_load_region_legality_context);
      filter_stage.finished({{"feasible_frontier_entries", std::to_string(feasible_sink_load_region_filter.entries.size())}});
    }
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
