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
 * @file CharCore.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-30
 * @brief Core electrical boundary and cost carrier for characterization.
 */

#pragma once

#include <cstdint>

#include "PatternId.hh"

namespace icts {

/**
 * @brief Core characterization data: electrical boundaries + cost metrics.
 *
 * Reusable component for both SegmentChar and HTreeTopologyChar.
 * Join keys are NOT stored here - they are computed by traits externally.
 */
class CharCore
{
 public:
  CharCore() = default;

  CharCore(uint16_t input_slew, uint16_t output_slew, uint16_t driven_cap, uint16_t load_cap, double delay, double power,
           PatternId pattern_id)
      : _input_slew(input_slew),
        _output_slew(output_slew),
        _driven_cap(driven_cap),
        _load_cap(load_cap),
        _delay(delay),
        _power(power),
        _pattern_id(pattern_id)
  {
  }

  // Getters - following iCTS naming convention
  uint16_t get_input_slew() const { return _input_slew; }
  uint16_t get_output_slew() const { return _output_slew; }
  uint16_t get_driven_cap() const { return _driven_cap; }
  uint16_t get_load_cap() const { return _load_cap; }
  double get_delay() const { return _delay; }
  double get_power() const { return _power; }
  PatternId get_pattern_id() const { return _pattern_id; }

 private:
  uint16_t _input_slew = 0;
  uint16_t _output_slew = 0;
  uint16_t _driven_cap = 0;
  uint16_t _load_cap = 0;
  double _delay = 0.0;
  double _power = 0.0;
  PatternId _pattern_id{PatternDomain::kSegmentPattern, 0};
};

}  // namespace icts
