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
 * @file SegmentLibrary.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief H-tree segment and topology pattern library helpers.
 */

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "characterization/Frontier.hh"
#include "log/Log.hh"
#include "synthesis/htree/segment_pruning/BufferStrength.hh"

namespace icts::htree {

enum class SegmentFrontierKind
{
  kAll,
  kTerminalBranchBuffered,
  kTerminalLeafUnbuffered,
};

class SegmentFrontierKindSet
{
 public:
  SegmentFrontierKindSet() = default;

  static auto allOnly() -> SegmentFrontierKindSet
  {
    SegmentFrontierKindSet result;
    result.add(SegmentFrontierKind::kAll);
    return result;
  }

  static auto branchConstrained() -> SegmentFrontierKindSet
  {
    auto result = allOnly();
    result.add(SegmentFrontierKind::kTerminalBranchBuffered);
    return result;
  }

  static auto leafConstrained() -> SegmentFrontierKindSet
  {
    auto result = allOnly();
    result.add(SegmentFrontierKind::kTerminalLeafUnbuffered);
    return result;
  }

  static auto full() -> SegmentFrontierKindSet
  {
    auto result = branchConstrained();
    result.add(SegmentFrontierKind::kTerminalLeafUnbuffered);
    return result;
  }

  auto add(SegmentFrontierKind kind) -> void { _mask |= bit(kind); }
  auto contains(SegmentFrontierKind kind) const -> bool { return (_mask & bit(kind)) != 0U; }
  auto empty() const -> bool { return _mask == 0U; }

  auto normalized() const -> SegmentFrontierKindSet
  {
    auto result = *this;
    if (!result.empty()) {
      result.add(SegmentFrontierKind::kAll);
    }
    return result;
  }

 private:
  static auto bit(SegmentFrontierKind kind) -> unsigned
  {
    switch (kind) {
      case SegmentFrontierKind::kAll:
        return 1U << 0U;
      case SegmentFrontierKind::kTerminalBranchBuffered:
        return 1U << 1U;
      case SegmentFrontierKind::kTerminalLeafUnbuffered:
        return 1U << 2U;
    }
    return 0U;
  }

  unsigned _mask = 0U;
};

struct BufferPatternLibrary
{
  auto add(BufferingPattern pattern) -> void
  {
    pattern = enrichBoundaryState(std::move(pattern));
    const PatternId pattern_id = pattern.get_pattern_id();
    composition_states[pattern_id] = PatternCompositionState{
        .terminal_semantic = pattern.hasTerminalBranchBuffer() ? TerminalSemantic::kBranchBuffered : TerminalSemantic::kLeafUnbuffered,
        .monotonic_boundary_state = pattern.get_monotonic_boundary_state(),
    };
    patterns[pattern_id] = std::move(pattern);
  }

  auto find(PatternId pattern_id) const -> const BufferingPattern*
  {
    auto it = patterns.find(pattern_id);
    return it == patterns.end() ? nullptr : &it->second;
  }

  auto getCompositionState(PatternId pattern_id) const -> PatternCompositionState
  {
    const auto it = composition_states.find(pattern_id);
    LOG_FATAL_IF(it == composition_states.end()) << "HTree: missing segment pattern composition-state cache entry.";
    return it->second;
  }

  auto getTerminalSemantic(PatternId pattern_id) const -> TerminalSemantic { return getCompositionState(pattern_id).terminal_semantic; }

 private:
  static auto resolveBoundaryBufferState(const BoundaryBufferState& explicit_state, const std::string& cell_master,
                                         BufferStrengthTable& strength_table) -> BoundaryBufferState
  {
    if (explicit_state.has_buffer) {
      return explicit_state;
    }
    if (cell_master.empty()) {
      return explicit_state;
    }
    return BoundaryBufferState{.has_buffer = true, .strength_rank = strength_table.getStrengthRank(cell_master)};
  }

  auto resolveBoundaryState(const BufferingPattern& pattern) -> MonotonicBoundaryState
  {
    const auto explicit_state = pattern.get_monotonic_boundary_state();
    if (!pattern.isBufferPattern()) {
      return explicit_state;
    }

    const auto& cell_masters = pattern.get_cell_masters();
    if (cell_masters.empty()) {
      return explicit_state;
    }

    return MonotonicBoundaryState{
        .source = resolveBoundaryBufferState(explicit_state.source, cell_masters.front(), _strength_table),
        .sink = resolveBoundaryBufferState(explicit_state.sink, cell_masters.back(), _strength_table),
    };
  }

  auto enrichBoundaryState(BufferingPattern pattern) -> BufferingPattern
  {
    const auto boundary_state = resolveBoundaryState(pattern);
    if (boundary_state == pattern.get_monotonic_boundary_state()) {
      return pattern;
    }

    return BufferingPattern(pattern.get_length_idx(), pattern.get_pattern_id(), pattern.get_buffer_positions(), pattern.get_cell_masters(),
                            pattern.hasTerminalBranchBuffer(), boundary_state);
  }

 public:
  std::unordered_map<PatternId, BufferingPattern> patterns;
  std::unordered_map<PatternId, PatternCompositionState> composition_states;

 private:
  BufferStrengthTable _strength_table;
};

struct SegmentFrontierRequest
{
  std::vector<unsigned> required_length_indices;
  SegmentFrontierKindSet required_kinds = SegmentFrontierKindSet::full();
};

class SegmentCandidateFrontierSet
{
 public:
  auto hasKind(SegmentFrontierKind kind) const -> bool { return _built_kinds.contains(kind); }

  auto find(SegmentFrontierKind kind) const -> const std::vector<SegmentChar>*
  {
    if (!hasKind(kind)) {
      return nullptr;
    }
    return &entries(kind);
  }

  auto require(SegmentFrontierKind kind) const -> const std::vector<SegmentChar>&
  {
    const auto* frontier = find(kind);
    LOG_FATAL_IF(frontier == nullptr) << "HTree: requested segment frontier kind was not synthesized.";
    return *frontier;
  }

  auto mutableEntries(SegmentFrontierKind kind) -> std::vector<SegmentChar>&
  {
    _built_kinds.add(kind);
    switch (kind) {
      case SegmentFrontierKind::kAll:
        return _all_frontier_entries;
      case SegmentFrontierKind::kTerminalBranchBuffered:
        return _branch_buffered_entries;
      case SegmentFrontierKind::kTerminalLeafUnbuffered:
        return _leaf_unbuffered_entries;
    }
    return _all_frontier_entries;
  }

  auto countEntries(SegmentFrontierKindSet kinds) const -> std::size_t
  {
    static constexpr std::array<SegmentFrontierKind, 3> frontier_kinds
        = {SegmentFrontierKind::kAll, SegmentFrontierKind::kTerminalBranchBuffered, SegmentFrontierKind::kTerminalLeafUnbuffered};
    std::size_t total_entries = 0U;
    for (const auto kind : frontier_kinds) {
      if (kinds.contains(kind)) {
        const auto* frontier = find(kind);
        total_entries += frontier == nullptr ? 0U : frontier->size();
      }
    }
    return total_entries;
  }

 private:
  auto entries(SegmentFrontierKind kind) const -> const std::vector<SegmentChar>&
  {
    switch (kind) {
      case SegmentFrontierKind::kAll:
        return _all_frontier_entries;
      case SegmentFrontierKind::kTerminalBranchBuffered:
        return _branch_buffered_entries;
      case SegmentFrontierKind::kTerminalLeafUnbuffered:
        return _leaf_unbuffered_entries;
    }
    return _all_frontier_entries;
  }

  SegmentFrontierKindSet _built_kinds;
  std::vector<SegmentChar> _all_frontier_entries;
  std::vector<SegmentChar> _branch_buffered_entries;
  std::vector<SegmentChar> _leaf_unbuffered_entries;
};

class SegmentFrontierCatalog
{
 public:
  SegmentFrontierCatalog() = default;
  explicit SegmentFrontierCatalog(std::unordered_map<unsigned, SegmentCandidateFrontierSet> entry_sets_by_length)
      : _entry_sets_by_length(std::move(entry_sets_by_length))
  {
  }

  auto empty() const -> bool { return _entry_sets_by_length.empty(); }
  auto lengthCount() const -> std::size_t { return _entry_sets_by_length.size(); }

  auto find(unsigned length_idx, SegmentFrontierKind kind) const -> const std::vector<SegmentChar>*
  {
    const auto it = _entry_sets_by_length.find(length_idx);
    if (it == _entry_sets_by_length.end()) {
      return nullptr;
    }
    return it->second.find(kind);
  }

  auto require(unsigned length_idx, SegmentFrontierKind kind) const -> const std::vector<SegmentChar>&
  {
    const auto* frontier = find(length_idx, kind);
    LOG_FATAL_IF(frontier == nullptr) << "HTree: requested segment frontier length/kind was not synthesized.";
    return *frontier;
  }

  auto countEntries(SegmentFrontierKindSet kinds) const -> std::size_t
  {
    std::size_t total_entries = 0U;
    for (const auto& [length_idx, entry_set] : _entry_sets_by_length) {
      (void) length_idx;
      total_entries += entry_set.countEntries(kinds);
    }
    return total_entries;
  }

 private:
  std::unordered_map<unsigned, SegmentCandidateFrontierSet> _entry_sets_by_length;
};

struct RequiredLengthStateKey
{
  std::vector<unsigned> pending_lengths;

  auto operator==(const RequiredLengthStateKey& rhs) const -> bool = default;
};

struct RequiredLengthStateKeyHash
{
  auto operator()(const RequiredLengthStateKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = 0U;
    for (const unsigned length_idx : key.pending_lengths) {
      hash_value ^= std::hash<unsigned>{}(length_idx) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    }
    return hash_value;
  }
};

struct SegmentClosureSolution
{
  bool feasible = false;
  unsigned total_cost = 0U;
  std::unordered_map<unsigned, SegmentCandidateFrontierSet> synthesized_entry_sets;
};

enum class TopologyPatternNodeKind
{
  kSeed,
  kConcat,
};

struct TopologyPatternNode
{
  PatternId pattern_id{PatternDomain::kTopologyPattern, 0U};
  unsigned levels = 0U;
  TerminalSemantic terminal_semantic = TerminalSemantic::kLeafUnbuffered;
  MonotonicBoundaryState monotonic_boundary_state{};
  TopologyPatternNodeKind kind = TopologyPatternNodeKind::kSeed;
  PatternId segment_pattern_id{PatternDomain::kSegmentPattern, 0U};
  PatternId upstream_pattern_id{PatternDomain::kTopologyPattern, 0U};
  PatternId downstream_pattern_id{PatternDomain::kTopologyPattern, 0U};
};

struct TopologyPatternLibrary
{
  auto addSeed(PatternId pattern_id, PatternId segment_pattern_id, const PatternCompositionState& composition_state) -> void
  {
    LOG_FATAL_IF(pattern_id.domain != PatternDomain::kTopologyPattern) << "HTree: topology library received a non-topology pattern ID.";
    LOG_FATAL_IF(pattern_id.local_id != nodes.size()) << "HTree: topology library requires sequential topology pattern IDs.";
    nodes.push_back(TopologyPatternNode{
        .pattern_id = pattern_id,
        .levels = 1U,
        .terminal_semantic = composition_state.terminal_semantic,
        .monotonic_boundary_state = composition_state.monotonic_boundary_state,
        .kind = TopologyPatternNodeKind::kSeed,
        .segment_pattern_id = segment_pattern_id,
    });
  }

  auto addConcat(PatternId pattern_id, unsigned levels, PatternId upstream_pattern_id, PatternId downstream_pattern_id,
                 const PatternCompositionState& composition_state) -> void
  {
    LOG_FATAL_IF(pattern_id.domain != PatternDomain::kTopologyPattern) << "HTree: topology library received a non-topology pattern ID.";
    LOG_FATAL_IF(pattern_id.local_id != nodes.size()) << "HTree: topology library requires sequential topology pattern IDs.";
    nodes.push_back(TopologyPatternNode{
        .pattern_id = pattern_id,
        .levels = levels,
        .terminal_semantic = composition_state.terminal_semantic,
        .monotonic_boundary_state = composition_state.monotonic_boundary_state,
        .kind = TopologyPatternNodeKind::kConcat,
        .upstream_pattern_id = upstream_pattern_id,
        .downstream_pattern_id = downstream_pattern_id,
    });
  }

  auto findNode(PatternId pattern_id) const -> const TopologyPatternNode*
  {
    if (pattern_id.domain != PatternDomain::kTopologyPattern || pattern_id.local_id >= nodes.size()) {
      return nullptr;
    }
    return &nodes.at(pattern_id.local_id);
  }

  auto getCompositionState(PatternId pattern_id) const -> PatternCompositionState
  {
    const auto* node = findNode(pattern_id);
    LOG_FATAL_IF(node == nullptr) << "HTree: missing topology pattern composition-state cache entry.";
    return PatternCompositionState{
        .terminal_semantic = node->terminal_semantic,
        .monotonic_boundary_state = node->monotonic_boundary_state,
    };
  }

  auto getTerminalSemantic(PatternId pattern_id) const -> TerminalSemantic { return getCompositionState(pattern_id).terminal_semantic; }

  auto materialize(PatternId pattern_id) const -> HTreeTopologyPattern
  {
    const auto* node = findNode(pattern_id);
    LOG_FATAL_IF(node == nullptr) << "HTree: missing topology pattern metadata.";

    std::vector<PatternId> level_segment_pattern_ids;
    level_segment_pattern_ids.reserve(node->levels);

    std::vector<PatternId> pending_pattern_ids;
    pending_pattern_ids.push_back(pattern_id);
    while (!pending_pattern_ids.empty()) {
      const PatternId current_pattern_id = pending_pattern_ids.back();
      pending_pattern_ids.pop_back();

      const auto* current_node = findNode(current_pattern_id);
      LOG_FATAL_IF(current_node == nullptr) << "HTree: missing topology pattern metadata during materialization.";

      if (current_node->kind == TopologyPatternNodeKind::kSeed) {
        level_segment_pattern_ids.push_back(current_node->segment_pattern_id);
        continue;
      }

      pending_pattern_ids.push_back(current_node->downstream_pattern_id);
      pending_pattern_ids.push_back(current_node->upstream_pattern_id);
    }

    LOG_FATAL_IF(level_segment_pattern_ids.size() != node->levels)
        << "HTree: materialized topology pattern level count does not match node metadata.";
    return HTreeTopologyPattern(pattern_id, node->levels, std::move(level_segment_pattern_ids));
  }

  std::vector<TopologyPatternNode> nodes;
};

struct PatternSearchResult
{
  bool success = false;
  std::string failure_reason;
  unsigned failure_level = 0U;
  unsigned failure_length_idx = 0U;
  std::vector<HTreeTopologyChar> frontier;
  TopologyPatternLibrary topology_pattern_library;
};

class SegmentPatternLibraryCombiner
{
 public:
  SegmentPatternLibraryCombiner(BufferPatternLibrary& library, unsigned start_id) : _library(&library), _next_id(start_id) {}

  auto canCompose(PatternId upstream, PatternId downstream) const -> bool
  {
    const auto* upstream_pattern = _library->find(upstream);
    const auto* downstream_pattern = _library->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTree: missing segment pattern during monotonic-state validation.";
    return BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern);
  }

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _library->find(upstream);
    const auto* downstream_pattern = _library->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr) << "HTree: missing segment pattern during composition.";
    LOG_FATAL_IF(!BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern))
        << "HTree: invalid non-monotonic segment pattern composition.";

    const PatternId merged_pattern_id = PatternId::segment(_next_id++);
    auto merged_pattern = BufferingPattern::concat(*upstream_pattern, *downstream_pattern);
    _library->add(BufferingPattern(merged_pattern.get_length_idx(), merged_pattern_id, merged_pattern.get_buffer_positions(),
                                   merged_pattern.get_cell_masters(), merged_pattern.hasTerminalBranchBuffer(),
                                   merged_pattern.get_monotonic_boundary_state()));
    return merged_pattern_id;
  }

  auto get_next_id() const -> unsigned { return _next_id; }

 private:
  BufferPatternLibrary* _library = nullptr;
  mutable unsigned _next_id;
};

class TopologyPatternLibraryCombiner
{
 public:
  TopologyPatternLibraryCombiner(TopologyPatternLibrary& library, unsigned start_id) : _library(&library), _next_id(start_id) {}

  auto canCompose(PatternId upstream, PatternId downstream) const -> bool
  {
    const auto upstream_state = _library->getCompositionState(upstream);
    const auto downstream_state = _library->getCompositionState(downstream);
    return upstream_state.monotonic_boundary_state.canComposeWith(downstream_state.monotonic_boundary_state);
  }

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _library->findNode(upstream);
    const auto* downstream_pattern = _library->findNode(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr) << "HTree: missing topology pattern during composition.";
    LOG_FATAL_IF(!canCompose(upstream, downstream)) << "HTree: invalid non-monotonic topology pattern composition.";

    const PatternId merged_pattern_id = PatternId::topology(_next_id++);
    const auto upstream_state = _library->getCompositionState(upstream);
    const auto downstream_state = _library->getCompositionState(downstream);
    _library->addConcat(merged_pattern_id, upstream_pattern->levels + downstream_pattern->levels, upstream, downstream,
                        PatternCompositionState{
                            .terminal_semantic = downstream_state.terminal_semantic,
                            .monotonic_boundary_state = MonotonicBoundaryState::compose(upstream_state.monotonic_boundary_state,
                                                                                        downstream_state.monotonic_boundary_state),
                        });
    return merged_pattern_id;
  }

  auto get_next_id() const -> unsigned { return _next_id; }

 private:
  TopologyPatternLibrary* _library = nullptr;
  mutable unsigned _next_id;
};

}  // namespace icts::htree
