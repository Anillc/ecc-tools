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
 * @file SegmentTraits.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Traits for segment Hash-Join operations.
 */

#pragma once

#include "HashJoinEngine.hh"
#include "database/characterization/SegmentChar.hh"

namespace icts {

/**
 * @brief Traits for SegmentChar Hash-Join operations.
 *
 * Join condition (equal):
 * - upstream.output_slew == downstream.input_slew
 * - upstream.load_cap == downstream.driven_cap
 */
struct SegmentTraits
{
  /**
   * @brief Build key from downstream entry.
   *
   * Key = pack(input_slew, driven_cap)
   */
  static uint32_t build_key(const SegmentChar& c) { return detail::pack(c.get_input_slew(), c.get_driven_cap()); }

  /**
   * @brief Probe key from upstream entry.
   *
   * Key = pack(output_slew, load_cap)
   */
  static uint32_t probe_key(const SegmentChar& c) { return detail::pack(c.get_output_slew(), c.get_load_cap()); }

  /**
   * @brief Compose upstream and downstream into merged result.
   */
  static SegmentChar compose(const SegmentChar& up, const SegmentChar& down, PatternId merged_pid)
  {
    return SegmentChar::compose(up, down, merged_pid);
  }
};

}  // namespace icts
