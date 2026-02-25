// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file HashJoinEngine.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Header-only Hash-Join engine for table concatenation.
 *
 * Implements build+probe Hash-Join with compile-time traits injection.
 * Zero virtual function overhead - all polymorphism resolved at compile time.
 */

#pragma once

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/characterization/PatternId.hh"

namespace icts {
namespace detail {

/**
 * @brief Pack slew and cap into a single 32-bit key.
 *
 * Key format: [slew:16][cap:16]
 */
inline uint32_t pack(uint16_t slew, uint16_t cap)
{
  return (static_cast<uint32_t>(slew) << 16) | cap;
}

/**
 * @brief Null pruner type for when pruning is disabled.
 */
struct NullPruner
{
};

/**
 * @brief Hash-Join concatenation of two tables.
 *
 * Performs Hash-Join with:
 * - Build phase: index downstream by build_key
 * - Probe phase: probe upstream against index using probe_key
 *
 * Complexity:
 * - Average: O(|U| + |D| + J) where J = number of matches
 * - Worst: O(|U| × |D|) if all keys collide
 *
 * @tparam CharT Characterization type (SegmentChar or HTreeTopologyChar)
 * @tparam Traits Traits class providing build_key, probe_key, compose
 * @tparam CombinerT Pattern combiner type
 * @tparam PrunerT Pruner type (use NullPruner to disable)
 *
 * @param upstream Upstream characterization entries
 * @param downstream Downstream characterization entries
 * @param combiner Pattern combiner for creating merged pattern IDs
 * @param out Output vector for composed results
 * @param pruner Optional pruner (nullptr to disable)
 */
template <class CharT, class Traits, class CombinerT, class PrunerT>
inline void HashJoinConcat(const std::vector<CharT>& upstream, const std::vector<CharT>& downstream, const CombinerT& combiner,
                           std::vector<CharT>& out, [[maybe_unused]] const PrunerT* pruner = nullptr)
{
  if (upstream.empty() || downstream.empty()) {
    return;
  }

  // Build phase: hash downstream entries by build_key
  std::unordered_map<uint32_t, std::vector<std::size_t>> index;
  index.reserve(downstream.size());  // Reduce rehashing
  for (std::size_t i = 0; i < downstream.size(); ++i) {
    index[Traits::build_key(downstream[i])].push_back(i);
  }

  // Estimate output size for reserve (assume ~1 match per upstream on average)
  out.reserve(out.size() + upstream.size());

  // Probe phase: probe upstream against index
  for (const auto& up : upstream) {
    uint32_t key = Traits::probe_key(up);
    auto it = index.find(key);
    if (it == index.end()) {
      continue;
    }

    for (std::size_t di : it->second) {
      const auto& down = downstream[di];
      PatternId merged_pid = combiner.combine(up.get_pattern_id(), down.get_pattern_id());
      CharT result = Traits::compose(up, down, merged_pid);

      // Apply pruning if enabled
      if constexpr (!std::is_same_v<PrunerT, NullPruner>) {
        if (pruner != nullptr) {
          // Simple Pareto check: skip if dominated by existing in same group
          // Full implementation would maintain frontier per group
          bool dominated = false;
          uint64_t group = pruner->group_key(result);
          for (const auto& existing : out) {
            if (pruner->group_key(existing) == group && pruner->dominates(existing, result)) {
              dominated = true;
              break;
            }
          }
          if (dominated) {
            continue;
          }
          // Remove entries dominated by new result
          auto new_end = std::remove_if(out.begin(), out.end(), [&](const CharT& existing) {
            return pruner->group_key(existing) == group && pruner->dominates(result, existing);
          });
          out.erase(new_end, out.end());
        }
      }

      out.push_back(std::move(result));
    }
  }
}

}  // namespace detail
}  // namespace icts
