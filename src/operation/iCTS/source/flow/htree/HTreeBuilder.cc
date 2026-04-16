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
 * @file HTreeBuilder.cc
 * @author OpenAI Codex
 * @date 2026-04-14
 * @brief End-to-end H-tree synthesis flow built from topology and characterization modules.
 */

#include "htree/HTreeBuilder.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharCore.hh"
#include "Log.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "adapter/sta/STAAdapter.hh"
#include "characterization/CharBuilder.hh"
#include "characterization/HTreeTopologyCharTable.hh"
#include "characterization/Pruner.hh"
#include "characterization/SegmentCharTable.hh"
#include "config/Config.hh"
#include "geometry/Geometry.hh"
#include "io/Wrapper.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "topology/TopologyGen.hh"

namespace icts {
namespace {

enum class CharGridSource
{
  kNone,
  kRuntimeConfig,
  kAutoDerived
};

struct CharacterizationGridPlan
{
  double wire_length_unit_um = 0.0;
  unsigned wire_length_iterations = 0U;
  unsigned unique_level_bins = 0U;
  double configured_wire_length_unit_um = 0.0;
  double auto_derived_wire_length_unit_um = 0.0;
  unsigned requested_level_lengths = 0U;
  bool configured_wire_length_missing = false;
  bool configured_grid_collapsed = false;
  bool adapted = false;
  CharGridSource source = CharGridSource::kNone;
};

auto toCharGridSourceName(CharGridSource source) -> const char*
{
  switch (source) {
    case CharGridSource::kNone:
      return "none";
    case CharGridSource::kRuntimeConfig:
      return "runtime_config";
    case CharGridSource::kAutoDerived:
      return "auto_derived";
  }
  return "none";
}

auto logInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void
{
  schema::EmitTable(title, headers, rows);
}

struct BufferPortInfo
{
  std::string input_pin;
  std::string output_pin;
};

struct BufferInstancePins
{
  Inst* inst = nullptr;
  Pin* input_pin = nullptr;
  Pin* output_pin = nullptr;
};

struct BufferPatternRegistry
{
  auto add(BufferingPattern pattern) -> void { patterns[pattern.get_pattern_id()] = std::move(pattern); }

  auto find(PatternId pattern_id) const -> const BufferingPattern*
  {
    auto it = patterns.find(pattern_id);
    return it == patterns.end() ? nullptr : &it->second;
  }

  std::unordered_map<PatternId, BufferingPattern> patterns;
};

struct TopologyPatternRegistry
{
  auto add(HTreeTopologyPattern pattern) -> void { patterns[pattern.get_pattern_id()] = std::move(pattern); }

  auto find(PatternId pattern_id) const -> const HTreeTopologyPattern*
  {
    auto it = patterns.find(pattern_id);
    return it == patterns.end() ? nullptr : &it->second;
  }

  std::unordered_map<PatternId, HTreeTopologyPattern> patterns;
};

class SegmentPatternRegistryCombiner
{
 public:
  SegmentPatternRegistryCombiner(BufferPatternRegistry& registry, unsigned start_id) : _registry(&registry), _next_id(start_id) {}

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _registry->find(upstream);
    const auto* downstream_pattern = _registry->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTreeBuilder: missing segment pattern during composition.";

    const PatternId merged_pattern_id = PatternId::segment(_next_id++);
    auto merged_pattern = BufferingPattern::concat(*upstream_pattern, *downstream_pattern);
    _registry->add(BufferingPattern(merged_pattern.get_length_idx(), merged_pattern_id, merged_pattern.get_buffer_positions(),
                                    merged_pattern.get_cell_masters()));
    return merged_pattern_id;
  }

  auto get_next_id() const -> unsigned { return _next_id; }

 private:
  BufferPatternRegistry* _registry = nullptr;
  mutable unsigned _next_id;
};

class TopologyPatternRegistryCombiner
{
 public:
  TopologyPatternRegistryCombiner(TopologyPatternRegistry& registry, unsigned start_id) : _registry(&registry), _next_id(start_id) {}

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _registry->find(upstream);
    const auto* downstream_pattern = _registry->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTreeBuilder: missing topology pattern during composition.";

    const PatternId merged_pattern_id = PatternId::topology(_next_id++);
    _registry->add(HTreeTopologyPattern::concat(*upstream_pattern, *downstream_pattern, merged_pattern_id));
    return merged_pattern_id;
  }

  auto get_next_id() const -> unsigned { return _next_id; }

 private:
  TopologyPatternRegistry* _registry = nullptr;
  mutable unsigned _next_id;
};

class BufferPortCache
{
 public:
  auto get(const std::string& cell_master) -> const BufferPortInfo*
  {
    auto it = _cache.find(cell_master);
    if (it != _cache.end()) {
      return &it->second;
    }

    auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (input_pin.empty() || output_pin.empty()) {
      return nullptr;
    }

    auto [inserted_it, inserted]
        = _cache.emplace(cell_master, BufferPortInfo{.input_pin = std::move(input_pin), .output_pin = std::move(output_pin)});
    (void) inserted;
    return &inserted_it->second;
  }

 private:
  std::unordered_map<std::string, BufferPortInfo> _cache;
};

class ScopedCharGridOverride
{
 public:
  ScopedCharGridOverride(double wire_length_unit_um, unsigned wire_length_iterations)
  {
    if (wire_length_unit_um <= 0.0 || wire_length_iterations == 0U) {
      return;
    }

    _original_wire_length_unit_um = CONFIG_INST.get_wire_length_unit_um();
    _original_wire_length_iterations = CONFIG_INST.get_wire_length_iterations();
    CONFIG_INST.set_wire_length_unit_um(wire_length_unit_um);
    CONFIG_INST.set_wire_length_iterations(wire_length_iterations);
    _active = true;
  }

  ~ScopedCharGridOverride()
  {
    if (!_active) {
      return;
    }

    CONFIG_INST.set_wire_length_unit_um(_original_wire_length_unit_um);
    CONFIG_INST.set_wire_length_iterations(_original_wire_length_iterations);
  }

 private:
  bool _active = false;
  double _original_wire_length_unit_um = 0.0;
  unsigned _original_wire_length_iterations = 0U;
};

template <class CharT>
auto BuildInputBoundaryFrontier(const std::vector<CharT>& chars) -> std::vector<CharT>
{
  const InputBoundaryPruner<CharT> pruner;
  std::unordered_map<unsigned, std::vector<const CharT*>> grouped_entries;
  grouped_entries.reserve(chars.size());

  for (const auto& entry : chars) {
    grouped_entries[pruner.groupKey(entry)].push_back(&entry);
  }

  std::vector<CharT> frontier_entries;
  frontier_entries.reserve(chars.size());
  for (const auto& [group_key, entries] : grouped_entries) {
    (void) group_key;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      bool dominated = false;
      for (std::size_t other_index = 0; other_index < entries.size(); ++other_index) {
        if (index == other_index) {
          continue;
        }
        if (pruner.dominates(*entries.at(other_index), *entries.at(index))) {
          dominated = true;
          break;
        }
      }
      if (!dominated) {
        frontier_entries.push_back(*entries.at(index));
      }
    }
  }

  std::sort(frontier_entries.begin(), frontier_entries.end(), [](const CharT& lhs, const CharT& rhs) -> bool {
    if (lhs.get_input_slew_idx() != rhs.get_input_slew_idx()) {
      return lhs.get_input_slew_idx() < rhs.get_input_slew_idx();
    }
    if (lhs.get_driven_cap_idx() != rhs.get_driven_cap_idx()) {
      return lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx();
    }
    if (lhs.get_output_slew_idx() != rhs.get_output_slew_idx()) {
      return lhs.get_output_slew_idx() < rhs.get_output_slew_idx();
    }
    if (lhs.get_load_cap_idx() != rhs.get_load_cap_idx()) {
      return lhs.get_load_cap_idx() > rhs.get_load_cap_idx();
    }
    if (lhs.get_delay() != rhs.get_delay()) {
      return lhs.get_delay() < rhs.get_delay();
    }
    if (lhs.get_power() != rhs.get_power()) {
      return lhs.get_power() < rhs.get_power();
    }
    return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
  });

  return frontier_entries;
}

template <class CharT>
auto SelectCompositionCandidates(const std::vector<CharT>& entries) -> std::vector<CharT>
{
  const std::size_t max_per_boundary_group = CONFIG_INST.get_relaxed_candidates_per_boundary_group();
  if (max_per_boundary_group == 0U) {
    return entries;
  }

  const InputBoundaryPruner<CharT> pruner;
  std::unordered_map<unsigned, std::size_t> group_counts;
  group_counts.reserve(entries.size());

  std::vector<CharT> selected_entries;
  selected_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    const unsigned group_key = pruner.groupKey(entry);
    auto& kept_count = group_counts[group_key];
    if (kept_count >= max_per_boundary_group) {
      continue;
    }
    selected_entries.push_back(entry);
    ++kept_count;
  }

  return selected_entries;
}

auto BuildSegmentCharTable(const std::vector<SegmentChar>& chars) -> SegmentCharTable
{
  SegmentCharTable table;
  table.reserve(chars.size());
  for (const auto& entry : chars) {
    table.addChar(entry);
  }
  return table;
}

auto BuildHTreeCharTable(const std::vector<HTreeTopologyChar>& chars) -> HTreeTopologyCharTable
{
  HTreeTopologyCharTable table;
  table.reserve(chars.size());
  for (const auto& entry : chars) {
    table.addChar(entry);
  }
  return table;
}

auto FindNextSegmentPatternId(const std::vector<SegmentChar>& chars) -> unsigned
{
  unsigned next_id = 0U;
  for (const auto& entry : chars) {
    next_id = std::max(next_id, entry.get_pattern_id().local_id + 1U);
  }
  return next_id;
}

auto MakeNearestLengthIndex(double length_um, double length_step_um) -> unsigned
{
  if (length_um <= 0.0 || length_step_um <= 0.0) {
    return 0U;
  }
  return std::max(1U, static_cast<unsigned>(std::lround(length_um / length_step_um)));
}

auto CountUniqueAlignedLengthBins(const std::vector<double>& requested_lengths_um, double length_step_um) -> unsigned
{
  if (requested_lengths_um.empty() || length_step_um <= 0.0) {
    return 0U;
  }

  std::vector<unsigned> aligned_bins;
  aligned_bins.reserve(requested_lengths_um.size());
  for (const double requested_length_um : requested_lengths_um) {
    const unsigned aligned_idx = MakeNearestLengthIndex(requested_length_um, length_step_um);
    if (aligned_idx > 0U) {
      aligned_bins.push_back(aligned_idx);
    }
  }

  if (aligned_bins.empty()) {
    return 0U;
  }

  std::ranges::sort(aligned_bins);
  const auto unique_tail = std::ranges::unique(aligned_bins);
  aligned_bins.erase(unique_tail.begin(), unique_tail.end());
  return static_cast<unsigned>(aligned_bins.size());
}

auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>
{
  std::vector<double> requested_lengths_um;
  const auto levels = topology.levels();
  if (levels.size() <= 1U) {
    return requested_lengths_um;
  }

  requested_lengths_um.reserve(levels.size() - 1U);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    long long distance_sum = 0;
    std::size_t distance_count = 0;
    for (const auto node_id : levels.at(level)) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += geometry::Manhattan(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      continue;
    }

    const int requested_length_dbu
        = static_cast<int>(std::llround(static_cast<double>(distance_sum) / static_cast<double>(distance_count)));
    const double requested_length_um
        = static_cast<double>(std::max(requested_length_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
    if (requested_length_um > 0.0) {
      requested_lengths_um.push_back(requested_length_um);
    }
  }

  return requested_lengths_um;
}

auto ResolveCharacterizationGridPlan(const Tree& topology, int32_t dbu_per_um) -> CharacterizationGridPlan
{
  CharacterizationGridPlan plan;
  const auto requested_lengths_um = CollectRequestedLevelLengthsUm(topology, dbu_per_um);
  if (requested_lengths_um.empty()) {
    return plan;
  }
  plan.requested_level_lengths = static_cast<unsigned>(requested_lengths_um.size());

  const double configured_unit_um = CONFIG_INST.get_wire_length_unit_um();
  plan.configured_wire_length_unit_um = configured_unit_um;
  plan.configured_wire_length_missing = configured_unit_um <= 0.0;

  double effective_unit_um = configured_unit_um;
  if (effective_unit_um > 0.0) {
    plan.unique_level_bins = CountUniqueAlignedLengthBins(requested_lengths_um, effective_unit_um);
    plan.source = CharGridSource::kRuntimeConfig;
  }

  const double max_requested_length_um = *std::ranges::max_element(requested_lengths_um);
  const double fallback_unit_um = max_requested_length_um / static_cast<double>(requested_lengths_um.size());
  const bool grid_collapsed = configured_unit_um > 0.0 && requested_lengths_um.size() > 1U && plan.unique_level_bins <= 1U;
  plan.configured_grid_collapsed = grid_collapsed;
  if (plan.configured_wire_length_missing || grid_collapsed) {
    if (plan.configured_wire_length_missing) {
      schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "HTreeBuilder",
                             "wire_length_unit_um is absent in runtime config; fallback to auto-derived topology grid unit.",
                             {
                                 {"effective_wire_length_unit_um", logformat::FormatWithUnit(fallback_unit_um, "um")},
                                 {"reason", "missing_runtime_config"},
                             });
    }
    if (grid_collapsed) {
      schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "HTreeBuilder",
                             "configured wire_length_unit_um collapses level bins to <=1; fallback to auto-derived topology grid unit.",
                             {
                                 {"configured_wire_length_unit_um", logformat::FormatWithUnit(configured_unit_um, "um")},
                                 {"effective_wire_length_unit_um", logformat::FormatWithUnit(fallback_unit_um, "um")},
                                 {"reason", "collapsed_bins"},
                             });
    }

    effective_unit_um = fallback_unit_um;
    plan.adapted = effective_unit_um > 0.0;
    plan.source = plan.adapted ? CharGridSource::kAutoDerived : CharGridSource::kNone;
    plan.auto_derived_wire_length_unit_um = effective_unit_um;
    plan.unique_level_bins = CountUniqueAlignedLengthBins(requested_lengths_um, effective_unit_um);
  }

  if (!plan.adapted || effective_unit_um <= 0.0) {
    return plan;
  }

  plan.wire_length_unit_um = effective_unit_um;
  plan.wire_length_iterations = std::max(1U, static_cast<unsigned>(std::ceil(max_requested_length_um / effective_unit_um)));
  return plan;
}

auto NormalizeMetric(double value, double min_value, double max_value) -> double
{
  if (max_value <= min_value) {
    return 0.0;
  }
  return std::clamp((value - min_value) / (max_value - min_value), 0.0, 1.0);
}

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

auto BuildSegmentLengthFrontiers(const std::vector<SegmentChar>& chars) -> std::unordered_map<unsigned, std::vector<SegmentChar>>
{
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_by_length;
  raw_by_length.reserve(chars.size());
  for (const auto& entry : chars) {
    raw_by_length[entry.get_length_idx()].push_back(entry);
  }

  std::unordered_map<unsigned, std::vector<SegmentChar>> frontier_by_length;
  frontier_by_length.reserve(raw_by_length.size());
  for (auto& [length_idx, raw_entries] : raw_by_length) {
    (void) length_idx;
    frontier_by_length[length_idx] = BuildInputBoundaryFrontier(raw_entries);
  }
  return frontier_by_length;
}

auto BuildLevelPlans(const Tree& topology, double length_step_um, int32_t dbu_per_um) -> std::vector<HTreeBuilder::LevelPlan>
{
  std::vector<HTreeBuilder::LevelPlan> plans;
  const auto levels = topology.levels();
  if (levels.size() <= 1U || length_step_um <= 0.0) {
    return plans;
  }

  plans.reserve(levels.size() - 1U);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    long long distance_sum = 0;
    std::size_t distance_count = 0;
    for (const auto node_id : levels.at(level)) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += geometry::Manhattan(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      plans.push_back({});
      continue;
    }

    const int requested_length_dbu
        = static_cast<int>(std::llround(static_cast<double>(distance_sum) / static_cast<double>(distance_count)));
    const double requested_length_um
        = static_cast<double>(std::max(requested_length_dbu, 0)) / static_cast<double>(std::max(dbu_per_um, int32_t{1}));
    const unsigned aligned_length_idx = MakeNearestLengthIndex(requested_length_um, length_step_um);
    plans.push_back(HTreeBuilder::LevelPlan{
        .requested_length_dbu = requested_length_dbu,
        .requested_length_um = requested_length_um,
        .aligned_length_idx = aligned_length_idx,
        .aligned_length_um = static_cast<double>(aligned_length_idx) * length_step_um,
    });
  }

  return plans;
}

auto ComposeRelaxedSegmentEntries(const std::vector<SegmentChar>& upstream, const std::vector<SegmentChar>& downstream,
                                  BufferPatternRegistry& pattern_registry, unsigned start_pattern_id)
    -> std::pair<std::vector<SegmentChar>, unsigned>
{
  SegmentPatternRegistryCombiner combiner(pattern_registry, start_pattern_id);
  std::vector<SegmentChar> composed_entries;
  composed_entries.reserve(upstream.size() * downstream.size());
  for (const auto& upstream_entry : upstream) {
    for (const auto& downstream_entry : downstream) {
      if (downstream_entry.get_input_slew_idx() < upstream_entry.get_output_slew_idx()
          || upstream_entry.get_load_cap_idx() < downstream_entry.get_driven_cap_idx()) {
        continue;
      }
      const auto merged_pattern_id = combiner.combine(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id());
      composed_entries.push_back(SegmentChar::compose(upstream_entry, downstream_entry, merged_pattern_id));
    }
  }

  return {std::move(composed_entries), combiner.get_next_id()};
}

auto SynthesizeSegmentFrontiers(const std::vector<SegmentChar>& base_segment_chars, BufferPatternRegistry& pattern_registry,
                                unsigned max_target_length_idx) -> std::unordered_map<unsigned, std::vector<SegmentChar>>
{
  auto frontier_by_length = BuildSegmentLengthFrontiers(base_segment_chars);
  std::unordered_map<unsigned, unsigned> min_piece_count_by_length;
  min_piece_count_by_length.reserve(frontier_by_length.size());
  for (const auto& [length_idx, entries] : frontier_by_length) {
    if (!entries.empty()) {
      min_piece_count_by_length[length_idx] = 1U;
    }
  }

  unsigned next_pattern_id = FindNextSegmentPatternId(base_segment_chars);
  for (unsigned length_idx = 1U; length_idx <= max_target_length_idx; ++length_idx) {
    if (frontier_by_length.contains(length_idx) && !frontier_by_length.at(length_idx).empty()) {
      continue;
    }

    unsigned best_piece_count = std::numeric_limits<unsigned>::max();
    std::vector<std::pair<unsigned, unsigned>> best_splits;
    for (unsigned left_idx = 1U; left_idx < length_idx; ++left_idx) {
      const unsigned right_idx = length_idx - left_idx;
      if (!min_piece_count_by_length.contains(left_idx) || !min_piece_count_by_length.contains(right_idx)) {
        continue;
      }

      const unsigned candidate_piece_count = min_piece_count_by_length.at(left_idx) + min_piece_count_by_length.at(right_idx);
      if (candidate_piece_count < best_piece_count) {
        best_piece_count = candidate_piece_count;
        best_splits.assign(1U, {left_idx, right_idx});
      } else if (candidate_piece_count == best_piece_count) {
        best_splits.emplace_back(left_idx, right_idx);
      }
    }

    if (best_splits.empty()) {
      continue;
    }

    SegmentPatternRegistryCombiner exact_combiner(pattern_registry, next_pattern_id);
    std::vector<SegmentChar> exact_entries;
    for (const auto& [left_idx, right_idx] : best_splits) {
      auto partial = BuildSegmentCharTable(frontier_by_length.at(left_idx))
                         .concatWith(BuildSegmentCharTable(frontier_by_length.at(right_idx)), exact_combiner)
                         .get_chars();
      exact_entries.insert(exact_entries.end(), partial.begin(), partial.end());
    }
    next_pattern_id = exact_combiner.get_next_id();

    if (!exact_entries.empty()) {
      frontier_by_length[length_idx] = BuildInputBoundaryFrontier(exact_entries);
      min_piece_count_by_length[length_idx] = best_piece_count;
      continue;
    }

    std::vector<SegmentChar> relaxed_entries;
    for (const auto& [left_idx, right_idx] : best_splits) {
      const auto left_candidates = SelectCompositionCandidates(frontier_by_length.at(left_idx));
      const auto right_candidates = SelectCompositionCandidates(frontier_by_length.at(right_idx));
      auto [partial, updated_next_pattern_id]
          = ComposeRelaxedSegmentEntries(left_candidates, right_candidates, pattern_registry, next_pattern_id);
      next_pattern_id = updated_next_pattern_id;
      relaxed_entries.insert(relaxed_entries.end(), partial.begin(), partial.end());
    }

    if (!relaxed_entries.empty()) {
      frontier_by_length[length_idx] = BuildInputBoundaryFrontier(relaxed_entries);
      min_piece_count_by_length[length_idx] = best_piece_count;
    }
  }

  return frontier_by_length;
}

auto MakeHTreeSeedEntries(const std::vector<SegmentChar>& segment_frontier, TopologyPatternRegistry& topology_registry,
                          unsigned& next_pattern_id) -> std::vector<HTreeTopologyChar>
{
  std::vector<HTreeTopologyChar> seed_entries;
  seed_entries.reserve(segment_frontier.size());
  for (const auto& segment_entry : segment_frontier) {
    const auto topology_pattern_id = PatternId::topology(next_pattern_id++);
    topology_registry.add(HTreeTopologyPattern(topology_pattern_id, 1U, std::vector<PatternId>{segment_entry.get_pattern_id()}));
    const CharCore core(segment_entry.get_input_slew_idx(), segment_entry.get_output_slew_idx(), segment_entry.get_driven_cap_idx(),
                        segment_entry.get_load_cap_idx(), segment_entry.get_delay(), segment_entry.get_power(), topology_pattern_id);
    seed_entries.emplace_back(core, 1U);
  }
  return seed_entries;
}

auto SelectBestHTreeChar(const std::vector<HTreeTopologyChar>& entries) -> std::optional<HTreeTopologyChar>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  const auto [min_delay_it, max_delay_it] = std::ranges::minmax_element(entries, {}, &HTreeTopologyChar::get_delay);
  const auto [min_power_it, max_power_it] = std::ranges::minmax_element(entries, {}, &HTreeTopologyChar::get_power);
  const double min_delay = min_delay_it->get_delay();
  const double max_delay = max_delay_it->get_delay();
  const double min_power = min_power_it->get_power();
  const double max_power = max_power_it->get_power();

  const auto pareto_front = BuildDelayPowerParetoFront(entries);
  if (pareto_front.empty()) {
    return entries.front();
  }

  constexpr double score_tolerance = 1e-12;
  const auto* best_entry = pareto_front.front();
  auto calc_score = [min_delay, max_delay, min_power, max_power](const HTreeTopologyChar& entry) -> double {
    const double normalized_delay = NormalizeMetric(entry.get_delay(), min_delay, max_delay);
    const double normalized_power = NormalizeMetric(entry.get_power(), min_power, max_power);
    return (normalized_delay * normalized_delay) + (normalized_power * normalized_power);
  };

  double best_score = calc_score(*best_entry);
  for (const auto* entry : pareto_front) {
    const double score = calc_score(*entry);
    const bool better = (score + score_tolerance < best_score)
                        || (std::abs(score - best_score) <= score_tolerance && PreferDelayPowerTieBreak(*entry, *best_entry));
    if (better) {
      best_entry = entry;
      best_score = score;
    }
  }

  return *best_entry;
}

auto InterpolateManhattanPoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>
{
  const double clamped_position = std::clamp(normalized_position, 0.0, 1.0);
  const int dx = sink.get_x() - source.get_x();
  const int dy = sink.get_y() - source.get_y();
  const int total_distance = std::abs(dx) + std::abs(dy);
  if (total_distance == 0) {
    return source;
  }

  const int target_distance = static_cast<int>(std::lround(clamped_position * static_cast<double>(total_distance)));
  const int x_step = std::min(std::abs(dx), target_distance);
  const int y_step = std::max(0, target_distance - x_step);
  const int x = source.get_x() + ((dx >= 0) ? x_step : -x_step);
  const int y = source.get_y() + ((dy >= 0) ? y_step : -y_step);
  return Point<int>(x, y);
}

auto MakePinName(const std::string& inst_name, const std::string& port_name) -> std::string
{
  return inst_name + "/" + port_name;
}

auto CreateBufferInstance(HTreeBuilder::BuildResult& result, const std::string& inst_name, const std::string& cell_master,
                          const Point<int>& location, const BufferPortInfo& ports) -> BufferInstancePins
{
  auto inst = std::make_unique<Inst>(inst_name, cell_master, InstType::kBuffer, location);
  auto* inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(MakePinName(inst_name, ports.input_pin), PinType::kIn, location, inst_ptr, nullptr, false);
  auto output_pin = std::make_unique<Pin>(MakePinName(inst_name, ports.output_pin), PinType::kOut, location, inst_ptr, nullptr, false);

  auto* input_pin_ptr = input_pin.get();
  auto* output_pin_ptr = output_pin.get();

  inst_ptr->insertDriverPin(output_pin_ptr);
  inst_ptr->add_pin(input_pin_ptr);

  result.inserted_insts.push_back(inst_ptr);
  result.inserted_pins.push_back(output_pin_ptr);
  result.inserted_pins.push_back(input_pin_ptr);

  result.inst_storage.push_back(std::move(inst));
  result.pin_storage.push_back(std::move(output_pin));
  result.pin_storage.push_back(std::move(input_pin));

  return BufferInstancePins{
      .inst = inst_ptr,
      .input_pin = input_pin_ptr,
      .output_pin = output_pin_ptr,
  };
}

auto CreateStandalonePin(HTreeBuilder::BuildResult& result, const std::string& pin_name, PinType pin_type, const Point<int>& location,
                         bool is_io) -> Pin*
{
  auto pin = std::make_unique<Pin>(pin_name, pin_type, location, nullptr, nullptr, is_io);
  auto* pin_ptr = pin.get();
  result.inserted_pins.push_back(pin_ptr);
  result.pin_storage.push_back(std::move(pin));
  return pin_ptr;
}

auto CreateNet(HTreeBuilder::BuildResult& result, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();

  if (driver != nullptr) {
    net_ptr->set_driver(driver);
    driver->set_net(net_ptr);
  }

  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net_ptr->add_load(load);
    load->set_net(net_ptr);
  }

  result.inserted_nets.push_back(net_ptr);
  result.net_storage.push_back(std::move(net));
  return net_ptr;
}

struct MaterializationContext
{
  HTreeBuilder::BuildResult* result = nullptr;
  BufferPortCache* port_cache = nullptr;
  std::size_t edge_buffer_counter = 0U;
  std::size_t net_counter = 0U;

  auto nextBufferName() -> std::string { return "cts_htree_edge_buf_" + std::to_string(edge_buffer_counter++); }

  auto nextNetName() -> std::string { return "cts_htree_net_" + std::to_string(net_counter++); }
};

auto MaterializeSegmentAndGetEntryLoads(MaterializationContext& context, const TreeNode& parent_node, const TreeNode& child_node,
                                        const BufferingPattern& segment_pattern, const std::vector<Pin*>& child_entry_loads)
    -> std::vector<Pin*>
{
  if (child_node.get_loads().empty()) {
    return {};
  }

  const auto& cell_masters = segment_pattern.get_cell_masters();
  const auto& positions = segment_pattern.get_buffer_positions();
  const auto buffer_count = std::min(cell_masters.size(), positions.size());
  if (buffer_count == 0U) {
    if (!child_entry_loads.empty()) {
      return child_entry_loads;
    }
    return child_node.get_loads();
  }

  std::vector<BufferInstancePins> segment_buffers;
  segment_buffers.reserve(buffer_count);
  for (std::size_t buffer_index = 0; buffer_index < buffer_count; ++buffer_index) {
    const auto* ports = context.port_cache->get(cell_masters.at(buffer_index));
    LOG_FATAL_IF(ports == nullptr) << "HTreeBuilder: unresolved ports for edge buffer master " << cell_masters.at(buffer_index);

    const auto buffer_location
        = InterpolateManhattanPoint(parent_node.get_position(), child_node.get_position(), positions.at(buffer_index));
    segment_buffers.push_back(
        CreateBufferInstance(*context.result, context.nextBufferName(), cell_masters.at(buffer_index), buffer_location, *ports));
  }

  for (std::size_t buffer_index = 0; buffer_index + 1U < segment_buffers.size(); ++buffer_index) {
    CreateNet(*context.result, context.nextNetName(), segment_buffers.at(buffer_index).output_pin,
              std::vector<Pin*>{segment_buffers.at(buffer_index + 1U).input_pin});
  }

  std::vector<Pin*> terminal_loads = child_entry_loads;
  if (terminal_loads.empty()) {
    terminal_loads = child_node.get_loads();
  }
  LOG_FATAL_IF(terminal_loads.empty()) << "HTreeBuilder: segment terminal loads are empty for child node " << child_node.get_id();

  CreateNet(*context.result, context.nextNetName(), segment_buffers.back().output_pin, terminal_loads);
  return std::vector<Pin*>{segment_buffers.front().input_pin};
}

auto MaterializeCTSObjects(HTreeBuilder::BuildResult& result, const BufferPatternRegistry& segment_pattern_registry) -> void
{
  if (!result.best_pattern.has_value()) {
    return;
  }

  const auto levels = result.topology.levels();
  if (levels.size() <= 1U) {
    result.success = result.success && result.best_char.has_value();
    return;
  }

  BufferPortCache port_cache;
  std::vector<PatternId> level_segment_pattern_ids = result.best_pattern->get_level_segment_pattern_ids();
  LOG_FATAL_IF(level_segment_pattern_ids.size() != result.levels.size())
      << "HTreeBuilder: best topology pattern levels do not match planned H-tree levels.";

  std::vector<const BufferingPattern*> level_patterns;
  level_patterns.reserve(level_segment_pattern_ids.size());
  for (const auto pattern_id : level_segment_pattern_ids) {
    const auto* level_pattern = segment_pattern_registry.find(pattern_id);
    LOG_FATAL_IF(level_pattern == nullptr) << "HTreeBuilder: selected segment pattern metadata is missing.";
    level_patterns.push_back(level_pattern);
  }

  const auto* root_node = result.topology.get_node(result.topology.get_root());
  LOG_FATAL_IF(root_node == nullptr) << "HTreeBuilder: topology root is missing during materialization.";

  result.root_inst = nullptr;
  result.root_input_pin = CreateStandalonePin(result, "cts_htree_root_in", PinType::kIn, root_node->get_position(), true);
  result.root_output_pin = CreateStandalonePin(result, "cts_htree_root_out", PinType::kOut, root_node->get_position(), true);

  MaterializationContext context{
      .result = &result,
      .port_cache = &port_cache,
  };

  std::unordered_map<std::size_t, std::vector<Pin*>> entry_loads_by_node;
  entry_loads_by_node.reserve(result.topology.get_size());

  for (const auto node_id : levels.back()) {
    const auto* node = result.topology.get_node(node_id);
    if (node == nullptr || node->get_loads().empty()) {
      continue;
    }
    entry_loads_by_node[node_id] = node->get_loads();
  }

  for (std::size_t reverse_depth = levels.size() - 1U; reverse_depth > 0U; --reverse_depth) {
    const std::size_t depth = reverse_depth - 1U;
    const auto* segment_pattern = level_patterns.at(depth);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: missing selected segment pattern metadata during materialization.";

    for (const auto node_id : levels.at(depth)) {
      const auto* node = result.topology.get_node(node_id);
      if (node == nullptr || node->get_loads().empty()) {
        continue;
      }

      std::vector<Pin*> node_entry_loads;
      for (const auto child_id : node->get_children()) {
        if (child_id == std::numeric_limits<std::size_t>::max()) {
          continue;
        }

        const auto* child_node = result.topology.get_node(child_id);
        if (child_node == nullptr || child_node->get_loads().empty()) {
          continue;
        }

        const auto child_it = entry_loads_by_node.find(child_id);
        const auto& child_entry_loads = (child_it != entry_loads_by_node.end()) ? child_it->second : child_node->get_loads();
        auto segment_entry_loads = MaterializeSegmentAndGetEntryLoads(context, *node, *child_node, *segment_pattern, child_entry_loads);
        node_entry_loads.insert(node_entry_loads.end(), segment_entry_loads.begin(), segment_entry_loads.end());
      }

      if (node_entry_loads.empty()) {
        node_entry_loads = node->get_loads();
      }
      entry_loads_by_node[node_id] = std::move(node_entry_loads);
    }
  }

  const auto root_it = entry_loads_by_node.find(result.topology.get_root());
  auto root_entry_loads = (root_it != entry_loads_by_node.end()) ? root_it->second : root_node->get_loads();
  LOG_FATAL_IF(root_entry_loads.empty()) << "HTreeBuilder: root entry loads are empty during materialization.";
  CreateNet(result, context.nextNetName(), result.root_output_pin, root_entry_loads);
}

}  // namespace

auto HTreeBuilder::build(const std::vector<Pin*>& loads) -> BuildResult
{
  BuildResult result;
  if (loads.empty()) {
    LOG_WARNING << "HTreeBuilder: build skipped because no loads were provided.";
    return result;
  }
  schema::ScopedStage build_stage("HTreeBuilder", "build");

  result.topology = TopologyGen::build(loads);
  const auto levels = result.topology.levels();
  if (levels.size() <= 1U) {
    LOG_WARNING << "HTreeBuilder: topology has no H-tree levels after generation.";
    build_stage.skip({{"reason", "no_h_tree_levels"}});
    return result;
  }

  const int32_t dbu_per_um = std::max(WRAPPER_INST.queryDbUnit(), int32_t{1});
  const auto char_grid_plan = ResolveCharacterizationGridPlan(result.topology, dbu_per_um);
  std::string grid_source = toCharGridSourceName(CharGridSource::kNone);
  if (char_grid_plan.adapted) {
    grid_source = toCharGridSourceName(char_grid_plan.source);
  } else if (char_grid_plan.configured_wire_length_unit_um > 0.0) {
    grid_source = toCharGridSourceName(CharGridSource::kRuntimeConfig);
  }

  std::string grid_effective_unit = "unresolved";
  if (char_grid_plan.adapted) {
    grid_effective_unit = logformat::FormatWithUnit(char_grid_plan.wire_length_unit_um, "um");
  } else if (char_grid_plan.configured_wire_length_unit_um > 0.0) {
    grid_effective_unit = logformat::FormatWithUnit(char_grid_plan.configured_wire_length_unit_um, "um");
  }
  std::string decision_flags = "none";
  if (char_grid_plan.configured_wire_length_missing && char_grid_plan.configured_grid_collapsed) {
    decision_flags = "missing_config+collapsed_bins";
  } else if (char_grid_plan.configured_wire_length_missing) {
    decision_flags = "missing_config";
  } else if (char_grid_plan.configured_grid_collapsed) {
    decision_flags = "collapsed_bins";
  }
  const logformat::TableRows grid_plan_rows = {
      {"source", grid_source, char_grid_plan.adapted ? "fallback derived from topology level lengths" : "use runtime-configured grid"},
      {"requested_level_lengths", std::to_string(char_grid_plan.requested_level_lengths),
       "average parent-child segment length per topology level"},
      {"configured_wire_length_unit_um",
       char_grid_plan.configured_wire_length_unit_um > 0.0 ? logformat::FormatWithUnit(char_grid_plan.configured_wire_length_unit_um, "um")
                                                           : std::string{"auto"},
       char_grid_plan.configured_wire_length_missing ? "missing" : "present"},
      {"effective_wire_length_unit_um", grid_effective_unit,
       char_grid_plan.adapted ? "temporary override for characterization" : "no override"},
      {"wire_length_iterations_override", std::to_string(char_grid_plan.wire_length_iterations),
       char_grid_plan.adapted ? "temporary override for characterization" : "0 (disabled)"},
      {"distinct_level_bins", std::to_string(char_grid_plan.unique_level_bins), "aligned-length bins under effective unit"},
      {"decision_flags", decision_flags, "fallback trigger diagnostics"},
  };
  logInfoTable("HTreeBuilder Characterization Grid Plan", {"Item", "Value", "Detail"}, grid_plan_rows);

  const ScopedCharGridOverride char_grid_override(char_grid_plan.wire_length_unit_um, char_grid_plan.wire_length_iterations);

  build_stage.markRunning("characterization");
  CharBuilder char_builder;
  char_builder.init();
  char_builder.build();

  const double length_step_um = char_builder.get_wire_length_unit_um();
  if (length_step_um <= 0.0 || char_builder.get_segment_chars().empty()) {
    LOG_WARNING << "HTreeBuilder: characterization did not produce usable segment chars.";
    build_stage.skip({{"reason", "no_usable_segment_chars"}});
    return result;
  }

  result.char_wire_length_unit_um = length_step_um;
  result.char_wire_length_iterations = char_builder.get_wire_length_iterations();
  result.char_unique_level_bins
      = char_grid_plan.adapted ? char_grid_plan.unique_level_bins
                               : CountUniqueAlignedLengthBins(CollectRequestedLevelLengthsUm(result.topology, dbu_per_um), length_step_um);
  result.char_grid_adapted = char_grid_plan.adapted;
  result.char_max_slew_ns = char_builder.get_max_slew();
  result.char_max_cap_pf = char_builder.get_max_cap();
  const std::string char_wire_length_source
      = char_builder.get_wire_length_unit_source().empty() ? std::string{"unresolved"} : char_builder.get_wire_length_unit_source();
  const std::string char_wire_length_detail = char_builder.get_wire_length_unit_detail().empty()
                                                  ? std::string{"no resolution detail"}
                                                  : char_builder.get_wire_length_unit_detail();
  const logformat::TableRows char_summary_rows = {
      {"wire_length_unit_um", logformat::FormatWithUnit(result.char_wire_length_unit_um, "um"), char_wire_length_source},
      {"wire_length_unit_detail", char_wire_length_detail,
       char_grid_plan.adapted ? "effective value under HTree grid override" : "direct CharBuilder resolution"},
      {"grid_plan_source", grid_source, char_grid_plan.adapted ? "HTreeBuilder adapted characterization grid" : "no HTree override"},
      {"wire_length_iterations", std::to_string(result.char_wire_length_iterations), "characterization sweep length bins"},
      {"max_slew_ns", logformat::FormatWithUnit(result.char_max_slew_ns, "ns"), "characterization upper bound"},
      {"max_cap_pf", logformat::FormatWithUnit(result.char_max_cap_pf, "pF"), "characterization upper bound"},
      {"segment_chars", std::to_string(char_builder.get_segment_chars().size()), "raw segment characterization entries"},
      {"buffer_patterns", std::to_string(char_builder.get_buffering_patterns().size()), "raw segment pattern count"},
  };
  logInfoTable("HTreeBuilder Characterization Summary", {"Item", "Value", "Detail"}, char_summary_rows);

  result.levels = BuildLevelPlans(result.topology, length_step_um, dbu_per_um);
  if (result.levels.empty()) {
    LOG_WARNING << "HTreeBuilder: failed to derive H-tree level plans from topology.";
    build_stage.skip({{"reason", "empty_level_plans"}});
    return result;
  }

  BufferPatternRegistry segment_pattern_registry;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_registry.add(pattern);
  }

  unsigned max_target_length_idx = 0U;
  for (const auto& level : result.levels) {
    max_target_length_idx = std::max(max_target_length_idx, level.aligned_length_idx);
  }

  auto frontier_by_length = SynthesizeSegmentFrontiers(char_builder.get_segment_chars(), segment_pattern_registry, max_target_length_idx);

  TopologyPatternRegistry topology_pattern_registry;
  unsigned next_topology_pattern_id = 0U;
  std::vector<HTreeTopologyChar> current_frontier;
  for (std::size_t reverse_level = result.levels.size(); reverse_level > 0U; --reverse_level) {
    auto& level = result.levels.at(reverse_level - 1U);
    const auto frontier_it = frontier_by_length.find(level.aligned_length_idx);
    if (frontier_it == frontier_by_length.end() || frontier_it->second.empty()) {
      LOG_WARNING << "HTreeBuilder: missing synthesized segment frontier for aligned length index " << level.aligned_length_idx;
      build_stage.skip({{"reason", "missing_segment_frontier"}, {"aligned_length_idx", std::to_string(level.aligned_length_idx)}});
      return result;
    }

    auto seed_entries = MakeHTreeSeedEntries(frontier_it->second, topology_pattern_registry, next_topology_pattern_id);
    if (current_frontier.empty()) {
      current_frontier = BuildInputBoundaryFrontier(seed_entries);
      continue;
    }

    TopologyPatternRegistryCombiner combiner(topology_pattern_registry, next_topology_pattern_id);
    auto composed_entries = BuildHTreeCharTable(seed_entries).concatWith(BuildHTreeCharTable(current_frontier), combiner).get_chars();
    next_topology_pattern_id = combiner.get_next_id();
    current_frontier = BuildInputBoundaryFrontier(composed_entries);
    if (current_frontier.empty()) {
      LOG_WARNING << "HTreeBuilder: H-tree frontier became empty at level " << (reverse_level - 1U);
      build_stage.skip({{"reason", "empty_frontier"}, {"level", std::to_string(reverse_level - 1U)}});
      return result;
    }
  }

  result.feasible_chars = current_frontier;
  result.best_char = SelectBestHTreeChar(result.feasible_chars);
  if (!result.best_char.has_value()) {
    LOG_WARNING << "HTreeBuilder: failed to select a best H-tree characterization entry.";
    build_stage.skip({{"reason", "missing_best_char"}});
    return result;
  }

  const auto* best_pattern = topology_pattern_registry.find(result.best_char->get_pattern_id());
  LOG_FATAL_IF(best_pattern == nullptr) << "HTreeBuilder: best H-tree pattern metadata is missing.";
  result.best_pattern = *best_pattern;

  LOG_FATAL_IF(best_pattern->get_level_segment_pattern_ids().size() != result.levels.size())
      << "HTreeBuilder: best H-tree pattern level count does not match topology depth.";
  for (std::size_t level = 0; level < result.levels.size(); ++level) {
    result.levels.at(level).segment_pattern_id = best_pattern->get_level_segment_pattern_ids().at(level);
  }

  MaterializeCTSObjects(result, segment_pattern_registry);
  result.success = result.best_char.has_value() && result.best_pattern.has_value() && result.root_input_pin != nullptr
                   && result.root_output_pin != nullptr && !result.inserted_nets.empty();

  const logformat::TableRows build_summary_rows = {
      {"levels", std::to_string(result.levels.size()), "synthesized H-tree levels"},
      {"selected_topology_pattern_id", std::to_string(result.best_char->get_pattern_id().local_id), "best feasible topology pattern"},
      {"feasible_solutions", std::to_string(result.feasible_chars.size()), "frontier entries after full composition"},
      {"inserted_insts", std::to_string(result.inserted_insts.size()), "materialized CTS buffer instances"},
      {"inserted_nets", std::to_string(result.inserted_nets.size()), "materialized CTS nets"},
      {"power", logformat::FormatPowerW(result.best_char->get_power()), "selected pattern metric (total power)"},
      {"delay", logformat::FormatWithUnit(result.best_char->get_delay(), "ns"), "selected pattern metric"},
      {"root_driven_cap_idx", std::to_string(result.best_char->get_driven_cap_idx()), "selected pattern metric"},
      {"leaf_output_slew_idx", std::to_string(result.best_char->get_output_slew_idx()), "selected pattern metric"},
      {"root_load_cap_idx", std::to_string(result.best_char->get_load_cap_idx()), "selected pattern metric"},
  };
  logInfoTable("HTreeBuilder Build Summary", {"Item", "Value", "Detail"}, build_summary_rows);
  build_stage.finish({{"success", result.success ? "true" : "false"},
                      {"levels", std::to_string(result.levels.size())},
                      {"inserted_insts", std::to_string(result.inserted_insts.size())},
                      {"inserted_nets", std::to_string(result.inserted_nets.size())}},
                     result.success ? "success" : "incomplete");
  return result;
}

}  // namespace icts
