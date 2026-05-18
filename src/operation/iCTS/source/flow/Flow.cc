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
 * @file Flow.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS flow lifecycle owner implementation.
 */

#include "Flow.hh"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ClockDAG.hh"
#include "ClockLayout.hh"
#include "design/Design.hh"
#include "evaluation/Evaluation.hh"
#include "instantiation/Instantiation.hh"
#include "instantiation/design_conversion/DesignConversion.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "optimization/Optimization.hh"
#include "report/Report.hh"
#include "setup/Setup.hh"
#include "synthesis/Synthesis.hh"

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

auto synthesisOutcomeName(SynthesisOutcome outcome) -> std::string
{
  switch (outcome) {
    case SynthesisOutcome::kFinished:
      return "finished";
    case SynthesisOutcome::kFailed:
      return "failed";
    case SynthesisOutcome::kNoOp:
      return "no_op";
  }
  return "failed";
}

}  // namespace

auto Flow::runCTS() -> void
{
  SCHEMA_WRITER_INST.resetRuntimeMetrics();
  auto total_runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("total");
  auto run_stage = SCHEMA_WRITER_INST.beginStage("CTS", "Clock tree synthesis API flow", {},
                                                 schema::StageReportOptions{.emit_success_summary = false});

  if (!_setup_ready) {
    _run_summary = SynthesisTraceSummary{};
    _run_summary.success = false;
    _run_summary.outcome = SynthesisOutcome::kFailed;
    run_stage.failed({{"reason", "setup_failed"}});
    const auto total_metric = total_runtime.failed();
    SCHEMA_WRITER_INST.emitSection("## Runtime Overview");
    SCHEMA_WRITER_INST.emitRuntimeSummary("CTS Runtime Overview");
    emitKeyResults(total_metric.elapsed_time_s, total_metric.peak_vmem_delta_mb);
    return;
  }

  if (!readData()) {
    _run_summary = SynthesisTraceSummary{};
    _run_summary.success = false;
    _run_summary.outcome = SynthesisOutcome::kFailed;
    run_stage.failed({{"reason", "read_data_failed"}});
    const auto total_metric = total_runtime.failed();
    SCHEMA_WRITER_INST.emitSection("## Runtime Overview");
    SCHEMA_WRITER_INST.emitRuntimeSummary("CTS Runtime Overview");
    emitKeyResults(total_metric.elapsed_time_s, total_metric.peak_vmem_delta_mb);
    return;
  }
  run();
  instantiate();
  evaluate();

  const bool run_success
      = _run_summary.outcome == SynthesisOutcome::kFinished && _run_summary.success && _instantiation_result.instantiation_done;
  const bool run_no_op = _run_summary.outcome == SynthesisOutcome::kNoOp;
  schema::SchemaWriter::RuntimeMetricSnapshot total_metric;
  if (run_success) {
    total_metric = total_runtime.finished();
  } else if (run_no_op) {
    total_metric = total_runtime.finish("no_op");
  } else {
    total_metric = total_runtime.failed();
  }
  run_stage.markRunning("Main CTS flow finished");
  if (run_success) {
    run_stage.finished();
  } else if (run_no_op) {
    run_stage.skip({{"reason", _run_summary.no_op_reason}});
  } else {
    run_stage.failed();
  }

  SCHEMA_WRITER_INST.emitSection("## Runtime Overview");
  SCHEMA_WRITER_INST.emitRuntimeSummary("CTS Runtime Overview");
  emitKeyResults(total_metric.elapsed_time_s, total_metric.peak_vmem_delta_mb);
}

auto Flow::readData() -> bool
{
  _run_summary = SynthesisTraceSummary{};
  _clock_layout.reset();
  _char_library = CharacterizationLibrary{};
  Evaluation::reset(_evaluation_state);
  _instantiation_result = InstantiationResult{};
  _evaluation_ready = false;

  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("read_data");
  auto read_stage
      = SCHEMA_WRITER_INST.beginStage("CTSReadData", "Read CTS clock data", {}, schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Input Overview");
  SCHEMA_WRITER_INST.emitSection("### Clock Data");
  const bool read_data_ready = DesignConversion::readClockData();
  if (read_data_ready) {
    (void) runtime.finished();
    read_stage.finished();
  } else {
    (void) runtime.failed();
    read_stage.failed({{"reason", "sdc_clock_resolution_failed"}});
  }
  return read_data_ready;
}

auto Flow::run() -> void
{
  Evaluation::reset(_evaluation_state);
  _evaluation_ready = false;
  _instantiation_result = InstantiationResult{};
  DESIGN_INST.clearClockDAG();
  _run_summary = Synthesis::run(_clock_layout, _char_library);
  if (_run_summary.outcome == SynthesisOutcome::kFinished && _run_summary.success && !DESIGN_INST.rebuildClockDAG()) {
    _run_summary.success = false;
    _run_summary.outcome = SynthesisOutcome::kFailed;
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "CTSFlow",
                           "synthesized CTS topology is not a valid clock DAG; instantiation and final evaluation are blocked.",
                           {{"reason", DESIGN_INST.get_clock_dag().get_status()}});
    return;
  }
  if (_run_summary.outcome == SynthesisOutcome::kFinished && _run_summary.success) {
    const auto optimization_result = Optimization::run(_clock_layout, _char_library);
    if (!optimization_result.success) {
      _run_summary.success = false;
      _run_summary.outcome = SynthesisOutcome::kFailed;
      return;
    }
    if (!DESIGN_INST.rebuildClockDAG()) {
      _run_summary.success = false;
      _run_summary.outcome = SynthesisOutcome::kFailed;
      schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "CTSFlow",
                             "optimized CTS topology is not a valid clock DAG; instantiation and final evaluation are blocked.",
                             {{"reason", DESIGN_INST.get_clock_dag().get_status()}});
    }
  }
}

auto Flow::instantiate() -> void
{
  _instantiation_result = InstantiationResult{};
  if (_run_summary.outcome == SynthesisOutcome::kFinished && _run_summary.success) {
    _instantiation_result = Instantiation::run();
    _clock_layout.markInstantiationDone(_instantiation_result.instantiation_done);
    _run_summary.success = _instantiation_result.instantiation_done;
  }
}

auto Flow::evaluate() -> void
{
  if (_run_summary.outcome != SynthesisOutcome::kFinished || !_instantiation_result.instantiation_done) {
    Evaluation::reset(_evaluation_state);
    _evaluation_ready = false;
    return;
  }
  _evaluation_ready = Evaluation::run(_evaluation_state, EvaluationOptions{.refresh_sta_timing = _instantiation_result.instantiation_done,
                                                                           .clock_layout = &_clock_layout})
                          .evaluation_ready;
}

auto Flow::report(const std::string& save_dir) -> void
{
  const auto report_result
      = Report::run(save_dir, _evaluation_ready, _instantiation_result.instantiation_done, _clock_layout, _evaluation_state);
  _evaluation_ready = report_result.evaluation_ready;
}

auto Flow::outputRuntimeSetup() -> void
{
  if (_runtime_setup_emitted) {
    return;
  }
  _runtime_setup_emitted = true;

  Setup::emitRuntimeSetup();
}

auto Flow::emitKeyResults(double elapsed_time_s, double peak_vmem_delta_mb) const -> void
{
  const auto evaluation_summary = outputSummary();
  const std::size_t sink_count = _run_summary.hard_macro_sinks + _run_summary.regular_sinks;

  schema::KeyValueFields fields = {
      {"status", synthesisOutcomeName(_run_summary.outcome)},
  };
  if (!_run_summary.no_op_reason.empty()) {
    fields.emplace_back("no_op_reason", _run_summary.no_op_reason);
  }
  fields.insert(fields.end(),
                {
                    {"clock_count", std::to_string(_run_summary.total_clocks)},
                    {"sink_count", std::to_string(sink_count)},
                    {"sink_domain_count", std::to_string(_run_summary.total_sink_domains)},
                    {"selected_htree_level_count", formatOptionalCount(_run_summary.selected_htree_level_count)},
                    {"selected_htree_depth", formatOptionalUnsigned(_run_summary.selected_htree_depth)},
                    {"htree_inserted_buffer_count", std::to_string(_run_summary.htree_inserted_buffer_count)},
                    {"final_clock_buffer_count", std::to_string(evaluation_summary.final_clock_buffer_count)},
                    {"final_buffer_area", formatValueWithUnit(logformat::FormatFixed(evaluation_summary.final_buffer_area_um2, 3), "um^2")},
                    {"max_clock_net_wirelength",
                     formatValueWithUnit(logformat::FormatFixed(evaluation_summary.max_clock_net_wirelength_um, 3), "um")},
                    {"total_clock_network_wirelength",
                     formatValueWithUnit(logformat::FormatFixed(evaluation_summary.total_clock_network_wirelength_um, 3), "um")},
                    {"elapsed_time", formatValueWithUnit(logformat::FormatFixed(elapsed_time_s, 3), "s")},
                    {"peak_vmem_delta", formatValueWithUnit(logformat::FormatFixed(peak_vmem_delta_mb, 3), "MB")},
                });

  SCHEMA_WRITER_INST.emitSection("## Run Results");
  schema::EmitKeyValueTable("CTS Key Results", fields);
}

auto Flow::outputSummary() const -> QorSummary
{
  if (!_evaluation_ready) {
    return {};
  }
  return Evaluation::outputSummary(_evaluation_state);
}

auto Flow::outputRunSummary() const -> SynthesisTraceSummary
{
  return _run_summary;
}

auto Flow::reset() -> void
{
  Evaluation::reset(_evaluation_state);
  _run_summary = SynthesisTraceSummary{};
  _clock_layout.reset();
  _char_library = CharacterizationLibrary{};
  _instantiation_result = InstantiationResult{};
  _runtime_setup_emitted = false;
  _setup_ready = false;
  _evaluation_ready = false;
}

}  // namespace icts
