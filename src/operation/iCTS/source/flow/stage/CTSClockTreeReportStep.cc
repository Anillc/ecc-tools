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
 * @file CTSClockTreeReportStep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-tree report step orchestration implementation.
 */

#include "stage/CTSClockTreeReportStep.hh"

#include <glog/logging.h>

#include <filesystem>
#include <ostream>
#include <string>

#include "Log.hh"
#include "config/Config.hh"
#include "evaluation/ClockTreeEvaluator.hh"
#include "logger/Schema.hh"
#include "report/CTSGdsReport.hh"
#include "report/CTSVisualizationReport.hh"
#include "stage/CTSClockTreeEvaluationStep.hh"

namespace icts {
namespace {

auto resolveReportRootDir(const std::string& save_dir) -> std::filesystem::path
{
  if (!save_dir.empty()) {
    return std::filesystem::path(save_dir);
  }
  return std::filesystem::path(CONFIG_INST.get_work_dir());
}

auto resolveVisualizationDir(const std::string& save_dir, const std::filesystem::path& report_root_dir) -> std::filesystem::path
{
  if (!save_dir.empty()) {
    return report_root_dir / "visualization";
  }
  if (!CONFIG_INST.get_visualization_dir().empty()) {
    return std::filesystem::path(CONFIG_INST.get_visualization_dir());
  }
  return report_root_dir / "visualization";
}

auto resolveStatisticsDir(const std::string& save_dir, const std::filesystem::path& report_root_dir) -> std::filesystem::path
{
  if (!save_dir.empty()) {
    return report_root_dir / "statistics";
  }
  if (!CONFIG_INST.get_statistics_dir().empty()) {
    return std::filesystem::path(CONFIG_INST.get_statistics_dir());
  }
  return report_root_dir / "statistics";
}

}  // namespace

auto CTSClockTreeReportStep::run(const std::string& save_dir, bool evaluation_ready, bool refresh_sta_timing,
                                 const ClockTreeReportData& report_data, ClockTreeEvaluationState& evaluation_state)
    -> CTSClockTreeReportResult
{
  LOG_FATAL_IF(CONFIG_INST.get_work_dir().empty()) << "CTS report requires an initialized CTS session.";

  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("report");
  auto report_stage = SCHEMA_WRITER_INST.beginStage("CTSReport", "Emit CTS statistics and visualization reports");
  const auto report_root_dir = resolveReportRootDir(save_dir);
  const auto visualization_dir = resolveVisualizationDir(save_dir, report_root_dir);
  const auto statistics_dir = resolveStatisticsDir(save_dir, report_root_dir);
  const bool reused_evaluation_state = evaluation_ready && ClockTreeEvaluator::hasEvaluationResult(evaluation_state);

  SCHEMA_WRITER_INST.emitSection("## Report Summary");
  schema::EmitKeyValueTable("CTS Report Mode",
                            {
                                {"mode", reused_evaluation_state ? "reuse_evaluation_state" : "rebuild_evaluation_state"},
                                {"save_dir", report_root_dir.string()},
                                {"visualization_dir", visualization_dir.string()},
                                {"statistics_dir", statistics_dir.string()},
                            });

  bool current_evaluation_ready = evaluation_ready;
  if (!reused_evaluation_state) {
    SCHEMA_WRITER_INST.emitSection("### Report Evaluation");
    current_evaluation_ready = CTSClockTreeEvaluationStep::run(evaluation_state, refresh_sta_timing).evaluation_ready;
  }

  const bool statistics_success
      = current_evaluation_ready && ClockTreeEvaluator::writeStatistics(evaluation_state, statistics_dir.string(), false);
  const auto visualization_result = report::EmitCTSVisualizationReports(visualization_dir, report_data);
  const auto gds_result = report::EmitCTSGdsReports(visualization_dir, report_data);
  const bool report_success = statistics_success && visualization_result.success && gds_result.success;
  const auto report_metric = report_success ? runtime.finished() : runtime.failed();
  SCHEMA_WRITER_INST.emitRuntimeMetricTable("CTS Report Runtime", "report", report_success ? "finished" : "failed", report_metric);
  if (report_success) {
    report_stage.finished({{"statistics_status", statistics_success ? "finished" : "failed"},
                           {"visualization_status", visualization_result.success ? "finished" : "failed"},
                           {"gds_status", gds_result.success ? "finished" : "failed"}});
  } else {
    report_stage.failed({{"statistics_status", statistics_success ? "finished" : "failed"},
                         {"visualization_status", visualization_result.success ? "finished" : "failed"},
                         {"gds_status", gds_result.success ? "finished" : "failed"}});
  }

  return CTSClockTreeReportResult{
      .report_success = report_success,
      .evaluation_ready = current_evaluation_ready,
  };
}

}  // namespace icts
