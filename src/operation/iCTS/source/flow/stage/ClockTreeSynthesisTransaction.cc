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
 * @file ClockTreeSynthesisTransaction.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS clock-tree synthesis transaction implementation.
 */

#include "stage/ClockTreeSynthesisTransaction.hh"

#include <algorithm>
#include <cstdint>
#include <ostream>

#include "Log.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "geometry/Geometry.hh"
#include "htree/CharacterizationLibrary.hh"
#include "htree/HTreeBuilder.hh"
#include "io/Wrapper.hh"
#include "netlist/ClockNetEditor.hh"
#include "report_data/ClockTreeReportData.hh"
#include "report_data/ClockTreeReportDataBuilder.hh"
#include "stage/CTSClockTreeRunSummary.hh"
#include "stage/ClockSinkDomainBuilder.hh"
#include "stage/ClockTreeSynthesisStatusTable.hh"
#include "synthesis/ClockSynthesisReportAdapter.hh"

namespace icts {
namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;

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

auto clearClockCtsMembership(Clock& clock) -> void
{
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();
}

}  // namespace

ClockTreeSynthesisTransaction::ClockTreeSynthesisTransaction(Clock& clock, std::size_t clock_index, ClockTreeReportData& report_data,
                                                             CTSClockTreeRunSummary& summary, ClockTreeSynthesisStatusTable& status_table,
                                                             CharacterizationLibrary& characterization_library, std::size_t valid_sinks)
    : _clock(&clock),
      _clock_index(clock_index),
      _report_data(&report_data),
      _summary(&summary),
      _status_table(&status_table),
      _characterization_library(&characterization_library),
      _valid_sinks(valid_sinks)
{
}

auto ClockTreeSynthesisTransaction::rollbackClock(Clock& clock) -> void
{
  ClockNetEditor::restoreClockSourceNetToClockLoads(clock);
  clearClockCtsMembership(clock);
}

auto ClockTreeSynthesisTransaction::collectRootInputs(const std::vector<ClockSinkDomainContext>& sink_domains) -> std::vector<Pin*>
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

auto ClockTreeSynthesisTransaction::collectSourceToRootLengthsUm(Pin* clock_source, const std::vector<Pin*>& root_inputs)
    -> std::vector<double>
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

auto ClockTreeSynthesisTransaction::commitSinkDomain(const ClockSinkDomainContext& context, ClockSynthesis::BuildResult& synthesis_result,
                                                     std::string& failure_reason) -> bool
{
  auto pending_report_data = ClockTreeReportDataBuilder::makeSinkDomainReportData(
      *_clock, _clock_index, context.makeReportTopology(), ClockSynthesisReportAdapter::makeSinkDomainReportInput(synthesis_result));
  if (!ClockNetEditor::commitInsertedObjects(*_clock, synthesis_result.inserted_insts, synthesis_result.inserted_pins,
                                             synthesis_result.inserted_nets)) {
    ClockNetEditor::reconnectNet(*context.downstream_net, context.downstream_net->get_driver(), context.sinks);
    failure_reason = "failed to commit inserted synthesis objects";
    rollbackClock(*_clock);
    return false;
  }

  ClockTreeReportDataBuilder::merge(*_report_data, pending_report_data);
  recordSynthesisResult(*_summary, synthesis_result);
  return true;
}

auto ClockTreeSynthesisTransaction::synthesizeSinkDomain(const ClockSinkDomainContext& context,
                                                         const std::vector<double>& source_to_root_lengths_um) -> bool
{
  const auto* const sink_domain_label = ToString(context.sink_domain);
  if (context.sinks.size() < kMinSynthesisSinkCount) {
    ClockTreeReportDataBuilder::appendDirectSinkDomain(*_report_data, *_clock, _clock_index, context.makeReportTopology());
    _status_table->append(*_clock, ClockTreeSynthesisStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "direct");
    return true;
  }

  ClockSynthesis::BuildOptions synthesis_options;
  synthesis_options.object_name_prefix = context.domain_prefix;
  synthesis_options.enable_sink_clustering = CONFIG_INST.is_enable_sink_clustering();
  synthesis_options.characterization_library = _characterization_library;
  synthesis_options.additional_characterization_lengths_um = source_to_root_lengths_um;
  synthesis_options.log_context = makeLogContext(*_clock, sink_domain_label, "downstream_htree", context.domain_prefix);

  std::string failure_reason;
  auto synthesis_result = ClockSynthesis::build(*context.downstream_net, synthesis_options);
  if (!synthesis_result.success) {
    failure_reason = synthesis_result.failure_reason.empty() ? "sink-domain synthesis failed" : synthesis_result.failure_reason;
  } else {
    (void) commitSinkDomain(context, synthesis_result, failure_reason);
  }

  if (!failure_reason.empty()) {
    _status_table->append(*_clock, ClockTreeSynthesisStatus::kFailed, context.sink_domain, _valid_sinks, context.sinks.size(),
                          failure_reason);
    LOG_ERROR << "ClockTreeSynthesisTransaction: clock \"" << _clock->get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed: " << failure_reason;
    rollbackClock(*_clock);
    return false;
  }

  _status_table->append(*_clock, ClockTreeSynthesisStatus::kFinished, context.sink_domain, _valid_sinks, context.sinks.size(), "synthesis");
  return true;
}

auto ClockTreeSynthesisTransaction::synthesizeSourceToRoot(const std::vector<Pin*>& root_inputs) -> bool
{
  const auto source_to_root_domain = CTSSinkDomain::kSourceToRoot;
  const auto* const source_to_root_label = ToString(source_to_root_domain);
  auto* clock_source = _clock->get_clock_source();
  auto* clock_source_net = _clock->get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    _clock->set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr || clock_source_net == nullptr) {
    _status_table->append(*_clock, ClockTreeSynthesisStatus::kFailed, source_to_root_domain, _valid_sinks, root_inputs.size(),
                          "missing clock source or source net");
    LOG_ERROR << "ClockTreeSynthesisTransaction: clock \"" << _clock->get_clock_name()
              << "\" source-to-root synthesis failed because the source pin or net is missing.";
    rollbackClock(*_clock);
    return false;
  }

  const auto source_to_root_prefix = ClockNetEditor::makeSinkDomainPrefix(*_clock, _clock_index, source_to_root_domain);
  ClockSynthesis::SourceToRootBuildOptions options{
      .object_name_prefix = source_to_root_prefix,
      .characterization_library = _characterization_library,
      .log_context = makeLogContext(*_clock, source_to_root_label, "source_to_root", source_to_root_prefix),
  };
  auto source_to_root_result = ClockSynthesis::buildSourceToRoot(*clock_source_net, clock_source, root_inputs, options);
  const auto source_to_root_phase = sourceToRootSynthesisPhase(source_to_root_result.stage);
  if (!source_to_root_result.success) {
    const auto failure_reason
        = source_to_root_result.failure_reason.empty() ? "source-to-root synthesis failed" : source_to_root_result.failure_reason;
    _status_table->append(*_clock, ClockTreeSynthesisStatus::kFailed, source_to_root_domain, _valid_sinks, root_inputs.size(),
                          failure_reason);
    LOG_ERROR << "ClockTreeSynthesisTransaction: clock \"" << _clock->get_clock_name()
              << "\" source-to-root synthesis failed: " << failure_reason;
    rollbackClock(*_clock);
    return false;
  }

  auto pending_report_data = ClockTreeReportDataBuilder::makeSourceToRootReportData(
      *_clock, _clock_index, *clock_source_net,
      ClockSynthesisReportAdapter::makeSourceToRootReportInput(source_to_root_result, source_to_root_phase), source_to_root_phase);
  if (!ClockNetEditor::commitInsertedObjects(*_clock, source_to_root_result.inserted_insts, source_to_root_result.inserted_pins,
                                             source_to_root_result.inserted_nets)) {
    _status_table->append(*_clock, ClockTreeSynthesisStatus::kFailed, source_to_root_domain, _valid_sinks, root_inputs.size(),
                          "failed to commit source-to-root objects");
    LOG_ERROR << "ClockTreeSynthesisTransaction: clock \"" << _clock->get_clock_name()
              << "\" source-to-root synthesis failed while committing inserted objects.";
    rollbackClock(*_clock);
    return false;
  }

  ClockTreeReportDataBuilder::merge(*_report_data, pending_report_data);
  recordSourceToRootResult(*_summary, source_to_root_result);
  _status_table->append(*_clock, ClockTreeSynthesisStatus::kFinished, source_to_root_domain, _valid_sinks, root_inputs.size(),
                        ToString(source_to_root_result.stage));
  return true;
}

}  // namespace icts
