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

#include <glog/logging.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "evaluation/ClockTreeEvaluator.hh"
#include "geometry/Geometry.hh"
#include "htree/CharacterizationLibrary.hh"
#include "htree/HTreeBuilder.hh"
#include "io/Wrapper.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "netlist/ClockNetManager.hh"
#include "report/CTSVisualizationReport.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {

class Inst;

namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;
constexpr const char* kHardMacroSinkGroup = "hard_macro";
constexpr const char* kRegularSinkGroup = "regular";
constexpr const char* kTopSinkGroup = "top";

struct SinkGroupContext
{
  std::string sink_group;
  std::string group_prefix;
  std::vector<Pin*> sinks;
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  Net* downstream_net = nullptr;
};

auto resolveReportRootDir(const std::string& save_dir) -> std::filesystem::path
{
  if (!save_dir.empty()) {
    return std::filesystem::path(save_dir);
  }
  return std::filesystem::path(CONFIG_INST.get_work_dir());
}

auto recordSynthesisResult(CTSFlowRunSummary& summary, const ClockSynthesis::BuildResult& result) -> void
{
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.selected_htree_level_count);
  if (result.selected_htree_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.selected_htree_depth);
  }
  summary.htree_inserted_buffer_count += result.htree_inserted_buffer_count;
  summary.htree_inserted_net_count += result.htree_inserted_net_count;
}

auto recordSourceToRootResult(CTSFlowRunSummary& summary, const ClockSynthesis::SourceToRootBuildResult& result) -> void
{
  if (result.htree_result.selected_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.htree_result.selected_depth);
  }
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.htree_result.levels.size());
  summary.htree_inserted_buffer_count += result.inserted_buffer_count;
  summary.htree_inserted_net_count += result.inserted_net_count;
}

auto makeLogContext(const Clock& clock, const std::string& sink_domain, const std::string& stage, const std::string& object_name_prefix)
    -> HTreeBuilder::LogContext
{
  return HTreeBuilder::LogContext{
      .clock_name = clock.get_clock_name(),
      .clock_net_name = clock.get_clock_net_name(),
      .sink_domain = sink_domain,
      .stage = stage,
      .object_name_prefix = object_name_prefix,
  };
}

auto synthesizeSinkGroup(Clock& clock, const SinkGroupContext& context, CharacterizationLibrary& char_library,
                         const std::vector<double>& source_to_root_lengths_um, CTSFlowRunSummary& summary, std::string& failure_reason)
    -> bool
{
  ClockSynthesis::BuildOptions synthesis_options;
  synthesis_options.object_name_prefix = context.group_prefix;
  synthesis_options.enable_sink_clustering = CONFIG_INST.is_enable_sink_clustering();
  synthesis_options.characterization_library = &char_library;
  synthesis_options.additional_characterization_lengths_um = source_to_root_lengths_um;
  synthesis_options.log_context = makeLogContext(clock, context.sink_group, "downstream_htree", context.group_prefix);
  auto synthesis_result = ClockSynthesis::build(*context.downstream_net, synthesis_options);
  if (!synthesis_result.success) {
    failure_reason = synthesis_result.failure_reason.empty() ? "sink-group synthesis failed" : synthesis_result.failure_reason;
    return false;
  }
  if (!ClockNetManager::commitInsertedObjects(clock, synthesis_result.inserted_insts, synthesis_result.inserted_pins,
                                              synthesis_result.inserted_nets)) {
    ClockNetManager::reconnectNet(*context.downstream_net, context.downstream_net->get_driver(), context.sinks);
    failure_reason = "failed to commit inserted synthesis objects";
    return false;
  }
  recordSynthesisResult(summary, synthesis_result);
  return true;
}

auto appendFlowRow(schema::TableRows& rows, const Clock& clock, const std::string& status, const std::string& sink_group,
                   std::size_t valid_sinks, std::size_t sink_group_sinks, const std::string& detail) -> void
{
  rows.push_back({
      clock.get_clock_name(),
      clock.get_clock_net_name(),
      status,
      sink_group,
      std::to_string(valid_sinks),
      std::to_string(sink_group_sinks),
      detail,
  });
}

auto clearClockCtsMembership(Clock& clock) -> void
{
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();
}

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

auto prepareSinkGroup(Clock& clock, std::size_t clock_index, const std::string& sink_group, const std::vector<Pin*>& sinks,
                      std::size_t valid_sinks, schema::TableRows& rows, SinkGroupContext& context) -> bool
{
  if (sinks.empty()) {
    return true;
  }

  const auto group_prefix = ClockNetManager::makeSinkGroupPrefix(clock, clock_index, sink_group);
  context.sink_group = sink_group;
  context.group_prefix = group_prefix;
  context.sinks = sinks;
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  if (!ClockNetManager::addRootBufferForSinkGroup(clock, group_prefix, sinks, root_buffer, root_input, root_output)) {
    appendFlowRow(rows, clock, "failed", sink_group, valid_sinks, sinks.size(), "failed to insert root buffer");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << sink_group
              << " failed because root-buffer insertion failed.";
    return false;
  }
  context.root_buffer = root_buffer;
  context.root_input = root_input;
  context.root_output = root_output;

  auto* downstream_net = ClockNetManager::connectSinkGroupDownstreamNet(clock, group_prefix, root_output, sinks);
  if (downstream_net == nullptr) {
    appendFlowRow(rows, clock, "failed", sink_group, valid_sinks, sinks.size(), "failed to create downstream net");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << sink_group
              << " failed because downstream net creation failed.";
    return false;
  }
  context.downstream_net = downstream_net;
  return true;
}

auto synthesizePreparedSinkGroup(Clock& clock, const SinkGroupContext& context, CharacterizationLibrary& char_library,
                                 const std::vector<double>& source_to_root_lengths_um, std::size_t valid_sinks, CTSFlowRunSummary& summary,
                                 schema::TableRows& rows) -> bool
{
  if (context.sinks.size() < kMinSynthesisSinkCount) {
    appendFlowRow(rows, clock, "finished", context.sink_group, valid_sinks, context.sinks.size(), "direct");
    return true;
  }
  std::string failure_reason;
  if (!synthesizeSinkGroup(clock, context, char_library, source_to_root_lengths_um, summary, failure_reason)) {
    appendFlowRow(rows, clock, "failed", context.sink_group, valid_sinks, context.sinks.size(), failure_reason);
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << context.sink_group
              << " failed: " << failure_reason;
    return false;
  }

  appendFlowRow(rows, clock, "finished", context.sink_group, valid_sinks, context.sinks.size(), "synthesis");
  return true;
}

auto collectRootInputs(const std::vector<SinkGroupContext>& sink_groups) -> std::vector<Pin*>
{
  std::vector<Pin*> root_inputs;
  root_inputs.reserve(sink_groups.size());
  for (const auto& context : sink_groups) {
    if (context.root_input != nullptr) {
      root_inputs.push_back(context.root_input);
    }
  }
  return root_inputs;
}

auto collectSourceToRootLengthsUm(Pin* clock_source, const std::vector<Pin*>& root_inputs) -> std::vector<double>
{
  std::vector<double> lengths_um;
  if (clock_source == nullptr) {
    return lengths_um;
  }

  const double dbu_per_um = static_cast<double>(std::max(WRAPPER_INST.queryDbUnit(), int32_t{1}));
  lengths_um.reserve(root_inputs.size());
  for (const auto* root_input : root_inputs) {
    if (root_input == nullptr) {
      continue;
    }
    const int distance_dbu = geometry::Manhattan(clock_source->get_location(), root_input->get_location());
    const double length_um = static_cast<double>(std::max(distance_dbu, 0)) / dbu_per_um;
    if (length_um > 0.0) {
      lengths_um.push_back(length_um);
    }
  }
  return lengths_um;
}

auto synthesizeSourceToRoot(Clock& clock, std::size_t clock_index, CharacterizationLibrary& char_library,
                            const std::vector<Pin*>& root_inputs, CTSFlowRunSummary& summary, schema::TableRows& rows,
                            std::size_t valid_sinks) -> bool
{
  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr || clock_source_net == nullptr) {
    appendFlowRow(rows, clock, "failed", kTopSinkGroup, valid_sinks, root_inputs.size(), "missing clock source or source net");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name()
              << "\" top-level source-to-root synthesis failed because the source pin or net is missing.";
    return false;
  }

  const auto top_prefix = ClockNetManager::makeSinkGroupPrefix(clock, clock_index, kTopSinkGroup);
  ClockSynthesis::SourceToRootBuildOptions options{
      .object_name_prefix = top_prefix,
      .characterization_library = &char_library,
      .log_context = makeLogContext(clock, kTopSinkGroup, kTopSinkGroup, top_prefix),
  };
  auto source_to_root_result = ClockSynthesis::buildSourceToRoot(*clock_source_net, clock_source, root_inputs, options);
  if (!source_to_root_result.success) {
    const auto failure_reason
        = source_to_root_result.failure_reason.empty() ? "source-to-root synthesis failed" : source_to_root_result.failure_reason;
    appendFlowRow(rows, clock, "failed", kTopSinkGroup, valid_sinks, root_inputs.size(), failure_reason);
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" top-level source-to-root synthesis failed: " << failure_reason;
    return false;
  }

  if (!ClockNetManager::commitInsertedObjects(clock, source_to_root_result.inserted_insts, source_to_root_result.inserted_pins,
                                              source_to_root_result.inserted_nets)) {
    appendFlowRow(rows, clock, "failed", kTopSinkGroup, valid_sinks, root_inputs.size(), "failed to commit source-to-root objects");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name()
              << "\" top-level source-to-root synthesis failed while committing inserted objects.";
    return false;
  }

  recordSourceToRootResult(summary, source_to_root_result);
  appendFlowRow(rows, clock, "finished", kTopSinkGroup, valid_sinks, root_inputs.size(), source_to_root_result.stage);
  return true;
}

auto emitFlowSummary(bool success, std::size_t total_clocks, std::size_t successful_clocks, std::size_t skipped_clocks,
                     std::size_t failed_clocks, std::size_t total_sink_groups, std::size_t hard_macro_sinks, std::size_t regular_sinks,
                     const schema::TableRows& rows) -> void
{
  SCHEMA_WRITER_INST.emitSection("### Flow Status");
  schema::EmitKeyValueTable("CTS Flow Summary", {
                                                    {"status", success ? "finished" : "failed"},
                                                    {"total_clocks", std::to_string(total_clocks)},
                                                    {"finished_clocks", std::to_string(successful_clocks)},
                                                    {"skipped_clocks", std::to_string(skipped_clocks)},
                                                    {"failed_clocks", std::to_string(failed_clocks)},
                                                    {"total_sink_groups", std::to_string(total_sink_groups)},
                                                    {"hard_macro_sinks", std::to_string(hard_macro_sinks)},
                                                    {"regular_sinks", std::to_string(regular_sinks)},
                                                });

  if (!rows.empty()) {
    schema::EmitTable("CTS Flow Sink Groups", {"Clock", "Net", "Status", "Sink Group", "Valid Sinks", "Group Sinks", "Detail"}, rows);
  }
}

auto runClock(Clock& clock, std::size_t clock_index, CTSFlowRunSummary& summary, schema::TableRows& rows, std::size_t& total_sink_groups,
              std::size_t& hard_macro_sink_count, std::size_t& regular_sink_count, bool& skipped) -> bool
{
  skipped = false;
  ClockNetManager::restoreClockSourceNetToClockLoads(clock);
  clearClockCtsMembership(clock);

  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr) {
    skipped = true;
    appendFlowRow(rows, clock, "skipped", "none", 0U, 0U, "clock source is null");
    LOG_WARNING << "FlowManager: skip clock \"" << clock.get_clock_name() << "\" because clock source is null.";
    return false;
  }
  if (clock_source_net == nullptr) {
    appendFlowRow(rows, clock, "failed", "none", 0U, 0U, "clock source net is null");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" failed because the clock source net is null.";
    return false;
  }

  std::vector<Pin*> macro_sinks;
  std::vector<Pin*> regular_sinks;
  ClockNetManager::partitionClockSinks(clock.get_loads(), macro_sinks, regular_sinks);
  const auto valid_sinks = macro_sinks.size() + regular_sinks.size();
  hard_macro_sink_count += macro_sinks.size();
  regular_sink_count += regular_sinks.size();
  if (valid_sinks == 0U) {
    skipped = true;
    appendFlowRow(rows, clock, "skipped", "none", 0U, 0U, "no valid sinks");
    LOG_WARNING << "FlowManager: skip clock \"" << clock.get_clock_name() << "\" because no valid sinks are available.";
    return false;
  }

  std::vector<SinkGroupContext> sink_group_contexts;
  sink_group_contexts.reserve(2U);

  auto prepare_non_empty_group = [&](const std::string& sink_group, const std::vector<Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    ++total_sink_groups;
    SinkGroupContext context;
    if (!prepareSinkGroup(clock, clock_index, sink_group, sinks, valid_sinks, rows, context)) {
      ClockNetManager::restoreClockSourceNetToClockLoads(clock);
      clearClockCtsMembership(clock);
      return false;
    }
    sink_group_contexts.push_back(std::move(context));
    return true;
  };

  if (!prepare_non_empty_group(kHardMacroSinkGroup, macro_sinks)) {
    return false;
  }
  if (!prepare_non_empty_group(kRegularSinkGroup, regular_sinks)) {
    return false;
  }

  auto root_inputs = collectRootInputs(sink_group_contexts);
  const auto source_to_root_lengths_um = collectSourceToRootLengthsUm(clock_source, root_inputs);
  CharacterizationLibrary char_library;
  for (const auto& context : sink_group_contexts) {
    if (!synthesizePreparedSinkGroup(clock, context, char_library, source_to_root_lengths_um, valid_sinks, summary, rows)) {
      ClockNetManager::restoreClockSourceNetToClockLoads(clock);
      clearClockCtsMembership(clock);
      return false;
    }
  }

  if (!synthesizeSourceToRoot(clock, clock_index, char_library, root_inputs, summary, rows, valid_sinks)) {
    ClockNetManager::restoreClockSourceNetToClockLoads(clock);
    clearClockCtsMembership(clock);
    return false;
  }
  return true;
}

}  // namespace

auto FlowManager::runCTS() -> void
{
  SCHEMA_WRITER_INST.resetRuntimeMetrics();
  auto total_runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("total");
  auto run_stage = SCHEMA_WRITER_INST.beginStage("CTS", "Clock tree synthesis API flow");

  readData();
  run();
  evaluate();

  const bool run_success = _run_summary.success;
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
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("read_data");
  auto read_stage = SCHEMA_WRITER_INST.beginStage("CTSReadData", "Read CTS clock data");
  SCHEMA_WRITER_INST.emitSection("## Input Summary");
  SCHEMA_WRITER_INST.emitSection("### Clock Data");
  _run_summary = CTSFlowRunSummary{};
  _evaluation_ready = false;
  ClockTreeEvaluator::resetSummary();
  ClockNetManager::readClockData();
  (void) runtime.finished();
  read_stage.finished();
}

auto FlowManager::run() -> void
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("synthesis");
  auto flow_stage = SCHEMA_WRITER_INST.beginStage("CTSFlow", "Run CTS synthesis flow");
  SCHEMA_WRITER_INST.emitSection("## Synthesis Summary");

  ClockTreeEvaluator::resetSummary();
  _run_summary = CTSFlowRunSummary{};
  _evaluation_ready = false;
  auto clocks = DESIGN_INST.get_clocks();
  const std::size_t total_clocks = clocks.size();
  std::size_t successful_clocks = 0U;
  std::size_t skipped_clocks = 0U;
  std::size_t failed_clocks = 0U;
  std::size_t total_sink_groups = 0U;
  std::size_t hard_macro_sinks = 0U;
  std::size_t regular_sinks = 0U;
  schema::TableRows rows;

  for (std::size_t clock_index = 0; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      ++skipped_clocks;
      rows.push_back({"", "", "skipped", "none", "0", "0", "clock pointer is null"});
      continue;
    }

    bool skipped = false;
    if (runClock(*clock, clock_index, _run_summary, rows, total_sink_groups, hard_macro_sinks, regular_sinks, skipped)) {
      ++successful_clocks;
    } else if (skipped) {
      ++skipped_clocks;
    } else {
      ++failed_clocks;
    }
  }

  const bool success = failed_clocks == 0U;
  LOG_INFO << "CTS flow finished with " << successful_clocks << " successful, " << skipped_clocks << " skipped, " << failed_clocks
           << " failed clocks.";
  _run_summary.success = success;
  _run_summary.total_clocks = total_clocks;
  _run_summary.successful_clocks = successful_clocks;
  _run_summary.skipped_clocks = skipped_clocks;
  _run_summary.failed_clocks = failed_clocks;
  _run_summary.total_sink_groups = total_sink_groups;
  _run_summary.hard_macro_sinks = hard_macro_sinks;
  _run_summary.regular_sinks = regular_sinks;
  emitFlowSummary(success, total_clocks, successful_clocks, skipped_clocks, failed_clocks, total_sink_groups, hard_macro_sinks,
                  regular_sinks, rows);

  if (success) {
    (void) runtime.finished();
    flow_stage.finished();
  } else {
    (void) runtime.failed();
    flow_stage.failed();
  }
}

auto FlowManager::evaluate() -> void
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("evaluation");
  auto evaluation_stage = SCHEMA_WRITER_INST.beginStage("CTSEvaluation", "Evaluate CTS clock tree");
  SCHEMA_WRITER_INST.emitSection("## Evaluation Summary");
  SCHEMA_WRITER_INST.emitSection("### Final Evaluation");
  ClockTreeEvaluator::evaluate();
  _evaluation_ready = ClockTreeEvaluator::hasEvaluationResult();
  if (_evaluation_ready) {
    (void) runtime.finished();
    evaluation_stage.finished();
  } else {
    (void) runtime.failed();
    evaluation_stage.failed();
  }
}

auto FlowManager::report(const std::string& save_dir) -> void
{
  LOG_FATAL_IF(CONFIG_INST.get_work_dir().empty()) << "CTS report requires an initialized CTS session.";

  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("report");
  auto report_stage = SCHEMA_WRITER_INST.beginStage("CTSReport", "Emit CTS statistics reports");
  const auto report_root_dir = resolveReportRootDir(save_dir);
  const bool reused_evaluation_state = _evaluation_ready && ClockTreeEvaluator::hasEvaluationResult();

  SCHEMA_WRITER_INST.emitSection("## Report Summary");
  schema::EmitKeyValueTable("CTS Report Mode",
                            {
                                {"mode", reused_evaluation_state ? "reuse_evaluation_state" : "rebuild_evaluation_state"},
                                {"save_dir", report_root_dir.string()},
                                {"statistics_dir", (report_root_dir / "statistics").string()},
                            });

  if (!reused_evaluation_state) {
    SCHEMA_WRITER_INST.emitSection("### Report Evaluation");
    evaluate();
  }

  const bool statistics_success = _evaluation_ready && ClockTreeEvaluator::writeStatistics(report_root_dir.string(), false);
  const auto visualization_result = report::EmitCTSVisualizationArtifacts(report_root_dir);
  const bool report_success = statistics_success && visualization_result.success;
  const auto report_metric = report_success ? runtime.finished() : runtime.failed();
  SCHEMA_WRITER_INST.emitRuntimeMetricTable("CTS Report Runtime", "report", report_success ? "finished" : "failed", report_metric);
  if (report_success) {
    report_stage.finished({{"statistics_status", statistics_success ? "finished" : "failed"},
                           {"visualization_status", visualization_result.success ? "finished" : "failed"}});
  } else {
    report_stage.failed({{"statistics_status", statistics_success ? "finished" : "failed"},
                         {"visualization_status", visualization_result.success ? "finished" : "failed"}});
  }
}

auto FlowManager::outputRuntimeSetup() -> void
{
  if (_runtime_setup_emitted) {
    return;
  }
  _runtime_setup_emitted = true;

  SCHEMA_WRITER_INST.emitSection("## Runtime Setup");
  schema::EmitKeyValueTable("Runtime Paths", {
                                                 {"cts_log", CONFIG_INST.get_log_file()},
                                                 {"work_dir", CONFIG_INST.get_work_dir()},
                                                 {"output_def_dir", CONFIG_INST.get_output_def_path()},
                                                 {"gds_file", CONFIG_INST.get_gds_file()},
                                             });
  SCHEMA_WRITER_INST.emitSection("### Runtime Configuration");
  CONFIG_INST.emitRuntimeConfigReport("Runtime Configuration");
  SCHEMA_WRITER_INST.emitSection("### Runtime Routing / Wire RC");
  STA_ADAPTER_INST.emitConfiguredUnitWireRcReport("Runtime Routing / Wire RC");
}

auto FlowManager::emitKeyResults(double elapsed_time_s, double peak_vmem_delta_mb) const -> void
{
  const auto evaluation_summary = outputSummary();
  const std::size_t sink_count = _run_summary.hard_macro_sinks + _run_summary.regular_sinks;

  schema::KeyValueFields fields = {
      {"status", _run_summary.success ? "finished" : "failed"},
      {"clock_count", std::to_string(_run_summary.total_clocks)},
      {"sink_count", std::to_string(sink_count)},
      {"sink_group_count", std::to_string(_run_summary.total_sink_groups)},
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
  return ClockTreeEvaluator::outputSummary();
}

auto FlowManager::outputRunSummary() const -> CTSFlowRunSummary
{
  return _run_summary;
}

auto FlowManager::reset() -> void
{
  ClockTreeEvaluator::resetSummary();
  _run_summary = CTSFlowRunSummary{};
  _runtime_setup_emitted = false;
  _evaluation_ready = false;
}

}  // namespace icts
