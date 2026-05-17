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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CharTimingLookup.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Characterization-backed timing lookup for CTS buffer sizing.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "PatternId.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"

namespace icts {

class BufferingPattern;
class CharBuilder;

namespace buffer_sizing {

enum class CharArcKind
{
  kWire,
  kTerminalBuffer
};

struct CharTimingQuery
{
  CharArcKind arc_kind = CharArcKind::kWire;
  std::string terminal_buffer_master;
  double length_um = 0.0;
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
};

struct CharTimingResult
{
  bool success = false;
  bool clamped = false;
  std::string failure_reason;
  double requested_length_um = 0.0;
  unsigned length_idx = 0U;
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  double driven_cap_pf = 0.0;
  double delay_ns = 0.0;
  double output_slew_ns = 0.0;
};

class CharTimingLookup
{
 public:
  CharTimingLookup() = default;
  CharTimingLookup(const std::vector<SegmentChar>& segment_chars, const std::vector<BufferingPattern>& buffering_patterns,
                   UniformValueLattice length_lattice, UniformValueLattice slew_lattice, UniformValueLattice cap_lattice);

  static auto buildFromCharBuilder(const CharBuilder& char_builder) -> CharTimingLookup;

  auto lookup(const CharTimingQuery& query) const -> CharTimingResult;
  auto isReady() const -> bool;

  auto get_length_lattice() const -> const UniformValueLattice& { return _length_lattice; }
  auto get_slew_lattice() const -> const UniformValueLattice& { return _slew_lattice; }
  auto get_cap_lattice() const -> const UniformValueLattice& { return _cap_lattice; }

 private:
  struct PatternInfo
  {
    PatternId pattern_id;
    CharArcKind arc_kind = CharArcKind::kWire;
    std::string terminal_buffer_master;
  };

  auto findPatternInfo(PatternId pattern_id) const -> const PatternInfo*;
  auto acceptsPattern(const SegmentChar& segment_char, const CharTimingQuery& query, unsigned length_idx) const -> bool;
  auto lookupAtLengthIndex(const CharTimingQuery& query, unsigned length_idx, bool length_clamped) const -> CharTimingResult;
  auto lookupComposed(const CharTimingQuery& query, unsigned requested_length_idx) const -> CharTimingResult;
  auto lookupSample(const CharTimingQuery& query, unsigned length_idx, unsigned slew_idx, unsigned cap_idx) const
      -> std::optional<CharTimingResult>;

  std::vector<SegmentChar> _segment_chars;
  std::vector<PatternInfo> _pattern_infos;
  UniformValueLattice _length_lattice;
  UniformValueLattice _slew_lattice;
  UniformValueLattice _cap_lattice;
};

}  // namespace buffer_sizing
}  // namespace icts
