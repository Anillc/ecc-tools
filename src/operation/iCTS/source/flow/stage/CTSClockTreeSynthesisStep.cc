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
 * @file CTSClockTreeSynthesisStep.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief CTS clock-tree synthesis step orchestration implementation.
 */

#include "stage/CTSClockTreeSynthesisStep.hh"

#include <cstddef>
#include <ostream>
#include <string>

#include "Log.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "report_data/ClockTreeReportData.hh"
#include "stage/ClockTreeSynthesisDriver.hh"

namespace icts {
namespace {

auto emitClockTreeSynthesisSummary(const CTSClockTreeRunSummary& summary, const schema::TableRows& rows) -> void
{
  SCHEMA_WRITER_INST.emitSection("### Flow Status");
  schema::EmitKeyValueTable("CTS Clock Tree Synthesis Summary", {
                                                                    {"status", summary.success ? "finished" : "failed"},
                                                                    {"total_clocks", std::to_string(summary.total_clocks)},
                                                                    {"finished_clocks", std::to_string(summary.successful_clocks)},
                                                                    {"skipped_clocks", std::to_string(summary.skipped_clocks)},
                                                                    {"failed_clocks", std::to_string(summary.failed_clocks)},
                                                                    {"total_sink_domains", std::to_string(summary.total_sink_domains)},
                                                                    {"hard_macro_sinks", std::to_string(summary.hard_macro_sinks)},
                                                                    {"regular_sinks", std::to_string(summary.regular_sinks)},
                                                                });

  if (!rows.empty()) {
    schema::EmitTable("CTS Clock Tree Sink Domains", {"Clock", "Net", "Status", "Sink Domain", "Valid Sinks", "Domain Sinks", "Detail"},
                      rows);
  }
}

}  // namespace

auto CTSClockTreeSynthesisStep::run(ClockTreeReportData& report_data) -> CTSClockTreeRunSummary
{
  auto runtime = SCHEMA_WRITER_INST.beginRuntimeMetric("synthesis");
  auto flow_stage = SCHEMA_WRITER_INST.beginStage("CTSFlow", "Run CTS synthesis flow");
  SCHEMA_WRITER_INST.emitSection("## Synthesis Summary");

  report_data.reset();
  report_data.set_design_dbu_per_um(WRAPPER_INST.queryDbUnit());
  CTSClockTreeRunSummary summary;
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

    const auto clock_result = ClockTreeSynthesisDriver::run(*clock, clock_index, report_data, summary, rows, total_sink_domains,
                                                            hard_macro_sinks, regular_sinks);
    if (clock_result.success) {
      ++successful_clocks;
    } else if (clock_result.skipped) {
      ++skipped_clocks;
    } else {
      ++failed_clocks;
    }
  }

  summary.success = failed_clocks == 0U;
  summary.total_clocks = total_clocks;
  summary.successful_clocks = successful_clocks;
  summary.skipped_clocks = skipped_clocks;
  summary.failed_clocks = failed_clocks;
  summary.total_sink_domains = total_sink_domains;
  summary.hard_macro_sinks = hard_macro_sinks;
  summary.regular_sinks = regular_sinks;
  report_data.markSynthesisComplete(summary.success);

  LOG_INFO << "CTS clock-tree synthesis finished with " << successful_clocks << " successful, " << skipped_clocks << " skipped, "
           << failed_clocks << " failed clocks.";
  emitClockTreeSynthesisSummary(summary, rows);

  if (summary.success) {
    (void) runtime.finished();
    flow_stage.finished();
  } else {
    (void) runtime.failed();
    flow_stage.failed();
  }
  return summary;
}

}  // namespace icts
