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
 * @file BufferingPattern.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Segment buffering pattern with buffer positions and cell masters.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PatternId.hh"

namespace icts {

/**
 * @brief Segment buffering pattern.
 *
 * Represents a wire segment with optional buffer insertions.
 * Buffer positions are normalized to (0, 1] relative to segment length.
 * Supports concatenation for segment composition.
 */
class BufferingPattern
{
 public:
  BufferingPattern() = default;

  BufferingPattern(uint64_t length_dbu, PatternId pattern_id, std::vector<double> buffer_positions, std::vector<std::string> cell_masters)
      : _length_dbu(length_dbu),
        _pattern_id(pattern_id),
        _buffer_positions(std::move(buffer_positions)),
        _cell_masters(std::move(cell_masters))
  {
  }

  // Getters
  uint64_t get_length() const { return _length_dbu; }
  PatternId get_pattern_id() const { return _pattern_id; }
  const std::vector<double>& get_buffer_positions() const { return _buffer_positions; }
  const std::vector<std::string>& get_cell_masters() const { return _cell_masters; }

  /**
   * @brief Check if this is a pure wire pattern (no buffers).
   */
  bool isWirePattern() const { return _buffer_positions.empty(); }

  /**
   * @brief Check if this is a buffer pattern (has buffers).
   */
  bool isBufferPattern() const { return !_buffer_positions.empty(); }

  /**
   * @brief Concatenate two buffering patterns.
   *
   * Upstream pattern comes first, downstream pattern comes after.
   * Buffer positions are renormalized to the combined length.
   */
  static BufferingPattern concat(const BufferingPattern& upstream, const BufferingPattern& downstream)
  {
    uint64_t total_length = upstream._length_dbu + downstream._length_dbu;
    if (total_length == 0) {
      return BufferingPattern{0, PatternId::segment(0), {}, {}};
    }

    double up_ratio = static_cast<double>(upstream._length_dbu) / total_length;

    // Renormalize upstream positions to [0, up_ratio]
    std::vector<double> merged_positions;
    merged_positions.reserve(upstream._buffer_positions.size() + downstream._buffer_positions.size());
    for (double pos : upstream._buffer_positions) {
      merged_positions.push_back(pos * up_ratio);
    }
    // Offset downstream positions to (up_ratio, 1]
    for (double pos : downstream._buffer_positions) {
      merged_positions.push_back(up_ratio + pos * (1.0 - up_ratio));
    }

    // Concatenate cell masters
    std::vector<std::string> merged_masters;
    merged_masters.reserve(upstream._cell_masters.size() + downstream._cell_masters.size());
    merged_masters.insert(merged_masters.end(), upstream._cell_masters.begin(), upstream._cell_masters.end());
    merged_masters.insert(merged_masters.end(), downstream._cell_masters.begin(), downstream._cell_masters.end());

    // Note: pattern_id for merged pattern should be assigned by the caller
    return BufferingPattern{total_length, PatternId::segment(0), std::move(merged_positions), std::move(merged_masters)};
  }

 private:
  uint64_t _length_dbu = 0;
  PatternId _pattern_id{PatternDomain::kSegmentPattern, 0};
  std::vector<double> _buffer_positions;   ///< Normalized positions in (0, 1]
  std::vector<std::string> _cell_masters;  ///< Cell master names for each buffer
};

}  // namespace icts
