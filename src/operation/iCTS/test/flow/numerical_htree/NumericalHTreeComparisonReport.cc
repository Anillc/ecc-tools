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
 * @file NumericalHTreeComparisonReport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Report helpers for native versus numerical H-tree comparison tests.
 */

#include "flow/numerical_htree/NumericalHTreeComparisonReport.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "PatternId.hh"
#include "common/io/TestArtifactIO.hh"
#include "htree/HTreeBuilder.hh"
#include "utils/logger/Schema.hh"

namespace icts_test::numerical_htree {
namespace {

constexpr double kComparisonEpsilon = 1e-12;

auto FormatBool(bool value) -> std::string_view
{
  return value ? "true" : "false";
}

auto RelativeDelta(double reference, double candidate) -> double
{
  const double absolute_delta = std::max(0.0, candidate - reference);
  const double reference_magnitude = std::abs(reference);
  if (reference_magnitude <= kComparisonEpsilon) {
    return absolute_delta;
  }
  return absolute_delta / reference_magnitude;
}

auto RuntimeRatio(double native_runtime_s, double numerical_runtime_s) -> double
{
  if (native_runtime_s <= kComparisonEpsilon) {
    return std::numeric_limits<double>::infinity();
  }
  return numerical_runtime_s / native_runtime_s;
}

auto FormatPatternIds(const std::vector<icts::PatternId>& pattern_ids) -> std::string
{
  if (pattern_ids.empty()) {
    return "none";
  }

  std::ostringstream output_stream;
  for (std::size_t index = 0; index < pattern_ids.size(); ++index) {
    if (index != 0U) {
      output_stream << "|";
    }
    output_stream << pattern_ids.at(index).local_id;
  }
  return output_stream.str();
}

auto AppendFlowSummary(std::ostringstream& report_stream, const FlowSummary& summary) -> void
{
  report_stream << summary.label << "_success=" << FormatBool(summary.success) << "\n";
  report_stream << summary.label << "_failure_reason=" << summary.failure_reason << "\n";
  report_stream << summary.label << "_runtime_s=" << summary.runtime_s << "\n";
  report_stream << summary.label << "_selected_depth=";
  if (summary.has_selected_depth) {
    report_stream << summary.selected_depth;
  } else {
    report_stream << "none";
  }
  report_stream << "\n";
  report_stream << summary.label << "_delay_ns=";
  if (summary.has_qor) {
    report_stream << summary.delay_ns;
  } else {
    report_stream << "none";
  }
  report_stream << "\n";
  report_stream << summary.label << "_power_w=";
  if (summary.has_qor) {
    report_stream << summary.power_w;
  } else {
    report_stream << "none";
  }
  report_stream << "\n";
  report_stream << summary.label << "_level_segment_pattern_ids=" << FormatPatternIds(summary.level_segment_pattern_ids) << "\n";
}

auto AppendNumericalQorField(std::ostringstream& report_stream, const LevelSummary& level_summary, std::string_view field_name,
                             double value) -> void
{
  report_stream << ", " << field_name << "=";
  if (level_summary.has_numerical_qor) {
    report_stream << value;
  } else {
    report_stream << "none";
  }
}

auto AppendSelectedLevelSummaries(std::ostringstream& report_stream, const FlowSummary& summary) -> void
{
  report_stream << summary.label << "_selected_level_summary_count=" << summary.selected_level_summaries.size() << "\n";
  for (std::size_t index = 0; index < summary.selected_level_summaries.size(); ++index) {
    const auto& level_summary = summary.selected_level_summaries.at(index);
    report_stream << summary.label << "_selected_level[" << index << "] level_index=" << level_summary.level_index
                  << ", segment_pattern_id=" << level_summary.segment_pattern_id.local_id << ", model_name=" << level_summary.model_name;
    AppendNumericalQorField(report_stream, level_summary, "input_slew_ns", level_summary.input_slew_ns);
    AppendNumericalQorField(report_stream, level_summary, "load_cap_pf", level_summary.load_cap_pf);
    AppendNumericalQorField(report_stream, level_summary, "output_slew_ns", level_summary.output_slew_ns);
    AppendNumericalQorField(report_stream, level_summary, "driven_cap_pf", level_summary.driven_cap_pf);
    AppendNumericalQorField(report_stream, level_summary, "delay_ns", level_summary.delay_ns);
    AppendNumericalQorField(report_stream, level_summary, "power_w", level_summary.power_w);
    AppendNumericalQorField(report_stream, level_summary, "source_boundary_switch_power_w", level_summary.source_boundary_switch_power_w);
    AppendNumericalQorField(report_stream, level_summary, "composed_power_contribution_w", level_summary.composed_power_contribution_w);
    report_stream << "\n";
  }
}

auto AppendModelMetrics(std::ostringstream& report_stream, const FlowSummary& summary) -> void
{
  report_stream << summary.label << "_model_metric_count=" << summary.model_metrics.size() << "\n";
  for (std::size_t index = 0; index < summary.model_metrics.size(); ++index) {
    const auto& metric = summary.model_metrics.at(index);
    report_stream << summary.label << "_model_metric[" << index << "] label=" << metric.label << ", sample_count=" << metric.sample_count
                  << ", rank=" << metric.rank << ", r2=" << metric.r2 << ", rmse=" << metric.rmse
                  << ", max_abs_error=" << metric.max_abs_error << "\n";
  }
}

}  // namespace

auto BuildNativeFlowSummary(const icts::HTreeBuilder::BuildResult& result, double runtime_s) -> FlowSummary
{
  FlowSummary summary;
  summary.label = "native";
  summary.success = result.success;
  summary.failure_reason = result.failure_reason;
  summary.runtime_s = runtime_s;
  summary.has_selected_depth = result.selected_depth.has_value();
  summary.selected_depth = result.selected_depth.value_or(0U);
  if (result.best_char.has_value()) {
    summary.has_qor = true;
    summary.delay_ns = result.best_char->get_delay();
    summary.power_w = result.best_char->get_power();
  }
  summary.level_segment_pattern_ids.reserve(result.levels.size());
  summary.selected_level_summaries.reserve(result.levels.size());
  for (const auto& level : result.levels) {
    summary.level_segment_pattern_ids.push_back(level.segment_pattern_id);
    summary.selected_level_summaries.push_back(LevelSummary{
        .level_index = static_cast<unsigned>(summary.selected_level_summaries.size()),
        .segment_pattern_id = level.segment_pattern_id,
        .model_name = {},
        .has_numerical_qor = false,
        .input_slew_ns = 0.0,
        .load_cap_pf = 0.0,
        .output_slew_ns = 0.0,
        .driven_cap_pf = 0.0,
        .delay_ns = 0.0,
        .power_w = 0.0,
        .source_boundary_switch_power_w = 0.0,
        .composed_power_contribution_w = 0.0,
    });
  }
  return summary;
}

auto ComputeComparisonDeltas(const FlowSummary& native, const FlowSummary& numerical, const ComparisonTolerance& tolerance)
    -> ComparisonDeltas
{
  ComparisonDeltas deltas;
  deltas.comparable = native.success && numerical.success && native.has_qor && numerical.has_qor;
  deltas.runtime_ratio = RuntimeRatio(native.runtime_s, numerical.runtime_s);
  deltas.numerical_runtime_faster = numerical.runtime_s < native.runtime_s;
  if (!deltas.comparable) {
    return deltas;
  }

  deltas.delay_abs_delta_ns = std::abs(numerical.delay_ns - native.delay_ns);
  deltas.power_abs_delta_w = std::abs(numerical.power_w - native.power_w);
  deltas.delay_relative_delta = RelativeDelta(native.delay_ns, numerical.delay_ns);
  deltas.power_relative_delta = RelativeDelta(native.power_w, numerical.power_w);
  deltas.delay_within_tolerance = deltas.delay_relative_delta <= tolerance.delay_relative;
  deltas.power_within_tolerance = deltas.power_relative_delta <= tolerance.power_relative;
  return deltas;
}

auto FormatComparisonReport(const ComparisonReportData& report_data) -> std::string
{
  std::ostringstream report_stream;
  report_stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report_stream << std::setprecision(9);
  report_stream << "scenario=" << report_data.scenario_name << "\n";
  report_stream << "clock_name=" << report_data.clock_name << "\n";
  report_stream << "load_count=" << report_data.load_count << "\n";
  report_stream << "delay_relative_tolerance=" << report_data.tolerance.delay_relative << "\n";
  report_stream << "power_relative_tolerance=" << report_data.tolerance.power_relative << "\n";
  AppendFlowSummary(report_stream, report_data.native);
  AppendFlowSummary(report_stream, report_data.numerical);
  report_stream << "comparison_comparable=" << FormatBool(report_data.deltas.comparable) << "\n";
  report_stream << "delay_abs_delta_ns=" << report_data.deltas.delay_abs_delta_ns << "\n";
  report_stream << "power_abs_delta_w=" << report_data.deltas.power_abs_delta_w << "\n";
  report_stream << "delay_relative_delta=" << report_data.deltas.delay_relative_delta << "\n";
  report_stream << "power_relative_delta=" << report_data.deltas.power_relative_delta << "\n";
  report_stream << "runtime_ratio_numerical_over_native=" << report_data.deltas.runtime_ratio << "\n";
  report_stream << "delay_within_tolerance=" << FormatBool(report_data.deltas.delay_within_tolerance) << "\n";
  report_stream << "power_within_tolerance=" << FormatBool(report_data.deltas.power_within_tolerance) << "\n";
  report_stream << "numerical_runtime_faster=" << FormatBool(report_data.deltas.numerical_runtime_faster) << "\n";
  AppendSelectedLevelSummaries(report_stream, report_data.native);
  AppendSelectedLevelSummaries(report_stream, report_data.numerical);
  AppendModelMetrics(report_stream, report_data.native);
  AppendModelMetrics(report_stream, report_data.numerical);
  return report_stream.str();
}

auto WriteComparisonReport(const std::filesystem::path& report_path, const ComparisonReportData& report_data) -> bool
{
  const bool wrote_report = common::io::WriteTextLog(report_path, FormatComparisonReport(report_data));
  if (wrote_report) {
    icts::schema::EmitArtifact("Numerical HTree comparison report", report_path);
  }
  return wrote_report;
}

}  // namespace icts_test::numerical_htree
