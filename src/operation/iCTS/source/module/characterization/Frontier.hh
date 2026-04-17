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
 * @file Frontier.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Boundary- and terminal-semantic-aware frontier helpers for characterization entries.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include "characterization/HTreeTopologyChar.hh"
#include "characterization/SegmentChar.hh"

namespace icts {

enum class TerminalSemantic : unsigned
{
  kLeafUnbuffered = 0U,
  kBranchBuffered = 1U,
};

struct SegmentFrontierStateKey
{
  unsigned input_slew_idx = 0U;
  unsigned driven_cap_idx = 0U;
  unsigned output_slew_idx = 0U;
  unsigned load_cap_idx = 0U;
  TerminalSemantic terminal_semantic = TerminalSemantic::kLeafUnbuffered;

  auto operator==(const SegmentFrontierStateKey& rhs) const -> bool = default;
};

struct SegmentFrontierStateKeyHash
{
  auto operator()(const SegmentFrontierStateKey& key) const -> std::size_t
  {
    std::size_t seed = std::hash<unsigned>{}(key.input_slew_idx);
    seed ^= std::hash<unsigned>{}(key.driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.output_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(static_cast<unsigned>(key.terminal_semantic)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

struct HTreeFrontierStateKey
{
  unsigned input_slew_idx = 0U;
  unsigned driven_cap_idx = 0U;
  unsigned leaf_driven_cap_idx = 0U;
  unsigned output_slew_idx = 0U;
  unsigned load_cap_idx = 0U;
  TerminalSemantic terminal_semantic = TerminalSemantic::kLeafUnbuffered;

  auto operator==(const HTreeFrontierStateKey& rhs) const -> bool = default;
};

struct HTreeFrontierStateKeyHash
{
  auto operator()(const HTreeFrontierStateKey& key) const -> std::size_t
  {
    std::size_t seed = std::hash<unsigned>{}(key.input_slew_idx);
    seed ^= std::hash<unsigned>{}(key.driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.leaf_driven_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.output_slew_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(key.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(static_cast<unsigned>(key.terminal_semantic)) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

inline auto SortSegmentFrontierEntries(std::vector<SegmentChar>& chars) -> void
{
  std::ranges::sort(chars, [](const SegmentChar& lhs, const SegmentChar& rhs) -> bool {
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
}

inline auto SortHTreeFrontierEntries(std::vector<HTreeTopologyChar>& chars) -> void
{
  std::ranges::sort(chars, [](const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool {
    if (lhs.get_input_slew_idx() != rhs.get_input_slew_idx()) {
      return lhs.get_input_slew_idx() < rhs.get_input_slew_idx();
    }
    if (lhs.get_driven_cap_idx() != rhs.get_driven_cap_idx()) {
      return lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx();
    }
    if (lhs.get_leaf_driven_cap_idx() != rhs.get_leaf_driven_cap_idx()) {
      return lhs.get_leaf_driven_cap_idx() < rhs.get_leaf_driven_cap_idx();
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
}

template <class CharT>
inline auto CostDominates(const CharT& lhs, const CharT& rhs) -> bool
{
  const bool not_worse = lhs.get_delay() <= rhs.get_delay() && lhs.get_power() <= rhs.get_power();
  if (!not_worse) {
    return false;
  }

  if (lhs.get_delay() < rhs.get_delay() || lhs.get_power() < rhs.get_power()) {
    return true;
  }

  return lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack();
}

template <class CharT, class KeyT, class KeyHashT, class KeyBuilderT>
class StateFrontierPruner
{
 public:
  using GroupKey = KeyT;
  using GroupKeyHash = KeyHashT;

  explicit StateFrontierPruner(KeyBuilderT key_builder) : _key_builder(std::move(key_builder)) {}

  auto groupKey(const CharT& entry) const -> GroupKey { return _key_builder(entry); }

  auto dominates(const CharT& lhs, const CharT& rhs) const -> bool { return CostDominates(lhs, rhs); }

  auto maxPerGroup() const -> std::size_t { return 0U; }

 private:
  KeyBuilderT _key_builder;
};

template <class SemanticResolverT>
inline auto MakeSegmentStateFrontierPruner(SemanticResolverT semantic_resolver)
{
  auto key_builder = [semantic_resolver = std::move(semantic_resolver)](const SegmentChar& entry) -> SegmentFrontierStateKey {
    return SegmentFrontierStateKey{
        .input_slew_idx = entry.get_input_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
        .terminal_semantic = semantic_resolver(entry),
    };
  };
  return StateFrontierPruner<SegmentChar, SegmentFrontierStateKey, SegmentFrontierStateKeyHash, decltype(key_builder)>(
      std::move(key_builder));
}

template <class SemanticResolverT>
inline auto MakeHTreeStateFrontierPruner(SemanticResolverT semantic_resolver)
{
  auto key_builder = [semantic_resolver = std::move(semantic_resolver)](const HTreeTopologyChar& entry) -> HTreeFrontierStateKey {
    return HTreeFrontierStateKey{
        .input_slew_idx = entry.get_input_slew_idx(),
        .driven_cap_idx = entry.get_driven_cap_idx(),
        .leaf_driven_cap_idx = entry.get_leaf_driven_cap_idx(),
        .output_slew_idx = entry.get_output_slew_idx(),
        .load_cap_idx = entry.get_load_cap_idx(),
        .terminal_semantic = semantic_resolver(entry),
    };
  };
  return StateFrontierPruner<HTreeTopologyChar, HTreeFrontierStateKey, HTreeFrontierStateKeyHash, decltype(key_builder)>(
      std::move(key_builder));
}

template <class CharT, class KeyT, class KeyHashT, class KeyBuilderT, class SortFnT>
inline auto BuildStateFrontierImpl(const std::vector<CharT>& chars, const KeyBuilderT& key_builder, const SortFnT& sort_fn)
    -> std::vector<CharT>
{
  std::unordered_map<KeyT, std::vector<const CharT*>, KeyHashT> grouped_entries;
  grouped_entries.reserve(chars.size());

  for (const auto& entry : chars) {
    grouped_entries[key_builder(entry)].push_back(&entry);
  }

  std::vector<CharT> frontier_entries;
  frontier_entries.reserve(chars.size());
  for (auto& [group_key, entries] : grouped_entries) {
    (void) group_key;
    std::ranges::sort(entries, [](const CharT* lhs, const CharT* rhs) -> bool {
      if (lhs->get_delay() != rhs->get_delay()) {
        return lhs->get_delay() < rhs->get_delay();
      }
      if (lhs->get_power() != rhs->get_power()) {
        return lhs->get_power() < rhs->get_power();
      }
      return lhs->get_pattern_id().pack() < rhs->get_pattern_id().pack();
    });

    double best_power = std::numeric_limits<double>::infinity();
    for (const CharT* entry : entries) {
      if (entry == nullptr || !(entry->get_power() < best_power)) {
        continue;
      }
      frontier_entries.push_back(*entry);
      best_power = entry->get_power();
    }
  }

  sort_fn(frontier_entries);
  return frontier_entries;
}

template <class SemanticResolverT>
inline auto BuildSegmentStateFrontier(const std::vector<SegmentChar>& chars, const SemanticResolverT& semantic_resolver)
    -> std::vector<SegmentChar>
{
  auto pruner = MakeSegmentStateFrontierPruner(std::move(semantic_resolver));
  return BuildStateFrontierImpl<SegmentChar, SegmentFrontierStateKey, SegmentFrontierStateKeyHash>(
      chars, [&](const SegmentChar& entry) -> SegmentFrontierStateKey { return pruner.groupKey(entry); }, SortSegmentFrontierEntries);
}

template <class SemanticResolverT>
inline auto BuildHTreeStateFrontier(const std::vector<HTreeTopologyChar>& chars, const SemanticResolverT& semantic_resolver)
    -> std::vector<HTreeTopologyChar>
{
  auto pruner = MakeHTreeStateFrontierPruner(std::move(semantic_resolver));
  return BuildStateFrontierImpl<HTreeTopologyChar, HTreeFrontierStateKey, HTreeFrontierStateKeyHash>(
      chars, [&](const HTreeTopologyChar& entry) -> HTreeFrontierStateKey { return pruner.groupKey(entry); }, SortHTreeFrontierEntries);
}

}  // namespace icts
