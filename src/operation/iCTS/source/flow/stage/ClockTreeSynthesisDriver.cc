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
 * @file ClockTreeSynthesisDriver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Per-clock CTS sink-domain synthesis coordinator implementation.
 */

#include "stage/ClockTreeSynthesisDriver.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "htree/CharacterizationLibrary.hh"
#include "htree/HTreeBuilder.hh"
#include "io/Wrapper.hh"
#include "netlist/ClockNetEditor.hh"
#include "report_data/ClockTreeReportDataBuilder.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {

namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;

struct SinkDomainContext
{
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  std::string domain_prefix;
  std::vector<Pin*> sinks;
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  Net* downstream_net = nullptr;
};

auto domainLabel(CTSSinkDomain domain) -> std::string
{
  return ToString(domain);
}

auto makeSinkDomainReportTopology(const SinkDomainContext& context) -> ClockSinkDomainReportTopology
{
  return ClockSinkDomainReportTopology{
      .sink_domain = context.sink_domain,
      .root_buffer = context.root_buffer,
      .downstream_net = context.downstream_net,
  };
}

auto recordSynthesisResult(CTSClockTreeRunSummary& summary, const ClockSynthesis::BuildResult& result) -> void
{
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.selected_htree_level_count);
  if (result.selected_htree_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.selected_htree_depth);
  }
  summary.htree_inserted_buffer_count += result.htree_inserted_buffer_count;
  summary.htree_inserted_net_count += result.htree_inserted_net_count;
}

auto recordSourceToRootResult(CTSClockTreeRunSummary& summary, const ClockSynthesis::SourceToRootBuildResult& result) -> void
{
  if (result.htree_result.selected_depth.has_value()) {
    summary.selected_htree_depth = std::max(summary.selected_htree_depth, *result.htree_result.selected_depth);
  }
  summary.selected_htree_level_count = std::max(summary.selected_htree_level_count, result.htree_result.levels.size());
  summary.htree_inserted_buffer_count += result.inserted_buffer_count;
  summary.htree_inserted_net_count += result.inserted_net_count;
}

auto sourceToRootSynthesisPhase(ClockSynthesis::SourceToRootStage stage) -> ClockTreeSynthesisPhase
{
  switch (stage) {
    case ClockSynthesis::SourceToRootStage::kSegment:
      return ClockTreeSynthesisPhase::kSourceToRootSegment;
    case ClockSynthesis::SourceToRootStage::kHTree:
      return ClockTreeSynthesisPhase::kSourceToRootHTree;
    case ClockSynthesis::SourceToRootStage::kUnknown:
      return ClockTreeSynthesisPhase::kUnknown;
  }
  return ClockTreeSynthesisPhase::kUnknown;
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

auto appendFlowRow(schema::TableRows& rows, const Clock& clock, const std::string& status, const std::string& sink_domain_label,
                   std::size_t valid_sinks, std::size_t sink_domain_sinks, const std::string& detail) -> void
{
  rows.push_back({
      clock.get_clock_name(),
      clock.get_clock_net_name(),
      status,
      sink_domain_label,
      std::to_string(valid_sinks),
      std::to_string(sink_domain_sinks),
      detail,
  });
}

auto clearClockCtsMembership(Clock& clock) -> void
{
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();
}

auto rollbackClockRun(Clock& clock) -> void
{
  ClockNetEditor::restoreClockSourceNetToClockLoads(clock);
  clearClockCtsMembership(clock);
}

auto prepareSinkDomain(Clock& clock, std::size_t clock_index, CTSSinkDomain sink_domain, const std::vector<Pin*>& sinks,
                       std::size_t valid_sinks, schema::TableRows& rows, SinkDomainContext& context) -> bool
{
  if (sinks.empty()) {
    return true;
  }

  const auto sink_domain_label = domainLabel(sink_domain);
  const auto domain_prefix = ClockNetEditor::makeSinkDomainPrefix(clock, clock_index, sink_domain);
  context.sink_domain = sink_domain;
  context.domain_prefix = domain_prefix;
  context.sinks = sinks;
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  if (!ClockNetEditor::addRootBufferForSinkDomain(clock, domain_prefix, sinks, root_buffer, root_input, root_output)) {
    appendFlowRow(rows, clock, "failed", sink_domain_label, valid_sinks, sinks.size(), "failed to insert root buffer");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed because root-buffer insertion failed.";
    return false;
  }
  context.root_buffer = root_buffer;
  context.root_input = root_input;
  context.root_output = root_output;

  auto* downstream_net = ClockNetEditor::connectSinkDomainDownstreamNet(clock, domain_prefix, root_output, sinks);
  if (downstream_net == nullptr) {
    appendFlowRow(rows, clock, "failed", sink_domain_label, valid_sinks, sinks.size(), "failed to create downstream net");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed because downstream net creation failed.";
    return false;
  }
  context.downstream_net = downstream_net;
  return true;
}

auto commitSynthesizedSinkDomain(Clock& clock, const SinkDomainContext& context, ClockSynthesis::BuildResult& synthesis_result,
                                 CTSClockTreeRunSummary& summary, std::string& failure_reason) -> bool
{
  if (!ClockNetEditor::commitInsertedObjects(clock, synthesis_result.inserted_insts, synthesis_result.inserted_pins,
                                             synthesis_result.inserted_nets)) {
    ClockNetEditor::reconnectNet(*context.downstream_net, context.downstream_net->get_driver(), context.sinks);
    failure_reason = "failed to commit inserted synthesis objects";
    return false;
  }
  recordSynthesisResult(summary, synthesis_result);
  return true;
}

auto synthesizePreparedSinkDomain(Clock& clock, std::size_t clock_index, ClockTreeReportData& report_data, const SinkDomainContext& context,
                                  CharacterizationLibrary& char_library, const std::vector<double>& source_to_root_lengths_um,
                                  std::size_t valid_sinks, CTSClockTreeRunSummary& summary, schema::TableRows& rows) -> bool
{
  const auto sink_domain_label = domainLabel(context.sink_domain);
  if (context.sinks.size() < kMinSynthesisSinkCount) {
    ClockTreeReportDataBuilder::appendDirectSinkDomain(report_data, clock, clock_index, makeSinkDomainReportTopology(context));
    appendFlowRow(rows, clock, "finished", sink_domain_label, valid_sinks, context.sinks.size(), "direct");
    return true;
  }
  std::string failure_reason;
  ClockSynthesis::BuildOptions synthesis_options;
  synthesis_options.object_name_prefix = context.domain_prefix;
  synthesis_options.enable_sink_clustering = CONFIG_INST.is_enable_sink_clustering();
  synthesis_options.characterization_library = &char_library;
  synthesis_options.additional_characterization_lengths_um = source_to_root_lengths_um;
  synthesis_options.log_context = makeLogContext(clock, sink_domain_label, "downstream_htree", context.domain_prefix);
  auto synthesis_result = ClockSynthesis::build(*context.downstream_net, synthesis_options);
  if (!synthesis_result.success) {
    failure_reason = synthesis_result.failure_reason.empty() ? "sink-domain synthesis failed" : synthesis_result.failure_reason;
  } else {
    auto pending_report_data
        = ClockTreeReportDataBuilder::makeSinkDomainReportData(clock, clock_index, makeSinkDomainReportTopology(context), synthesis_result);
    if (commitSynthesizedSinkDomain(clock, context, synthesis_result, summary, failure_reason)) {
      ClockTreeReportDataBuilder::merge(report_data, pending_report_data);
    }
  }
  if (!failure_reason.empty()) {
    appendFlowRow(rows, clock, "failed", sink_domain_label, valid_sinks, context.sinks.size(), failure_reason);
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed: " << failure_reason;
    return false;
  }

  appendFlowRow(rows, clock, "finished", sink_domain_label, valid_sinks, context.sinks.size(), "synthesis");
  return true;
}

auto collectRootInputs(const std::vector<SinkDomainContext>& sink_domains) -> std::vector<Pin*>
{
  std::vector<Pin*> root_inputs;
  root_inputs.reserve(sink_domains.size());
  for (const auto& context : sink_domains) {
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

auto synthesizeSourceToRoot(Clock& clock, std::size_t clock_index, ClockTreeReportData& report_data, CharacterizationLibrary& char_library,
                            const std::vector<Pin*>& root_inputs, CTSClockTreeRunSummary& summary, schema::TableRows& rows,
                            std::size_t valid_sinks) -> bool
{
  const auto source_to_root_domain = CTSSinkDomain::kSourceToRoot;
  const auto source_to_root_label = domainLabel(source_to_root_domain);
  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr || clock_source_net == nullptr) {
    appendFlowRow(rows, clock, "failed", source_to_root_label, valid_sinks, root_inputs.size(), "missing clock source or source net");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name()
              << "\" top-level source-to-root synthesis failed because the source pin or net is missing.";
    return false;
  }

  const auto source_to_root_prefix = ClockNetEditor::makeSinkDomainPrefix(clock, clock_index, source_to_root_domain);
  ClockSynthesis::SourceToRootBuildOptions options{
      .object_name_prefix = source_to_root_prefix,
      .characterization_library = &char_library,
      .log_context = makeLogContext(clock, source_to_root_label, "source_to_root", source_to_root_prefix),
  };
  auto source_to_root_result = ClockSynthesis::buildSourceToRoot(*clock_source_net, clock_source, root_inputs, options);
  const auto source_to_root_phase = sourceToRootSynthesisPhase(source_to_root_result.stage);
  if (!source_to_root_result.success) {
    const auto failure_reason
        = source_to_root_result.failure_reason.empty() ? "source-to-root synthesis failed" : source_to_root_result.failure_reason;
    appendFlowRow(rows, clock, "failed", source_to_root_label, valid_sinks, root_inputs.size(), failure_reason);
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" top-level source-to-root synthesis failed: " << failure_reason;
    return false;
  }

  auto pending_report_data = ClockTreeReportDataBuilder::makeSourceToRootReportData(clock, clock_index, *clock_source_net,
                                                                                    source_to_root_result, source_to_root_phase);
  if (!ClockNetEditor::commitInsertedObjects(clock, source_to_root_result.inserted_insts, source_to_root_result.inserted_pins,
                                             source_to_root_result.inserted_nets)) {
    appendFlowRow(rows, clock, "failed", source_to_root_label, valid_sinks, root_inputs.size(), "failed to commit source-to-root objects");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name()
              << "\" top-level source-to-root synthesis failed while committing inserted objects.";
    return false;
  }

  ClockTreeReportDataBuilder::merge(report_data, pending_report_data);
  recordSourceToRootResult(summary, source_to_root_result);
  appendFlowRow(rows, clock, "finished", source_to_root_label, valid_sinks, root_inputs.size(), ToString(source_to_root_result.stage));
  return true;
}

}  // namespace

auto ClockTreeSynthesisDriver::run(Clock& clock, std::size_t clock_index, ClockTreeReportData& report_data, CTSClockTreeRunSummary& summary,
                                   schema::TableRows& rows, std::size_t& total_sink_domains, std::size_t& hard_macro_sink_count,
                                   std::size_t& regular_sink_count) -> ClockTreeSynthesisResult
{
  rollbackClockRun(clock);
  ClockTreeReportData clock_report_data;
  clock_report_data.ensureClock(clock.get_clock_name(), clock.get_clock_net_name(), clock_index);

  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr) {
    appendFlowRow(rows, clock, "skipped", "none", 0U, 0U, "clock source is null");
    LOG_WARNING << "FlowManager: skip clock \"" << clock.get_clock_name() << "\" because clock source is null.";
    return ClockTreeSynthesisResult{.success = false, .skipped = true};
  }
  if (clock_source_net == nullptr) {
    appendFlowRow(rows, clock, "failed", "none", 0U, 0U, "clock source net is null");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" failed because the clock source net is null.";
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }

  std::vector<Pin*> macro_sinks;
  std::vector<Pin*> regular_sinks;
  ClockNetEditor::partitionClockSinks(clock.get_loads(), macro_sinks, regular_sinks);
  const auto valid_sinks = macro_sinks.size() + regular_sinks.size();
  hard_macro_sink_count += macro_sinks.size();
  regular_sink_count += regular_sinks.size();
  if (valid_sinks == 0U) {
    appendFlowRow(rows, clock, "skipped", "none", 0U, 0U, "no valid sinks");
    LOG_WARNING << "FlowManager: skip clock \"" << clock.get_clock_name() << "\" because no valid sinks are available.";
    return ClockTreeSynthesisResult{.success = false, .skipped = true};
  }
  ClockTreeReportDataBuilder::appendSinkInsts(clock_report_data, clock, clock_index, macro_sinks, CTSSinkDomain::kHardMacro);
  ClockTreeReportDataBuilder::appendSinkInsts(clock_report_data, clock, clock_index, regular_sinks, CTSSinkDomain::kRegular);

  std::vector<SinkDomainContext> sink_domain_contexts;
  sink_domain_contexts.reserve(2U);

  auto prepare_non_empty_domain = [&](CTSSinkDomain sink_domain, const std::vector<Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    ++total_sink_domains;
    SinkDomainContext context;
    if (!prepareSinkDomain(clock, clock_index, sink_domain, sinks, valid_sinks, rows, context)) {
      rollbackClockRun(clock);
      return false;
    }
    sink_domain_contexts.push_back(std::move(context));
    return true;
  };

  if (!prepare_non_empty_domain(CTSSinkDomain::kHardMacro, macro_sinks)) {
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }
  if (!prepare_non_empty_domain(CTSSinkDomain::kRegular, regular_sinks)) {
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }

  auto root_inputs = collectRootInputs(sink_domain_contexts);
  const auto source_to_root_lengths_um = collectSourceToRootLengthsUm(clock_source, root_inputs);
  CharacterizationLibrary char_library;
  for (const auto& context : sink_domain_contexts) {
    if (!synthesizePreparedSinkDomain(clock, clock_index, clock_report_data, context, char_library, source_to_root_lengths_um, valid_sinks,
                                      summary, rows)) {
      rollbackClockRun(clock);
      return ClockTreeSynthesisResult{.success = false, .skipped = false};
    }
  }

  if (!synthesizeSourceToRoot(clock, clock_index, clock_report_data, char_library, root_inputs, summary, rows, valid_sinks)) {
    rollbackClockRun(clock);
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }
  ClockTreeReportDataBuilder::merge(report_data, clock_report_data);
  return ClockTreeSynthesisResult{.success = true, .skipped = false};
}

}  // namespace icts
