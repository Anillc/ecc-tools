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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CharBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-18
 * @brief Segment characterization builder implementation.
 */

#include "CharBuilder.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharCore.hh"
#include "Log.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace icts {
namespace {

constexpr std::uint64_t kTopologyPresentMask = 1ULL;
constexpr double kPercentFactor = 100.0;
constexpr double kCharClockPeriodNs = 10.0;
constexpr double kCapFeasibilityEpsilonPf = 1e-6;
constexpr unsigned kMaxTopologySlots = std::numeric_limits<std::uint64_t>::digits - 1U;
constexpr std::size_t kCharProgressLogStride = 32U;
constexpr bool kEnableCharPowerSampling = true;

enum class ResolutionSource
{
  kRuntimeConfig,
  kLibertyPinLimit,
  kLibertyTableAxis,
  kAutoDerived,
  kUnresolved
};

struct ResolvedValue
{
  double value = 0.0;
  ResolutionSource source = ResolutionSource::kUnresolved;
  std::string detail;
};

auto toResolutionSourceName(ResolutionSource source) -> const char*
{
  switch (source) {
    case ResolutionSource::kRuntimeConfig:
      return "runtime_config";
    case ResolutionSource::kLibertyPinLimit:
      return "liberty_pin_limit";
    case ResolutionSource::kLibertyTableAxis:
      return "liberty_table_axis";
    case ResolutionSource::kAutoDerived:
      return "auto_derived";
    case ResolutionSource::kUnresolved:
      return "unresolved";
  }
  return "unresolved";
}

auto formatFixed(double value, int precision = 4) -> std::string
{
  return logformat::FormatFixed(value, precision);
}

auto logInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void
{
  schema::EmitTable(title, headers, rows);
}

auto formatOptionalWireWidth(std::optional<double> wire_width_um) -> std::string
{
  return wire_width_um.has_value() ? logformat::FormatWithUnit(*wire_width_um, "um") : "library_default";
}

auto makeDenseWireLengthIndices(unsigned iterations) -> std::vector<unsigned>
{
  std::vector<unsigned> indices;
  indices.reserve(iterations);
  for (unsigned length_idx = 1U; length_idx <= iterations; ++length_idx) {
    indices.push_back(length_idx);
  }
  return indices;
}

auto normalizeWireLengthIndices(std::vector<unsigned> indices) -> std::vector<unsigned>
{
  indices.erase(std::remove(indices.begin(), indices.end(), 0U), indices.end());
  std::ranges::sort(indices);
  const auto unique_tail = std::ranges::unique(indices);
  indices.erase(unique_tail.begin(), unique_tail.end());
  return indices;
}

auto clampWireLengthIndices(std::vector<unsigned> indices, unsigned max_length_idx) -> std::pair<std::vector<unsigned>, bool>
{
  indices = normalizeWireLengthIndices(std::move(indices));
  const auto retained_end
      = std::remove_if(indices.begin(), indices.end(), [&](unsigned length_idx) { return length_idx > max_length_idx; });
  const bool truncated = retained_end != indices.end();
  indices.erase(retained_end, indices.end());
  return {std::move(indices), truncated};
}

auto makeWireLengths(double unit_um, const std::vector<unsigned>& indices) -> std::vector<double>
{
  std::vector<double> wire_lengths_um;
  wire_lengths_um.reserve(indices.size());
  for (const unsigned length_idx : indices) {
    wire_lengths_um.push_back(static_cast<double>(length_idx) * unit_um);
  }
  return wire_lengths_um;
}

auto hasTerminalLatticeBuffer(double wire_length_um, double length_unit_um, unsigned num_slots, std::uint64_t topology_bits_value) -> bool
{
  if (wire_length_um <= 0.0 || length_unit_um <= 0.0 || num_slots == 0U) {
    return false;
  }

  const unsigned terminal_slot_index = num_slots - 1U;
  if (((topology_bits_value >> terminal_slot_index) & kTopologyPresentMask) == 0U) {
    return false;
  }

  const double terminal_slot_boundary_um = std::min(static_cast<double>(num_slots) * length_unit_um, wire_length_um);
  return std::abs(terminal_slot_boundary_um - wire_length_um) <= kValueLatticeEpsilon;
}

auto resolveRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  return static_cast<int>(routing_layers.front());
}

auto resolveWireWidth() -> std::optional<double>
{
  const double wire_width = CONFIG_INST.get_wire_width();
  return wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
}

// Output-port cap limit is the primary drive-strength proxy; table-axis max is the fallback.
auto resolveBufferDriveCap(const std::string& cell_master) -> double
{
  double max_cap = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (max_cap <= 0.0) {
    max_cap = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return max_cap;
}

auto resolveMaxSlew() -> ResolvedValue
{
  const double configured_max_slew_ns = CONFIG_INST.get_max_buf_tran();
  if (CONFIG_INST.has_max_buf_tran() && configured_max_slew_ns > 0.0) {
    return ResolvedValue{
        .value = configured_max_slew_ns,
        .source = ResolutionSource::kRuntimeConfig,
        .detail = "Config.max_buf_tran",
    };
  }

  double liberty_port_min_slew = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double slew_limit = STA_ADAPTER_INST.queryCellInPinSlewLimit(cell_master);
    if (slew_limit > 0.0) {
      liberty_port_min_slew = std::min(liberty_port_min_slew, slew_limit);
      found_port_limit = true;
    }
  }
  if (found_port_limit) {
    return ResolvedValue{
        .value = liberty_port_min_slew,
        .source = ResolutionSource::kLibertyPinLimit,
        .detail = "minimum liberty input pin slew limit",
    };
  }

  double liberty_table_min_slew = std::numeric_limits<double>::infinity();
  bool found_table_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double table_axis_max = STA_ADAPTER_INST.queryCellInPinSlewTableAxisMax(cell_master);
    if (table_axis_max > 0.0) {
      liberty_table_min_slew = std::min(liberty_table_min_slew, table_axis_max);
      found_table_limit = true;
    }
  }
  if (found_table_limit) {
    return ResolvedValue{
        .value = liberty_table_min_slew,
        .source = ResolutionSource::kLibertyTableAxis,
        .detail = "minimum liberty input slew table-axis max",
    };
  }

  LOG_WARNING << "CharBuilder: failed to resolve max_slew from Config/liberty limits/liberty tables";
  return ResolvedValue{
      .value = 0.0,
      .source = ResolutionSource::kUnresolved,
      .detail = "missing Config/liberty slew limits",
  };
}

auto resolveMaxCap() -> ResolvedValue
{
  const double configured_max_cap_pf = CONFIG_INST.get_max_cap();
  if (CONFIG_INST.has_max_cap() && configured_max_cap_pf > 0.0) {
    return ResolvedValue{
        .value = configured_max_cap_pf,
        .source = ResolutionSource::kRuntimeConfig,
        .detail = "Config.max_cap",
    };
  }

  double liberty_port_min_cap = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double cap_limit = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
    if (cap_limit > 0.0) {
      liberty_port_min_cap = std::min(liberty_port_min_cap, cap_limit);
      found_port_limit = true;
    }
  }
  if (found_port_limit) {
    return ResolvedValue{
        .value = liberty_port_min_cap,
        .source = ResolutionSource::kLibertyPinLimit,
        .detail = "minimum liberty output pin cap limit",
    };
  }

  double liberty_table_min_cap = std::numeric_limits<double>::infinity();
  bool found_table_limit = false;
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    const double table_axis_max = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
    if (table_axis_max > 0.0) {
      liberty_table_min_cap = std::min(liberty_table_min_cap, table_axis_max);
      found_table_limit = true;
    }
  }
  if (found_table_limit) {
    return ResolvedValue{
        .value = liberty_table_min_cap,
        .source = ResolutionSource::kLibertyTableAxis,
        .detail = "minimum liberty output cap table-axis max",
    };
  }

  LOG_WARNING << "CharBuilder: failed to resolve max_cap from Config/liberty limits/liberty tables";
  return ResolvedValue{
      .value = 0.0,
      .source = ResolutionSource::kUnresolved,
      .detail = "missing Config/liberty cap limits",
  };
}

auto collectSortedBuffers() -> std::vector<CharBufferInfo>
{
  std::vector<CharBufferInfo> sorted_buffers;
  const auto& buffer_types = CONFIG_INST.get_buffer_types();
  if (buffer_types.empty()) {
    LOG_WARNING << "CharBuilder: no buffer types configured in Config";
    return sorted_buffers;
  }

  for (const auto& cell_master : buffer_types) {
    const double max_cap = resolveBufferDriveCap(cell_master);
    if (max_cap <= 0.0) {
      LOG_WARNING << "CharBuilder: buffer " << cell_master << " has invalid max_cap (" << max_cap << " pF), skipped";
      continue;
    }

    const double input_cap = STA_ADAPTER_INST.queryCharInputPinCap(cell_master);
    auto [in_pin, out_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
    if (in_pin.empty() || out_pin.empty()) {
      LOG_WARNING << "CharBuilder: buffer " << cell_master << " has unresolved port names, skipped";
      continue;
    }

    sorted_buffers.push_back(CharBufferInfo{
        .cell_master = cell_master,
        .max_cap_pf = max_cap,
        .input_cap_pf = input_cap,
        .input_pin = std::move(in_pin),
        .output_pin = std::move(out_pin),
    });
  }

  std::ranges::sort(sorted_buffers, [](const CharBufferInfo& lhs_buffer_info, const CharBufferInfo& rhs_buffer_info) -> bool {
    return lhs_buffer_info.max_cap_pf < rhs_buffer_info.max_cap_pf;
  });

  const double buffer_redundancy_pct = CONFIG_INST.get_char_buf_redundancy_pct();
  if (buffer_redundancy_pct > 0.0 && sorted_buffers.size() > 1U) {
    std::vector<CharBufferInfo> filtered_buffers;
    filtered_buffers.push_back(sorted_buffers.front());
    for (std::size_t buffer_index = 1; buffer_index < sorted_buffers.size(); ++buffer_index) {
      const double prev_cap = filtered_buffers.back().max_cap_pf;
      const double curr_cap = sorted_buffers.at(buffer_index).max_cap_pf;
      if (prev_cap <= 0.0 || (curr_cap - prev_cap) / prev_cap >= buffer_redundancy_pct) {
        filtered_buffers.push_back(sorted_buffers.at(buffer_index));
      } else {
        LOG_INFO << "CharBuilder: removed near-neighbor buffer " << sorted_buffers.at(buffer_index).cell_master << " (max_cap=" << curr_cap
                 << " pF, gap=" << ((curr_cap - prev_cap) / prev_cap * kPercentFactor) << "%)";
      }
    }
    sorted_buffers = std::move(filtered_buffers);
  }

  return sorted_buffers;
}

auto resolveWireLengthUnitUm(const std::vector<CharBufferInfo>& sorted_buffers) -> ResolvedValue
{
  const double configured_unit_um = CONFIG_INST.get_wire_length_unit_um();
  if (configured_unit_um > 0.0) {
    return ResolvedValue{
        .value = configured_unit_um,
        .source = ResolutionSource::kRuntimeConfig,
        .detail = "active runtime wire_length_unit_um",
    };
  }

  // choose the strongest usable buffer and use 10x cell height.
  double strongest_cap_pf = -1.0;
  double strongest_height_um = 0.0;
  std::string strongest_master;
  for (const auto& buffer_info : sorted_buffers) {
    const double cell_height_um = STA_ADAPTER_INST.queryCellHeightUm(buffer_info.cell_master);
    if (cell_height_um <= 0.0) {
      LOG_WARNING << "CharBuilder: cannot derive wire_length_unit from " << buffer_info.cell_master
                  << " because its physical height is unavailable";
      continue;
    }

    if (buffer_info.max_cap_pf >= strongest_cap_pf) {
      strongest_cap_pf = buffer_info.max_cap_pf;
      strongest_height_um = cell_height_um;
      strongest_master = buffer_info.cell_master;
    }
  }

  if (strongest_height_um <= 0.0) {
    LOG_WARNING << "CharBuilder: failed to resolve wire_length_unit from Config or strongest buffer height";
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "CharBuilder",
                           "wire_length_unit_um is absent in active runtime config and auto-derivation failed.",
                           {{"reason", "no_valid_strongest_buffer_height"}});
    return ResolvedValue{
        .value = 0.0,
        .source = ResolutionSource::kUnresolved,
        .detail = "no valid strongest buffer height",
    };
  }

  const double fallback_unit_um = strongest_height_um * 10.0;
  schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "CharBuilder",
                         "wire_length_unit_um is absent in active runtime config, fallback to auto-derived strongest-buffer height.",
                         {{"strongest_buffer", strongest_master},
                          {"buffer_height_um", logformat::FormatWithUnit(strongest_height_um, "um")},
                          {"derived_wire_length_unit_um", logformat::FormatWithUnit(fallback_unit_um, "um")}});
  return ResolvedValue{
      .value = fallback_unit_um,
      .source = ResolutionSource::kAutoDerived,
      .detail = "strongest buffer " + strongest_master + " height=" + logformat::FormatWithUnit(strongest_height_um, "um") + " x10",
  };
}

}  // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

auto CharBuilder::init() -> void
{
  init(InitOptions{});
}

auto CharBuilder::init(const InitOptions& options) -> void
{
  schema::ScopedStage init_stage("CharBuilder", "initialization");

  _segment_chars.clear();
  _buffering_patterns.clear();
  _wire_length_indices.clear();
  _temp_inst_names.clear();
  _temp_net_names.clear();
  _source_inst_name.clear();
  _source_in_pin.clear();
  _sink_inst_name.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
  _char_clock_name.clear();
  _next_pattern_id = 0U;
  _char_circuit_id = 0U;
  _output_slew_overflow_samples = 0U;
  _driven_cap_overflow_samples = 0U;
  _driven_cap_overflow_load_points = 0U;
  _max_observed_output_slew_ns = 0.0;
  _max_observed_output_slew_idx = 0U;
  _max_observed_driven_cap_pf = 0.0;
  _max_observed_driven_cap_idx = 0U;

  _sorted_buffers = collectSortedBuffers();
  const ResolvedValue max_slew_resolution = resolveMaxSlew();
  const ResolvedValue max_cap_resolution = resolveMaxCap();
  const ResolvedValue wire_length_unit_resolution = resolveWireLengthUnitUm(_sorted_buffers);
  _max_slew = max_slew_resolution.value;
  _max_cap = max_cap_resolution.value;
  _length_unit_um = options.wire_length_unit_um.value_or(wire_length_unit_resolution.value);
  if (options.wire_length_unit_um.has_value()) {
    _wire_length_unit_source = "caller_override";
    _wire_length_unit_detail = "CharBuilder::InitOptions.wire_length_unit_um";
  } else {
    _wire_length_unit_source = toResolutionSourceName(wire_length_unit_resolution.source);
    _wire_length_unit_detail = wire_length_unit_resolution.detail;
  }
  _wire_length_iterations = std::max(1U, options.wire_length_iterations.value_or(CONFIG_INST.get_wire_length_iterations()));
  _slew_steps = CONFIG_INST.get_slew_steps();
  _cap_steps = CONFIG_INST.get_cap_steps();
  bool wire_length_indices_truncated = false;
  if (options.wire_length_indices.has_value()) {
    auto [clamped_indices, truncated] = clampWireLengthIndices(*options.wire_length_indices, _wire_length_iterations);
    _wire_length_indices = std::move(clamped_indices);
    wire_length_indices_truncated = truncated;
  }
  if (_wire_length_indices.empty()) {
    _wire_length_indices = makeDenseWireLengthIndices(_wire_length_iterations);
  }
  _wire_lengths_um = makeWireLengths(_length_unit_um, _wire_length_indices);
  _max_length = _wire_lengths_um.empty() ? 0.0 : _wire_lengths_um.back();
  _slews_to_test = get_slew_lattice().sampleValues();
  _loads_to_test = get_cap_lattice().sampleValues();
  _routing_layer = resolveRoutingLayer();
  _wire_width = resolveWireWidth();
  _sink_input_cap_pf = _sorted_buffers.empty() ? 0.0 : _sorted_buffers.front().input_cap_pf;

  CONFIG_INST.emitRuntimeConfigReport("CharBuilder Runtime Configuration");

  if (wire_length_indices_truncated) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "CharBuilder",
                           "wire_length_indices exceeded wire_length_iterations; clamp direct characterization to the configured max iter.",
                           {
                               {"wire_length_iterations", std::to_string(_wire_length_iterations)},
                               {"wire_length_points", std::to_string(_wire_length_indices.size())},
                           });
  }

  const logformat::TableRows parameter_rows = {
      {"max_slew_ns", logformat::FormatWithUnit(_max_slew, "ns"), toResolutionSourceName(max_slew_resolution.source),
       max_slew_resolution.detail},
      {"max_cap_pf", logformat::FormatWithUnit(_max_cap, "pF"), toResolutionSourceName(max_cap_resolution.source),
       max_cap_resolution.detail},
      {"wire_length_unit_um", logformat::FormatWithUnit(_length_unit_um, "um"),
       options.wire_length_unit_um.has_value() ? "caller_override" : toResolutionSourceName(wire_length_unit_resolution.source),
       options.wire_length_unit_um.has_value() ? "CharBuilder::InitOptions.wire_length_unit_um" : wire_length_unit_resolution.detail},
      {"wire_length_iterations", std::to_string(_wire_length_iterations),
       options.wire_length_iterations.has_value() ? "caller_override" : "runtime_config",
       options.wire_length_iterations.has_value() ? "CharBuilder::InitOptions.wire_length_iterations" : "Config.wire_length_iterations"},
      {"wire_length_points", std::to_string(_wire_length_indices.size()),
       options.wire_length_indices.has_value() ? "caller_override" : "dense_prefix",
       options.wire_length_indices.has_value() ? "CharBuilder::InitOptions.wire_length_indices"
                                               : "characterize every length_idx in [1, iterations]"},
      {"routing_layer", std::to_string(_routing_layer), "runtime_config", "first routing layer or fallback=1"},
      {"wire_width_um", formatOptionalWireWidth(_wire_width), "runtime_config", "explicit width override or technology default"},
  };
  logInfoTable("CharBuilder Initialization Parameters", {"Parameter", "Value", "Source", "Detail"}, parameter_rows);

  STA_ADAPTER_INST.emitUnitWireRcReport("CharBuilder Routing / Wire RC", _routing_layer, _wire_width);

  const logformat::TableRows sweep_rows = {
      {"wire_lengths", std::to_string(_wire_lengths_um.size()), std::to_string(_wire_length_iterations),
       logformat::FormatWithUnit(_max_length, "um")},
      {"input_slews", std::to_string(_slews_to_test.size()), std::to_string(_slew_steps), logformat::FormatWithUnit(_max_slew, "ns")},
      {"load_caps", std::to_string(_loads_to_test.size()), std::to_string(_cap_steps), logformat::FormatWithUnit(_max_cap, "pF")},
  };
  logInfoTable("CharBuilder Sweep Grids", {"Grid", "Points", "Steps", "Upper Bound"}, sweep_rows);

  logformat::TableRows buffer_rows;
  buffer_rows.reserve(_sorted_buffers.size());
  for (std::size_t buffer_index = 0; buffer_index < _sorted_buffers.size(); ++buffer_index) {
    buffer_rows.push_back({
        std::to_string(buffer_index),
        _sorted_buffers.at(buffer_index).cell_master,
        logformat::FormatWithUnit(_sorted_buffers.at(buffer_index).max_cap_pf, "pF"),
        logformat::FormatWithUnit(_sorted_buffers.at(buffer_index).input_cap_pf, "pF"),
    });
  }
  if (!buffer_rows.empty()) {
    logInfoTable("CharBuilder Sorted Buffers", {"Index", "Cell Master", "Max Cap", "Input Cap"}, buffer_rows);
  }
  init_stage.finish({
      {"buffers", std::to_string(_sorted_buffers.size())},
      {"wire_lengths", std::to_string(_wire_lengths_um.size())},
      {"slews", std::to_string(_slews_to_test.size())},
      {"loads", std::to_string(_loads_to_test.size())},
  });
}

auto CharBuilder::build() -> void
{
  schema::ScopedStage build_stage("CharBuilder", "build");
  logformat::TableRows progress_rows;

  _output_slew_overflow_samples = 0U;
  _driven_cap_overflow_samples = 0U;
  _driven_cap_overflow_load_points = 0U;
  _max_observed_output_slew_ns = 0.0;
  _max_observed_output_slew_idx = 0U;
  _max_observed_driven_cap_pf = 0.0;
  _max_observed_driven_cap_idx = 0U;

  if (_sorted_buffers.empty()) {
    LOG_WARNING << "CharBuilder: no usable buffers remain after Config/liberty filtering, skip characterization build";
    build_stage.skip({{"reason", "no_usable_buffers"}});
    return;
  }
  if (_wire_lengths_um.empty()) {
    LOG_ERROR << "CharBuilder: no wire lengths to enumerate, aborting build";
    build_stage.skip({{"reason", "no_wire_lengths"}}, "failed");
    return;
  }
  if (_slews_to_test.empty() || _loads_to_test.empty()) {
    LOG_WARNING << "CharBuilder: characterization limits are unresolved"
                << " (max_slew_ns=" << _max_slew << ", max_cap_pf=" << _max_cap << "), skip characterization build";
    build_stage.skip({{"reason", "unresolved_characterization_limits"}});
    return;
  }

  // CharBuilder owns the char-only lifecycle so callers do not need to coordinate
  // STA graph resets around characterization.
  STA_ADAPTER_INST.initCharOnly();

  for (std::size_t wire_length_index = 0; wire_length_index < _wire_lengths_um.size(); ++wire_length_index) {
    const unsigned length_idx = _wire_length_indices.at(wire_length_index);
    const double wire_length_um = _wire_lengths_um.at(wire_length_index);
    const unsigned topology_slots = length_idx;
    const std::size_t estimated_patterns_per_wire_length = estimatePatternCountPerWireLength(wire_length_um);
    const std::size_t estimated_sta_samples_per_wire_length
        = estimated_patterns_per_wire_length * _loads_to_test.size() * _slews_to_test.size();
    const std::size_t char_count_before = _segment_chars.size();
    const std::size_t pattern_count_before = _buffering_patterns.size();
    BuildProgress build_progress;
    build_progress.wire_length_um = wire_length_um;
    build_progress.estimated_patterns = estimated_patterns_per_wire_length;
    build_progress.estimated_sta_samples = estimated_sta_samples_per_wire_length;
    build_stage.markRunning("wire_length=" + formatFixed(wire_length_um) + " um",
                            {
                                {"estimated_patterns", std::to_string(estimated_patterns_per_wire_length)},
                                {"estimated_sta_samples", std::to_string(estimated_sta_samples_per_wire_length)},
                                {"topology_slots", std::to_string(topology_slots)},
                            });
    enumerateWireLength(length_idx, wire_length_um, build_progress);

    progress_rows.push_back({
        formatFixed(wire_length_um) + " um",
        std::to_string(topology_slots),
        std::to_string(_segment_chars.size() - char_count_before),
        std::to_string(_buffering_patterns.size() - pattern_count_before),
        std::to_string(build_progress.feasible_patterns),
        std::to_string(build_progress.skipped_patterns_infeasible),
        std::to_string(build_progress.executed_sta_samples),
        std::to_string(build_progress.skipped_sta_samples),
        std::to_string(build_progress.output_slew_overflow_samples),
        std::to_string(build_progress.driven_cap_overflow_samples),
        std::to_string(build_progress.driven_cap_overflow_load_points),
    });

    _output_slew_overflow_samples += build_progress.output_slew_overflow_samples;
    _driven_cap_overflow_samples += build_progress.driven_cap_overflow_samples;
    _driven_cap_overflow_load_points += build_progress.driven_cap_overflow_load_points;
    _max_observed_output_slew_ns = std::max(_max_observed_output_slew_ns, build_progress.max_observed_output_slew_ns);
    _max_observed_output_slew_idx = std::max(_max_observed_output_slew_idx, build_progress.max_observed_output_slew_idx);
    _max_observed_driven_cap_pf = std::max(_max_observed_driven_cap_pf, build_progress.max_observed_driven_cap_pf);
    _max_observed_driven_cap_idx = std::max(_max_observed_driven_cap_idx, build_progress.max_observed_driven_cap_idx);

    LOG_INFO << "CharBuilder: [RUNNING] wire_length=" << formatFixed(wire_length_um)
             << " um, generated_chars=" << (_segment_chars.size() - char_count_before)
             << ", generated_patterns=" << (_buffering_patterns.size() - pattern_count_before)
             << ", feasible_patterns=" << build_progress.feasible_patterns
             << ", skipped_patterns=" << build_progress.skipped_patterns_infeasible
             << ", executed_sta_samples=" << build_progress.executed_sta_samples
             << ", skipped_sta_samples=" << build_progress.skipped_sta_samples
             << ", output_slew_overflow_samples=" << build_progress.output_slew_overflow_samples
             << ", driven_cap_overflow_samples=" << build_progress.driven_cap_overflow_samples
             << ", driven_cap_overflow_load_points=" << build_progress.driven_cap_overflow_load_points;
  }

  if (!progress_rows.empty()) {
    logInfoTable(
        "CharBuilder Sweep Progress",
        {"Wire Length", "Topology Slots", "Generated Chars", "Generated Patterns", "Feasible Patterns", "Skipped Patterns",
         "Executed STA Samples", "Skipped STA Samples", "Output Slew Overflow", "Driven Cap Overflow", "Driven Cap Overflow Load Points"},
        progress_rows);
  }

  const schema::KeyValueFields observed_fields = {
      {"segment_chars", std::to_string(_segment_chars.size())},
      {"output_slew_overflow_samples", std::to_string(_output_slew_overflow_samples)},
      {"max_observed_output_slew_ns", logformat::FormatWithUnit(_max_observed_output_slew_ns, "ns")},
      {"max_observed_output_slew_idx", std::to_string(_max_observed_output_slew_idx)},
      {"slew_steps", std::to_string(_slew_steps)},
      {"driven_cap_overflow_samples", std::to_string(_driven_cap_overflow_samples)},
      {"driven_cap_overflow_load_points", std::to_string(_driven_cap_overflow_load_points)},
      {"max_observed_driven_cap_pf", logformat::FormatWithUnit(_max_observed_driven_cap_pf, "pF")},
      {"max_observed_driven_cap_idx", std::to_string(_max_observed_driven_cap_idx)},
      {"cap_steps", std::to_string(_cap_steps)},
  };
  SCHEMA_WRITER_INST.emitKeyValueTable("CharBuilder Observed Sample Bounds", observed_fields);

  build_stage.finish({
      {"segment_chars", std::to_string(_segment_chars.size())},
      {"patterns", std::to_string(_buffering_patterns.size())},
      {"output_slew_overflow_samples", std::to_string(_output_slew_overflow_samples)},
      {"driven_cap_overflow_samples", std::to_string(_driven_cap_overflow_samples)},
      {"driven_cap_overflow_load_points", std::to_string(_driven_cap_overflow_load_points)},
  });
  STA_ADAPTER_INST.finishCharOnly();
}

// ---------------------------------------------------------------------------
// Pattern enumeration
// ---------------------------------------------------------------------------

auto CharBuilder::calcTopologySlotCount(double wire_length_um) const -> unsigned
{
  const auto length_idx = get_length_lattice().tryObservedIndex(wire_length_um);
  auto slot_count = length_idx.value_or(get_length_lattice().coveringIndex(wire_length_um));
  if (slot_count > kMaxTopologySlots) {
    static bool has_logged_slot_clamp = false;
    if (!has_logged_slot_clamp) {
      LOG_WARNING << "CharBuilder: slot count exceeds topology bit capacity, clamp to " << kMaxTopologySlots;
      has_logged_slot_clamp = true;
    }
    slot_count = kMaxTopologySlots;
  }
  return slot_count;
}

auto CharBuilder::countSelectedSlots(TopologyBits topology_bits) -> unsigned
{
  unsigned slot_count = 0U;
  auto remaining_bits = topology_bits.value;
  while (remaining_bits != 0U) {
    slot_count += static_cast<unsigned>(remaining_bits & 1U);
    remaining_bits >>= 1U;
  }
  return slot_count;
}

auto CharBuilder::estimatePatternCountPerWireLength(double wire_length_um) const -> std::size_t
{
  const unsigned num_slots = calcTopologySlotCount(wire_length_um);
  LOG_FATAL_IF(num_slots >= std::numeric_limits<std::uint64_t>::digits)
      << "CharBuilder: buffer slot count " << num_slots << " exceeds topology bit capacity.";

  std::size_t total_patterns = 0;
  const std::uint64_t num_topologies = std::uint64_t{1} << num_slots;
  for (std::uint64_t topology_bits_value = 0; topology_bits_value < num_topologies; ++topology_bits_value) {
    const unsigned num_buffer_positions = countSelectedSlots(TopologyBits{topology_bits_value});
    total_patterns += (num_buffer_positions == 0U) ? 1U : getMonotonicComboCount(_sorted_buffers.size(), num_buffer_positions);
  }
  return total_patterns;
}

auto CharBuilder::enumerateWireLength(unsigned length_idx, double wire_length_um, BuildProgress& build_progress) -> void
{
  const unsigned num_slots = calcTopologySlotCount(wire_length_um);
  LOG_FATAL_IF(num_slots >= std::numeric_limits<std::uint64_t>::digits)
      << "CharBuilder: buffer slot count " << num_slots << " exceeds topology bit capacity.";

  const std::uint64_t num_topologies = std::uint64_t{1} << num_slots;
  for (std::uint64_t topology_bits_value = 0; topology_bits_value < num_topologies; ++topology_bits_value) {
    enumerateTopology(length_idx, wire_length_um, num_slots, TopologyBits{topology_bits_value}, build_progress);
  }
}

auto CharBuilder::enumerateTopology(unsigned length_idx, double wire_length_um, unsigned num_slots, TopologyBits topology_bits,
                                    BuildProgress& build_progress) -> void
{
  const TopologyDesc topo = buildTopologyDesc(wire_length_um, num_slots, topology_bits);
  const std::size_t num_buf_positions = topo.buffer_positions.size();

  if (num_buf_positions == 0) {
    const std::vector<std::string> empty_masters;
    characterizeTopology(length_idx, topo, empty_masters, build_progress);
    return;
  }

  const std::size_t num_buf_types = _sorted_buffers.size();
  if (num_buf_types == 0) {
    return;
  }

  std::vector<std::size_t> buf_indices(num_buf_positions, 0);
  while (true) {
    std::vector<std::string> buf_masters;
    buf_masters.reserve(num_buf_positions);
    for (const auto buffer_index : std::ranges::reverse_view(buf_indices)) {
      buf_masters.push_back(_sorted_buffers.at(buffer_index).cell_master);
    }

    characterizeTopology(length_idx, topo, buf_masters, build_progress);
    if (!advanceToNextMonotonic(buf_indices, num_buf_types)) {
      break;
    }
  }
}

auto CharBuilder::getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t
{
  if (num_buf_types == 0 || num_positions == 0) {
    return 0;
  }
  const std::size_t combination_n = num_buf_types + num_positions - 1;
  std::size_t combination_k = num_positions;
  combination_k = std::min(combination_k, combination_n - combination_k);
  std::size_t result = 1;
  for (std::size_t index = 0; index < combination_k; ++index) {
    result = result * (combination_n - index) / (index + 1);
  }
  return result;
}

auto CharBuilder::advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool
{
  if (buf_indices.empty() || num_buf_types == 0) {
    return false;
  }

  int position = static_cast<int>(buf_indices.size()) - 1;

  while (position >= 0) {
    const auto index = static_cast<std::size_t>(position);
    if (buf_indices.at(index) + 1 < num_buf_types) {
      ++buf_indices.at(index);
      for (std::size_t tail_index = index + 1; tail_index < buf_indices.size(); ++tail_index) {
        buf_indices.at(tail_index) = buf_indices.at(index);
      }
      return true;
    }
    --position;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Topology construction
// ---------------------------------------------------------------------------

auto CharBuilder::buildTopologyDesc(double wire_length_um, unsigned num_slots, TopologyBits topology_bits) const -> TopologyDesc
{
  TopologyDesc desc;
  desc.has_terminal_branch_buffer = hasTerminalLatticeBuffer(wire_length_um, _length_unit_um, num_slots, topology_bits.value);

  if (num_slots == 0U || _length_unit_um <= 0.0) {
    desc.wire_segments_um.push_back(wire_length_um);
    return desc;
  }

  // Slots are pinned to the active wire-length lattice.
  double previous_boundary_um = 0.0;

  for (unsigned slot_index = 0; slot_index < num_slots; ++slot_index) {
    const double slot_boundary_um = std::min((static_cast<double>(slot_index) + 1.0) * _length_unit_um, wire_length_um);
    if (((topology_bits.value >> slot_index) & kTopologyPresentMask) != 0U) {
      desc.buffer_positions.push_back(slot_index);
      desc.wire_segments_um.push_back(slot_boundary_um - previous_boundary_um);
      previous_boundary_um = slot_boundary_um;
    }
  }

  // Keep terminal segment so createCharCircuit always sees buffer_count + 1 nets.
  desc.wire_segments_um.push_back(std::max(0.0, wire_length_um - previous_boundary_um));

  return desc;
}

// ---------------------------------------------------------------------------
// Characterization measurement
// ---------------------------------------------------------------------------

auto CharBuilder::findBufferInfo(const std::string& cell_master) const -> const CharBufferInfo*
{
  auto it = std::ranges::find_if(_sorted_buffers,
                                 [&cell_master](const CharBufferInfo& info) -> bool { return info.cell_master == cell_master; });
  return it == _sorted_buffers.end() ? nullptr : &(*it);
}

auto CharBuilder::analyzePatternFeasibility(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) const
    -> PatternFeasibility
{
  if (_sorted_buffers.empty()) {
    return {};
  }

  // Intermediate stages must absorb wire plus next-buffer input cap locally.
  // Only the terminal stage contributes residual budget for external load sweep.
  const auto* source_buffer = &_sorted_buffers.back();
  const auto* sink_buffer = &_sorted_buffers.front();
  double max_external_load_pf = std::numeric_limits<double>::infinity();

  for (std::size_t segment_index = 0; segment_index < topo.wire_segments_um.size(); ++segment_index) {
    const CharBufferInfo* driver_buffer = nullptr;
    if (segment_index == 0U) {
      driver_buffer = source_buffer;
    } else {
      driver_buffer = findBufferInfo(buf_masters.at(segment_index - 1U));
    }
    if (driver_buffer == nullptr || driver_buffer->max_cap_pf <= 0.0) {
      return {};
    }

    const double wire_cap_pf = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.at(segment_index), _wire_width);
    const bool is_last_segment = (segment_index + 1U == topo.wire_segments_um.size());
    const CharBufferInfo* next_buffer = is_last_segment ? sink_buffer : findBufferInfo(buf_masters.at(segment_index));
    const double next_input_cap_pf = next_buffer != nullptr ? next_buffer->input_cap_pf : 0.0;
    if (!is_last_segment && next_input_cap_pf <= 0.0) {
      return {};
    }

    const double static_stage_cap_pf = wire_cap_pf + next_input_cap_pf;
    if (!is_last_segment && static_stage_cap_pf > driver_buffer->max_cap_pf + kCapFeasibilityEpsilonPf) {
      return {};
    }
    if (is_last_segment) {
      max_external_load_pf = std::min(max_external_load_pf, driver_buffer->max_cap_pf - static_stage_cap_pf);
    }
  }

  if (!std::isfinite(max_external_load_pf)) {
    return PatternFeasibility{.is_pattern_feasible = true, .max_load_pf = std::numeric_limits<double>::infinity()};
  }
  if (max_external_load_pf + kCapFeasibilityEpsilonPf < 0.0) {
    return {};
  }
  return PatternFeasibility{.is_pattern_feasible = true, .max_load_pf = max_external_load_pf};
}

auto CharBuilder::tryMakeStoredSampleIndices(unsigned input_slew_idx, unsigned load_cap_idx, double output_slew_ns, double driven_cap_pf,
                                             BuildProgress& build_progress) const -> std::optional<StoredSampleIndices>
{
  const auto slew_lattice = get_slew_lattice();
  const auto cap_lattice = get_cap_lattice();
  const unsigned output_slew_idx = slew_lattice.coveringIndex(output_slew_ns);
  const unsigned driven_cap_idx = cap_lattice.coveringIndex(driven_cap_pf);

  build_progress.max_observed_output_slew_ns = std::max(build_progress.max_observed_output_slew_ns, output_slew_ns);
  build_progress.max_observed_output_slew_idx = std::max(build_progress.max_observed_output_slew_idx, output_slew_idx);
  build_progress.max_observed_driven_cap_pf = std::max(build_progress.max_observed_driven_cap_pf, driven_cap_pf);
  build_progress.max_observed_driven_cap_idx = std::max(build_progress.max_observed_driven_cap_idx, driven_cap_idx);

  const auto observed_output_slew_idx = slew_lattice.tryObservedIndex(output_slew_ns);
  if (!observed_output_slew_idx.has_value()) {
    ++build_progress.output_slew_overflow_samples;
    return std::nullopt;
  }
  const auto observed_driven_cap_idx = cap_lattice.tryObservedIndex(driven_cap_pf);
  if (!observed_driven_cap_idx.has_value()) {
    ++build_progress.driven_cap_overflow_samples;
    return std::nullopt;
  }

  return StoredSampleIndices{
      .input_slew_idx = input_slew_idx,
      .output_slew_idx = *observed_output_slew_idx,
      .driven_cap_idx = *observed_driven_cap_idx,
      .load_cap_idx = load_cap_idx,
  };
}

auto CharBuilder::characterizeTopology(unsigned length_idx, const TopologyDesc& topo, const std::vector<std::string>& buf_masters,
                                       BuildProgress& build_progress) -> void
{
  ++build_progress.evaluated_patterns;

  double total_length_um = 0.0;
  for (const double seg_len : topo.wire_segments_um) {
    total_length_um += seg_len;
  }

  std::vector<double> buffer_positions_norm;
  if (!topo.buffer_positions.empty() && total_length_um > 0.0) {
    double cumulative_um = 0.0;
    size_t buf_idx = 0;
    for (size_t seg = 0; seg < topo.wire_segments_um.size() && buf_idx < topo.buffer_positions.size(); ++seg) {
      cumulative_um += topo.wire_segments_um.at(seg);
      if (seg < topo.wire_segments_um.size() - 1) {
        const double normalized = cumulative_um / total_length_um;
        buffer_positions_norm.push_back(normalized);
        ++buf_idx;
      }
    }
  }
  if (topo.has_terminal_branch_buffer
      && (buffer_positions_norm.empty() || std::abs(buffer_positions_norm.back() - 1.0) > kValueLatticeEpsilon)) {
    buffer_positions_norm.push_back(1.0);
  }

  const PatternId pid = PatternId::segment(_next_pattern_id);
  BufferingPattern pattern(length_idx, pid, buffer_positions_norm, buf_masters, topo.has_terminal_branch_buffer);
  _buffering_patterns.push_back(std::move(pattern));

  const PatternFeasibility feasibility = analyzePatternFeasibility(topo, buf_masters);
  if (!feasibility.is_pattern_feasible) {
    ++build_progress.skipped_patterns_infeasible;
    if ((build_progress.evaluated_patterns % kCharProgressLogStride) == 0U) {
      LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_progress.evaluated_patterns << "/"
               << build_progress.estimated_patterns << " patterns"
               << " (feasible=" << build_progress.feasible_patterns << ", skipped=" << build_progress.skipped_patterns_infeasible
               << ", executed_sta_samples=" << build_progress.executed_sta_samples << ")";
    }
    ++_next_pattern_id;
    return;
  }
  ++build_progress.feasible_patterns;

  createCharCircuit(topo, buf_masters);
  STA_ADAPTER_INST.createCharClock(_source_out_pin, _char_clock_name, kCharClockPeriodNs);
  STA_ADAPTER_INST.prepareCharTimingContext(_source_in_pin, _source_out_pin, _sink_in_pin);

  std::vector<std::string> power_inst_names = _temp_inst_names;
  bool power_context_ready = false;
  if (kEnableCharPowerSampling) {
    power_context_ready = STA_ADAPTER_INST.prepareCharPower(power_inst_names, _temp_net_names, _source_in_pin);
    if (!power_context_ready) {
      LOG_WARNING << "CharBuilder: iPA characterization power is unavailable for this topology; affected samples use zero power.";
    }
  }

  for (const double load_pf : _loads_to_test) {
    if (load_pf > feasibility.max_load_pf + kCapFeasibilityEpsilonPf) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    const double effective_load = load_pf - _sink_input_cap_pf;
    if (effective_load < 0.0) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      continue;
    }

    double driven_cap_pf = 0.0;
    if (!buf_masters.empty()) {
      driven_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(buf_masters.front());
      driven_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, topo.wire_segments_um.front(), _wire_width);
    } else {
      driven_cap_pf = load_pf;
      for (const double seg_len_um : topo.wire_segments_um) {
        driven_cap_pf += STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);
      }
    }

    build_progress.max_observed_driven_cap_pf = std::max(build_progress.max_observed_driven_cap_pf, driven_cap_pf);
    build_progress.max_observed_driven_cap_idx
        = std::max(build_progress.max_observed_driven_cap_idx, get_cap_lattice().coveringIndex(driven_cap_pf));
    const auto driven_cap_idx = get_cap_lattice().tryObservedIndex(driven_cap_pf);
    if (!driven_cap_idx.has_value()) {
      ++build_progress.skipped_load_points;
      build_progress.skipped_sta_samples += _slews_to_test.size();
      build_progress.driven_cap_overflow_samples += _slews_to_test.size();
      ++build_progress.driven_cap_overflow_load_points;
      continue;
    }

    setCharParasitics(topo, effective_load);
    if (kEnableCharPowerSampling && power_context_ready) {
      const bool refreshed_power_load = STA_ADAPTER_INST.refreshCharPowerLoad();
      if (!refreshed_power_load) {
        LOG_WARNING << "CharBuilder: external iPA load refresh failed, remaining samples for this topology use zero power.";
        power_context_ready = false;
        STA_ADAPTER_INST.destroyCharPower();
      }
    }

    bool is_first_slew_sample_for_load = true;
    const unsigned load_cap_idx = get_cap_lattice().coveringIndex(load_pf);
    for (const double input_slew_ns : _slews_to_test) {
      if (is_first_slew_sample_for_load) {
        STA_ADAPTER_INST.prepareCharTimingSample();
      }

      // The source clock is rooted at the buffer output to exclude source-cell delay,
      // but the output slew/current must still be derived from the source buffer arc.
      if (is_first_slew_sample_for_load) {
        STA_ADAPTER_INST.setCharBufferInputSlew(input_slew_ns);
      } else {
        STA_ADAPTER_INST.setCharBufferInputSlewIncremental(input_slew_ns);
      }

      if (is_first_slew_sample_for_load) {
        STA_ADAPTER_INST.updateCharTimingSample();
      } else {
        STA_ADAPTER_INST.updateCharTimingIncrementalSample();
      }
      ++build_progress.executed_sta_samples;

      const double delay_ns = STA_ADAPTER_INST.queryCharClockAT(_char_clock_name);
      const unsigned input_slew_idx = get_slew_lattice().coveringIndex(input_slew_ns);

      const double output_slew_ns = STA_ADAPTER_INST.queryCharSlew();

      double power_w = 0.0;
      if (kEnableCharPowerSampling && power_context_ready) {
        const bool power_updated = STA_ADAPTER_INST.updateCharPower();
        if (power_updated) {
          power_w = STA_ADAPTER_INST.queryCharPower();
        } else {
          LOG_WARNING << "CharBuilder: iPA characterization update failed, remaining samples for this topology use zero power.";
          power_context_ready = false;
          STA_ADAPTER_INST.destroyCharPower();
        }
      }

      const auto stored_sample_indices
          = tryMakeStoredSampleIndices(input_slew_idx, load_cap_idx, output_slew_ns, driven_cap_pf, build_progress);
      is_first_slew_sample_for_load = false;
      if (!stored_sample_indices.has_value()) {
        continue;
      }

      const CharCore core(stored_sample_indices->input_slew_idx, stored_sample_indices->output_slew_idx,
                          stored_sample_indices->driven_cap_idx, stored_sample_indices->load_cap_idx, delay_ns, power_w, pid);
      const SegmentChar seg_char(core, length_idx);
      _segment_chars.push_back(seg_char);
    }
  }

  if (kEnableCharPowerSampling) {
    STA_ADAPTER_INST.destroyCharPower();
  }
  STA_ADAPTER_INST.destroyCharClock();

  destroyCharCircuit();
  if ((build_progress.evaluated_patterns % kCharProgressLogStride) == 0U) {
    LOG_INFO << "CharBuilder: wire_length=" << total_length_um << " um progress " << build_progress.evaluated_patterns << "/"
             << build_progress.estimated_patterns << " patterns"
             << " (feasible=" << build_progress.feasible_patterns << ", skipped=" << build_progress.skipped_patterns_infeasible
             << ", executed_sta_samples=" << build_progress.executed_sta_samples
             << ", skipped_sta_samples=" << build_progress.skipped_sta_samples << ")";
  }
  ++_next_pattern_id;
}

// ---------------------------------------------------------------------------
// Temporary circuit management
// ---------------------------------------------------------------------------

auto CharBuilder::createCharCircuit(const TopologyDesc& topo, const std::vector<std::string>& buf_masters) -> void
{
  _temp_inst_names.clear();
  _temp_net_names.clear();

  const std::string id_prefix = "cts_char_" + std::to_string(_char_circuit_id) + "_";

  // Keep the fixture chain source-to-sink non-increasing even when the pattern is empty.
  const auto& source_buf = _sorted_buffers.back();
  const auto& sink_buf = _sorted_buffers.front();

  _source_inst_name = id_prefix + "source";
  STA_ADAPTER_INST.createCharInstance(source_buf.cell_master, _source_inst_name);
  _source_in_pin = _source_inst_name + "/" + source_buf.input_pin;
  _source_out_pin = _source_inst_name + "/" + source_buf.output_pin;

  _sink_inst_name = id_prefix + "sink";
  STA_ADAPTER_INST.createCharInstance(sink_buf.cell_master, _sink_inst_name);
  _sink_in_pin = _sink_inst_name + "/" + sink_buf.input_pin;

  for (size_t i = 0; i < buf_masters.size(); ++i) {
    const std::string inst_name = id_prefix + "buf_" + std::to_string(i);
    STA_ADAPTER_INST.createCharInstance(buf_masters.at(i), inst_name);
    _temp_inst_names.push_back(inst_name);
  }

  for (size_t i = 0; i < topo.wire_segments_um.size(); ++i) {
    const std::string net_name = id_prefix + "net_" + std::to_string(i);
    STA_ADAPTER_INST.createCharNet(net_name);
    _temp_net_names.push_back(net_name);
  }

  STA_ADAPTER_INST.attachCharPin(_source_inst_name, source_buf.output_pin, _temp_net_names.front());

  for (size_t bi = 0; bi < buf_masters.size(); ++bi) {
    const CharBufferInfo* buf_info = findBufferInfo(buf_masters.at(bi));
    if (buf_info == nullptr) {
      LOG_FATAL << "Buffer info not found for: " << buf_masters.at(bi);
      return;
    }
    const auto& buffer_info = *buf_info;

    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.input_pin, _temp_net_names.at(bi));
    STA_ADAPTER_INST.attachCharPin(_temp_inst_names.at(bi), buffer_info.output_pin, _temp_net_names.at(bi + 1));
  }

  STA_ADAPTER_INST.attachCharPin(_sink_inst_name, sink_buf.input_pin, _temp_net_names.back());

  for (const auto& net_name : _temp_net_names) {
    STA_ADAPTER_INST.buildCharNetGraph(net_name);
  }

  _char_clock_name = id_prefix + "clk";

  ++_char_circuit_id;
}

auto CharBuilder::setCharParasitics(const TopologyDesc& topo, double load_pf) -> void
{
  for (size_t i = 0; i < _temp_net_names.size(); ++i) {
    const double seg_len_um = topo.wire_segments_um.at(i);
    const double wire_res = STA_ADAPTER_INST.queryWireResistance(_routing_layer, seg_len_um, _wire_width);
    const double wire_cap = STA_ADAPTER_INST.queryWireCapacitance(_routing_layer, seg_len_um, _wire_width);

    const double seg_load = (i == _temp_net_names.size() - 1) ? load_pf : 0.0;
    STAAdapter::CharRcTreeConfig rc_tree_config;
    rc_tree_config.wire_res = wire_res;
    rc_tree_config.wire_cap = wire_cap;
    rc_tree_config.load_cap = seg_load;
    STA_ADAPTER_INST.buildCharRcTree(_temp_net_names.at(i), rc_tree_config);
  }
}

auto CharBuilder::destroyCharCircuit() -> void
{
  STA_ADAPTER_INST.resetCharContext();
  _sink_inst_name.clear();
  _source_inst_name.clear();
  _temp_net_names.clear();
  _temp_inst_names.clear();
  _source_in_pin.clear();
  _source_out_pin.clear();
  _sink_in_pin.clear();
}

}  // namespace icts
