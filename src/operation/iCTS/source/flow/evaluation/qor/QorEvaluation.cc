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
 * @file QorEvaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree evaluation stage implementation.
 */

#include "evaluation/qor/QorEvaluation.hh"

#include <glog/logging.h>

#include <optional>
#include <ostream>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "evaluation/qor/ClockQorMetricCollector.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "qor/Qor.hh"

namespace icts {

auto QorEvaluation::evaluate(EvaluationState& state, const EvaluationInput& input, const EvaluationConfig& evaluation_config) -> void
{
  LOG_FATAL_IF(input.config == nullptr) << "Evaluation requires config.";
  LOG_FATAL_IF(input.design == nullptr) << "Evaluation requires design.";
  LOG_FATAL_IF(input.wrapper == nullptr) << "Evaluation requires wrapper.";
  LOG_FATAL_IF(input.sta_adapter == nullptr) << "Evaluation requires STA adapter.";
  LOG_FATAL_IF(input.reporter == nullptr) << "Evaluation requires reporter.";
  const auto& config = *input.config;
  auto& design = *input.design;
  auto& wrapper = *input.wrapper;
  auto& sta_adapter = *input.sta_adapter;
  auto& reporter = *input.reporter;
  auto& summary = state.summary;
  auto& statistics = state.statistics;
  qor_evaluation::ClearSummary(summary);
  qor_evaluation::ClearStatistics(statistics);

  auto clocks = design.get_clocks();
  summary.design_dbu_per_um = wrapper.queryDbUnit();
  if (summary.design_dbu_per_um <= 0) {
    summary.design_dbu_per_um = 0;
    EmitDiagnostic(reporter, DiagnosticLevel::kWarning, "CTS Evaluation",
                   "CTS evaluation reports degraded wirelength metrics because DBU-per-micron is unavailable.",
                   {{"wirelength_metric_status", "degraded"}});
  }
  const bool clock_dag_valid = design.rebuildClockDAG();
  const auto& clock_dag = design.get_clock_dag();
  qor_evaluation::AppendPathDepthStats(clock_dag.pathBufferStats(), summary);
  if (!clock_dag_valid) {
    EmitDiagnostic(reporter, DiagnosticLevel::kError, "CTS Evaluation",
                   "CTS evaluation skipped because committed topology is not a valid clock DAG.",
                   {{"path_depth_metric_status", summary.path_depth_metric_status}, {"reason", clock_dag.get_status()}});
    qor_evaluation::SyncCompatibilityAliases(summary);
    qor_evaluation::EmitEvaluationSummary(reporter, summary, false);
    return;
  }

  const bool should_refresh_sta = wrapper.is_design_ready() && evaluation_config.refresh_sta_timing;
  if (should_refresh_sta) {
    sta_adapter.refreshFullDesignTimingContext(config);
    summary.propagated_clock_count = sta_adapter.setPropagatedClocks();
    summary.sta_clocks_propagated = summary.propagated_clock_count > 0U;
  }

  std::unordered_set<const Inst*> counted_buffer_insts;
  std::vector<qor_evaluation::ClockNetMeasurement> clock_net_measurements;
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }

    int32_t clock_member_buffer_count = 0;
    for (auto* inst : clock->get_insts()) {
      if (inst == nullptr || !inst->is_buffer()) {
        continue;
      }
      ++clock_member_buffer_count;
      const bool is_new_buffer_inst = counted_buffer_insts.insert(inst).second;
      if (is_new_buffer_inst) {
        ++summary.final_clock_buffer_count;
        qor_evaluation::AccumulateInstStatistics(sta_adapter, *inst, statistics);
      }
      if (wrapper.is_layout_ready() && is_new_buffer_inst) {
        summary.final_buffer_area_um2 += sta_adapter.queryCellAreaUm2(inst->get_cell_master());
      }
    }
    summary.clock_member_buffer_count += clock_member_buffer_count;

    for (auto* net : clock_dag.reachableNets(clock)) {
      if (net == nullptr) {
        continue;
      }
      if (auto measurement = qor_evaluation::InstallClockNetRcTreeAndMeasure(qor_evaluation::ClockNetMeasurementInput{
              .config = &config,
              .sta_adapter = &sta_adapter,
              .wrapper = &wrapper,
              .net = net,
              .role = qor_evaluation::ClassifyClockNet(*clock, net),
              .install_sta_rc_tree = should_refresh_sta,
          });
          measurement.has_value()) {
        clock_net_measurements.push_back(*measurement);
      }
    }
  }

  bool timing_updated = false;
  if (should_refresh_sta) {
    sta_adapter.updateTiming();
    timing_updated = true;
    (void) sta_adapter.reportTiming();
    qor_evaluation::AppendClockLatencySkew(sta_adapter, summary);
  }
  qor_evaluation::AppendClockTimings(qor_evaluation::ClockTimingAppendInput{
      .sta_adapter = &sta_adapter,
      .reporter = &reporter,
      .query_sta_timing = timing_updated,
      .summary = &summary,
  });
  if (timing_updated) {
    qor_evaluation::EmitRootInputToLeafOutputProbeReport(qor_evaluation::RootInputToLeafOutputProbeReportInput{
        .sta_adapter = &sta_adapter,
        .design = &design,
        .reporter = &reporter,
        .clocks = &clocks,
        .clock_layout = input.clock_layout,
        .query_sta_timing = timing_updated,
    });
  }
  qor_evaluation::AppendClockNetStatistics(clock_net_measurements, summary, statistics);
  qor_evaluation::SyncCompatibilityAliases(summary);
  summary.timing_metric_source = timing_updated ? "final_sta" : "unavailable";
  if (should_refresh_sta) {
    summary.physical_metric_source = "final_idb";
  } else if (clock_net_measurements.empty()) {
    summary.physical_metric_source = "unavailable";
  } else {
    summary.physical_metric_source = "estimated_cts";
  }
  if (timing_updated) {
    summary.qor_metric_status = "final";
  } else if (clock_net_measurements.empty()) {
    summary.qor_metric_status = "unavailable";
  } else {
    summary.qor_metric_status = "estimated_only";
  }
  statistics.valid = timing_updated;
  summary.has_evaluation_result = timing_updated;
  qor_evaluation::EmitEvaluationSummary(reporter, summary, timing_updated);
}

auto QorEvaluation::outputSummary(const EvaluationState& state) -> QorSummary
{
  return state.summary;
}

auto QorEvaluation::isEvaluationReady(const EvaluationState& state) -> bool
{
  return state.summary.has_evaluation_result && state.statistics.valid;
}

auto QorEvaluation::reset(EvaluationState& state) -> void
{
  qor_evaluation::ClearSummary(state.summary);
  qor_evaluation::ClearStatistics(state.statistics);
}

}  // namespace icts
