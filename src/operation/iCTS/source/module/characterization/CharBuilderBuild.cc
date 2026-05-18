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
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace icts {
namespace {

auto formatFixed(double value, int precision = 4) -> std::string
{
  return logformat::FormatFixed(value, precision);
}

auto calcRatio(std::size_t numerator, std::size_t denominator) -> double
{
  if (denominator == 0U) {
    return 0.0;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

auto DetailStageReportOptions() -> schema::StageReportOptions
{
  return schema::StageReportOptions{.context_sink = schema::ReportSink::kDetail, .summary_sink = schema::ReportSink::kDetail};
}

}  // namespace

auto CharBuilder::build() -> void
{
  auto build_stage = SCHEMA_WRITER_INST.beginStage("CharBuilder", "build", {}, DetailStageReportOptions());
  logformat::TableRows progress_rows;

  _executed_sta_samples = 0U;
  _skipped_sta_samples = 0U;
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
  if (_wirelengths_um.empty()) {
    LOG_ERROR << "CharBuilder: no wirelengths to enumerate, aborting build";
    build_stage.failed({{"reason", "no_wirelengths"}});
    return;
  }
  if (_slews_to_test.empty() || _loads_to_test.empty()) {
    LOG_WARNING << "CharBuilder: characterization limits are unresolved"
                << " (max_slew=" << _max_slew << " ns, max_cap=" << _max_cap << " pF), skip characterization build";
    build_stage.skip({{"reason", "unresolved_characterization_limits"}});
    return;
  }

  for (std::size_t wirelength_index = 0; wirelength_index < _wirelengths_um.size(); ++wirelength_index) {
    const unsigned length_idx = _wirelength_indices.at(wirelength_index);
    const double wirelength_um = _wirelengths_um.at(wirelength_index);
    const unsigned topology_slots = length_idx;
    const std::size_t estimated_patterns_per_wirelength = estimatePatternCountPerWirelength(wirelength_um);
    const std::size_t estimated_sta_samples_per_wirelength
        = estimated_patterns_per_wirelength * _loads_to_test.size() * _slews_to_test.size();
    const std::size_t char_count_before = _segment_chars.size();
    const std::size_t pattern_count_before = _buffering_patterns.size();
    BuildProgress build_progress;
    build_progress.wirelength_um = wirelength_um;
    build_progress.estimated_patterns = estimated_patterns_per_wirelength;
    build_progress.estimated_sta_samples = estimated_sta_samples_per_wirelength;
    build_stage.markRunning("wirelength=" + formatFixed(wirelength_um) + " um",
                            {
                                {"estimated_patterns", std::to_string(estimated_patterns_per_wirelength)},
                                {"estimated_sta_samples", std::to_string(estimated_sta_samples_per_wirelength)},
                                {"topology_slots", std::to_string(topology_slots)},
                            });
    enumerateWirelength(length_idx, wirelength_um, build_progress);

    progress_rows.push_back({
        formatFixed(wirelength_um) + " um",
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
    _executed_sta_samples += build_progress.executed_sta_samples;
    _skipped_sta_samples += build_progress.skipped_sta_samples;
    _driven_cap_overflow_samples += build_progress.driven_cap_overflow_samples;
    _driven_cap_overflow_load_points += build_progress.driven_cap_overflow_load_points;
    _max_observed_output_slew_ns = std::max(_max_observed_output_slew_ns, build_progress.max_observed_output_slew_ns);
    _max_observed_output_slew_idx = std::max(_max_observed_output_slew_idx, build_progress.max_observed_output_slew_idx);
    _max_observed_driven_cap_pf = std::max(_max_observed_driven_cap_pf, build_progress.max_observed_driven_cap_pf);
    _max_observed_driven_cap_idx = std::max(_max_observed_driven_cap_idx, build_progress.max_observed_driven_cap_idx);

    LOG_INFO << "CharBuilder: [RUNNING] wirelength=" << formatFixed(wirelength_um)
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
    SCHEMA_WRITER_INST.emitTableTo(
        "CharBuilder Sweep Progress Detail",
        {"Wirelength", "Topology Slots", "Generated Chars", "Generated Patterns", "Feasible Patterns", "Skipped Patterns",
         "Executed STA Samples", "Skipped STA Samples", "Output Slew Overflow", "Driven Cap Overflow", "Driven Cap Overflow Load Points"},
        progress_rows, schema::ReportSink::kDetail);
  }

  const double output_slew_overflow_ratio = calcRatio(_output_slew_overflow_samples, _executed_sta_samples);
  const double driven_cap_overflow_ratio = calcRatio(_driven_cap_overflow_samples, _executed_sta_samples);
  const schema::KeyValueFields default_observed_fields = {
      {"segment_chars", std::to_string(_segment_chars.size())},
      {"executed_sta_samples", std::to_string(_executed_sta_samples)},
      {"skipped_sta_samples", std::to_string(_skipped_sta_samples)},
      {"output_slew_overflow_samples", std::to_string(_output_slew_overflow_samples)},
      {"output_slew_overflow_ratio", logformat::FormatPercent(output_slew_overflow_ratio, 2)},
      {"max_observed_output_slew", logformat::FormatWithUnit(_max_observed_output_slew_ns, "ns")},
      {"driven_cap_overflow_samples", std::to_string(_driven_cap_overflow_samples)},
      {"driven_cap_overflow_ratio", logformat::FormatPercent(driven_cap_overflow_ratio, 2)},
      {"driven_cap_overflow_load_points", std::to_string(_driven_cap_overflow_load_points)},
      {"max_observed_driven_cap", logformat::FormatWithUnit(_max_observed_driven_cap_pf, "pF")},
  };
  const schema::KeyValueFields detail_observed_fields = {
      {"segment_chars", std::to_string(_segment_chars.size())},
      {"executed_sta_samples", std::to_string(_executed_sta_samples)},
      {"skipped_sta_samples", std::to_string(_skipped_sta_samples)},
      {"output_slew_overflow_samples", std::to_string(_output_slew_overflow_samples)},
      {"output_slew_overflow_ratio", logformat::FormatPercent(output_slew_overflow_ratio, 2)},
      {"max_observed_output_slew", logformat::FormatWithUnit(_max_observed_output_slew_ns, "ns")},
      {"max_observed_output_slew_idx", std::to_string(_max_observed_output_slew_idx)},
      {"slew_lattice_source", "CharBuilder Setup"},
      {"driven_cap_overflow_samples", std::to_string(_driven_cap_overflow_samples)},
      {"driven_cap_overflow_ratio", logformat::FormatPercent(driven_cap_overflow_ratio, 2)},
      {"driven_cap_overflow_load_points", std::to_string(_driven_cap_overflow_load_points)},
      {"max_observed_driven_cap", logformat::FormatWithUnit(_max_observed_driven_cap_pf, "pF")},
      {"max_observed_driven_cap_idx", std::to_string(_max_observed_driven_cap_idx)},
      {"cap_lattice_source", "CharBuilder Setup"},
  };
  SCHEMA_WRITER_INST.emitSection("### Characterization Results");
  SCHEMA_WRITER_INST.emitKeyValueTable("CharBuilder Results", default_observed_fields);
  SCHEMA_WRITER_INST.emitKeyValueTableTo("CharBuilder Results Detail", detail_observed_fields, schema::ReportSink::kDetail);
  if (_output_slew_overflow_samples > 0U) {
    schema::EmitDiagnostic(output_slew_overflow_ratio >= 0.10 ? schema::DiagnosticLevel::kWarning : schema::DiagnosticLevel::kInfo,
                           "CharBuilder",
                           "output slew overflow occurred during characterization; samples were capped to the configured slew lattice.",
                           {
                               {"output_slew_overflow_samples", std::to_string(_output_slew_overflow_samples)},
                               {"max_observed_output_slew", logformat::FormatWithUnit(_max_observed_output_slew_ns, "ns")},
                               {"overflow_ratio_source", "CharBuilder Results"},
                               {"slew_lattice_source", "CharBuilder Setup"},
                           });
  }

  build_stage.finished({
      {"segment_chars", std::to_string(_segment_chars.size())},
      {"patterns", std::to_string(_buffering_patterns.size())},
  });
}

}  // namespace icts
