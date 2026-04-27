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
 * @file CharBuilderBuild.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Characterization sweep build driver and progress reporting.
 */

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "CharBuilder.hh"
#include "Log.hh"
#include "SegmentChar.hh"
#include "adapter/sta/STAAdapter.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace icts {
namespace {

auto formatFixed(double value, int precision = 4) -> std::string
{
  return logformat::FormatFixed(value, precision);
}

auto logInfoTable(const std::string& title, const std::vector<std::string>& headers, const logformat::TableRows& rows) -> void
{
  schema::EmitTable(title, headers, rows);
}

}  // namespace

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
    LOG_WARNING << "CharBuilder: no usable buffers remain after InitOptions/liberty filtering, skip characterization build";
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

}  // namespace icts
