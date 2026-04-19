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
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief End-to-end H-tree synthesis flow built from topology and characterization modules.
 */

#include "htree/HTreeBuilder.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <compare>
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
#include <unordered_set>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharCore.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "TopologyConfig.hh"
#include "Tree.hh"
#include "adapter/sta/STAAdapter.hh"
#include "characterization/CharBuilder.hh"
#include "characterization/Frontier.hh"
#include "characterization/HTreeTraits.hh"
#include "characterization/HashJoinEngine.hh"
#include "characterization/SegmentTraits.hh"
#include "clustering/Clustering.hh"
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

struct ResolvedBuildOptions
{
  bool force_branch_buffer = false;
  bool force_leaf_unbuffered = false;
  std::optional<double> min_top_input_slew_ns = std::nullopt;
  std::optional<unsigned> top_input_slew_floor_idx = std::nullopt;
  std::optional<double> min_leaf_driven_cap_pf = std::nullopt;
  std::optional<unsigned> leaf_driven_cap_floor_idx = std::nullopt;
};

struct LeafDrivenCapResolution
{
  std::optional<double> cap_pf = std::nullopt;
  bool used_explicit_value = false;
  std::string source = "unconstrained";
  bool depth_infeasible = false;
  std::string failure_reason;
  std::size_t evaluated_leaf_count = 0U;
  double leaf_cap_min_pf = 0.0;
  double leaf_cap_max_pf = 0.0;
  double leaf_cap_mean_pf = 0.0;
  double leaf_cap_median_pf = 0.0;
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
  int bottom_most_buffered_level = -1;
  PatternId segment_pattern_id = PatternId::segment(0);
};

struct ActualLoadLegalityContext
{
  std::unordered_map<ActualLoadLegalitySignature, ActualLoadLegalityResult, ActualLoadLegalitySignatureHash> result_by_signature;
  int max_monotone_failed_level = std::numeric_limits<int>::min();
};

enum class SegmentEntrySelection
{
  kAllFrontier,
  kBranchBuffered,
  kLeafUnbuffered,
};

enum class PatternRepresentativeMode
{
  kWorstCaseSelection,
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
                    << "; monotonic composition falls back to an unconstrained boundary state.";
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
        .source_strength_rank = explicit_state.source_strength_rank != 0U ? explicit_state.source_strength_rank
                                                                          : _strength_cache.getStrengthRank(cell_masters.front()),
        .sink_strength_rank = explicit_state.sink_strength_rank != 0U ? explicit_state.sink_strength_rank
                                                                      : _strength_cache.getStrengthRank(cell_masters.back()),
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
  // Raw composed entries are retained even after extracting a frontier so later
  // stages can still form A-frontier + B-all and A-raw + B-leaf_unbuffered.
  std::vector<SegmentChar> all_raw_entries;
  // Frontier of A-frontier + B-all-raw compositions. This is the default
  // family when no terminal semantic is forced on the current H-tree level.
  std::vector<SegmentChar> all_frontier_entries;
  // Frontier of solutions whose downstream-most terminal remains buffered.
  // This family is valid for enforcing branch-buffered selection on any
  // H-tree level, not only the leaf level.
  std::vector<SegmentChar> branch_buffered_entries;
  // Frontier of solutions whose downstream-most terminal remains unbuffered.
  // It is built from A-raw + B-leaf_unbuffered compositions and is only used
  // for the final H-tree level when the caller explicitly requests an
  // unbuffered leaf terminal.
  std::vector<SegmentChar> leaf_unbuffered_entries;
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

auto BuildDelayPowerParetoFront(const std::vector<HTreeTopologyChar>& entries) -> std::vector<const HTreeTopologyChar*>;
auto PreferDelayPowerTieBreak(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool;
auto PreferPowerMedianOrder(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool;
auto InterpolateManhattanPoint(const Point<int>& source, const Point<int>& sink, double normalized_position) -> Point<int>;

struct StructuralPatternKey
{
  std::vector<unsigned> level_segment_pattern_ids;

  auto operator==(const StructuralPatternKey& rhs) const -> bool = default;
};

struct StructuralPatternKeyHash
{
  auto operator()(const StructuralPatternKey& key) const noexcept -> std::size_t
  {
    std::size_t hash_value = 0U;
    for (const unsigned packed_id : key.level_segment_pattern_ids) {
      hash_value ^= std::hash<unsigned>{}(packed_id) + 0x9e3779b9U + (hash_value << 6U) + (hash_value >> 2U);
    }
    return hash_value;
  }
};

class TopologyPatternStructuralKeyCache
{
 public:
  auto get(PatternId topology_pattern_id, const TopologyPatternRegistry& topology_registry) -> const StructuralPatternKey&
  {
    const auto cache_it = _key_by_pattern.find(topology_pattern_id);
    if (cache_it != _key_by_pattern.end()) {
      return cache_it->second;
    }

    StructuralPatternKey key;
    const auto topology_pattern = topology_registry.materialize(topology_pattern_id);
    key.level_segment_pattern_ids.reserve(topology_pattern.get_level_segment_pattern_ids().size());
    for (const auto segment_pattern_id : topology_pattern.get_level_segment_pattern_ids()) {
      key.level_segment_pattern_ids.push_back(segment_pattern_id.pack());
    }

    return _key_by_pattern.emplace(topology_pattern_id, std::move(key)).first->second;
  }

 private:
  std::unordered_map<PatternId, StructuralPatternKey> _key_by_pattern;
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
  std::vector<HTreeTopologyChar> candidate_chars;
  std::vector<HTreeTopologyChar> candidate_pattern_representatives;
  std::vector<HTreeTopologyChar> feasible_chars;
  std::vector<HTreeTopologyChar> feasible_pattern_representatives;
  std::optional<HTreeTopologyChar> best_char = std::nullopt;
  bool used_boundary_fallback = false;
  std::optional<double> boundary_fallback_score = std::nullopt;
  std::string boundary_fallback_reason;
  TopologyPatternRegistry topology_pattern_registry;
};

struct ActualLoadRepresentativeFilterResult
{
  std::vector<HTreeTopologyChar> entries;
  std::string first_failure_reason;
};

struct CandidateRepresentativeRef
{
  std::size_t candidate_index = 0U;
  const HTreeTopologyChar* entry = nullptr;
};

auto resolveSegmentCompositionState(const BufferPatternRegistry& pattern_registry, PatternId pattern_id) -> PatternCompositionState
{
  return pattern_registry.getCompositionState(pattern_id);
}

auto resolveHTreeCompositionState(const TopologyPatternRegistry& topology_registry, PatternId topology_pattern_id)
    -> PatternCompositionState
{
  return topology_registry.getCompositionState(topology_pattern_id);
}

auto BuildSegmentStateFrontier(const std::vector<SegmentChar>& chars, const BufferPatternRegistry& pattern_registry)
    -> std::vector<SegmentChar>
{
  return icts::BuildSegmentStateFrontier(chars, [&](const SegmentChar& entry) -> PatternCompositionState {
    return resolveSegmentCompositionState(pattern_registry, entry.get_pattern_id());
  });
}

auto BuildHTreeStateFrontier(const std::vector<HTreeTopologyChar>& chars, const TopologyPatternRegistry& topology_registry,
                             const BufferPatternRegistry& segment_pattern_registry) -> std::vector<HTreeTopologyChar>
{
  (void) segment_pattern_registry;
  return icts::BuildHTreeStateFrontier(chars, [&](const HTreeTopologyChar& entry) -> PatternCompositionState {
    return resolveHTreeCompositionState(topology_registry, entry.get_pattern_id());
  });
}

auto SelectSegmentCompositionCandidates(const std::vector<SegmentChar>& entries, const BufferPatternRegistry& pattern_registry)
    -> std::vector<SegmentChar>
{
  const std::size_t max_per_state_group = CONFIG_INST.get_relaxed_candidates_per_boundary_group();
  if (max_per_state_group == 0U) {
    return entries;
  }

  std::vector<SegmentChar> ordered_entries = entries;
  SortSegmentFrontierEntries(ordered_entries);

  std::unordered_map<SegmentFrontierStateKey, std::size_t, SegmentFrontierStateKeyHash> group_counts;
  group_counts.reserve(ordered_entries.size());

  std::vector<SegmentChar> selected_entries;
  selected_entries.reserve(ordered_entries.size());
  for (const auto& entry : ordered_entries) {
    const auto composition_state = resolveSegmentCompositionState(pattern_registry, entry.get_pattern_id());
    const SegmentFrontierStateKey group_key{
        .input_slew_idx = entry.get_input_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
        .terminal_semantic = composition_state.terminal_semantic,
        .monotonic_boundary_state = composition_state.monotonic_boundary_state,
    };
    auto& kept_count = group_counts[group_key];
    if (kept_count >= max_per_state_group) {
      continue;
    }
    selected_entries.push_back(entry);
    ++kept_count;
  }

  return selected_entries;
}

auto SelectHTreeCompositionCandidates(const std::vector<HTreeTopologyChar>& entries, const TopologyPatternRegistry& topology_registry,
                                      const BufferPatternRegistry& segment_pattern_registry) -> std::vector<HTreeTopologyChar>
{
  (void) segment_pattern_registry;
  const std::size_t max_per_state_group = CONFIG_INST.get_relaxed_candidates_per_boundary_group();
  if (max_per_state_group == 0U) {
    return entries;
  }

  std::vector<HTreeTopologyChar> ordered_entries = entries;
  SortHTreeFrontierEntries(ordered_entries);

  std::unordered_map<HTreeFrontierStateKey, std::size_t, HTreeFrontierStateKeyHash> group_counts;
  group_counts.reserve(ordered_entries.size());

  std::vector<HTreeTopologyChar> selected_entries;
  selected_entries.reserve(ordered_entries.size());
  for (const auto& entry : ordered_entries) {
    const auto composition_state = resolveHTreeCompositionState(topology_registry, entry.get_pattern_id());
    const HTreeFrontierStateKey group_key{
        .input_slew_idx = entry.get_input_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .leaf_driven_cap_idx = entry.get_leaf_driven_cap_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
        .terminal_semantic = composition_state.terminal_semantic,
        .monotonic_boundary_state = composition_state.monotonic_boundary_state,
    };
    auto& kept_count = group_counts[group_key];
    if (kept_count >= max_per_state_group) {
      continue;
    }
    selected_entries.push_back(entry);
    ++kept_count;
  }

  return selected_entries;
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

auto hasTerminalBranchBufferPattern(const BufferPatternRegistry& pattern_registry, PatternId pattern_id) -> bool
{
  const auto* pattern = pattern_registry.find(pattern_id);
  return pattern != nullptr && pattern->hasTerminalBranchBuffer();
}

auto DiscretizeBoundaryValue(double value, double max_value, unsigned steps) -> std::optional<unsigned>
{
  if (value <= 0.0 || max_value <= 0.0 || steps == 0U) {
    return std::nullopt;
  }
  if (value > max_value) {
    return steps + 1U;
  }

  const double step_size = max_value / static_cast<double>(steps);
  if (step_size <= 0.0) {
    return std::nullopt;
  }

  const auto boundary_idx = static_cast<unsigned>(std::ceil(value / step_size));
  return std::clamp(boundary_idx, 1U, steps);
}

auto ResolveBuildOptions(const HTreeBuilder::BuildOptions& options, const CharBuilder& char_builder) -> ResolvedBuildOptions
{
  ResolvedBuildOptions resolved;
  resolved.force_leaf_unbuffered = options.force_leaf_unbuffered.value_or(false);
  resolved.force_branch_buffer = options.force_branch_buffer.value_or(CONFIG_INST.is_force_branch_buffer());
  if (resolved.force_leaf_unbuffered && !options.force_branch_buffer.has_value()) {
    resolved.force_branch_buffer = false;
  }
  LOG_FATAL_IF(resolved.force_branch_buffer && resolved.force_leaf_unbuffered)
      << "HTreeBuilder: force_branch_buffer and force_leaf_unbuffered cannot be enabled at the same time.";

  if (options.min_top_input_slew_ns.has_value() && *options.min_top_input_slew_ns > 0.0) {
    resolved.min_top_input_slew_ns = options.min_top_input_slew_ns;
    resolved.top_input_slew_floor_idx
        = DiscretizeBoundaryValue(*options.min_top_input_slew_ns, char_builder.get_max_slew(), char_builder.get_slew_steps());
  }

  if (options.min_leaf_driven_cap_pf.has_value() && *options.min_leaf_driven_cap_pf > 0.0) {
    resolved.min_leaf_driven_cap_pf = options.min_leaf_driven_cap_pf;
    resolved.leaf_driven_cap_floor_idx
        = DiscretizeBoundaryValue(*options.min_leaf_driven_cap_pf, char_builder.get_max_cap(), char_builder.get_cap_steps());
  }

  return resolved;
}

auto HasBoundaryConstraints(const ResolvedBuildOptions& options) -> bool
{
  return options.top_input_slew_floor_idx.has_value() || options.leaf_driven_cap_floor_idx.has_value();
}

auto BuildSegmentLengthEntrySets(const std::vector<SegmentChar>& chars, const BufferPatternRegistry& pattern_registry)
    -> std::unordered_map<unsigned, SegmentFrontierSet>
{
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_all_by_length;
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_leaf_unbuffered_by_length;
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_branch_by_length;
  raw_all_by_length.reserve(chars.size());
  raw_leaf_unbuffered_by_length.reserve(chars.size());
  raw_branch_by_length.reserve(chars.size());
  for (const auto& entry : chars) {
    raw_all_by_length[entry.get_length_idx()].push_back(entry);
    if (hasTerminalBranchBufferPattern(pattern_registry, entry.get_pattern_id())) {
      raw_branch_by_length[entry.get_length_idx()].push_back(entry);
    } else {
      raw_leaf_unbuffered_by_length[entry.get_length_idx()].push_back(entry);
    }
  }

  std::unordered_map<unsigned, SegmentFrontierSet> entry_sets_by_length;
  entry_sets_by_length.reserve(raw_all_by_length.size());
  for (auto& [length_idx, raw_entries] : raw_all_by_length) {
    auto& entry_set = entry_sets_by_length[length_idx];
    entry_set.all_raw_entries = std::move(raw_entries);
    entry_set.all_frontier_entries = BuildSegmentStateFrontier(entry_set.all_raw_entries, pattern_registry);
  }
  for (auto& [length_idx, raw_entries] : raw_branch_by_length) {
    auto& entry_set = entry_sets_by_length[length_idx];
    entry_set.branch_buffered_entries = BuildSegmentStateFrontier(raw_entries, pattern_registry);
  }
  for (auto& [length_idx, raw_entries] : raw_leaf_unbuffered_by_length) {
    auto& entry_set = entry_sets_by_length[length_idx];
    entry_set.leaf_unbuffered_entries = BuildSegmentStateFrontier(raw_entries, pattern_registry);
  }
  return entry_sets_by_length;
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
        .is_leaf_level = (level + 1U == levels.size()),
        .selected_has_any_buffer = false,
        .selected_has_terminal_branch_buffer = false,
        .selected_leaf_buffer_cell_master = {},
        .selected_terminal_cell_master = {},
        .segment_pattern_id = PatternId::segment(0),
    });
  }

  return plans;
}

auto MakeCandidateLevelPlans(const std::vector<HTreeBuilder::LevelPlan>& full_level_plans, unsigned depth)
    -> std::vector<HTreeBuilder::LevelPlan>
{
  std::vector<HTreeBuilder::LevelPlan> candidate_levels;
  if (depth == 0U || full_level_plans.empty()) {
    return candidate_levels;
  }

  const std::size_t level_count = std::min<std::size_t>(depth, full_level_plans.size());
  candidate_levels.reserve(level_count);
  for (std::size_t level_index = 0; level_index < level_count; ++level_index) {
    auto level = full_level_plans.at(level_index);
    level.is_leaf_level = (level_index + 1U == level_count);
    level.selected_has_any_buffer = false;
    level.selected_has_terminal_branch_buffer = false;
    level.selected_leaf_buffer_cell_master.clear();
    level.selected_terminal_cell_master.clear();
    level.segment_pattern_id = PatternId::segment(0);
    candidate_levels.push_back(std::move(level));
  }

  return candidate_levels;
}

auto CountCandidateLeafNodes(const Tree& topology, unsigned depth) -> std::size_t
{
  const auto topology_levels = topology.levels();
  if (depth == 0U || depth >= topology_levels.size()) {
    return 0U;
  }
  return topology_levels.at(depth).size();
}

auto ResolveDepthCandidates(unsigned max_depth, const HTreeBuilder::BuildOptions& options) -> std::vector<unsigned>
{
  if (max_depth == 0U) {
    return {};
  }

  if (options.target_depth.has_value()) {
    const unsigned resolved_depth = std::clamp(*options.target_depth, 1U, max_depth);
    return {resolved_depth};
  }

  const unsigned requested_window = options.depth_explore_window.value_or(CONFIG_INST.get_htree_depth_explore_window());
  const unsigned resolved_window = std::max(1U, std::min(requested_window, max_depth));

  std::vector<unsigned> candidates;
  candidates.reserve(resolved_window);
  for (unsigned offset = 0U; offset < resolved_window; ++offset) {
    candidates.push_back(max_depth - offset);
  }
  return candidates;
}

auto ExceedsLeafFanoutCapacity(std::size_t load_count, std::size_t leaf_count, std::size_t max_fanout) -> bool
{
  if (max_fanout == 0U) {
    return false;
  }
  if (leaf_count == 0U) {
    return load_count > 0U;
  }
  if (leaf_count > std::numeric_limits<std::size_t>::max() / max_fanout) {
    return false;
  }
  return load_count > leaf_count * max_fanout;
}

auto CalcMedianCapPf(std::vector<double> caps_pf) -> double
{
  if (caps_pf.empty()) {
    return 0.0;
  }

  std::ranges::sort(caps_pf);
  const std::size_t middle = caps_pf.size() / 2U;
  if ((caps_pf.size() & 1U) == 1U) {
    return caps_pf.at(middle);
  }
  return (caps_pf.at(middle - 1U) + caps_pf.at(middle)) * 0.5;
}

auto BuildCapDistributionStats(const std::vector<double>& caps_pf) -> CapDistributionStats
{
  CapDistributionStats stats;
  if (caps_pf.empty()) {
    return stats;
  }

  stats.group_count = caps_pf.size();
  stats.cap_min_pf = *std::ranges::min_element(caps_pf);
  stats.cap_max_pf = *std::ranges::max_element(caps_pf);

  double total_cap_pf = 0.0;
  for (const double cap_pf : caps_pf) {
    total_cap_pf += cap_pf;
  }
  stats.cap_mean_pf = total_cap_pf / static_cast<double>(caps_pf.size());
  stats.cap_median_pf = CalcMedianCapPf(caps_pf);
  return stats;
}

auto BuildLeafElectricalConfig() -> LinearClusteringConfig
{
  const double max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  auto config = TopologyGen::buildLinearClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), max_cap);
  config.enable_exact_cap = true;
  config.always_build_exact_cap = true;
  config.scoring_strategy = LinearScoringStrategy::kTotalWirelength;
  return config;
}

auto BuildLeafFeasibilityReason(std::size_t node_id, const Point<int>& anchor, const std::string& detail) -> std::string
{
  std::ostringstream stream;
  stream << "leaf_node_" << node_id << " anchor=(" << anchor.get_x() << "," << anchor.get_y() << ") " << detail;
  return stream.str();
}

auto ResolveBottomMostBufferedLevel(const HTreeTopologyPattern& topology_pattern, const BufferPatternRegistry& segment_pattern_registry)
    -> int
{
  const auto& level_segment_pattern_ids = topology_pattern.get_level_segment_pattern_ids();
  for (int level = static_cast<int>(level_segment_pattern_ids.size()) - 1; level >= 0; --level) {
    const auto segment_pattern_id = level_segment_pattern_ids.at(static_cast<std::size_t>(level));
    const auto* segment_pattern = segment_pattern_registry.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: missing segment pattern metadata while resolving actual-load boundary.";
    if (!segment_pattern->get_buffer_positions().empty()) {
      return level;
    }
  }
  return -1;
}

auto ResolveActualLoadLegalitySignature(const HTreeTopologyPattern& topology_pattern, const BufferPatternRegistry& segment_pattern_registry)
    -> ActualLoadLegalitySignature
{
  const int bottom_most_buffered_level = ResolveBottomMostBufferedLevel(topology_pattern, segment_pattern_registry);
  ActualLoadLegalitySignature signature;
  signature.bottom_most_buffered_level = bottom_most_buffered_level;
  if (bottom_most_buffered_level >= 0) {
    signature.segment_pattern_id
        = topology_pattern.get_level_segment_pattern_ids().at(static_cast<std::size_t>(bottom_most_buffered_level));
  }
  return signature;
}

auto BuildActualLoadFeasibilityReason(std::size_t node_id, const Point<int>& anchor, const std::string& detail) -> std::string
{
  std::ostringstream stream;
  stream << "htree_load_group_node_" << node_id << " anchor=(" << anchor.get_x() << "," << anchor.get_y() << ") " << detail;
  return stream.str();
}

auto CollectActualLoadBoundaryGroups(const Tree& topology, const ActualLoadLegalitySignature& signature,
                                     const BufferPatternRegistry& segment_pattern_registry)
    -> std::pair<std::vector<ActualLoadBoundaryGroup>, std::string>
{
  std::vector<ActualLoadBoundaryGroup> groups;

  const auto* root_node = topology.get_node(topology.get_root());
  if (root_node == nullptr) {
    return {std::move(groups), "missing_topology_root"};
  }

  if (signature.bottom_most_buffered_level < 0) {
    if (root_node->get_loads().empty()) {
      return {std::move(groups), "empty_root_load_group"};
    }
    groups.push_back(ActualLoadBoundaryGroup{
        .node_id = root_node->get_id(),
        .anchor = root_node->get_position(),
        .loads = &root_node->get_loads(),
    });
    return {std::move(groups), {}};
  }

  const auto topology_levels = topology.levels();
  const std::size_t boundary_level = static_cast<std::size_t>(signature.bottom_most_buffered_level) + 1U;
  if (boundary_level >= topology_levels.size()) {
    return {std::move(groups), "missing_actual_load_boundary_level"};
  }

  const auto* segment_pattern = segment_pattern_registry.find(signature.segment_pattern_id);
  if (segment_pattern == nullptr) {
    return {std::move(groups), "missing_boundary_segment_pattern"};
  }
  if (segment_pattern->get_buffer_positions().empty()) {
    return {std::move(groups), "missing_boundary_buffer_position"};
  }

  const double last_buffer_position = segment_pattern->get_buffer_positions().back();
  groups.reserve(topology_levels.at(boundary_level).size());
  for (const auto node_id : topology_levels.at(boundary_level)) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr) {
      return {std::move(groups), "missing_boundary_topology_node"};
    }
    if (node->get_loads().empty()) {
      continue;
    }

    const auto* parent_node = topology.get_node(node->get_parent());
    if (parent_node == nullptr) {
      return {std::move(groups), "missing_boundary_parent_node"};
    }

    groups.push_back(ActualLoadBoundaryGroup{
        .node_id = node_id,
        .anchor = InterpolateManhattanPoint(parent_node->get_position(), node->get_position(), last_buffer_position),
        .loads = &node->get_loads(),
    });
  }

  if (groups.empty()) {
    return {std::move(groups), "empty_actual_load_groups"};
  }
  return {std::move(groups), {}};
}

auto EvaluateActualLoadLegality(const Tree& topology, const ActualLoadLegalitySignature& signature,
                                const BufferPatternRegistry& segment_pattern_registry) -> ActualLoadLegalityResult
{
  ActualLoadLegalityResult result;
  result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
  result.segment_pattern_id = signature.segment_pattern_id;

  const auto [groups, collection_error] = CollectActualLoadBoundaryGroups(topology, signature, segment_pattern_registry);
  if (!collection_error.empty()) {
    result.failure_reason = collection_error;
    if (collection_error == "missing_topology_root") {
      result.violation = ActualLoadViolation::kMissingTopologyRoot;
    } else if (collection_error == "missing_actual_load_boundary_level") {
      result.violation = ActualLoadViolation::kMissingTopologyLevel;
    } else if (collection_error == "missing_boundary_segment_pattern") {
      result.violation = ActualLoadViolation::kMissingSegmentPattern;
    } else if (collection_error == "missing_boundary_buffer_position") {
      result.violation = ActualLoadViolation::kMissingBufferPosition;
    } else if (collection_error == "empty_root_load_group" || collection_error == "empty_actual_load_groups") {
      result.violation = ActualLoadViolation::kEmptyLoadGroup;
    } else {
      result.violation = ActualLoadViolation::kMissingTopologyNode;
    }
    return result;
  }

  const auto electrical_config = BuildLeafElectricalConfig();
  const std::size_t max_fanout = CONFIG_INST.get_max_fanout();
  for (const auto& group : groups) {
    const auto* loads = group.loads;
    if (loads == nullptr || loads->empty()) {
      result.violation = ActualLoadViolation::kEmptyLoadGroup;
      result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "empty_group_loads");
      return result;
    }
    if (max_fanout > 0U && loads->size() > max_fanout) {
      std::ostringstream detail;
      detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
      result.violation = ActualLoadViolation::kFanout;
      result.monotone_hard_fail = true;
      result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      return result;
    }

    const auto lower_bound = Clustering::evaluateClusterElectrical(*loads, group.anchor, electrical_config, false);
    if (!lower_bound.legal) {
      if (lower_bound.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
        result.violation = ActualLoadViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else if (lower_bound.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "pin_cap_lower_bound_violation total_cap_pf=" << lower_bound.summary.total_cap_pf;
        if (CONFIG_INST.has_max_cap()) {
          detail << ", max_cap_pf=" << CONFIG_INST.get_max_cap();
        }
        result.violation = ActualLoadViolation::kPinCapLowerBound;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else {
        result.violation = ActualLoadViolation::kEmptyLoadGroup;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "lower_bound_evaluation_failed");
      }
      return result;
    }
  }

  std::vector<double> total_caps_pf;
  total_caps_pf.reserve(groups.size());
  for (const auto& group : groups) {
    const auto* loads = group.loads;
    LOG_FATAL_IF(loads == nullptr || loads->empty()) << "HTreeBuilder: actual-load boundary group lost its load set.";

    const auto exact = Clustering::evaluateClusterElectrical(*loads, group.anchor, electrical_config, true);
    if (!exact.legal) {
      if (exact.violation == ClusterElectricalViolation::kRoutingFailed) {
        result.violation = ActualLoadViolation::kRoutingFailed;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "routing_failed");
      } else if (exact.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "cap_violation total_cap_pf=" << exact.summary.total_cap_pf;
        if (CONFIG_INST.has_max_cap()) {
          detail << ", max_cap_pf=" << CONFIG_INST.get_max_cap();
        }
        result.violation = ActualLoadViolation::kCapacitance;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else if (exact.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << loads->size() << ", max_fanout=" << max_fanout;
        result.violation = ActualLoadViolation::kFanout;
        result.monotone_hard_fail = true;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, detail.str());
      } else {
        result.violation = ActualLoadViolation::kEmptyLoadGroup;
        result.failure_reason = BuildActualLoadFeasibilityReason(group.node_id, group.anchor, "exact_evaluation_failed");
      }
      return result;
    }

    total_caps_pf.push_back(exact.summary.total_cap_pf);
  }

  result.cap_distribution = BuildCapDistributionStats(total_caps_pf);
  result.violation = ActualLoadViolation::kNone;
  result.legal = true;
  return result;
}

auto ResolveActualLoadLegality(const Tree& topology, PatternId topology_pattern_id, const TopologyPatternRegistry& topology_registry,
                               const BufferPatternRegistry& segment_pattern_registry, ActualLoadLegalityContext& legality_context)
    -> ActualLoadLegalityResult
{
  const auto topology_pattern = topology_registry.materialize(topology_pattern_id);
  const auto signature = ResolveActualLoadLegalitySignature(topology_pattern, segment_pattern_registry);
  if (signature.bottom_most_buffered_level <= legality_context.max_monotone_failed_level) {
    ActualLoadLegalityResult result;
    result.bottom_most_buffered_level = signature.bottom_most_buffered_level;
    result.segment_pattern_id = signature.segment_pattern_id;
    result.violation = ActualLoadViolation::kFanout;
    std::ostringstream detail;
    detail << "monotone_pruned_by_bottom_most_buffered_level threshold=" << legality_context.max_monotone_failed_level
           << ", candidate_level=" << signature.bottom_most_buffered_level;
    result.failure_reason = detail.str();
    return result;
  }

  const auto cache_it = legality_context.result_by_signature.find(signature);
  if (cache_it != legality_context.result_by_signature.end()) {
    return cache_it->second;
  }

  auto evaluated = EvaluateActualLoadLegality(topology, signature, segment_pattern_registry);
  if (!evaluated.legal && evaluated.monotone_hard_fail) {
    legality_context.max_monotone_failed_level = std::max(legality_context.max_monotone_failed_level, signature.bottom_most_buffered_level);
  }
  return legality_context.result_by_signature.emplace(signature, std::move(evaluated)).first->second;
}

auto FilterActualLoadLegalRepresentatives(const std::vector<HTreeTopologyChar>& representatives, const Tree& topology,
                                          const TopologyPatternRegistry& topology_registry,
                                          const BufferPatternRegistry& segment_pattern_registry,
                                          ActualLoadLegalityContext& legality_context) -> ActualLoadRepresentativeFilterResult
{
  ActualLoadRepresentativeFilterResult result;
  result.entries.reserve(representatives.size());
  for (const auto& representative : representatives) {
    const auto legality = ResolveActualLoadLegality(topology, representative.get_pattern_id(), topology_registry, segment_pattern_registry,
                                                    legality_context);
    if (!legality.legal) {
      if (result.first_failure_reason.empty()) {
        result.first_failure_reason = legality.failure_reason;
      }
      continue;
    }
    result.entries.push_back(representative);
  }
  return result;
}

auto EstimateLeafDrivenCapResolution(const Tree& topology, unsigned depth, const HTreeBuilder::BuildOptions& build_options)
    -> LeafDrivenCapResolution
{
  LeafDrivenCapResolution resolution;
  if (build_options.min_leaf_driven_cap_pf.has_value() && *build_options.min_leaf_driven_cap_pf > 0.0) {
    resolution.cap_pf = build_options.min_leaf_driven_cap_pf;
    resolution.used_explicit_value = true;
    resolution.source = "explicit_build_option";
  }

  const auto* root = topology.get_node(topology.get_root());
  if (root == nullptr) {
    resolution.depth_infeasible = true;
    resolution.failure_reason = "missing_topology_root";
    return resolution;
  }

  const auto topology_levels = topology.levels();
  if (depth == 0U || depth >= topology_levels.size()) {
    resolution.depth_infeasible = true;
    resolution.failure_reason = "invalid_candidate_depth";
    return resolution;
  }

  const auto& leaf_node_ids = topology_levels.at(depth);
  const std::size_t leaf_count = leaf_node_ids.size();
  const std::size_t load_count = root->get_loads().size();
  const std::size_t max_fanout = CONFIG_INST.get_max_fanout();
  if (ExceedsLeafFanoutCapacity(load_count, leaf_count, max_fanout)) {
    resolution.depth_infeasible = true;
    std::ostringstream reason;
    reason << "max_fanout_precheck_failed load_count=" << load_count << ", leaf_count=" << leaf_count << ", max_fanout=" << max_fanout;
    resolution.failure_reason = reason.str();
    return resolution;
  }

  const auto electrical_config = BuildLeafElectricalConfig();
  std::vector<double> leaf_caps_pf;
  leaf_caps_pf.reserve(leaf_node_ids.size());
  for (const auto node_id : leaf_node_ids) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr) {
      resolution.depth_infeasible = true;
      resolution.failure_reason = "missing_leaf_node";
      return resolution;
    }

    const auto& leaf_loads = node->get_loads();
    if (leaf_loads.empty()) {
      continue;
    }
    if (max_fanout > 0U && leaf_loads.size() > max_fanout) {
      resolution.depth_infeasible = true;
      std::ostringstream detail;
      detail << "fanout_violation load_count=" << leaf_loads.size() << ", max_fanout=" << max_fanout;
      resolution.failure_reason = BuildLeafFeasibilityReason(node_id, node->get_position(), detail.str());
      return resolution;
    }

    const auto electrical = Clustering::evaluateClusterElectrical(leaf_loads, node->get_position(), electrical_config);
    if (!electrical.legal) {
      resolution.depth_infeasible = true;
      if (electrical.violation == ClusterElectricalViolation::kRoutingFailed) {
        resolution.failure_reason = BuildLeafFeasibilityReason(node_id, node->get_position(), "routing_failed");
      } else if (electrical.violation == ClusterElectricalViolation::kCapacitance) {
        std::ostringstream detail;
        detail << "cap_violation total_cap_pf=" << electrical.summary.total_cap_pf;
        if (CONFIG_INST.has_max_cap()) {
          detail << ", max_cap_pf=" << CONFIG_INST.get_max_cap();
        }
        resolution.failure_reason = BuildLeafFeasibilityReason(node_id, node->get_position(), detail.str());
      } else if (electrical.violation == ClusterElectricalViolation::kFanout) {
        std::ostringstream detail;
        detail << "fanout_violation load_count=" << leaf_loads.size() << ", max_fanout=" << max_fanout;
        resolution.failure_reason = BuildLeafFeasibilityReason(node_id, node->get_position(), detail.str());
      } else {
        resolution.failure_reason = BuildLeafFeasibilityReason(node_id, node->get_position(), "electrical_evaluation_failed");
      }
      return resolution;
    }

    leaf_caps_pf.push_back(electrical.summary.total_cap_pf);
  }

  if (load_count > 0U && leaf_caps_pf.empty()) {
    resolution.depth_infeasible = true;
    resolution.failure_reason = "empty_leaf_cap_distribution";
    return resolution;
  }

  if (!leaf_caps_pf.empty()) {
    const auto stats = BuildCapDistributionStats(leaf_caps_pf);
    resolution.evaluated_leaf_count = stats.group_count;
    resolution.leaf_cap_min_pf = stats.cap_min_pf;
    resolution.leaf_cap_max_pf = stats.cap_max_pf;
    resolution.leaf_cap_mean_pf = stats.cap_mean_pf;
    resolution.leaf_cap_median_pf = stats.cap_median_pf;
  }

  if (!resolution.used_explicit_value && resolution.evaluated_leaf_count > 0U) {
    resolution.cap_pf = resolution.leaf_cap_max_pf;
    resolution.source = "leaf_total_cap_max";
  }
  return resolution;
}

auto ApplyLeafDrivenCapResolution(const ResolvedBuildOptions& base_options, const LeafDrivenCapResolution& cap_resolution,
                                  const CharBuilder& char_builder) -> ResolvedBuildOptions
{
  ResolvedBuildOptions resolved = base_options;
  resolved.min_leaf_driven_cap_pf = std::nullopt;
  resolved.leaf_driven_cap_floor_idx = std::nullopt;
  if (cap_resolution.cap_pf.has_value() && *cap_resolution.cap_pf > 0.0) {
    resolved.min_leaf_driven_cap_pf = cap_resolution.cap_pf;
    resolved.leaf_driven_cap_floor_idx
        = DiscretizeBoundaryValue(*cap_resolution.cap_pf, char_builder.get_max_cap(), char_builder.get_cap_steps());
  }
  return resolved;
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
      if (!combiner.canCompose(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id())) {
        continue;
      }
      const auto merged_pattern_id = combiner.combine(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id());
      composed_entries.push_back(SegmentChar::compose(upstream_entry, downstream_entry, merged_pattern_id));
    }
  }

  return {std::move(composed_entries), combiner.get_next_id()};
}

struct ExactSegmentEntriesWithFrontier
{
  std::vector<SegmentChar> raw_entries;
  std::vector<SegmentChar> frontier_entries;
  unsigned next_pattern_id = 0U;
};

auto ComposeExactSegmentEntriesWithFrontier(const std::vector<SegmentChar>& upstream, const std::vector<SegmentChar>& downstream,
                                            BufferPatternRegistry& pattern_registry, unsigned start_pattern_id)
    -> ExactSegmentEntriesWithFrontier
{
  ExactSegmentEntriesWithFrontier result;
  SegmentPatternRegistryCombiner combiner(pattern_registry, start_pattern_id);
  auto pruner = MakeSegmentStateFrontierPruner(
      [&](const SegmentChar& entry) -> PatternCompositionState { return pattern_registry.getCompositionState(entry.get_pattern_id()); });
  detail::HashJoinConcatRawAndFrontier<SegmentChar, SegmentTraits>(upstream, downstream, combiner, result.raw_entries,
                                                                   result.frontier_entries, pruner);
  SortSegmentFrontierEntries(result.frontier_entries);
  result.next_pattern_id = combiner.get_next_id();
  return result;
}

auto ResolveSegmentEntrySelection(bool is_leaf_level, const ResolvedBuildOptions& resolved_options) -> SegmentEntrySelection
{
  if (resolved_options.force_branch_buffer) {
    return SegmentEntrySelection::kBranchBuffered;
  }
  if (is_leaf_level && resolved_options.force_leaf_unbuffered) {
    return SegmentEntrySelection::kLeafUnbuffered;
  }
  return SegmentEntrySelection::kAllFrontier;
}

auto SelectSegmentEntriesForLevel(const SegmentFrontierSet& entry_set, SegmentEntrySelection selection) -> const std::vector<SegmentChar>&
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

auto SynthesizeSegmentEntrySets(const std::vector<SegmentChar>& base_segment_chars, BufferPatternRegistry& pattern_registry,
                                unsigned max_target_length_idx) -> std::unordered_map<unsigned, SegmentFrontierSet>
{
  auto entry_sets_by_length = BuildSegmentLengthEntrySets(base_segment_chars, pattern_registry);
  std::unordered_map<unsigned, unsigned> min_piece_count_by_length;
  min_piece_count_by_length.reserve(entry_sets_by_length.size());
  for (const auto& [length_idx, entry_set] : entry_sets_by_length) {
    if (!entry_set.all_frontier_entries.empty()) {
      min_piece_count_by_length[length_idx] = 1U;
    }
  }

  unsigned next_pattern_id = FindNextSegmentPatternId(base_segment_chars);
  for (unsigned length_idx = 1U; length_idx <= max_target_length_idx; ++length_idx) {
    unsigned best_piece_count = std::numeric_limits<unsigned>::max();
    std::vector<std::pair<unsigned, unsigned>> best_splits;
    const bool needs_all_frontier
        = !entry_sets_by_length.contains(length_idx) || entry_sets_by_length.at(length_idx).all_frontier_entries.empty();
    const bool needs_branch_frontier
        = !entry_sets_by_length.contains(length_idx) || entry_sets_by_length.at(length_idx).branch_buffered_entries.empty();
    const bool needs_leaf_unbuffered
        = !entry_sets_by_length.contains(length_idx) || entry_sets_by_length.at(length_idx).leaf_unbuffered_entries.empty();

    if (needs_all_frontier || needs_branch_frontier || needs_leaf_unbuffered) {
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
    }

    if (needs_all_frontier && !best_splits.empty()) {
      std::vector<SegmentChar> composed_raw_entries;
      std::vector<SegmentChar> composed_frontier_entries;
      for (const auto& [left_idx, right_idx] : best_splits) {
        auto partial
            = ComposeExactSegmentEntriesWithFrontier(entry_sets_by_length.at(left_idx).all_frontier_entries,
                                                     entry_sets_by_length.at(right_idx).all_raw_entries, pattern_registry, next_pattern_id);
        next_pattern_id = partial.next_pattern_id;
        composed_raw_entries.insert(composed_raw_entries.end(), partial.raw_entries.begin(), partial.raw_entries.end());
        composed_frontier_entries.insert(composed_frontier_entries.end(), partial.frontier_entries.begin(), partial.frontier_entries.end());
      }

      if (!composed_raw_entries.empty()) {
        auto& entry_set = entry_sets_by_length[length_idx];
        entry_set.all_raw_entries = std::move(composed_raw_entries);
        entry_set.all_frontier_entries = BuildSegmentStateFrontier(composed_frontier_entries, pattern_registry);
        min_piece_count_by_length[length_idx] = best_piece_count;
      } else {
        std::vector<SegmentChar> relaxed_entries;
        for (const auto& [left_idx, right_idx] : best_splits) {
          const auto left_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(left_idx).all_frontier_entries, pattern_registry);
          const auto right_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(right_idx).all_raw_entries, pattern_registry);
          auto [partial, updated_next_pattern_id]
              = ComposeRelaxedSegmentEntries(left_candidates, right_candidates, pattern_registry, next_pattern_id);
          next_pattern_id = updated_next_pattern_id;
          relaxed_entries.insert(relaxed_entries.end(), partial.begin(), partial.end());
        }

        if (!relaxed_entries.empty()) {
          auto& entry_set = entry_sets_by_length[length_idx];
          entry_set.all_raw_entries = std::move(relaxed_entries);
          entry_set.all_frontier_entries = BuildSegmentStateFrontier(entry_set.all_raw_entries, pattern_registry);
          min_piece_count_by_length[length_idx] = best_piece_count;
        }
      }
    }

    if (needs_branch_frontier && !best_splits.empty()) {
      std::vector<SegmentChar> branch_frontier_entries;
      for (const auto& [left_idx, right_idx] : best_splits) {
        if (entry_sets_by_length.at(right_idx).branch_buffered_entries.empty()) {
          continue;
        }
        auto partial = ComposeExactSegmentEntriesWithFrontier(entry_sets_by_length.at(left_idx).all_frontier_entries,
                                                              entry_sets_by_length.at(right_idx).branch_buffered_entries, pattern_registry,
                                                              next_pattern_id);
        next_pattern_id = partial.next_pattern_id;
        branch_frontier_entries.insert(branch_frontier_entries.end(), partial.frontier_entries.begin(), partial.frontier_entries.end());
      }

      if (!branch_frontier_entries.empty()) {
        entry_sets_by_length[length_idx].branch_buffered_entries = BuildSegmentStateFrontier(branch_frontier_entries, pattern_registry);
      } else {
        std::vector<SegmentChar> relaxed_entries;
        for (const auto& [left_idx, right_idx] : best_splits) {
          if (entry_sets_by_length.at(right_idx).branch_buffered_entries.empty()) {
            continue;
          }
          const auto left_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(left_idx).all_frontier_entries, pattern_registry);
          const auto right_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(right_idx).branch_buffered_entries, pattern_registry);
          auto [partial, updated_next_pattern_id]
              = ComposeRelaxedSegmentEntries(left_candidates, right_candidates, pattern_registry, next_pattern_id);
          next_pattern_id = updated_next_pattern_id;
          relaxed_entries.insert(relaxed_entries.end(), partial.begin(), partial.end());
        }

        if (!relaxed_entries.empty()) {
          entry_sets_by_length[length_idx].branch_buffered_entries = BuildSegmentStateFrontier(relaxed_entries, pattern_registry);
        }
      }
    }

    if (needs_leaf_unbuffered && !best_splits.empty()) {
      std::vector<SegmentChar> leaf_unbuffered_frontier_entries;
      for (const auto& [left_idx, right_idx] : best_splits) {
        if (entry_sets_by_length.at(left_idx).all_raw_entries.empty()
            || entry_sets_by_length.at(right_idx).leaf_unbuffered_entries.empty()) {
          continue;
        }
        auto partial = ComposeExactSegmentEntriesWithFrontier(entry_sets_by_length.at(left_idx).all_raw_entries,
                                                              entry_sets_by_length.at(right_idx).leaf_unbuffered_entries, pattern_registry,
                                                              next_pattern_id);
        next_pattern_id = partial.next_pattern_id;
        leaf_unbuffered_frontier_entries.insert(leaf_unbuffered_frontier_entries.end(), partial.frontier_entries.begin(),
                                                partial.frontier_entries.end());
      }

      if (!leaf_unbuffered_frontier_entries.empty()) {
        entry_sets_by_length[length_idx].leaf_unbuffered_entries
            = BuildSegmentStateFrontier(leaf_unbuffered_frontier_entries, pattern_registry);
      } else {
        std::vector<SegmentChar> relaxed_entries;
        for (const auto& [left_idx, right_idx] : best_splits) {
          if (entry_sets_by_length.at(left_idx).all_raw_entries.empty()
              || entry_sets_by_length.at(right_idx).leaf_unbuffered_entries.empty()) {
            continue;
          }
          const auto left_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(left_idx).all_raw_entries, pattern_registry);
          const auto right_candidates
              = SelectSegmentCompositionCandidates(entry_sets_by_length.at(right_idx).leaf_unbuffered_entries, pattern_registry);
          auto [partial, updated_next_pattern_id]
              = ComposeRelaxedSegmentEntries(left_candidates, right_candidates, pattern_registry, next_pattern_id);
          next_pattern_id = updated_next_pattern_id;
          relaxed_entries.insert(relaxed_entries.end(), partial.begin(), partial.end());
        }

        if (!relaxed_entries.empty()) {
          entry_sets_by_length[length_idx].leaf_unbuffered_entries = BuildSegmentStateFrontier(relaxed_entries, pattern_registry);
        }
      }
    }
  }

  return entry_sets_by_length;
}

auto MakeHTreeSeedEntries(const std::vector<SegmentChar>& segment_frontier, const BufferPatternRegistry& segment_pattern_registry,
                          TopologyPatternRegistry& topology_registry, unsigned& next_pattern_id) -> std::vector<HTreeTopologyChar>
{
  std::vector<HTreeTopologyChar> seed_entries;
  seed_entries.reserve(segment_frontier.size());
  for (const auto& segment_entry : segment_frontier) {
    const auto topology_pattern_id = PatternId::topology(next_pattern_id++);
    topology_registry.addSeed(topology_pattern_id, segment_entry.get_pattern_id(),
                              segment_pattern_registry.getCompositionState(segment_entry.get_pattern_id()));
    const CharCore core(segment_entry.get_input_slew_idx(), segment_entry.get_output_slew_idx(), segment_entry.get_driven_cap_idx(),
                        segment_entry.get_load_cap_idx(), segment_entry.get_delay(), segment_entry.get_power(), topology_pattern_id);
    seed_entries.emplace_back(core, 1U, segment_entry.get_driven_cap_idx());
  }
  return seed_entries;
}

auto ComposeRelaxedHTreeEntries(const std::vector<HTreeTopologyChar>& upstream, const std::vector<HTreeTopologyChar>& downstream,
                                TopologyPatternRegistry& topology_registry, unsigned start_pattern_id)
    -> std::pair<std::vector<HTreeTopologyChar>, unsigned>
{
  TopologyPatternRegistryCombiner combiner(topology_registry, start_pattern_id);
  std::vector<HTreeTopologyChar> composed_entries;
  composed_entries.reserve(upstream.size() * downstream.size());
  for (const auto& upstream_entry : upstream) {
    for (const auto& downstream_entry : downstream) {
      if (downstream_entry.get_input_slew_idx() < upstream_entry.get_output_slew_idx()
          || HTreeTraits::halfCapKey(upstream_entry.get_load_cap_idx()) < downstream_entry.get_driven_cap_idx()) {
        continue;
      }
      if (!combiner.canCompose(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id())) {
        continue;
      }
      const auto merged_pattern_id = combiner.combine(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id());
      composed_entries.push_back(HTreeTopologyChar::compose(upstream_entry, downstream_entry, merged_pattern_id));
    }
  }

  return {std::move(composed_entries), combiner.get_next_id()};
}

struct ExactHTreeEntriesWithFrontier
{
  std::vector<HTreeTopologyChar> raw_entries;
  std::vector<HTreeTopologyChar> frontier_entries;
  unsigned next_pattern_id = 0U;
};

auto ComposeExactHTreeEntriesWithFrontier(const std::vector<HTreeTopologyChar>& upstream, const std::vector<HTreeTopologyChar>& downstream,
                                          TopologyPatternRegistry& topology_registry, unsigned start_pattern_id)
    -> ExactHTreeEntriesWithFrontier
{
  ExactHTreeEntriesWithFrontier result;
  TopologyPatternRegistryCombiner combiner(topology_registry, start_pattern_id);
  auto pruner = MakeHTreeStateFrontierPruner([&](const HTreeTopologyChar& entry) -> PatternCompositionState {
    return topology_registry.getCompositionState(entry.get_pattern_id());
  });
  detail::HashJoinConcatRawAndFrontier<HTreeTopologyChar, HTreeTraits>(upstream, downstream, combiner, result.raw_entries,
                                                                       result.frontier_entries, pruner);
  SortHTreeFrontierEntries(result.frontier_entries);
  result.next_pattern_id = combiner.get_next_id();
  return result;
}

auto BuildHTreeComposition(const std::vector<HTreeBuilder::LevelPlan>& levels,
                           const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                           const BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& resolved_options)
    -> HTreeCompositionResult
{
  HTreeCompositionResult result;
  unsigned next_topology_pattern_id = 0U;
  std::vector<HTreeTopologyChar> current_raw_entries;
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
    const SegmentEntrySelection entry_selection = ResolveSegmentEntrySelection(level.is_leaf_level, resolved_options);
    const auto& base_segment_frontier = SelectSegmentEntriesForLevel(entry_set, entry_selection);
    if (base_segment_frontier.empty()) {
      result.failure_reason = DescribeMissingSegmentEntries(entry_selection);
      return result;
    }

    auto seed_entries
        = MakeHTreeSeedEntries(base_segment_frontier, segment_pattern_registry, result.topology_pattern_registry, next_topology_pattern_id);
    if (current_frontier_entries.empty()) {
      current_raw_entries = std::move(seed_entries);
      current_frontier_entries = BuildHTreeStateFrontier(current_raw_entries, result.topology_pattern_registry, segment_pattern_registry);
      continue;
    }

    auto exact_entries = ComposeExactHTreeEntriesWithFrontier(seed_entries, current_raw_entries, result.topology_pattern_registry,
                                                              next_topology_pattern_id);
    next_topology_pattern_id = exact_entries.next_pattern_id;
    if (exact_entries.raw_entries.empty()) {
      const auto upstream_candidates
          = SelectHTreeCompositionCandidates(seed_entries, result.topology_pattern_registry, segment_pattern_registry);
      const auto downstream_candidates
          = SelectHTreeCompositionCandidates(current_raw_entries, result.topology_pattern_registry, segment_pattern_registry);
      auto [relaxed_entries, updated_next_pattern_id] = ComposeRelaxedHTreeEntries(
          upstream_candidates, downstream_candidates, result.topology_pattern_registry, next_topology_pattern_id);
      next_topology_pattern_id = updated_next_pattern_id;
      if (relaxed_entries.empty()) {
        result.failure_reason = "empty_frontier";
        return result;
      }
      current_raw_entries = std::move(relaxed_entries);
      current_frontier_entries = BuildHTreeStateFrontier(current_raw_entries, result.topology_pattern_registry, segment_pattern_registry);
    } else {
      current_raw_entries = std::move(exact_entries.raw_entries);
      current_frontier_entries = std::move(exact_entries.frontier_entries);
    }
    if (current_frontier_entries.empty()) {
      result.failure_reason = "empty_frontier";
      return result;
    }
  }

  result.success = !current_frontier_entries.empty();
  result.frontier = std::move(current_frontier_entries);
  return result;
}

auto FilterBoundaryFeasibleHTreeChars(const std::vector<HTreeTopologyChar>& entries, const ResolvedBuildOptions& resolved_options)
    -> std::vector<HTreeTopologyChar>
{
  if (!HasBoundaryConstraints(resolved_options)) {
    return entries;
  }

  std::vector<HTreeTopologyChar> filtered_entries;
  filtered_entries.reserve(entries.size());
  for (const auto& entry : entries) {
    if (resolved_options.top_input_slew_floor_idx.has_value() && entry.get_input_slew_idx() < *resolved_options.top_input_slew_floor_idx) {
      continue;
    }
    if (resolved_options.leaf_driven_cap_floor_idx.has_value()
        && entry.get_leaf_driven_cap_idx() < *resolved_options.leaf_driven_cap_floor_idx) {
      continue;
    }
    filtered_entries.push_back(entry);
  }
  return filtered_entries;
}

auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const ResolvedBuildOptions& resolved_options, unsigned slew_steps,
                               unsigned cap_steps) -> double;

auto PreferWorstCaseRepresentative(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  if (lhs.get_power() != rhs.get_power()) {
    return lhs.get_power() > rhs.get_power();
  }
  if (lhs.get_delay() != rhs.get_delay()) {
    return lhs.get_delay() > rhs.get_delay();
  }
  if (lhs.get_driven_cap_idx() != rhs.get_driven_cap_idx()) {
    return lhs.get_driven_cap_idx() > rhs.get_driven_cap_idx();
  }
  if (lhs.get_output_slew_idx() != rhs.get_output_slew_idx()) {
    return lhs.get_output_slew_idx() > rhs.get_output_slew_idx();
  }
  if (lhs.get_load_cap_idx() != rhs.get_load_cap_idx()) {
    return lhs.get_load_cap_idx() > rhs.get_load_cap_idx();
  }
  if (lhs.get_input_slew_idx() != rhs.get_input_slew_idx()) {
    return lhs.get_input_slew_idx() < rhs.get_input_slew_idx();
  }
  return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
}

auto PreferPatternRepresentative(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs, PatternRepresentativeMode mode) -> bool
{
  if (mode == PatternRepresentativeMode::kWorstCaseSelection) {
    return PreferWorstCaseRepresentative(lhs, rhs);
  }

  return false;
}

auto BuildPatternRepresentativePool(const std::vector<HTreeTopologyChar>& entries, PatternRepresentativeMode mode,
                                    const TopologyPatternRegistry& topology_registry,
                                    TopologyPatternStructuralKeyCache& structural_key_cache) -> std::vector<HTreeTopologyChar>
{
  std::unordered_map<StructuralPatternKey, HTreeTopologyChar, StructuralPatternKeyHash> representative_by_pattern;
  representative_by_pattern.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto& structural_key = structural_key_cache.get(entry.get_pattern_id(), topology_registry);
    auto [it, inserted] = representative_by_pattern.emplace(structural_key, entry);
    if (!inserted && PreferPatternRepresentative(entry, it->second, mode)) {
      it->second = entry;
    }
  }

  std::vector<HTreeTopologyChar> representatives;
  representatives.reserve(representative_by_pattern.size());
  for (const auto& [structural_key, representative] : representative_by_pattern) {
    (void) structural_key;
    representatives.push_back(representative);
  }

  std::ranges::sort(representatives, [](const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool {
    return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
  });
  return representatives;
}

auto CalcBoundaryFallbackScore(const HTreeTopologyChar& entry, const ResolvedBuildOptions& resolved_options, unsigned slew_steps,
                               unsigned cap_steps) -> double
{
  double score = 0.0;
  if (resolved_options.top_input_slew_floor_idx.has_value() && slew_steps > 0U) {
    score += static_cast<double>(entry.get_input_slew_idx()) / static_cast<double>(slew_steps);
  }
  if (resolved_options.leaf_driven_cap_floor_idx.has_value() && cap_steps > 0U) {
    score += static_cast<double>(entry.get_leaf_driven_cap_idx()) / static_cast<double>(cap_steps);
  }
  return score;
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
    LOG_FATAL_IF(lhs == nullptr || rhs == nullptr) << "HTreeBuilder: null pareto entry encountered during final selection.";
    return PreferPowerMedianOrder(*lhs, *rhs);
  });

  const std::size_t median_index = (pareto_front.size() - 1U) / 2U;
  return *pareto_front.at(median_index);
}

auto BuildDelayPowerParetoFront(const std::vector<CandidateRepresentativeRef>& entries) -> std::vector<CandidateRepresentativeRef>
{
  std::vector<CandidateRepresentativeRef> pareto_front;
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

  std::ranges::sort(pareto_front, [](const CandidateRepresentativeRef& lhs, const CandidateRepresentativeRef& rhs) -> bool {
    LOG_FATAL_IF(lhs.entry == nullptr || rhs.entry == nullptr) << "HTreeBuilder: null global candidate representative encountered.";
    return PreferPowerMedianOrder(*lhs.entry, *rhs.entry);
  });
  return pareto_front;
}

auto SelectBestGlobalRepresentative(const std::vector<CandidateRepresentativeRef>& entries) -> std::optional<CandidateRepresentativeRef>
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

auto EvaluateCandidateBuild(const std::vector<HTreeBuilder::LevelPlan>& levels,
                            const std::unordered_map<unsigned, SegmentFrontierSet>& entry_sets_by_length,
                            const BufferPatternRegistry& segment_pattern_registry, const ResolvedBuildOptions& resolved_options,
                            const Tree& topology, ActualLoadLegalityContext& actual_load_legality_context, std::size_t leaf_count,
                            unsigned depth, double char_max_cap_pf, unsigned char_slew_steps, unsigned char_cap_steps)
    -> CandidateBuildEvaluation
{
  CandidateBuildEvaluation result;
  result.depth = depth;
  result.leaf_count = leaf_count;
  result.resolved_options = resolved_options;
  result.levels = levels;

  const bool has_boundary_constraints = HasBoundaryConstraints(resolved_options);
  const auto composition = BuildHTreeComposition(levels, entry_sets_by_length, segment_pattern_registry, resolved_options);
  if (!composition.success) {
    result.failure_reason = composition.failure_reason.empty() ? std::string{"empty_frontier"} : composition.failure_reason;
    result.failure_level = composition.failure_level;
    result.failure_length_idx = composition.failure_length_idx;
    return result;
  }

  result.topology_pattern_registry = composition.topology_pattern_registry;
  TopologyPatternStructuralKeyCache structural_key_cache;
  if (has_boundary_constraints) {
    result.candidate_chars = composition.frontier;
    result.candidate_pattern_representatives = BuildPatternRepresentativePool(
        result.candidate_chars, PatternRepresentativeMode::kWorstCaseSelection, result.topology_pattern_registry, structural_key_cache);
    auto candidate_actual_filter
        = FilterActualLoadLegalRepresentatives(result.candidate_pattern_representatives, topology, result.topology_pattern_registry,
                                               segment_pattern_registry, actual_load_legality_context);
    result.candidate_pattern_representatives = std::move(candidate_actual_filter.entries);
    result.feasible_chars = FilterBoundaryFeasibleHTreeChars(result.candidate_chars, resolved_options);
    result.feasible_pattern_representatives = BuildPatternRepresentativePool(
        result.feasible_chars, PatternRepresentativeMode::kWorstCaseSelection, result.topology_pattern_registry, structural_key_cache);
    auto feasible_actual_filter
        = FilterActualLoadLegalRepresentatives(result.feasible_pattern_representatives, topology, result.topology_pattern_registry,
                                               segment_pattern_registry, actual_load_legality_context);
    result.feasible_pattern_representatives = std::move(feasible_actual_filter.entries);
    if (result.candidate_pattern_representatives.empty() && !candidate_actual_filter.first_failure_reason.empty()) {
      result.failure_reason = candidate_actual_filter.first_failure_reason;
    }
    if (result.feasible_pattern_representatives.empty() && result.failure_reason.empty()
        && !feasible_actual_filter.first_failure_reason.empty()) {
      result.failure_reason = feasible_actual_filter.first_failure_reason;
    }
  } else {
    result.feasible_chars = composition.frontier;
    result.feasible_pattern_representatives = BuildPatternRepresentativePool(
        result.feasible_chars, PatternRepresentativeMode::kWorstCaseSelection, result.topology_pattern_registry, structural_key_cache);
    auto feasible_actual_filter
        = FilterActualLoadLegalRepresentatives(result.feasible_pattern_representatives, topology, result.topology_pattern_registry,
                                               segment_pattern_registry, actual_load_legality_context);
    result.feasible_pattern_representatives = std::move(feasible_actual_filter.entries);
    if (result.feasible_pattern_representatives.empty() && !feasible_actual_filter.first_failure_reason.empty()) {
      result.failure_reason = feasible_actual_filter.first_failure_reason;
    }
  }

  if (!result.feasible_pattern_representatives.empty()) {
    result.best_char = SelectBestHTreeChar(result.feasible_pattern_representatives);
  } else if (has_boundary_constraints) {
    result.best_char = SelectBestHTreeChar(result.candidate_pattern_representatives);
    if (result.best_char.has_value()) {
      result.used_boundary_fallback = true;
      result.boundary_fallback_reason = "no_strict_boundary_feasible_solution";
      result.boundary_fallback_score = CalcBoundaryFallbackScore(*result.best_char, resolved_options, char_slew_steps, char_cap_steps);
    }
  }

  result.success = result.best_char.has_value();
  if (!result.success && result.failure_reason.empty()) {
    result.failure_reason = "no_actual_load_legal_pattern_representatives";
  }
  (void) char_max_cap_pf;
  return result;
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

template <typename T>
auto EraseRawPointer(std::vector<T*>& objects, T* target) -> void
{
  objects.erase(std::remove(objects.begin(), objects.end(), target), objects.end());
}

template <typename T>
auto EraseOwnedPointer(std::vector<std::unique_ptr<T>>& objects, T* target) -> void
{
  objects.erase(
      std::remove_if(objects.begin(), objects.end(), [target](const std::unique_ptr<T>& object) -> bool { return object.get() == target; }),
      objects.end());
}

auto FindSingleBufferInputPin(Inst* inst) -> Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }

  const auto* driver_pin = inst->findDriverPin();
  for (auto* pin : inst->get_pins()) {
    if (pin == nullptr || pin == driver_pin) {
      continue;
    }
    if (pin->get_type() == PinType::kIn || pin->get_type() == PinType::kClock) {
      return pin;
    }
  }

  for (auto* pin : inst->get_pins()) {
    if (pin != nullptr && pin != driver_pin) {
      return pin;
    }
  }
  return nullptr;
}

auto ReplaceNetLoad(Net* net, Pin* old_load, Pin* new_load) -> bool
{
  if (net == nullptr || old_load == nullptr || new_load == nullptr) {
    return false;
  }

  auto updated_loads = net->get_loads();
  const auto load_it = std::ranges::find(updated_loads, old_load);
  if (load_it == updated_loads.end()) {
    return false;
  }

  *load_it = new_load;
  net->set_loads(updated_loads);
  return true;
}

auto PruneLeafSingleLoadBuffers(HTreeBuilder::BuildResult& result) -> std::size_t
{
  const std::unordered_set<Inst*> inserted_inst_set(result.inserted_insts.begin(), result.inserted_insts.end());
  const std::vector<Inst*> candidate_insts = result.inserted_insts;
  std::size_t pruned_count = 0U;

  for (auto* inst : candidate_insts) {
    if (inst == nullptr || !inst->is_buffer()) {
      continue;
    }

    auto* output_pin = inst->findDriverPin();
    if (output_pin == nullptr) {
      continue;
    }
    auto* output_net = output_pin->get_net();
    if (output_net == nullptr || output_net->get_loads().size() != 1U || output_net->get_loads().front() == nullptr) {
      continue;
    }

    auto* downstream_load = output_net->get_loads().front();
    auto* downstream_inst = downstream_load->get_inst();
    if (downstream_inst != nullptr && inserted_inst_set.contains(downstream_inst)) {
      continue;
    }

    auto* input_pin = FindSingleBufferInputPin(inst);
    if (input_pin == nullptr) {
      continue;
    }
    auto* upstream_net = input_pin->get_net();
    if (upstream_net == nullptr || !ReplaceNetLoad(upstream_net, input_pin, downstream_load)) {
      continue;
    }

    downstream_load->set_net(upstream_net);
    input_pin->set_net(nullptr);
    output_pin->set_net(nullptr);
    output_net->set_driver(nullptr);
    output_net->set_loads({});

    const auto inst_pins = inst->get_pins();
    for (auto* pin : inst_pins) {
      if (pin == nullptr) {
        continue;
      }
      pin->set_inst(nullptr);
      pin->set_net(nullptr);
      EraseRawPointer(result.inserted_pins, pin);
      EraseOwnedPointer(result.pin_storage, pin);
    }

    EraseRawPointer(result.inserted_nets, output_net);
    EraseOwnedPointer(result.net_storage, output_net);
    EraseRawPointer(result.inserted_insts, inst);
    EraseOwnedPointer(result.inst_storage, inst);
    ++pruned_count;
  }

  return pruned_count;
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

  std::vector<Pin*> terminal_loads = child_entry_loads;
  if (terminal_loads.empty()) {
    terminal_loads = child_node.get_loads();
  }
  LOG_FATAL_IF(terminal_loads.empty()) << "HTreeBuilder: segment terminal loads are empty for child node " << child_node.get_id();

  const std::size_t materialized_buffer_count = buffer_count;
  if (materialized_buffer_count == 0U) {
    return terminal_loads;
  }

  std::vector<BufferInstancePins> segment_buffers;
  segment_buffers.reserve(materialized_buffer_count);
  for (std::size_t buffer_index = 0; buffer_index < materialized_buffer_count; ++buffer_index) {
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

  CreateNet(*context.result, context.nextNetName(), segment_buffers.back().output_pin, terminal_loads);
  return std::vector<Pin*>{segment_buffers.front().input_pin};
}

auto MaterializeCTSObjects(HTreeBuilder::BuildResult& result, const BufferPatternRegistry& segment_pattern_registry) -> void
{
  if (!result.best_pattern.has_value()) {
    return;
  }

  const auto topology_levels = result.topology.levels();
  const std::size_t candidate_level_count = result.levels.size();
  if (topology_levels.size() <= 1U || candidate_level_count == 0U || candidate_level_count >= topology_levels.size()) {
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

  for (const auto node_id : topology_levels.at(candidate_level_count)) {
    const auto* node = result.topology.get_node(node_id);
    if (node == nullptr || node->get_loads().empty()) {
      continue;
    }
    entry_loads_by_node[node_id] = node->get_loads();
  }

  for (std::size_t reverse_depth = candidate_level_count; reverse_depth > 0U; --reverse_depth) {
    const std::size_t depth = reverse_depth - 1U;
    const auto* segment_pattern = level_patterns.at(depth);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: missing selected segment pattern metadata during materialization.";

    for (const auto node_id : topology_levels.at(depth)) {
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
  result.pruned_leaf_single_load_buffers = PruneLeafSingleLoadBuffers(result);
}

}  // namespace

auto HTreeBuilder::build(const std::vector<Pin*>& loads) -> BuildResult
{
  return build(loads, BuildOptions{});
}

auto HTreeBuilder::build(const std::vector<Pin*>& loads, const BuildOptions& options) -> BuildResult
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
       char_grid_plan.adapted ? "caller override for characterization" : "no override"},
      {"wire_length_iterations_override", std::to_string(char_grid_plan.wire_length_iterations),
       char_grid_plan.adapted ? "caller override for characterization" : "0 (disabled)"},
      {"distinct_level_bins", std::to_string(char_grid_plan.unique_level_bins), "aligned-length bins under effective unit"},
      {"decision_flags", decision_flags, "fallback trigger diagnostics"},
  };
  logInfoTable("HTreeBuilder Characterization Grid Plan", {"Item", "Value", "Detail"}, grid_plan_rows);

  build_stage.markRunning("characterization");
  CharBuilder char_builder;
  CharBuilder::InitOptions char_options;
  if (char_grid_plan.adapted) {
    char_options.wire_length_unit_um = char_grid_plan.wire_length_unit_um;
    char_options.wire_length_iterations = char_grid_plan.wire_length_iterations;
  }
  char_builder.init(char_options);
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
  result.char_slew_steps = char_builder.get_slew_steps();
  result.char_cap_steps = char_builder.get_cap_steps();
  const std::string char_wire_length_source
      = char_builder.get_wire_length_unit_source().empty() ? std::string{"unresolved"} : char_builder.get_wire_length_unit_source();
  const std::string char_wire_length_detail = char_builder.get_wire_length_unit_detail().empty()
                                                  ? std::string{"no resolution detail"}
                                                  : char_builder.get_wire_length_unit_detail();
  const logformat::TableRows char_summary_rows = {
      {"wire_length_unit_um", logformat::FormatWithUnit(result.char_wire_length_unit_um, "um"), char_wire_length_source},
      {"wire_length_unit_detail", char_wire_length_detail,
       char_grid_plan.adapted ? "effective value under HTree build override" : "direct CharBuilder resolution"},
      {"grid_plan_source", grid_source, char_grid_plan.adapted ? "HTreeBuilder adapted characterization grid" : "no HTree override"},
      {"wire_length_iterations", std::to_string(result.char_wire_length_iterations), "characterization sweep length bins"},
      {"max_slew_ns", logformat::FormatWithUnit(result.char_max_slew_ns, "ns"), "characterization upper bound"},
      {"max_cap_pf", logformat::FormatWithUnit(result.char_max_cap_pf, "pF"), "characterization upper bound"},
      {"segment_chars", std::to_string(char_builder.get_segment_chars().size()), "raw segment characterization entries"},
      {"buffer_patterns", std::to_string(char_builder.get_buffering_patterns().size()), "raw segment pattern count"},
  };
  logInfoTable("HTreeBuilder Characterization Summary", {"Item", "Value", "Detail"}, char_summary_rows);

  const auto base_resolved_options = ResolveBuildOptions(options, char_builder);
  result.force_branch_buffer = base_resolved_options.force_branch_buffer;
  result.force_leaf_unbuffered = base_resolved_options.force_leaf_unbuffered;
  result.target_depth = options.target_depth;

  const auto full_level_plans = BuildLevelPlans(result.topology, length_step_um, dbu_per_um);
  if (full_level_plans.empty()) {
    LOG_WARNING << "HTreeBuilder: failed to derive H-tree level plans from topology.";
    build_stage.skip({{"reason", "empty_level_plans"}});
    return result;
  }

  const unsigned max_depth = static_cast<unsigned>(full_level_plans.size());
  const auto depth_candidates = ResolveDepthCandidates(max_depth, options);
  if (depth_candidates.empty()) {
    LOG_WARNING << "HTreeBuilder: no depth candidates were resolved from topology.";
    build_stage.skip({{"reason", "empty_depth_candidates"}});
    return result;
  }
  result.depth_explore_window = static_cast<unsigned>(depth_candidates.size());

  BufferPatternRegistry segment_pattern_registry;
  for (const auto& pattern : char_builder.get_buffering_patterns()) {
    segment_pattern_registry.add(pattern);
  }

  unsigned max_target_length_idx = 0U;
  for (const auto& level : full_level_plans) {
    max_target_length_idx = std::max(max_target_length_idx, level.aligned_length_idx);
  }

  auto entry_sets_by_length = SynthesizeSegmentEntrySets(char_builder.get_segment_chars(), segment_pattern_registry, max_target_length_idx);

  std::vector<CandidateBuildEvaluation> candidate_evaluations;
  candidate_evaluations.reserve(depth_candidates.size());

  std::vector<CandidateRepresentativeRef> global_feasible_pool;
  std::vector<CandidateRepresentativeRef> global_candidate_pool;
  ActualLoadLegalityContext actual_load_legality_context;

  for (const unsigned depth : depth_candidates) {
    auto candidate_levels = MakeCandidateLevelPlans(full_level_plans, depth);
    const std::size_t leaf_count = CountCandidateLeafNodes(result.topology, depth);
    const auto cap_resolution = EstimateLeafDrivenCapResolution(result.topology, depth, options);
    const auto candidate_options = ApplyLeafDrivenCapResolution(base_resolved_options, cap_resolution, char_builder);
    CandidateBuildEvaluation evaluation;
    if (cap_resolution.depth_infeasible) {
      evaluation.depth = depth;
      evaluation.leaf_count = leaf_count;
      evaluation.resolved_options = candidate_options;
      evaluation.levels = candidate_levels;
      evaluation.success = false;
      evaluation.failure_reason = cap_resolution.failure_reason;
      LOG_WARNING << "HTreeBuilder: skip depth " << depth << " before characterization because " << cap_resolution.failure_reason << ".";
    } else {
      evaluation = EvaluateCandidateBuild(candidate_levels, entry_sets_by_length, segment_pattern_registry, candidate_options,
                                          result.topology, actual_load_legality_context, leaf_count, depth, result.char_max_cap_pf,
                                          result.char_slew_steps, result.char_cap_steps);
    }

    const std::string candidate_failure_reason
        = cap_resolution.depth_infeasible ? cap_resolution.failure_reason : evaluation.failure_reason;
    result.depth_candidates.push_back(BuildResult::DepthCandidateSummary{
        .depth = depth,
        .leaf_count = leaf_count,
        .success = evaluation.success,
        .selected = false,
        .used_explicit_target_depth = options.target_depth.has_value(),
        .used_explicit_leaf_driven_cap = cap_resolution.used_explicit_value,
        .requested_leaf_driven_cap_pf = cap_resolution.cap_pf.value_or(0.0),
        .leaf_driven_cap_source = cap_resolution.source,
        .failure_reason = candidate_failure_reason,
        .evaluated_leaf_count = cap_resolution.evaluated_leaf_count,
        .leaf_cap_min_pf = cap_resolution.leaf_cap_min_pf,
        .leaf_cap_max_pf = cap_resolution.leaf_cap_max_pf,
        .leaf_cap_mean_pf = cap_resolution.leaf_cap_mean_pf,
        .leaf_cap_median_pf = cap_resolution.leaf_cap_median_pf,
        .candidate_solution_count = evaluation.candidate_chars.size(),
        .feasible_solution_count = evaluation.feasible_chars.size(),
        .feasible_pattern_count = evaluation.feasible_pattern_representatives.size(),
        .used_boundary_fallback = evaluation.used_boundary_fallback,
        .selected_power_w = evaluation.best_char.has_value() ? evaluation.best_char->get_power() : 0.0,
        .selected_delay_ns = evaluation.best_char.has_value() ? evaluation.best_char->get_delay() : 0.0,
    });
    candidate_evaluations.push_back(std::move(evaluation));
    const auto candidate_index = candidate_evaluations.size() - 1U;
    const auto& stored_evaluation = candidate_evaluations.back();
    for (const auto& entry : stored_evaluation.feasible_pattern_representatives) {
      global_feasible_pool.push_back(CandidateRepresentativeRef{
          .candidate_index = candidate_index,
          .entry = &entry,
      });
    }
    for (const auto& entry : stored_evaluation.candidate_pattern_representatives) {
      global_candidate_pool.push_back(CandidateRepresentativeRef{
          .candidate_index = candidate_index,
          .entry = &entry,
      });
    }
  }

  const auto selected_feasible_ref = SelectBestGlobalRepresentative(global_feasible_pool);
  const auto selected_fallback_ref = selected_feasible_ref.has_value() ? std::optional<CandidateRepresentativeRef>{}
                                                                       : SelectBestGlobalRepresentative(global_candidate_pool);
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_fallback_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    result.failure_reason = global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    LOG_WARNING << "HTreeBuilder: failed to select a best H-tree characterization entry across depth candidates.";
    build_stage.skip({{"reason", result.failure_reason}, {"depth_candidates", std::to_string(depth_candidates.size())}});
    return result;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = result.depth_candidates.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  const auto selected_actual_legality
      = ResolveActualLoadLegality(result.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_registry,
                                  segment_pattern_registry, actual_load_legality_context);
  LOG_FATAL_IF(!selected_actual_legality.legal) << "HTreeBuilder: selected global representative is missing actual-load legality coverage.";
  selected_summary.htree_load_group_count = selected_actual_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_actual_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_actual_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_actual_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_actual_legality.cap_distribution.cap_median_pf;

  result.selected_depth = selected_evaluation.depth;
  result.best_char = *selected_ref->entry;
  result.levels = selected_evaluation.levels;
  result.candidate_chars = std::move(selected_evaluation.candidate_chars);
  result.candidate_pattern_representatives = std::move(selected_evaluation.candidate_pattern_representatives);
  result.feasible_chars = std::move(selected_evaluation.feasible_chars);
  result.feasible_pattern_representatives = std::move(selected_evaluation.feasible_pattern_representatives);
  result.min_top_input_slew_ns = selected_evaluation.resolved_options.min_top_input_slew_ns;
  result.top_input_slew_floor_idx = selected_evaluation.resolved_options.top_input_slew_floor_idx;
  result.min_leaf_driven_cap_pf = selected_evaluation.resolved_options.min_leaf_driven_cap_pf;
  result.leaf_driven_cap_floor_idx = selected_evaluation.resolved_options.leaf_driven_cap_floor_idx;

  if (!selected_feasible_ref.has_value()) {
    result.used_boundary_fallback = true;
    result.boundary_fallback_reason = "no_strict_boundary_feasible_solution_any_depth";
    result.boundary_fallback_score
        = CalcBoundaryFallbackScore(*result.best_char, selected_evaluation.resolved_options, result.char_slew_steps, result.char_cap_steps);

    schema::EmitDiagnostic(
        schema::DiagnosticLevel::kFallback, "HTreeBuilder",
        "no depth candidate satisfied caller boundary constraints; selected fallback solution from the global candidate pool.",
        {
            {"reason", result.boundary_fallback_reason},
            {"selected_depth", std::to_string(result.selected_depth.value_or(0U))},
            {"fallback_score", std::to_string(result.boundary_fallback_score.value_or(0.0))},
            {"selected_top_input_slew_idx", std::to_string(result.best_char->get_input_slew_idx())},
            {"selected_leaf_driven_cap_idx", std::to_string(result.best_char->get_leaf_driven_cap_idx())},
        });
  }

  result.best_pattern = selected_evaluation.topology_pattern_registry.materialize(result.best_char->get_pattern_id());
  const auto& best_level_segment_pattern_ids = result.best_pattern->get_level_segment_pattern_ids();
  LOG_FATAL_IF(best_level_segment_pattern_ids.size() != result.levels.size())
      << "HTreeBuilder: best H-tree pattern level count does not match selected depth.";
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto segment_pattern_id = best_level_segment_pattern_ids.at(level_index);
    result.levels.at(level_index).segment_pattern_id = segment_pattern_id;
    const auto* segment_pattern = segment_pattern_registry.find(segment_pattern_id);
    LOG_FATAL_IF(segment_pattern == nullptr) << "HTreeBuilder: selected segment pattern metadata is missing.";
    result.levels.at(level_index).selected_has_any_buffer = !segment_pattern->get_cell_masters().empty();
    if (!segment_pattern->get_cell_masters().empty()) {
      result.levels.at(level_index).selected_leaf_buffer_cell_master = segment_pattern->get_cell_masters().back();
    } else {
      result.levels.at(level_index).selected_leaf_buffer_cell_master.clear();
    }
    result.levels.at(level_index).selected_has_terminal_branch_buffer = segment_pattern->hasTerminalBranchBuffer();
    if (segment_pattern->hasTerminalBranchBuffer() && !segment_pattern->get_cell_masters().empty()) {
      result.levels.at(level_index).selected_terminal_cell_master = segment_pattern->get_cell_masters().back();
    } else {
      result.levels.at(level_index).selected_terminal_cell_master.clear();
    }
  }

  MaterializeCTSObjects(result, segment_pattern_registry);
  result.success = result.best_char.has_value() && result.best_pattern.has_value() && result.root_input_pin != nullptr
                   && result.root_output_pin != nullptr && !result.inserted_nets.empty();

  const bool selected_has_boundary_constraints = HasBoundaryConstraints(selected_evaluation.resolved_options);

  const logformat::TableRows build_summary_rows = {
      {"levels", std::to_string(result.levels.size()), "selected H-tree levels"},
      {"depth_candidates", std::to_string(result.depth_candidates.size()), "evaluated descending depth candidates"},
      {"selected_depth", result.selected_depth.has_value() ? std::to_string(*result.selected_depth) : "none",
       "global winner across all evaluated depth candidates"},
      {"selected_topology_pattern_id", std::to_string(result.best_char->get_pattern_id().local_id),
       result.used_boundary_fallback ? "selected fallback topology pattern from candidate pattern representatives"
                                     : "selected strict-feasible topology pattern from the global feasible pool"},
      {"selection_policy", result.used_boundary_fallback ? "global_boundary_fallback" : "global_pattern_worst_case_pareto_power_median",
       result.used_boundary_fallback
           ? "the global strict-feasible pool across all depth candidates is empty; fallback selection uses the global candidate "
             "representative pool with delay-power Pareto power-median ordering"
           : "each structural pattern contributes its worst feasible delay/power scenario per candidate depth; the global feasible "
             "representative pool is Pareto filtered and the lower power-ordered median entry is selected"},
      {"candidate_solutions", std::to_string(result.candidate_chars.size()),
       selected_has_boundary_constraints ? "selected-depth frontier entries after full composition"
                                         : "not materialized on unrestricted builds"},
      {"candidate_unique_patterns", std::to_string(result.candidate_pattern_representatives.size()),
       selected_has_boundary_constraints ? "selected-depth worst-case representatives before feasible filtering"
                                         : "not materialized on unrestricted builds"},
      {"feasible_solutions", std::to_string(result.feasible_chars.size()),
       selected_has_boundary_constraints ? "selected-depth strict-feasible entries after boundary filtering" : "same as composed frontier"},
      {"feasible_unique_patterns", std::to_string(result.feasible_pattern_representatives.size()),
       "selected-depth one representative entry per structural buffering pattern after feasible filtering"},
      {"inserted_insts", std::to_string(result.inserted_insts.size()), "materialized CTS buffer instances"},
      {"inserted_nets", std::to_string(result.inserted_nets.size()), "materialized CTS nets"},
      {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers),
       "post-materialization redundant leaf buffers removed when a leaf buffer directly drove one external load"},
      {"power", logformat::FormatPowerW(result.best_char->get_power()), "selected pattern metric (total power)"},
      {"delay", logformat::FormatWithUnit(result.best_char->get_delay(), "ns"), "selected pattern metric"},
      {"root_driven_cap_idx", std::to_string(result.best_char->get_driven_cap_idx()), "selected pattern metric"},
      {"leaf_driven_cap_idx", std::to_string(result.best_char->get_leaf_driven_cap_idx()), "selected pattern metric"},
      {"leaf_output_slew_idx", std::to_string(result.best_char->get_output_slew_idx()), "selected pattern metric"},
      {"root_load_cap_idx", std::to_string(result.best_char->get_load_cap_idx()), "selected pattern metric"},
      {"force_branch_buffer", logformat::FormatBool(selected_evaluation.resolved_options.force_branch_buffer),
       selected_evaluation.resolved_options.force_branch_buffer ? "every H-tree level requires terminal-buffered segment frontier"
                                                                : "disabled"},
      {"force_leaf_unbuffered", logformat::FormatBool(selected_evaluation.resolved_options.force_leaf_unbuffered),
       selected_evaluation.resolved_options.force_leaf_unbuffered ? "leaf H-tree level requires terminal-unbuffered segment frontier"
                                                                  : "disabled"},
      {"top_input_slew_floor_idx",
       selected_evaluation.resolved_options.top_input_slew_floor_idx.has_value()
           ? std::to_string(*selected_evaluation.resolved_options.top_input_slew_floor_idx)
           : "none",
       selected_evaluation.resolved_options.min_top_input_slew_ns.has_value()
           ? logformat::FormatWithUnit(*selected_evaluation.resolved_options.min_top_input_slew_ns, "ns")
           : "unconstrained"},
      {"leaf_driven_cap_floor_idx",
       selected_evaluation.resolved_options.leaf_driven_cap_floor_idx.has_value()
           ? std::to_string(*selected_evaluation.resolved_options.leaf_driven_cap_floor_idx)
           : "none",
       selected_evaluation.resolved_options.min_leaf_driven_cap_pf.has_value()
           ? logformat::FormatWithUnit(*selected_evaluation.resolved_options.min_leaf_driven_cap_pf, "pF")
           : "unconstrained"},
      {"leaf_load_count", std::to_string(selected_summary.evaluated_leaf_count),
       "selected depth topology leaf groups with exact routing-cap evaluation for driven-cap resolution"},
      {"leaf_load_cap_min", logformat::FormatWithUnit(selected_summary.leaf_cap_min_pf, "pF"),
       "selected depth topology leaf total-cap minimum across precheck groups"},
      {"leaf_load_cap_max", logformat::FormatWithUnit(selected_summary.leaf_cap_max_pf, "pF"),
       "selected depth topology leaf total-cap maximum across precheck groups; auto-driven-cap source uses this value"},
      {"leaf_load_cap_mean", logformat::FormatWithUnit(selected_summary.leaf_cap_mean_pf, "pF"),
       "selected depth topology leaf total-cap mean across precheck groups"},
      {"leaf_load_cap_median", logformat::FormatWithUnit(selected_summary.leaf_cap_median_pf, "pF"),
       "selected depth topology leaf total-cap median across precheck groups"},
      {"htree_load_group_count", std::to_string(selected_summary.htree_load_group_count),
       "selected H-tree external-load groups driven by the bottom-most buffered segments"},
      {"htree_load_cap_min", logformat::FormatWithUnit(selected_summary.htree_load_cap_min_pf, "pF"),
       "selected H-tree external-load total-cap minimum across real driven-load groups"},
      {"htree_load_cap_max", logformat::FormatWithUnit(selected_summary.htree_load_cap_max_pf, "pF"),
       "selected H-tree external-load total-cap maximum across real driven-load groups"},
      {"htree_load_cap_mean", logformat::FormatWithUnit(selected_summary.htree_load_cap_mean_pf, "pF"),
       "selected H-tree external-load total-cap mean across real driven-load groups"},
      {"htree_load_cap_median", logformat::FormatWithUnit(selected_summary.htree_load_cap_median_pf, "pF"),
       "selected H-tree external-load total-cap median across real driven-load groups"},
      {"used_boundary_fallback", logformat::FormatBool(result.used_boundary_fallback),
       result.used_boundary_fallback ? result.boundary_fallback_reason : "constraints satisfied without fallback"},
      {"boundary_fallback_score", result.boundary_fallback_score.has_value() ? std::to_string(*result.boundary_fallback_score) : "none",
       result.used_boundary_fallback ? "diagnostic normalized active-boundary score of the selected fallback" : "not used"},
  };
  logInfoTable("HTreeBuilder Build Summary", {"Item", "Value", "Detail"}, build_summary_rows);
  build_stage.finish({{"success", result.success ? "true" : "false"},
                      {"levels", std::to_string(result.levels.size())},
                      {"inserted_insts", std::to_string(result.inserted_insts.size())},
                      {"inserted_nets", std::to_string(result.inserted_nets.size())},
                      {"pruned_leaf_single_load_buffers", std::to_string(result.pruned_leaf_single_load_buffers)}},
                     result.success ? "success" : "incomplete");
  return result;
}

}  // namespace icts
