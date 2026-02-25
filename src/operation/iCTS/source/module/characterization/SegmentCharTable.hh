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
 * @file SegmentCharTable.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Segment characterization table with Hash-Join concatenation.
 */

#pragma once

#include <vector>

#include "HashJoinEngine.hh"
#include "SegmentTraits.hh"
#include "database/characterization/SegmentChar.hh"

namespace icts {

/**
 * @brief Table of segment characterizations.
 *
 * Supports Hash-Join based concatenation for efficient table composition.
 */
class SegmentCharTable
{
 public:
  SegmentCharTable() = default;

  /**
   * @brief Add a characterization entry.
   */
  void addChar(SegmentChar c) { _chars.push_back(std::move(c)); }

  /**
   * @brief Clear all entries.
   */
  void clear() { _chars.clear(); }

  /**
   * @brief Get read-only access to characterization entries.
   */
  const std::vector<SegmentChar>& get_chars() const { return _chars; }

  /**
   * @brief Get the number of entries.
   */
  std::size_t size() const { return _chars.size(); }

  /**
   * @brief Reserve capacity.
   */
  void reserve(std::size_t n) { _chars.reserve(n); }

  /**
   * @brief Concatenate with downstream table using Hash-Join.
   *
   * Join condition (equal):
   * - this.output_slew == downstream.input_slew
   * - this.load_cap == downstream.driven_cap
   *
   * @tparam CombinerT Pattern combiner type
   * @tparam PrunerT Pruner type (default: NullPruner for no pruning)
   * @param downstream Downstream table to join with
   * @param combiner Pattern combiner for merged pattern IDs
   * @param pruner Optional pruner (nullptr to disable)
   * @return New table with composed entries
   */
  template <class CombinerT, class PrunerT = detail::NullPruner>
  SegmentCharTable concatWith(const SegmentCharTable& downstream, const CombinerT& combiner, const PrunerT* pruner = nullptr) const
  {
    SegmentCharTable result;
    detail::HashJoinConcat<SegmentChar, SegmentTraits>(_chars, downstream._chars, combiner, result._chars, pruner);
    return result;
  }

 private:
  std::vector<SegmentChar> _chars;
};

}  // namespace icts
