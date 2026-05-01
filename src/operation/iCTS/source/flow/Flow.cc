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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
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

#include "evaluation/Evaluation.hh"
#include "instantiation/Instantiation.hh"
#include "instantiation/design_conversion/DesignConversion.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
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

}  // namespace

auto Flow::runCTS() -> void
{
  SCHEMA_WRITER_INST.resetRuntimeMetrics();
  auto total_runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("total");
  auto run_stage = SCHEMA_WRITER_INST.beginStage("CTS", "Clock tree synthesis API flow");

  readData();
  run();
  instantiate();
  evaluate();

  const bool run_success = _run_summary.success && _instantiation_result.instantiation_done;
  const auto total_metric = run_success ? total_runtime.finished() : total_runtime.failed();
  run_stage.markRunning("Main CTS flow finished");
  if (run_success) {
    run_stage.finished();
  } else {
    run_stage.failed();
  }

  SCHEMA_WRITER_INST.emitSection("## Runtime Overview");
  SCHEMA_WRITER_INST.emitRuntimeSummary("CTS Runtime Overview");
  emitKeyResults(total_metric.elapsed_time_s, total_metric.peak_vmem_delta_mb);
}

auto Flow::readData() -> void
{
  _run_summary = SynthesisTraceSummary{};
  _clock_layout.reset();
  Evaluation::reset(_evaluation_state);
  _instantiation_result = InstantiationResult{};
  _evaluation_ready = false;

  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("read_data");
  auto read_stage = SCHEMA_WRITER_INST.beginStage("CTSReadData", "Read CTS clock data");
  SCHEMA_WRITER_INST.emitSection("## Input Overview");
  SCHEMA_WRITER_INST.emitSection("### Clock Data");
  DesignConversion::readClockData();
  (void) runtime.finished();
  read_stage.finished();
}

auto Flow::run() -> void
{
  Evaluation::reset(_evaluation_state);
  _evaluation_ready = false;
  _instantiation_result = InstantiationResult{};
  _run_summary = Synthesis::run(_clock_layout);
}

auto Flow::instantiate() -> void
{
  _instantiation_result = InstantiationResult{};
  if (_run_summary.success) {
    _instantiation_result = Instantiation::run();
    _clock_layout.markInstantiationDone(_instantiation_result.instantiation_done);
    _run_summary.success = _instantiation_result.instantiation_done;
  }
}

auto Flow::evaluate() -> void
{
  _evaluation_ready = Evaluation::run(_evaluation_state, _instantiation_result.instantiation_done).evaluation_ready;
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
      {"total_clock_network_wirelength",
       formatValueWithUnit(logformat::FormatFixed(evaluation_summary.total_clock_network_wirelength_um, 3), "um")},
      {"elapsed_time", formatValueWithUnit(logformat::FormatFixed(elapsed_time_s, 3), "s")},
      {"peak_vmem_delta", formatValueWithUnit(logformat::FormatFixed(peak_vmem_delta_mb, 3), "MB")},
  };

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
  _instantiation_result = InstantiationResult{};
  _runtime_setup_emitted = false;
  _evaluation_ready = false;
}

}  // namespace icts
