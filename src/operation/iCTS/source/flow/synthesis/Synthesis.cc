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
 * @file Synthesis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS synthesis entry facade implementation.
 */

#include "synthesis/Synthesis.hh"

#include <glog/logging.h>

#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "synthesis/distribution/ClockDistribution.hh"
#include "synthesis/topology/Topology.hh"
#include "synthesis/trace/domain_status/DomainStatus.hh"
#include "synthesis/trace/layout/ClockLayoutBuilder.hh"

namespace icts {
namespace {

struct ClockSynthesisResult
{
  bool success = false;
  bool skipped = false;
};

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

auto emitSynthesisOverview(const SynthesisTraceSummary& summary, const schema::TableRows& rows) -> void
{
  SCHEMA_WRITER_INST.emitSection("### Flow Status");
  schema::KeyValueFields fields = {
      {"status", synthesisOutcomeName(summary.outcome)},
  };
  if (!summary.no_op_reason.empty()) {
    fields.emplace_back("no_op_reason", summary.no_op_reason);
  }
  fields.insert(fields.end(), {
                                  {"total_clocks", std::to_string(summary.total_clocks)},
                                  {"finished_clocks", std::to_string(summary.successful_clocks)},
                                  {"skipped_clocks", std::to_string(summary.skipped_clocks)},
                                  {"failed_clocks", std::to_string(summary.failed_clocks)},
                                  {"total_sink_domains", std::to_string(summary.total_sink_domains)},
                                  {"hard_macro_sinks", std::to_string(summary.hard_macro_sinks)},
                                  {"regular_sinks", std::to_string(summary.regular_sinks)},
                              });
  schema::EmitKeyValueTable("CTS Clock Tree Synthesis Overview", fields);

  if (!rows.empty()) {
    schema::EmitTable("CTS Clock Tree Sink Domains", {"Clock", "Net", "Status", "Sink Domain", "Valid Sinks", "Domain Sinks", "Detail"},
                      rows);
  }
}

auto synthesizeClock(Clock& clock, std::size_t clock_index, ClockLayout& clock_layout, SynthesisTraceSummary& summary,
                     schema::TableRows& rows, std::size_t& total_sink_domains, std::size_t& hard_macro_sink_count,
                     std::size_t& regular_sink_count, CharacterizationLibrary& char_library) -> ClockSynthesisResult
{
  DomainStatusTable status_table(rows);
  Topology::resetClockTopology(clock);
  ClockLayout per_clock_layout;
  per_clock_layout.ensureClock(clock.get_clock_name(), clock.get_clock_net_name(), clock_index);

  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr) {
    status_table.appendNoDomain(clock, DomainStatus::kSkipped, 0U, 0U, "clock source is null");
    LOG_WARNING << "Synthesis: skip clock \"" << clock.get_clock_name() << "\" because clock source is null.";
    return ClockSynthesisResult{.success = false, .skipped = true};
  }
  if (clock_source_net == nullptr) {
    status_table.appendNoDomain(clock, DomainStatus::kFailed, 0U, 0U, "clock source net is null");
    LOG_ERROR << "Synthesis: clock \"" << clock.get_clock_name() << "\" failed because the clock source net is null.";
    return ClockSynthesisResult{.success = false, .skipped = false};
  }

  const auto sink_partition = ClockDistribution::partitionSinkDomains(clock);
  const auto valid_sinks = sink_partition.valid_sink_count;
  hard_macro_sink_count += sink_partition.macro_sinks.size();
  regular_sink_count += sink_partition.regular_sinks.size();
  if (valid_sinks == 0U) {
    status_table.appendNoDomain(clock, DomainStatus::kSkipped, 0U, 0U, "no valid sinks");
    LOG_WARNING << "Synthesis: skip clock \"" << clock.get_clock_name() << "\" because no valid sinks are available.";
    return ClockSynthesisResult{.success = false, .skipped = true};
  }
  ClockLayoutBuilder::appendSinkInsts(per_clock_layout, clock, clock_index, sink_partition.macro_sinks, SinkDomainKind::kHardMacro);
  ClockLayoutBuilder::appendSinkInsts(per_clock_layout, clock, clock_index, sink_partition.regular_sinks, SinkDomainKind::kRegular);

  std::vector<ClockDistributionContext> sink_domain_contexts;
  sink_domain_contexts.reserve(2U);

  auto prepare_non_empty_domain = [&](SinkDomainKind sink_domain, const std::vector<Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    ++total_sink_domains;
    ClockDistributionContext context;
    if (!ClockDistribution::prepare(clock, clock_index, sink_domain, sinks, valid_sinks, status_table, context)) {
      Topology::resetClockTopology(clock);
      return false;
    }
    sink_domain_contexts.push_back(std::move(context));
    return true;
  };

  if (!prepare_non_empty_domain(SinkDomainKind::kHardMacro, sink_partition.macro_sinks)) {
    return ClockSynthesisResult{.success = false, .skipped = false};
  }
  if (!prepare_non_empty_domain(SinkDomainKind::kRegular, sink_partition.regular_sinks)) {
    return ClockSynthesisResult{.success = false, .skipped = false};
  }

  if (!Topology::formClock(clock, clock_index, per_clock_layout, summary, status_table, char_library, valid_sinks, sink_domain_contexts)) {
    return ClockSynthesisResult{.success = false, .skipped = false};
  }
  ClockLayoutBuilder::merge(clock_layout, per_clock_layout);
  return ClockSynthesisResult{.success = true, .skipped = false};
}

auto recordDomainStatusRows(SynthesisTraceSummary& summary, const schema::TableRows& rows) -> void
{
  summary.domain_status.clear();
  summary.domain_status.reserve(rows.size());
  for (const auto& row : rows) {
    if (row.size() < 7U) {
      continue;
    }
    auto parse_count = [](const std::string& value) -> std::size_t {
      std::size_t count = 0U;
      std::istringstream stream(value);
      stream >> count;
      return stream.fail() ? 0U : count;
    };
    summary.domain_status.push_back(SynthesisTraceStatusRecord{
        .clock_name = row.at(0),
        .clock_net_name = row.at(1),
        .status = row.at(2),
        .sink_domain = row.at(3),
        .valid_sink_count = parse_count(row.at(4)),
        .sink_domain_sink_count = parse_count(row.at(5)),
        .detail = row.at(6),
    });
  }
}

}  // namespace

auto Synthesis::run(ClockLayout& clock_layout, CharacterizationLibrary& char_library) -> SynthesisTraceSummary
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("synthesis");
  auto flow_stage
      = SCHEMA_WRITER_INST.beginStage("CTSFlow", "Run CTS synthesis flow", {}, schema::StageReportOptions{.emit_success_summary = false});
  SCHEMA_WRITER_INST.emitSection("## Synthesis Overview");

  clock_layout.reset();
  clock_layout.set_design_dbu_per_um(WRAPPER_INST.queryDbUnit());
  SynthesisTraceSummary summary;
  auto clocks = DESIGN_INST.get_clocks();
  const std::size_t total_clocks = clocks.size();
  std::size_t successful_clocks = 0U;
  std::size_t skipped_clocks = 0U;
  std::size_t failed_clocks = 0U;
  std::size_t total_sink_domains = 0U;
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

    const auto clock_result = synthesizeClock(*clock, clock_index, clock_layout, summary, rows, total_sink_domains, hard_macro_sinks,
                                              regular_sinks, char_library);
    if (clock_result.success) {
      ++successful_clocks;
    } else if (clock_result.skipped) {
      ++skipped_clocks;
    } else {
      ++failed_clocks;
    }
  }

  summary.total_clocks = total_clocks;
  summary.successful_clocks = successful_clocks;
  summary.skipped_clocks = skipped_clocks;
  summary.failed_clocks = failed_clocks;
  summary.success = successful_clocks > 0U && failed_clocks == 0U;
  if (total_clocks == 0U) {
    summary.outcome = SynthesisOutcome::kNoOp;
    summary.no_op_reason = "no_clocks_discovered";
  } else if (successful_clocks == 0U && skipped_clocks > 0U && failed_clocks == 0U) {
    summary.outcome = SynthesisOutcome::kNoOp;
    summary.no_op_reason = "all_clocks_skipped";
  } else {
    summary.outcome = summary.success ? SynthesisOutcome::kFinished : SynthesisOutcome::kFailed;
  }
  summary.total_sink_domains = total_sink_domains;
  summary.hard_macro_sinks = hard_macro_sinks;
  summary.regular_sinks = regular_sinks;
  recordDomainStatusRows(summary, rows);
  clock_layout.markSynthesisComplete(summary.success);

  LOG_INFO << "CTS clock-tree synthesis finished with " << successful_clocks << " successful, " << skipped_clocks << " skipped, "
           << failed_clocks << " failed clocks.";
  emitSynthesisOverview(summary, rows);

  if (summary.outcome == SynthesisOutcome::kFinished) {
    (void) runtime.finished();
    flow_stage.finished();
  } else if (summary.outcome == SynthesisOutcome::kNoOp) {
    (void) runtime.finish("no_op");
    flow_stage.skip({{"reason", summary.no_op_reason}});
  } else {
    (void) runtime.failed();
    flow_stage.failed();
  }
  return summary;
}

}  // namespace icts
