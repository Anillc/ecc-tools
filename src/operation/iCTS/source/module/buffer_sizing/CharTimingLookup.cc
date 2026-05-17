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
 * @file CharTimingLookup.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-17
 * @brief Characterization-backed timing lookup implementation.
 */

#include "buffer_sizing/CharTimingLookup.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "characterization/CharBuilder.hh"

namespace icts::buffer_sizing {
namespace {

auto isFinitePositive(double value) -> bool
{
  return std::isfinite(value) && value > 0.0;
}

auto clampToIndex(double value, const UniformValueLattice& lattice) -> std::pair<unsigned, bool>
{
  if (!lattice.isValid()) {
    return {0U, false};
  }
  const auto observed = lattice.tryObservedIndex(value);
  if (observed.has_value()) {
    return {*observed, false};
  }
  if (value <= 0.0) {
    return {1U, true};
  }
  return {lattice.steps(), true};
}

auto coveringLengthIndex(double length_um, const UniformValueLattice& lattice) -> unsigned
{
  if (!lattice.isValid() || length_um <= 0.0) {
    return 0U;
  }
  return lattice.coveringIndex(length_um);
}

auto lowerIndexForValue(double value, const UniformValueLattice& lattice) -> unsigned
{
  if (!lattice.isValid()) {
    return 0U;
  }
  if (value <= lattice.stepValue()) {
    return 1U;
  }
  if (value >= lattice.maxValue()) {
    return lattice.steps();
  }
  const auto raw_index = static_cast<unsigned>(std::floor(value / lattice.stepValue()));
  return std::clamp(raw_index, 1U, lattice.steps());
}

auto clampToValue(double value, const UniformValueLattice& lattice) -> std::pair<double, bool>
{
  if (!lattice.isValid()) {
    return {0.0, false};
  }
  const double min_value = lattice.stepValue();
  const double max_value = lattice.maxValue();
  if (value < min_value) {
    return {min_value, true};
  }
  if (value > max_value) {
    return {max_value, true};
  }
  return {value, false};
}

auto interpolate(double x, double x0, double x1, double y0, double y1) -> double
{
  if (std::abs(x1 - x0) <= kValueLatticeEpsilon) {
    return y0;
  }
  const double ratio = (x - x0) / (x1 - x0);
  return y0 + ((y1 - y0) * ratio);
}

auto bilinear(double x, double y, double x0, double x1, double y0, double y1, double q00, double q10, double q01, double q11) -> double
{
  const double lower = interpolate(x, x0, x1, q00, q10);
  const double upper = interpolate(x, x0, x1, q01, q11);
  return interpolate(y, y0, y1, lower, upper);
}

}  // namespace

CharTimingLookup::CharTimingLookup(const std::vector<SegmentChar>& segment_chars, const std::vector<BufferingPattern>& buffering_patterns,
                                   UniformValueLattice length_lattice, UniformValueLattice slew_lattice, UniformValueLattice cap_lattice)
    : _segment_chars(segment_chars), _length_lattice(length_lattice), _slew_lattice(slew_lattice), _cap_lattice(cap_lattice)
{
  _pattern_infos.reserve(buffering_patterns.size());
  for (const auto& pattern : buffering_patterns) {
    PatternInfo info;
    info.pattern_id = pattern.get_pattern_id();
    if (pattern.hasTerminalBranchBuffer() && !pattern.get_cell_masters().empty()) {
      info.arc_kind = CharArcKind::kTerminalBuffer;
      info.terminal_buffer_master = pattern.get_cell_masters().back();
    } else if (pattern.isWirePattern()) {
      info.arc_kind = CharArcKind::kWire;
    } else {
      continue;
    }
    _pattern_infos.push_back(std::move(info));
  }
}

auto CharTimingLookup::buildFromCharBuilder(const CharBuilder& char_builder) -> CharTimingLookup
{
  return CharTimingLookup(char_builder.get_segment_chars(), char_builder.get_buffering_patterns(), char_builder.get_length_lattice(),
                          char_builder.get_slew_lattice(), char_builder.get_cap_lattice());
}

auto CharTimingLookup::isReady() const -> bool
{
  return !_segment_chars.empty() && !_pattern_infos.empty() && _length_lattice.isValid() && _slew_lattice.isValid()
         && _cap_lattice.isValid();
}

auto CharTimingLookup::findPatternInfo(PatternId pattern_id) const -> const PatternInfo*
{
  const auto iter = std::ranges::find_if(
      _pattern_infos, [pattern_id](const PatternInfo& pattern_info) -> bool { return pattern_info.pattern_id == pattern_id; });
  return iter == _pattern_infos.end() ? nullptr : &(*iter);
}

auto CharTimingLookup::acceptsPattern(const SegmentChar& segment_char, const CharTimingQuery& query, unsigned length_idx) const -> bool
{
  if (segment_char.get_length_idx() != length_idx) {
    return false;
  }
  const auto* pattern_info = findPatternInfo(segment_char.get_pattern_id());
  if (pattern_info == nullptr || pattern_info->arc_kind != query.arc_kind) {
    return false;
  }
  if (query.arc_kind == CharArcKind::kTerminalBuffer) {
    return pattern_info->terminal_buffer_master == query.terminal_buffer_master;
  }
  return true;
}

auto CharTimingLookup::lookupSample(const CharTimingQuery& query, unsigned length_idx, unsigned slew_idx, unsigned cap_idx) const
    -> std::optional<CharTimingResult>
{
  const SegmentChar* best_char = nullptr;
  for (const auto& segment_char : _segment_chars) {
    if (!acceptsPattern(segment_char, query, length_idx) || segment_char.get_input_slew_idx() != slew_idx
        || segment_char.get_load_cap_idx() != cap_idx) {
      continue;
    }
    if (best_char == nullptr || segment_char.get_delay() < best_char->get_delay()) {
      best_char = &segment_char;
    }
  }
  if (best_char == nullptr) {
    return std::nullopt;
  }
  return CharTimingResult{
      .success = true,
      .clamped = false,
      .failure_reason = "",
      .requested_length_um = query.length_um,
      .length_idx = length_idx,
      .input_slew_ns = _slew_lattice.valueForIndex(slew_idx),
      .load_cap_pf = _cap_lattice.valueForIndex(cap_idx),
      .driven_cap_pf = _cap_lattice.valueForIndex(best_char->get_driven_cap_idx()),
      .delay_ns = best_char->get_delay(),
      .output_slew_ns = _slew_lattice.valueForIndex(best_char->get_output_slew_idx()),
  };
}

auto CharTimingLookup::lookupAtLengthIndex(const CharTimingQuery& query, unsigned length_idx, bool length_clamped) const -> CharTimingResult
{
  const auto [clamped_slew, slew_clamped] = clampToValue(query.input_slew_ns, _slew_lattice);
  const auto [clamped_cap, cap_clamped] = clampToValue(query.load_cap_pf, _cap_lattice);
  if (length_idx == 0U || clamped_slew <= 0.0 || clamped_cap <= 0.0) {
    return CharTimingResult{.success = false, .failure_reason = "invalid_lattice"};
  }

  const unsigned slew_low = lowerIndexForValue(clamped_slew, _slew_lattice);
  const unsigned cap_low = lowerIndexForValue(clamped_cap, _cap_lattice);
  const unsigned slew_high = std::min(slew_low + 1U, _slew_lattice.steps());
  const unsigned cap_high = std::min(cap_low + 1U, _cap_lattice.steps());

  const auto q00 = lookupSample(query, length_idx, slew_low, cap_low);
  const auto q10 = lookupSample(query, length_idx, slew_high, cap_low);
  const auto q01 = lookupSample(query, length_idx, slew_low, cap_high);
  const auto q11 = lookupSample(query, length_idx, slew_high, cap_high);
  if (!q00.has_value() || !q10.has_value() || !q01.has_value() || !q11.has_value()) {
    return CharTimingResult{
        .success = false,
        .clamped = length_clamped || slew_clamped || cap_clamped,
        .failure_reason = "missing_lattice_sample",
        .requested_length_um = query.length_um,
        .length_idx = length_idx,
    };
  }

  const double slew0 = _slew_lattice.valueForIndex(slew_low);
  const double slew1 = _slew_lattice.valueForIndex(slew_high);
  const double cap0 = _cap_lattice.valueForIndex(cap_low);
  const double cap1 = _cap_lattice.valueForIndex(cap_high);
  return CharTimingResult{
      .success = true,
      .clamped = length_clamped || slew_clamped || cap_clamped,
      .failure_reason = "",
      .requested_length_um = query.length_um,
      .length_idx = length_idx,
      .input_slew_ns = clamped_slew,
      .load_cap_pf = clamped_cap,
      .driven_cap_pf = bilinear(clamped_slew, clamped_cap, slew0, slew1, cap0, cap1, q00->driven_cap_pf, q10->driven_cap_pf,
                                q01->driven_cap_pf, q11->driven_cap_pf),
      .delay_ns = bilinear(clamped_slew, clamped_cap, slew0, slew1, cap0, cap1, q00->delay_ns, q10->delay_ns, q01->delay_ns, q11->delay_ns),
      .output_slew_ns = bilinear(clamped_slew, clamped_cap, slew0, slew1, cap0, cap1, q00->output_slew_ns, q10->output_slew_ns,
                                 q01->output_slew_ns, q11->output_slew_ns),
  };
}

auto CharTimingLookup::lookupComposed(const CharTimingQuery& query, unsigned requested_length_idx) const -> CharTimingResult
{
  if (requested_length_idx == 0U || requested_length_idx <= _length_lattice.steps()) {
    const auto [length_idx, length_clamped] = clampToIndex(query.length_um, _length_lattice);
    return lookupAtLengthIndex(query, length_idx, length_clamped);
  }

  constexpr unsigned unit_length_idx = 1U;
  const double unit_length_um = _length_lattice.valueForIndex(unit_length_idx);
  auto unit_load = lookupAtLengthIndex(
      CharTimingQuery{
          .arc_kind = CharArcKind::kWire,
          .terminal_buffer_master = "",
          .length_um = unit_length_um,
          .input_slew_ns = query.input_slew_ns,
          .load_cap_pf = query.load_cap_pf,
      },
      unit_length_idx, false);
  if (!unit_load.success) {
    return unit_load;
  }
  if (unit_load.driven_cap_pf <= 0.0 || !std::isfinite(unit_load.driven_cap_pf)) {
    return CharTimingResult{.success = false, .failure_reason = "invalid_unit_driven_cap"};
  }

  double current_slew_ns = query.input_slew_ns;
  double delay_ns = 0.0;
  bool clamped = true;
  for (unsigned index = 0U; index < requested_length_idx; ++index) {
    const bool is_terminal_unit = index + 1U == requested_length_idx;
    const auto unit_result = lookupAtLengthIndex(
        CharTimingQuery{
            .arc_kind = is_terminal_unit ? query.arc_kind : CharArcKind::kWire,
            .terminal_buffer_master = is_terminal_unit ? query.terminal_buffer_master : "",
            .length_um = unit_length_um,
            .input_slew_ns = current_slew_ns,
            .load_cap_pf = is_terminal_unit ? query.load_cap_pf : unit_load.driven_cap_pf,
        },
        unit_length_idx, false);
    if (!unit_result.success) {
      return unit_result;
    }
    clamped = clamped || unit_result.clamped;
    delay_ns += std::max(0.0, unit_result.delay_ns);
    current_slew_ns = std::max(0.0, unit_result.output_slew_ns);
  }

  return CharTimingResult{
      .success = true,
      .clamped = clamped,
      .failure_reason = "",
      .requested_length_um = query.length_um,
      .length_idx = std::numeric_limits<unsigned>::max(),
      .input_slew_ns = query.input_slew_ns,
      .load_cap_pf = query.load_cap_pf,
      .driven_cap_pf = unit_load.driven_cap_pf,
      .delay_ns = delay_ns,
      .output_slew_ns = current_slew_ns,
  };
}

auto CharTimingLookup::lookup(const CharTimingQuery& query) const -> CharTimingResult
{
  if (!isReady()) {
    return CharTimingResult{.success = false, .failure_reason = "lookup_not_ready"};
  }
  if (!isFinitePositive(query.length_um) || !isFinitePositive(query.input_slew_ns) || !isFinitePositive(query.load_cap_pf)) {
    return CharTimingResult{.success = false, .failure_reason = "invalid_query_value"};
  }
  if (query.arc_kind == CharArcKind::kTerminalBuffer && query.terminal_buffer_master.empty()) {
    return CharTimingResult{.success = false, .failure_reason = "missing_terminal_buffer_master"};
  }

  return lookupComposed(query, coveringLengthIndex(query.length_um, _length_lattice));
}

}  // namespace icts::buffer_sizing
