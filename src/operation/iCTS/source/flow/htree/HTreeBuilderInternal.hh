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
 * @file HTreeBuilderInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Shared internal H-tree builder data structures and helper contracts.
 */

#pragma once

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "LogFormat.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "Tree.hh"
#include "ValueLattice.hh"
#include "adapter/sta/STAAdapter.hh"
#include "characterization/CharBuilder.hh"
#include "characterization/Frontier.hh"
#include "characterization/HTreeTraits.hh"
#include "characterization/HashJoinEngine.hh"
#include "characterization/SegmentTraits.hh"
#include "htree/HTreeBuilder.hh"
#include "log/Log.hh"

namespace icts::htree_builder {

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
  unsigned configured_wire_length_iterations = 0U;
  unsigned required_covering_iterations = 0U;
  unsigned unique_level_bins = 0U;
  double configured_wire_length_unit_um = 0.0;
  double auto_derived_wire_length_unit_um = 0.0;
  unsigned requested_level_lengths = 0U;
  bool configured_wire_length_missing = false;
  bool configured_grid_collapsed = false;
  bool adapted = false;
  CharGridSource source = CharGridSource::kNone;
};

struct ResolvedBuildOptions
{
  bool force_branch_buffer = false;
  std::optional<double> min_top_input_slew_ns = std::nullopt;
  std::optional<unsigned> top_input_slew_covering_idx = std::nullopt;
};

struct CapDistributionStats
{
  std::size_t group_count = 0U;
  double cap_min_pf = 0.0;
  double cap_max_pf = 0.0;
  double cap_mean_pf = 0.0;
  double cap_median_pf = 0.0;
};

struct ActualLoadBoundaryGroup
{
  std::size_t node_id = std::numeric_limits<std::size_t>::max();
  Point<int> anchor;
  const std::vector<Pin*>* loads = nullptr;
};

struct ActualLoadLegalitySignature
{
  int bottom_most_buffered_level = -1;
  PatternId segment_pattern_id = PatternId::segment(0);

  auto operator==(const ActualLoadLegalitySignature& rhs) const -> bool = default;
};

struct ActualLoadLegalitySignatureHash
{
  auto operator()(const ActualLoadLegalitySignature& signature) const noexcept -> std::size_t
  {
    std::size_t hash_value = std::hash<int>{}(signature.bottom_most_buffered_level);
    hash_value ^= std::hash<unsigned>{}(signature.segment_pattern_id.pack()) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    return hash_value;
  }
};

enum class ActualLoadViolation
{
  kNone,
  kMissingTopologyRoot,
  kMissingTopologyNode,
  kMissingTopologyLevel,
  kMissingSegmentPattern,
  kMissingBufferPosition,
  kEmptyLoadGroup,
  kFanout,
  kPinCapLowerBound,
  kRoutingFailed,
  kCapacitance,
};

struct ActualLoadLegalityResult
{
  bool legal = false;
  bool monotone_hard_fail = false;
  ActualLoadViolation violation = ActualLoadViolation::kMissingTopologyRoot;
  std::string failure_reason;
  CapDistributionStats cap_distribution;
  double required_leaf_load_cap_pf = 0.0;
  std::optional<unsigned> required_leaf_load_cap_covering_idx = std::nullopt;
  int bottom_most_buffered_level = -1;
  PatternId segment_pattern_id = PatternId::segment(0);
};

struct ActualLoadLegalityContext
{
  std::unordered_map<ActualLoadLegalitySignature, ActualLoadLegalityResult, ActualLoadLegalitySignatureHash> result_by_signature;
  int max_monotone_failed_level = std::numeric_limits<int>::min();
  UniformValueLattice cap_lattice;
};

enum class SegmentEntrySelection
{
  kAllFrontier,
  kBranchBuffered,
  kLeafUnbuffered,
};

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

class BufferStrengthCache
{
 public:
  auto getStrengthRank(const std::string& cell_master) -> unsigned
  {
    if (cell_master.empty()) {
      return 0U;
    }

    if (!_drive_caps.contains(cell_master)) {
      double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
      if (drive_cap_pf <= 0.0) {
        drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
      }
      _drive_caps[cell_master] = drive_cap_pf;
      _ranks_dirty = true;

      if (drive_cap_pf <= 0.0) {
        LOG_WARNING << "HTreeBuilder: failed to resolve drive-strength rank for buffer master " << cell_master
                    << "; monotonic composition keeps an explicit boundary buffer with unresolved size class.";
      }
    }

    if (_ranks_dirty) {
      rebuildRanks();
    }

    const auto rank_it = _strength_ranks.find(cell_master);
    return rank_it == _strength_ranks.end() ? 0U : rank_it->second;
  }

 private:
  auto rebuildRanks() -> void
  {
    std::vector<std::pair<std::string, double>> ordered_caps;
    ordered_caps.reserve(_drive_caps.size());
    for (const auto& [cell_master, drive_cap_pf] : _drive_caps) {
      if (drive_cap_pf > 0.0) {
        ordered_caps.emplace_back(cell_master, drive_cap_pf);
      }
    }

    std::ranges::sort(ordered_caps, [](const auto& lhs, const auto& rhs) -> bool {
      if (lhs.second != rhs.second) {
        return lhs.second < rhs.second;
      }
      return lhs.first < rhs.first;
    });

    _strength_ranks.clear();
    unsigned current_rank = 0U;
    double last_cap_pf = 0.0;
    bool has_last_cap = false;
    for (const auto& [cell_master, drive_cap_pf] : ordered_caps) {
      if (!has_last_cap || std::abs(drive_cap_pf - last_cap_pf) > 1e-12) {
        ++current_rank;
        last_cap_pf = drive_cap_pf;
        has_last_cap = true;
      }
      _strength_ranks[cell_master] = current_rank;
    }

    _ranks_dirty = false;
  }

  std::unordered_map<std::string, double> _drive_caps;
  std::unordered_map<std::string, unsigned> _strength_ranks;
  bool _ranks_dirty = false;
};

struct BufferPatternRegistry
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
    LOG_FATAL_IF(it == composition_states.end()) << "HTreeBuilder: missing segment pattern composition-state cache entry.";
    return it->second;
  }

  auto getTerminalSemantic(PatternId pattern_id) const -> TerminalSemantic { return getCompositionState(pattern_id).terminal_semantic; }

 private:
  static auto resolveBoundaryBufferState(const BoundaryBufferState& explicit_state, const std::string& cell_master,
                                         BufferStrengthCache& strength_cache) -> BoundaryBufferState
  {
    if (explicit_state.has_buffer) {
      return explicit_state;
    }
    if (cell_master.empty()) {
      return explicit_state;
    }
    return BoundaryBufferState{.has_buffer = true, .strength_rank = strength_cache.getStrengthRank(cell_master)};
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
        .source = resolveBoundaryBufferState(explicit_state.source, cell_masters.front(), _strength_cache),
        .sink = resolveBoundaryBufferState(explicit_state.sink, cell_masters.back(), _strength_cache),
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
  BufferStrengthCache _strength_cache;
};

struct SegmentFrontierSet
{
  std::vector<SegmentChar> all_frontier_entries;
  std::vector<SegmentChar> branch_buffered_entries;
  std::vector<SegmentChar> leaf_unbuffered_entries;
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
  std::unordered_map<unsigned, SegmentFrontierSet> synthesized_entry_sets;
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

struct TopologyPatternRegistry
{
  auto addSeed(PatternId pattern_id, PatternId segment_pattern_id, const PatternCompositionState& composition_state) -> void
  {
    LOG_FATAL_IF(pattern_id.domain != PatternDomain::kTopologyPattern)
        << "HTreeBuilder: topology registry received a non-topology pattern ID.";
    LOG_FATAL_IF(pattern_id.local_id != nodes.size()) << "HTreeBuilder: topology registry requires sequential topology pattern IDs.";
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
    LOG_FATAL_IF(pattern_id.domain != PatternDomain::kTopologyPattern)
        << "HTreeBuilder: topology registry received a non-topology pattern ID.";
    LOG_FATAL_IF(pattern_id.local_id != nodes.size()) << "HTreeBuilder: topology registry requires sequential topology pattern IDs.";
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
    LOG_FATAL_IF(node == nullptr) << "HTreeBuilder: missing topology pattern composition-state cache entry.";
    return PatternCompositionState{
        .terminal_semantic = node->terminal_semantic,
        .monotonic_boundary_state = node->monotonic_boundary_state,
    };
  }

  auto getTerminalSemantic(PatternId pattern_id) const -> TerminalSemantic { return getCompositionState(pattern_id).terminal_semantic; }

  auto materialize(PatternId pattern_id) const -> HTreeTopologyPattern
  {
    const auto* node = findNode(pattern_id);
    LOG_FATAL_IF(node == nullptr) << "HTreeBuilder: missing topology pattern metadata.";

    std::vector<PatternId> level_segment_pattern_ids;
    level_segment_pattern_ids.reserve(node->levels);

    std::vector<PatternId> pending_pattern_ids;
    pending_pattern_ids.push_back(pattern_id);
    while (!pending_pattern_ids.empty()) {
      const PatternId current_pattern_id = pending_pattern_ids.back();
      pending_pattern_ids.pop_back();

      const auto* current_node = findNode(current_pattern_id);
      LOG_FATAL_IF(current_node == nullptr) << "HTreeBuilder: missing topology pattern metadata during materialization.";

      if (current_node->kind == TopologyPatternNodeKind::kSeed) {
        level_segment_pattern_ids.push_back(current_node->segment_pattern_id);
        continue;
      }

      pending_pattern_ids.push_back(current_node->downstream_pattern_id);
      pending_pattern_ids.push_back(current_node->upstream_pattern_id);
    }

    LOG_FATAL_IF(level_segment_pattern_ids.size() != node->levels)
        << "HTreeBuilder: materialized topology pattern level count does not match node metadata.";
    return HTreeTopologyPattern(pattern_id, node->levels, std::move(level_segment_pattern_ids));
  }

  std::vector<TopologyPatternNode> nodes;
};

struct HTreeCompositionResult
{
  bool success = false;
  std::string failure_reason;
  unsigned failure_level = 0U;
  unsigned failure_length_idx = 0U;
  std::vector<HTreeTopologyChar> frontier;
  TopologyPatternRegistry topology_pattern_registry;
};

class SegmentPatternRegistryCombiner
{
 public:
  SegmentPatternRegistryCombiner(BufferPatternRegistry& registry, unsigned start_id) : _registry(&registry), _next_id(start_id) {}

  auto canCompose(PatternId upstream, PatternId downstream) const -> bool
  {
    const auto* upstream_pattern = _registry->find(upstream);
    const auto* downstream_pattern = _registry->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTreeBuilder: missing segment pattern during monotonic-state validation.";
    return BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern);
  }

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _registry->find(upstream);
    const auto* downstream_pattern = _registry->find(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTreeBuilder: missing segment pattern during composition.";
    LOG_FATAL_IF(!BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern))
        << "HTreeBuilder: invalid non-monotonic segment pattern composition.";

    const PatternId merged_pattern_id = PatternId::segment(_next_id++);
    auto merged_pattern = BufferingPattern::concat(*upstream_pattern, *downstream_pattern);
    _registry->add(BufferingPattern(merged_pattern.get_length_idx(), merged_pattern_id, merged_pattern.get_buffer_positions(),
                                    merged_pattern.get_cell_masters(), merged_pattern.hasTerminalBranchBuffer(),
                                    merged_pattern.get_monotonic_boundary_state()));
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

  auto canCompose(PatternId upstream, PatternId downstream) const -> bool
  {
    const auto upstream_state = _registry->getCompositionState(upstream);
    const auto downstream_state = _registry->getCompositionState(downstream);
    return upstream_state.monotonic_boundary_state.canComposeWith(downstream_state.monotonic_boundary_state);
  }

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _registry->findNode(upstream);
    const auto* downstream_pattern = _registry->findNode(downstream);
    LOG_FATAL_IF(upstream_pattern == nullptr || downstream_pattern == nullptr)
        << "HTreeBuilder: missing topology pattern during composition.";
    LOG_FATAL_IF(!canCompose(upstream, downstream)) << "HTreeBuilder: invalid non-monotonic topology pattern composition.";

    const PatternId merged_pattern_id = PatternId::topology(_next_id++);
    const auto upstream_state = _registry->getCompositionState(upstream);
    const auto downstream_state = _registry->getCompositionState(downstream);
    _registry->addConcat(merged_pattern_id, upstream_pattern->levels + downstream_pattern->levels, upstream, downstream,
                         PatternCompositionState{
                             .terminal_semantic = downstream_state.terminal_semantic,
                             .monotonic_boundary_state = MonotonicBoundaryState::compose(upstream_state.monotonic_boundary_state,
                                                                                         downstream_state.monotonic_boundary_state),
                         });
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

struct CandidateBuildEvaluation
{
  unsigned depth = 0U;
  std::size_t leaf_count = 0U;
  ResolvedBuildOptions resolved_options;
  std::vector<HTreeBuilder::LevelPlan> levels;
  bool success = false;
  std::string failure_reason;
  std::optional<unsigned> failure_level = std::nullopt;
  std::optional<unsigned> failure_length_idx = std::nullopt;
  std::size_t final_frontier_count = 0U;
  std::vector<HTreeTopologyChar> candidate_chars;
  std::vector<HTreeTopologyChar> candidate_frontier_entries;
  std::vector<HTreeTopologyChar> feasible_chars;
  std::vector<HTreeTopologyChar> feasible_frontier_entries;
  std::optional<HTreeTopologyChar> best_char = std::nullopt;
  bool used_boundary_fallback = false;
  std::optional<double> boundary_fallback_score = std::nullopt;
  std::string boundary_fallback_reason;
  TopologyPatternRegistry topology_pattern_registry;
};

struct HTreeDepthCandidateSummary
{
  unsigned depth = 0U;
  std::size_t leaf_count = 0U;
  bool success = false;
  bool selected = false;
  bool used_explicit_target_depth = false;
  std::string failure_reason;
  std::size_t htree_load_group_count = 0U;
  double htree_load_cap_min_pf = 0.0;
  double htree_load_cap_max_pf = 0.0;
  double htree_load_cap_mean_pf = 0.0;
  double htree_load_cap_median_pf = 0.0;
  std::size_t final_frontier_count = 0U;
  std::size_t candidate_solution_count = 0U;
  std::size_t candidate_frontier_entry_count = 0U;
  std::size_t feasible_solution_count = 0U;
  std::size_t feasible_frontier_entry_count = 0U;
  bool used_boundary_fallback = false;
  double selected_power_w = 0.0;
  double selected_delay_ns = 0.0;
};

struct ActualLoadEntryFilterResult
{
  std::vector<HTreeTopologyChar> entries;
  std::string first_failure_reason;
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

struct HTreeCharacterizationFlowResult
{
  bool success = false;
  std::string failure_reason;
  double length_step_um = 0.0;
};

struct HTreeDepthExplorationResult
{
  std::vector<CandidateBuildEvaluation> candidate_evaluations;
  std::vector<HTreeDepthCandidateSummary> depth_summaries;
  std::vector<CandidateCharRef> global_feasible_pool;
  std::vector<CandidateCharRef> global_candidate_pool;
  ActualLoadLegalityContext actual_load_legality_context;
};

struct HTreeDepthCandidateEvaluationResult
{
  CandidateBuildEvaluation evaluation;
  std::size_t leaf_count = 0U;
};

struct MaterializationContext
{
  HTreeBuilder::BuildResult* result = nullptr;
  BufferPortCache* port_cache = nullptr;
  std::size_t edge_buffer_counter = 0U;
  std::size_t net_counter = 0U;

  auto nextBufferName() -> std::string { return "cts_htree_edge_buf_" + std::to_string(edge_buffer_counter++); }

  auto nextNetName() -> std::string { return "cts_htree_net_" + std::to_string(net_counter++); }
};

auto ToCharGridSourceName(CharGridSource source) -> const char*;
auto LogInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void;
auto CountUniqueAlignedLengthBins(const std::vector<double>& requested_lengths_um, double length_step_um) -> unsigned;
auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>;
auto ResolveCharacterizationGridPlan(const Tree& topology, int32_t dbu_per_um) -> CharacterizationGridPlan;
auto BuildLevelPlans(const Tree& topology, double length_step_um, int32_t dbu_per_um) -> std::vector<HTreeBuilder::LevelPlan>;
auto ResolveDirectCharacterizationLengthIndices(const Tree& topology, const CharacterizationGridPlan& char_grid_plan, int32_t dbu_per_um)
    -> std::vector<unsigned>;
auto MakeCandidateLevelPlans(const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth)
    -> std::vector<HTreeBuilder::LevelPlan>;
auto CountCandidateLeafNodes(const Tree& topology, unsigned depth) -> std::size_t;
auto ResolveDepthCandidates(unsigned max_depth, const HTreeBuilder::BuildOptions& options) -> std::vector<unsigned>;
auto ResolveBuildOptions(const HTreeBuilder::BuildOptions& options, const CharBuilder& char_builder) -> ResolvedBuildOptions;
auto RunCharacterizationFlow(const Tree& topology, int32_t dbu_per_um, HTreeBuilder::BuildResult& result, CharBuilder& char_builder)
    -> HTreeCharacterizationFlowResult;
auto ExploreDepthCandidates(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans,
                            const std::vector<unsigned>& depth_candidates,
                            const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                            BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                            const UniformValueLattice& cap_lattice, unsigned char_slew_steps, bool used_explicit_target_depth)
    -> HTreeDepthExplorationResult;
auto EvaluateDepthCandidate(const Tree& topology, const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth,
                            const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                            BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& base_resolved_options,
                            ActualLoadLegalityContext& actual_load_legality_context, unsigned char_slew_steps)
    -> HTreeDepthCandidateEvaluationResult;
auto RecordDepthCandidateResult(unsigned depth, bool used_explicit_target_depth,
                                const HTreeDepthCandidateEvaluationResult& candidate_result,
                                std::vector<HTreeDepthCandidateSummary>& depth_summaries) -> void;
auto AppendGlobalCandidateRefs(std::size_t candidate_index, const CandidateBuildEvaluation& evaluation,
                               std::vector<CandidateCharRef>& global_feasible_pool, std::vector<CandidateCharRef>& global_candidate_pool)
    -> void;
auto LogHTreeBuildSummary(const HTreeBuilder::BuildResult& result, const CandidateBuildEvaluation& selected_evaluation,
                          const HTreeDepthCandidateSummary& selected_summary) -> void;
auto HasBoundaryConstraints(const ResolvedBuildOptions& options) -> bool;
auto CoveringBoundaryIndex(double value, const UniformValueLattice& lattice) -> std::optional<unsigned>;
auto SynthesizeSegmentEntrySets(const std::vector<SegmentChar>& base_segment_chars, BufferPatternRegistry& pattern_registry,
                                const std::vector<unsigned>& required_length_indices) -> std::unordered_map<unsigned, SegmentFrontierSet>;
auto CollectRequiredLengthIndices(const std::vector<HTreeBuilder::LevelPlan>& levels) -> std::vector<unsigned>;
auto EvaluateCandidateBuild(const std::vector<HTreeBuilder::LevelPlan>& levels,
                            const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                            const BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& resolved_options,
                            const Tree& topology, ActualLoadLegalityContext& actual_load_legality_context, std::size_t leaf_count,
                            unsigned depth, unsigned char_slew_steps) -> CandidateBuildEvaluation;
auto FilterGlobalEntriesByActualBoundaryCoverage(const std::vector<CandidateCharRef>& entries,
                                                 const std::vector<CandidateBuildEvaluation>& evaluations, const Tree& topology,
                                                 const BufferPatternRegistry& segment_pattern_registry,
                                                 ActualLoadLegalityContext& legality_context) -> CandidateCharRefFilterResult;
auto ResolveActualLoadLegality(const Tree& topology, PatternId topology_pattern_id, const TopologyPatternRegistry& topology_registry,
                               const BufferPatternRegistry& segment_pattern_registry, ActualLoadLegalityContext& legality_context)
    -> ActualLoadLegalityResult;
auto FilterActualLoadLegalEntries(const std::vector<HTreeTopologyChar>& entries, const Tree& topology,
                                  const TopologyPatternRegistry& topology_registry, const BufferPatternRegistry& segment_pattern_registry,
                                  ActualLoadLegalityContext& legality_context) -> ActualLoadEntryFilterResult;
auto SelectBestGlobalEntry(const std::vector<CandidateCharRef>& entries) -> std::optional<CandidateCharRef>;
auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const ResolvedBuildOptions& resolved_options, unsigned slew_steps) -> double;
auto InterpolateManhattanPoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>;
auto MaterializeCTSObjects(HTreeBuilder::BuildResult& result, const BufferPatternRegistry& segment_pattern_registry) -> void;

}  // namespace icts::htree_builder
