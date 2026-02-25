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
 * @file SegmentChar.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Segment characterization with length and electrical properties.
 */

#pragma once

#include <cstdint>

#include "CharCore.hh"
#include "PatternId.hh"

namespace icts {

/**
 * @brief Segment characterization entry.
 *
 * Combines CharCore (electrical boundaries + cost) with segment length.
 * Supports composition for segment concatenation via Hash-Join.
 */
class SegmentChar
{
 public:
  SegmentChar() = default;

  SegmentChar(CharCore core, uint64_t length_dbu) : _core(std::move(core)), _length_dbu(length_dbu) {}

  // Forwarded getters from CharCore
  uint16_t get_input_slew() const { return _core.get_input_slew(); }
  uint16_t get_output_slew() const { return _core.get_output_slew(); }
  uint16_t get_driven_cap() const { return _core.get_driven_cap(); }
  uint16_t get_load_cap() const { return _core.get_load_cap(); }
  double get_delay() const { return _core.get_delay(); }
  double get_power() const { return _core.get_power(); }
  PatternId get_pattern_id() const { return _core.get_pattern_id(); }

  // Segment-specific getter
  uint64_t get_length() const { return _length_dbu; }

  /**
   * @brief Compose two segment characterizations.
   *
   * Composition rules (fixed):
   * - length = upstream.length + downstream.length
   * - input_slew = upstream.input_slew
   * - output_slew = downstream.output_slew
   * - driven_cap = upstream.driven_cap (unchanged from source)
   * - load_cap = downstream.load_cap
   * - delay = upstream.delay + downstream.delay
   * - power = upstream.power + downstream.power
   *
   * @param upstream Upstream segment (closer to source)
   * @param downstream Downstream segment (closer to sink)
   * @param merged_pid Pattern ID for the composed segment
   */
  static SegmentChar compose(const SegmentChar& upstream, const SegmentChar& downstream, PatternId merged_pid)
  {
    CharCore merged_core(upstream.get_input_slew(),                      // input from upstream
                         downstream.get_output_slew(),                   // output from downstream
                         upstream.get_driven_cap(),                      // driven_cap from upstream (source side)
                         downstream.get_load_cap(),                      // load_cap from downstream (sink side)
                         upstream.get_delay() + downstream.get_delay(),  // additive delay
                         upstream.get_power() + downstream.get_power(),  // additive power
                         merged_pid);
    return SegmentChar(std::move(merged_core), upstream._length_dbu + downstream._length_dbu);
  }

 private:
  CharCore _core;
  uint64_t _length_dbu = 0;
};

}  // namespace icts
