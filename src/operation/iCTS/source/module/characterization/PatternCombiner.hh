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
 * @file PatternCombiner.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Pattern combiners for segment and topology pattern ID generation.
 */

#pragma once

#include <cstdint>

#include "database/characterization/PatternId.hh"

namespace icts {

/**
 * @brief Combiner for segment patterns.
 *
 * Generates new pattern IDs for composed segments.
 * In a full implementation, this would register the composed pattern
 * in a pattern registry and return the assigned ID.
 */
class SegmentPatternCombiner
{
 public:
  explicit SegmentPatternCombiner(uint32_t start_id = 0) : _next_id(start_id) {}

  PatternId combine([[maybe_unused]] PatternId up, [[maybe_unused]] PatternId down) const { return PatternId::segment(_next_id++); }

  uint32_t get_next_id() const { return _next_id; }

 private:
  mutable uint32_t _next_id;
};

/**
 * @brief Combiner for topology patterns.
 *
 * Generates new pattern IDs for composed topologies.
 */
class TopologyPatternCombiner
{
 public:
  explicit TopologyPatternCombiner(uint32_t start_id = 0) : _next_id(start_id) {}

  PatternId combine([[maybe_unused]] PatternId up, [[maybe_unused]] PatternId down) const { return PatternId::topology(_next_id++); }

  uint32_t get_next_id() const { return _next_id; }

 private:
  mutable uint32_t _next_id;
};

}  // namespace icts
