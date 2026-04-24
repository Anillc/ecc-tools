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
 * @file NumericalHTreeComparisonReport.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Report helpers for native versus numerical H-tree comparison tests.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "characterization/PatternId.hh"
#include "flow/htree/HTreeBuilder.hh"

namespace icts_test::numerical_htree {

struct ComparisonTolerance
{
  double delay_relative = 0.20;
  double power_relative = 0.25;
};

struct ModelMetricSummary
{
  std::string label;
  std::size_t sample_count = 0U;
  unsigned rank = 0U;
  double r2 = 0.0;
  double rmse = 0.0;
  double max_abs_error = 0.0;
};

struct LevelSummary
{
  unsigned level_index = 0U;
  icts::PatternId segment_pattern_id = icts::PatternId::segment(0U);
  std::string model_name;
  bool has_numerical_qor = false;
  double input_slew_ns = 0.0;
  double load_cap_pf = 0.0;
  double output_slew_ns = 0.0;
  double driven_cap_pf = 0.0;
  double delay_ns = 0.0;
  double power_w = 0.0;
  double source_boundary_switch_power_w = 0.0;
  double composed_power_contribution_w = 0.0;
};

struct FlowSummary
{
  std::string label;
  bool success = false;
  std::string failure_reason;
  double runtime_s = 0.0;
  bool has_selected_depth = false;
  unsigned selected_depth = 0U;
  bool has_qor = false;
  double delay_ns = 0.0;
  double power_w = 0.0;
  std::vector<icts::PatternId> level_segment_pattern_ids;
  std::vector<LevelSummary> selected_level_summaries;
  std::vector<ModelMetricSummary> model_metrics;
};

struct ComparisonDeltas
{
  bool comparable = false;
  double delay_abs_delta_ns = 0.0;
  double power_abs_delta_w = 0.0;
  double delay_relative_delta = 0.0;
  double power_relative_delta = 0.0;
  double runtime_ratio = 0.0;
  bool delay_within_tolerance = false;
  bool power_within_tolerance = false;
  bool numerical_runtime_faster = false;
};

struct ComparisonReportData
{
  std::string scenario_name;
  std::string clock_name;
  std::size_t load_count = 0U;
  ComparisonTolerance tolerance;
  FlowSummary native;
  FlowSummary numerical;
  ComparisonDeltas deltas;
};

auto BuildNativeFlowSummary(const icts::HTreeBuilder::BuildResult& result, double runtime_s) -> FlowSummary;
auto ComputeComparisonDeltas(const FlowSummary& native, const FlowSummary& numerical, const ComparisonTolerance& tolerance)
    -> ComparisonDeltas;
auto FormatComparisonReport(const ComparisonReportData& report_data) -> std::string;
auto WriteComparisonReport(const std::filesystem::path& report_path, const ComparisonReportData& report_data) -> bool;

}  // namespace icts_test::numerical_htree
