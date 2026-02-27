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
 * @file HTreeTopologyChar.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief H-tree topology characterization with levels.
 */

#pragma once

#include <cstdint>

#include "CharCore.hh"
#include "PatternId.hh"

namespace icts {

/**
 * @brief H-tree topology characterization entry.
 *
 * Combines CharCore (electrical boundaries + cost) with H-tree level count.
 * Supports composition for H-tree concatenation via Hash-Join.
 *
 * Key difference from SegmentChar:
 * - Uses load_cap/2 in probe key (binary fan-out)
 * - Power composition: merged.power = up.power + 2 * down.power
 */
class HTreeTopologyChar
{
 public:
  HTreeTopologyChar() = default;

  HTreeTopologyChar(CharCore core, uint32_t levels) : _core(std::move(core)), _levels(levels) {}

  // Forwarded getters from CharCore
  uint16_t get_input_slew() const { return _core.get_input_slew(); }
  uint16_t get_output_slew() const { return _core.get_output_slew(); }
  uint16_t get_driven_cap() const { return _core.get_driven_cap(); }
  uint16_t get_load_cap() const { return _core.get_load_cap(); }
  double get_delay() const { return _core.get_delay(); }
  double get_power() const { return _core.get_power(); }
  PatternId get_pattern_id() const { return _core.get_pattern_id(); }

  // H-tree specific getter
  uint32_t get_levels() const { return _levels; }

  /**
   * @brief Compose two H-tree topology characterizations.
   *
   * Composition rules:
   * - levels = upstream.levels + downstream.levels
   * - input_slew = upstream.input_slew
   * - output_slew = downstream.output_slew
   * - driven_cap = upstream.driven_cap
   * - load_cap = downstream.load_cap
   * - delay = upstream.delay + downstream.delay
   * - power = upstream.power + 2 * downstream.power (binary fan-out)
   *
   * The /2 cap relationship is enforced by HTreeTraits::probeKey,
   * not here in compose.
   *
   * @param upstream Upstream H-tree (closer to root)
   * @param downstream Downstream H-tree (closer to leaves)
   * @param merged_topo_pid Pattern ID for the composed topology
   */
  static HTreeTopologyChar compose(const HTreeTopologyChar& upstream, const HTreeTopologyChar& downstream, PatternId merged_topo_pid)
  {
    CharCore merged_core(upstream.get_input_slew(),                      // input from upstream
                         downstream.get_output_slew(),                   // output from downstream
                         upstream.get_driven_cap(),                      // driven_cap from upstream
                         downstream.get_load_cap(),                      // load_cap from downstream
                         upstream.get_delay() + downstream.get_delay(),  // additive delay
                         // Binary fan-out: downstream power is doubled
                         upstream.get_power() + 2.0 * downstream.get_power(), merged_topo_pid);
    return HTreeTopologyChar(std::move(merged_core), upstream._levels + downstream._levels);
  }

 private:
  CharCore _core;
  uint32_t _levels = 0;
};

}  // namespace icts
