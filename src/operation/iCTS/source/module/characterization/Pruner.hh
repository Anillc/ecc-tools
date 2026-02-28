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
 * @file Pruner.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Pareto pruner functor for characterization table filtering.
 */

#pragma once

#include <cstddef>

namespace icts {

/**
 * @brief Pareto pruner for characterization entries.
 *
 * Filters dominated solutions within the same group.
 * Domination criteria (all must be better or equal, at least one strictly
 * better):
 * - Lower output_slew is better
 * - Higher load_cap is better (can drive more)
 * - Lower delay is better
 * - Lower power is better
 *
 * @tparam CharT Characterization type (SegmentChar or HTreeTopologyChar)
 */
template <class CharT>
struct ParetoPruner
{
  /**
   * @brief Compute group key for a characterization entry.
   *
   * Entries are only compared within the same group.
   * Default: group by pattern ID.
   */
  unsigned groupKey(const CharT& c) const { return c.get_pattern_id().pack(); }

  /**
   * @brief Check if entry 'a' dominates entry 'b'.
   *
   * @return true if a is better or equal in all metrics and strictly better in
   * at least one
   */
  bool dominates(const CharT& a, const CharT& b) const
  {
    // All metrics: a must be >= b in desirability
    bool all_ge = (a.get_output_slew_idx() <= b.get_output_slew_idx()) && (a.get_load_cap_idx() >= b.get_load_cap_idx())
                  && (a.get_delay() <= b.get_delay()) && (a.get_power() <= b.get_power());
    if (!all_ge) {
      return false;
    }

    // At least one metric: a must be strictly better
    bool any_gt = (a.get_output_slew_idx() < b.get_output_slew_idx()) || (a.get_load_cap_idx() > b.get_load_cap_idx())
                  || (a.get_delay() < b.get_delay()) || (a.get_power() < b.get_power());
    return any_gt;
  }

  /**
   * @brief Maximum entries to keep per group.
   *
   * @return 0 means no limit (keep all non-dominated)
   */
  std::size_t maxPerGroup() const { return 0; }
};

/**
 * @brief Pruner that groups by input slew and driven cap.
 *
 * Useful when you want to keep the best options for each unique
 * (input_slew, driven_cap) combination.
 */
template <class CharT>
struct InputBoundaryPruner
{
  unsigned groupKey(const CharT& c) const { return (c.get_input_slew_idx() << 16) | c.get_driven_cap_idx(); }

  bool dominates(const CharT& a, const CharT& b) const
  {
    bool all_ge = (a.get_output_slew_idx() <= b.get_output_slew_idx()) && (a.get_load_cap_idx() >= b.get_load_cap_idx())
                  && (a.get_delay() <= b.get_delay()) && (a.get_power() <= b.get_power());
    if (!all_ge) {
      return false;
    }
    return (a.get_output_slew_idx() < b.get_output_slew_idx()) || (a.get_load_cap_idx() > b.get_load_cap_idx())
           || (a.get_delay() < b.get_delay()) || (a.get_power() < b.get_power());
  }

  std::size_t maxPerGroup() const { return 0; }
};

}  // namespace icts
