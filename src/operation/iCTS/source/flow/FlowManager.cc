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
 * @file FlowManager.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief CTS flow orchestration manager implementation
 */

#include "FlowManager.hh"

#include <cstddef>
#include <string>

#include "evaluation/ClockTreeEvaluator.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "session/CTSRunEnvironment.hh"
#include "stage/CTSClockDataLoadStep.hh"
#include "stage/CTSClockTreeEvaluationStep.hh"
#include "stage/CTSClockTreeReportStep.hh"
#include "stage/CTSClockTreeSynthesisStep.hh"
#include "stage/CTSClockTreeWritebackStep.hh"

namespace icts {
namespace {

auto formatValueWithUnit(const std::string& value, const std::string& unit) -> std::string
{
  if (value == "n/a" || unit.empty()) {
    return value;
  }
  return value + " " + unit;
}

auto formatOptionalCount(std::size_t value) -> std::string
{
  return value == 0U ? std::string{"n/a"} : std::to_string(value);
}

auto formatOptionalUnsigned(unsigned value) -> std::string
{
  return value == 0U ? std::string{"n/a"} : std::to_string(value);
}

}  // namespace

auto FlowManager::runCTS() -> void
{
  SCHEMA_WRITER_INST.resetRuntimeMetrics();
  auto total_runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("total");
  auto run_stage = SCHEMA_WRITER_INST.beginStage("CTS", "Clock tree synthesis API flow");

  readData();
  run();
  writeback();
  evaluate();

  const bool run_success = _run_summary.success && _writeback_result.writeback_done;
  const auto total_metric = run_success ? total_runtime.finished() : total_runtime.failed();
  run_stage.markRunning("Main CTS flow finished");
  if (run_success) {
    run_stage.finished();
  } else {
    run_stage.failed();
  }

  SCHEMA_WRITER_INST.emitSection("## Runtime Summary");
  SCHEMA_WRITER_INST.emitRuntimeSummary("CTS Runtime Summary");
  emitKeyResults(total_metric.elapsed_time_s, total_metric.peak_vmem_delta_mb);
}

auto FlowManager::readData() -> void
{
  _run_summary = CTSClockTreeRunSummary{};
  _report_data.reset();
  ClockTreeEvaluator::reset(_evaluation_state);
  _writeback_result = CTSClockTreeWritebackResult{};
  _evaluation_ready = false;
  CTSClockDataLoadStep::run();
}

auto FlowManager::run() -> void
{
  ClockTreeEvaluator::reset(_evaluation_state);
  _evaluation_ready = false;
  _writeback_result = CTSClockTreeWritebackResult{};
  _run_summary = CTSClockTreeSynthesisStep::run(_report_data);
}

auto FlowManager::writeback() -> void
{
  _writeback_result = CTSClockTreeWritebackResult{};
  if (_run_summary.success) {
    _writeback_result = CTSClockTreeWritebackStep::run();
    _report_data.markWritebackDone(_writeback_result.writeback_done);
    _run_summary.success = _writeback_result.writeback_done;
  }
}

auto FlowManager::evaluate() -> void
{
  _evaluation_ready = CTSClockTreeEvaluationStep::run(_evaluation_state, _writeback_result.writeback_done).evaluation_ready;
}

auto FlowManager::report(const std::string& save_dir) -> void
{
  const auto report_result
      = CTSClockTreeReportStep::run(save_dir, _evaluation_ready, _writeback_result.writeback_done, _report_data, _evaluation_state);
  _evaluation_ready = report_result.evaluation_ready;
}

auto FlowManager::outputRuntimeSetup() -> void
{
  if (_runtime_setup_emitted) {
    return;
  }
  _runtime_setup_emitted = true;

  CTSRunEnvironment::emitRuntimeSetup();
}

auto FlowManager::emitKeyResults(double elapsed_time_s, double peak_vmem_delta_mb) const -> void
{
  const auto evaluation_summary = outputSummary();
  const std::size_t sink_count = _run_summary.hard_macro_sinks + _run_summary.regular_sinks;

  schema::KeyValueFields fields = {
      {"status", _run_summary.success ? "finished" : "failed"},
      {"clock_count", std::to_string(_run_summary.total_clocks)},
      {"sink_count", std::to_string(sink_count)},
      {"sink_domain_count", std::to_string(_run_summary.total_sink_domains)},
      {"selected_htree_level_count", formatOptionalCount(_run_summary.selected_htree_level_count)},
      {"selected_htree_depth", formatOptionalUnsigned(_run_summary.selected_htree_depth)},
      {"htree_inserted_buffer_count", std::to_string(_run_summary.htree_inserted_buffer_count)},
      {"final_clock_buffer_count", std::to_string(evaluation_summary.final_clock_buffer_count)},
      {"final_buffer_area", formatValueWithUnit(logformat::FormatFixed(evaluation_summary.final_buffer_area_um2, 3), "um^2")},
      {"max_clock_net_wirelength", formatValueWithUnit(logformat::FormatFixed(evaluation_summary.max_clock_net_wirelength_um, 3), "um")},
      {"total_clock_tree_wirelength",
       formatValueWithUnit(logformat::FormatFixed(evaluation_summary.total_clock_tree_wirelength_um, 3), "um")},
      {"elapsed_time", formatValueWithUnit(logformat::FormatFixed(elapsed_time_s, 3), "s")},
      {"peak_vmem_delta", formatValueWithUnit(logformat::FormatFixed(peak_vmem_delta_mb, 3), "MB")},
  };

  SCHEMA_WRITER_INST.emitSection("## Run Results");
  schema::EmitKeyValueTable("CTS Key Results", fields);
}

auto FlowManager::outputSummary() const -> ClockTreeSummary
{
  if (!_evaluation_ready) {
    return {};
  }
  return ClockTreeEvaluator::outputSummary(_evaluation_state);
}

auto FlowManager::outputRunSummary() const -> CTSClockTreeRunSummary
{
  return _run_summary;
}

auto FlowManager::reset() -> void
{
  ClockTreeEvaluator::reset(_evaluation_state);
  _run_summary = CTSClockTreeRunSummary{};
  _report_data.reset();
  _writeback_result = CTSClockTreeWritebackResult{};
  _runtime_setup_emitted = false;
  _evaluation_ready = false;
}

}  // namespace icts
