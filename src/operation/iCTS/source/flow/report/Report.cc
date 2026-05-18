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
 * @file Report.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS report entry facade implementation.
 */

#include "report/Report.hh"

#include <glog/logging.h>

#include <filesystem>
#include <ostream>
#include <utility>

#include "Log.hh"
#include "config/Config.hh"
#include "evaluation/Evaluation.hh"
#include "evaluation/qor/QorEvaluation.hh"
#include "logger/Schema.hh"
#include "report/export/ResultExport.hh"
#include "report/overview/Overview.hh"
#include "report/qor/QorReport.hh"
#include "report/visualization/Visualization.hh"

namespace icts {

auto Report::run(const std::string& save_dir, bool evaluation_ready, bool refresh_sta_timing, const ClockLayout& clock_layout,
                 EvaluationState& evaluation_state) -> ReportResult
{
  LOG_FATAL_IF(CONFIG_INST.get_work_dir().empty()) << "CTS report requires initialized CTS run setup.";

  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("report");
  auto report_stage = SCHEMA_WRITER_INST.beginStage("Report", "Emit CTS statistics and visualization reports", {},
                                                    schema::StageReportOptions{.emit_success_summary = false});
  const auto paths = ResultExport::resolvePaths(save_dir);
  const bool reused_evaluation_state = evaluation_ready && Evaluation::hasEvaluationResult(evaluation_state);

  SCHEMA_WRITER_INST.emitSection("## Report Overview");
  Overview::emitReportMode(reused_evaluation_state, paths);

  bool current_evaluation_ready = evaluation_ready;
  if (!reused_evaluation_state) {
    SCHEMA_WRITER_INST.emitSection("### Report Evaluation");
    current_evaluation_ready
        = Evaluation::run(evaluation_state, EvaluationOptions{.refresh_sta_timing = refresh_sta_timing, .clock_layout = &clock_layout})
              .evaluation_ready;
  }

  const bool statistics_success = current_evaluation_ready && QorReport::write(evaluation_state, paths.statistics_dir.string(), false);
  const auto visualization_result = Visualization::emit(paths.visualization_dir, clock_layout);
  const bool report_success = statistics_success && visualization_result.success;
  const auto report_metric = report_success ? runtime.finished() : runtime.failed();
  SCHEMA_WRITER_INST.emitRuntimeMetricTable("CTS Report Runtime", "report", report_success ? "finished" : "failed", report_metric);
  if (report_success) {
    report_stage.finished({{"statistics_status", statistics_success ? "finished" : "failed"},
                           {"visualization_status", visualization_result.svg_success ? "finished" : "failed"},
                           {"gds_status", visualization_result.gds_success ? "finished" : "failed"}});
  } else {
    report_stage.failed({{"statistics_status", statistics_success ? "finished" : "failed"},
                         {"visualization_status", visualization_result.svg_success ? "finished" : "failed"},
                         {"gds_status", visualization_result.gds_success ? "finished" : "failed"}});
  }

  return ReportResult{
      .report_success = report_success,
      .evaluation_ready = current_evaluation_ready,
  };
}

}  // namespace icts
