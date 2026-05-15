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
 * @file CharBuilderConfig.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Characterization buffer discovery, limits, and sweep-grid initialization.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "Log.hh"
#include "SegmentChar.hh"
#include "ValueLattice.hh"
#include "adapter/sta/STAAdapter.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace icts {
namespace {

constexpr double kPercentFactor = 100.0;
constexpr unsigned kDefaultWirelengthIterations = 3U;

enum class ResolutionSource
{
  kRuntimeConfig,
  kCallerOverride,
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
    case ResolutionSource::kCallerOverride:
      return "caller_override";
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

auto logInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void
{
  schema::EmitTable(title, headers, rows);
}

auto makeDenseWirelengthIndices(unsigned iterations) -> std::vector<unsigned>
{
  std::vector<unsigned> indices;
  indices.reserve(iterations);
  for (unsigned length_idx = 1U; length_idx <= iterations; ++length_idx) {
    indices.push_back(length_idx);
  }
  return indices;
}

auto normalizeWirelengthIndices(std::vector<unsigned> indices) -> std::vector<unsigned>
{
  std::erase(indices, 0U);
  std::ranges::sort(indices);
  const auto unique_tail = std::ranges::unique(indices);
  indices.erase(unique_tail.begin(), unique_tail.end());
  return indices;
}

auto clampWirelengthIndices(std::vector<unsigned> indices, unsigned max_length_idx) -> std::pair<std::vector<unsigned>, bool>
{
  indices = normalizeWirelengthIndices(std::move(indices));
  const auto old_size = indices.size();
  std::erase_if(indices, [&](unsigned length_idx) -> bool { return length_idx > max_length_idx; });
  const bool truncated = old_size != indices.size();
  return {std::move(indices), truncated};
}

auto makeWirelengths(double unit_um, const std::vector<unsigned>& indices) -> std::vector<double>
{
  std::vector<double> wirelengths_um;
  wirelengths_um.reserve(indices.size());
  for (const unsigned length_idx : indices) {
    wirelengths_um.push_back(static_cast<double>(length_idx) * unit_um);
  }
  return wirelengths_um;
}

auto resolveRoutingLayer(const CharBuilder::InitOptions& options) -> int
{
  return options.routing_layer.has_value() && *options.routing_layer > 0 ? *options.routing_layer : 1;
}

auto resolveWireWidth(const CharBuilder::InitOptions& options) -> std::optional<double>
{
  return options.wire_width.has_value() && *options.wire_width > 0.0 ? options.wire_width : std::nullopt;
}

auto resolveBufferDriveCap(const std::string& cell_master) -> double
{
  double max_cap = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (max_cap <= 0.0) {
    max_cap = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return max_cap;
}

auto resolveMaxSlew(const CharBuilder::InitOptions& options) -> ResolvedValue
{
  if (options.max_slew_ns.has_value() && *options.max_slew_ns > 0.0) {
    return ResolvedValue{
        .value = *options.max_slew_ns,
        .source = ResolutionSource::kRuntimeConfig,
        .detail = "CharBuilder init option: max slew",
    };
  }

  double liberty_port_min_slew = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : options.buffer_types) {
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
  for (const auto& cell_master : options.buffer_types) {
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

  LOG_WARNING << "CharBuilder: failed to resolve max_slew from InitOptions/liberty limits/liberty tables";
  return ResolvedValue{
      .value = 0.0,
      .source = ResolutionSource::kUnresolved,
      .detail = "missing InitOptions/liberty slew limits",
  };
}

auto resolveMaxCap(const CharBuilder::InitOptions& options) -> ResolvedValue
{
  if (options.max_cap_pf.has_value() && *options.max_cap_pf > 0.0) {
    return ResolvedValue{
        .value = *options.max_cap_pf,
        .source = ResolutionSource::kRuntimeConfig,
        .detail = "CharBuilder init option: max cap",
    };
  }

  double liberty_port_min_cap = std::numeric_limits<double>::infinity();
  bool found_port_limit = false;
  for (const auto& cell_master : options.buffer_types) {
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
  for (const auto& cell_master : options.buffer_types) {
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

  LOG_WARNING << "CharBuilder: failed to resolve max_cap from InitOptions/liberty limits/liberty tables";
  return ResolvedValue{
      .value = 0.0,
      .source = ResolutionSource::kUnresolved,
      .detail = "missing InitOptions/liberty cap limits",
  };
}

auto collectSortedBuffers(const CharBuilder::InitOptions& options) -> std::vector<CharBufferInfo>
{
  std::vector<CharBufferInfo> sorted_buffers;
  const auto& buffer_types = options.buffer_types;
  if (buffer_types.empty()) {
    LOG_WARNING << "CharBuilder: no buffer types configured in InitOptions";
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

  const double buffer_redundancy_pct = options.char_buf_redundancy_pct.value_or(0.0);
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

auto resolveWirelengthUnitUm(const std::vector<CharBufferInfo>& sorted_buffers) -> ResolvedValue
{
  double strongest_cap_pf = -1.0;
  double strongest_height_um = 0.0;
  std::string strongest_master;
  for (const auto& buffer_info : sorted_buffers) {
    const double cell_height_um = STA_ADAPTER_INST.queryCellHeightUm(buffer_info.cell_master);
    if (cell_height_um <= 0.0) {
      LOG_WARNING << "CharBuilder: cannot derive wirelength_unit from " << buffer_info.cell_master
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
    LOG_WARNING << "CharBuilder: failed to resolve wirelength_unit from InitOptions or strongest buffer height";
    schema::EmitDiagnostic(schema::DiagnosticLevel::kWarning, "CharBuilder",
                           "wirelength unit is absent in CharBuilder options and auto-derivation failed.",
                           {{"reason", "no_valid_strongest_buffer_height"}});
    return ResolvedValue{
        .value = 0.0,
        .source = ResolutionSource::kUnresolved,
        .detail = "no valid strongest buffer height",
    };
  }

  const double fallback_unit_um = strongest_height_um * 10.0;
  schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "CharBuilder",
                         "wirelength unit is absent in CharBuilder options, fallback to auto-derived strongest-buffer height.",
                         {{"strongest_buffer", strongest_master},
                          {"buffer_height", logformat::FormatWithUnit(strongest_height_um, "um")},
                          {"derived_wirelength_unit", logformat::FormatWithUnit(fallback_unit_um, "um")}});
  return ResolvedValue{
      .value = fallback_unit_um,
      .source = ResolutionSource::kAutoDerived,
      .detail = "strongest buffer " + strongest_master + " height=" + logformat::FormatWithUnit(strongest_height_um, "um") + " x10",
  };
}

auto resolveWirelengthUnitUm(const CharBuilder::InitOptions& options, const std::vector<CharBufferInfo>& sorted_buffers,
                             bool caller_override) -> ResolvedValue
{
  if (options.wirelength_unit_um.has_value() && *options.wirelength_unit_um > 0.0) {
    return ResolvedValue{
        .value = *options.wirelength_unit_um,
        .source = caller_override ? ResolutionSource::kCallerOverride : ResolutionSource::kRuntimeConfig,
        .detail = caller_override ? "CharBuilder caller override: wirelength unit" : "runtime configuration",
    };
  }

  return resolveWirelengthUnitUm(sorted_buffers);
}

auto DetailStageReportOptions() -> schema::StageReportOptions
{
  return schema::StageReportOptions{.context_sink = schema::ReportSink::kDetail, .summary_sink = schema::ReportSink::kDetail};
}

}  // namespace

auto CharBuilder::init(const InitOptions& options) -> void
{
  auto init_stage = SCHEMA_WRITER_INST.beginStage("CharBuilder", "initialization", {}, DetailStageReportOptions());
  const InitOptions& effective_options = options;

  _segment_chars.clear();
  _buffering_patterns.clear();
  _wirelength_indices.clear();
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
  _executed_sta_samples = 0U;
  _skipped_sta_samples = 0U;
  _output_slew_overflow_samples = 0U;
  _driven_cap_overflow_samples = 0U;
  _driven_cap_overflow_load_points = 0U;
  _max_observed_output_slew_ns = 0.0;
  _max_observed_output_slew_idx = 0U;
  _max_observed_driven_cap_pf = 0.0;
  _max_observed_driven_cap_idx = 0U;

  _sorted_buffers = collectSortedBuffers(effective_options);
  const ResolvedValue max_slew_resolution = resolveMaxSlew(effective_options);
  const ResolvedValue max_cap_resolution = resolveMaxCap(effective_options);
  const ResolvedValue wirelength_unit_resolution
      = resolveWirelengthUnitUm(effective_options, _sorted_buffers, options.wirelength_unit_um.has_value());
  _max_slew = max_slew_resolution.value;
  _max_cap = max_cap_resolution.value;
  _length_unit_um = wirelength_unit_resolution.value;
  _wirelength_unit_source = toResolutionSourceName(wirelength_unit_resolution.source);
  _wirelength_unit_detail = wirelength_unit_resolution.detail;
  _wirelength_iterations = std::max(1U, effective_options.wirelength_iterations.value_or(kDefaultWirelengthIterations));
  _slew_steps = effective_options.slew_steps.value_or(15U);
  _cap_steps = effective_options.cap_steps.value_or(15U);
  bool wirelength_indices_truncated = false;
  if (effective_options.wirelength_indices.has_value()) {
    auto [clamped_indices, truncated] = clampWirelengthIndices(*effective_options.wirelength_indices, _wirelength_iterations);
    _wirelength_indices = std::move(clamped_indices);
    wirelength_indices_truncated = truncated;
  }
  if (_wirelength_indices.empty()) {
    _wirelength_indices = makeDenseWirelengthIndices(_wirelength_iterations);
  }
  _wirelengths_um = makeWirelengths(_length_unit_um, _wirelength_indices);
  _max_length = _wirelengths_um.empty() ? 0.0 : _wirelengths_um.back();
  _slews_to_test = get_slew_lattice().sampleValues();
  _loads_to_test = get_cap_lattice().sampleValues();
  _routing_layer = resolveRoutingLayer(effective_options);
  _wire_width = resolveWireWidth(effective_options);
  _sink_input_cap_pf = _sorted_buffers.empty() ? 0.0 : _sorted_buffers.front().input_cap_pf;

  if (wirelength_indices_truncated) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kFallback, "CharBuilder",
                           "wirelength_indices exceeded wirelength_iterations; clamp direct characterization to the configured max iter.",
                           {
                               {"wirelength_iterations", std::to_string(_wirelength_iterations)},
                               {"wirelength_points", std::to_string(_wirelength_indices.size())},
                           });
  }

  SCHEMA_WRITER_INST.emitSection("### Characterization Setup");
  logformat::TableRows default_setup_rows;
  if (!effective_options.wirelength_indices.has_value()) {
    default_setup_rows.push_back({"resolved_wirelength_unit", logformat::FormatWithUnit(_length_unit_um, "um"),
                                  toResolutionSourceName(wirelength_unit_resolution.source)});
  }
  default_setup_rows.push_back(
      {"wirelength_points", std::to_string(_wirelengths_um.size()),
       effective_options.wirelength_indices.has_value() ? "HTree Characterization Grid Plan" : wirelength_unit_resolution.detail});
  default_setup_rows.push_back({"routing_rc", "Runtime Routing / Wire RC", "STAAdapter"});
  default_setup_rows.push_back({"buffer_count", std::to_string(_sorted_buffers.size()), "resolved_buffers"});
  const logformat::TableRows detail_setup_rows = {
      {"max_slew", logformat::FormatWithUnit(_max_slew, "ns"), toResolutionSourceName(max_slew_resolution.source)},
      {"max_cap", logformat::FormatWithUnit(_max_cap, "pF"), toResolutionSourceName(max_cap_resolution.source)},
      {"resolved_wirelength_unit", logformat::FormatWithUnit(_length_unit_um, "um"),
       effective_options.wirelength_indices.has_value() ? "HTree Characterization Grid Plan"
                                                        : toResolutionSourceName(wirelength_unit_resolution.source)},
      {"wirelength_points", std::to_string(_wirelengths_um.size()),
       effective_options.wirelength_indices.has_value() ? "HTree Characterization Grid Plan" : wirelength_unit_resolution.detail},
      {"slew_steps", std::to_string(_slew_steps), "Runtime Configuration"},
      {"cap_steps", std::to_string(_cap_steps), "Runtime Configuration"},
      {"routing_rc", "Runtime Routing / Wire RC", "STAAdapter"},
      {"buffer_count", std::to_string(_sorted_buffers.size()), "resolved_buffers"},
  };
  logInfoTable("CharBuilder Setup", {"Parameter", "Value", "Source"}, default_setup_rows);
  SCHEMA_WRITER_INST.emitTableTo("CharBuilder Setup Detail", {"Parameter", "Value", "Source"}, detail_setup_rows,
                                 schema::ReportSink::kDetail);

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
  init_stage.finished({
      {"buffers", std::to_string(_sorted_buffers.size())},
      {"wirelengths", std::to_string(_wirelengths_um.size())},
      {"slews", std::to_string(_slews_to_test.size())},
      {"loads", std::to_string(_loads_to_test.size())},
  });
}

}  // namespace icts
