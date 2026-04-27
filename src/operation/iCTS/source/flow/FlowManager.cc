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

#include <cstddef>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/Schema.hh"
#include "netlist/ClockNetManager.hh"
#include "synthesis/ClockSynthesis.hh"

namespace icts {

class Inst;

namespace {

constexpr std::size_t kMinSynthesisSinkCount = 2U;
constexpr const char* kHardMacroSinkGroup = "hard_macro";
constexpr const char* kRegularSinkGroup = "regular";

auto synthesizeSinkGroup(Clock& clock, const std::string& group_prefix, Net& downstream_net, const std::vector<Pin*>& sinks,
                         std::string& failure_reason) -> bool
{
  ClockSynthesis::BuildOptions synthesis_options;
  synthesis_options.object_name_prefix = group_prefix;
  auto synthesis_result = ClockSynthesis::build(downstream_net, synthesis_options);
  if (!synthesis_result.success) {
    failure_reason = synthesis_result.failure_reason.empty() ? "sink-group synthesis failed" : synthesis_result.failure_reason;
    return false;
  }
  if (!ClockNetManager::commitInsertedObjects(clock, synthesis_result.inserted_insts, synthesis_result.inserted_pins,
                                              synthesis_result.inserted_nets)) {
    ClockNetManager::reconnectNet(downstream_net, downstream_net.get_driver(), sinks);
    failure_reason = "failed to commit inserted synthesis objects";
    return false;
  }
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

auto buildSinkGroup(Clock& clock, std::size_t clock_index, const std::string& sink_group, const std::vector<Pin*>& sinks,
                    std::vector<Pin*>& root_inputs, std::size_t valid_sinks, schema::TableRows& rows) -> bool
{
  if (sinks.empty()) {
    return true;
  }

  const auto group_prefix = ClockNetManager::makeSinkGroupPrefix(clock, clock_index, sink_group);
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  if (!ClockNetManager::addRootBufferForSinkGroup(clock, group_prefix, sinks, root_buffer, root_input, root_output)) {
    appendFlowRow(rows, clock, "failed", sink_group, valid_sinks, sinks.size(), "failed to insert root buffer");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << sink_group
              << " failed because root-buffer insertion failed.";
    return false;
  }
  if (root_input != nullptr) {
    root_inputs.push_back(root_input);
  }

  auto* downstream_net = ClockNetManager::connectSinkGroupDownstreamNet(clock, group_prefix, root_output, sinks);
  if (downstream_net == nullptr) {
    appendFlowRow(rows, clock, "failed", sink_group, valid_sinks, sinks.size(), "failed to create downstream net");
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << sink_group
              << " failed because downstream net creation failed.";
    return false;
  }

  if (sinks.size() < kMinSynthesisSinkCount) {
    appendFlowRow(rows, clock, "success", sink_group, valid_sinks, sinks.size(), "direct");
    return true;
  }

  std::string failure_reason;
  if (!synthesizeSinkGroup(clock, group_prefix, *downstream_net, sinks, failure_reason)) {
    appendFlowRow(rows, clock, "failed", sink_group, valid_sinks, sinks.size(), failure_reason);
    LOG_ERROR << "FlowManager: clock \"" << clock.get_clock_name() << "\" sink group " << sink_group << " failed: " << failure_reason;
    return false;
  }

  appendFlowRow(rows, clock, "success", sink_group, valid_sinks, sinks.size(), "synthesis");
  return true;
}

auto emitFlowSummary(bool success, std::size_t total_clocks, std::size_t successful_clocks, std::size_t skipped_clocks,
                     std::size_t failed_clocks, std::size_t total_sink_groups, std::size_t hard_macro_sinks, std::size_t regular_sinks,
                     const schema::TableRows& rows) -> void
{
  schema::EmitKeyValueTable("CTS Flow Summary", {
                                                    {"success", success ? "true" : "false"},
                                                    {"total_clocks", std::to_string(total_clocks)},
                                                    {"successful_clocks", std::to_string(successful_clocks)},
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

auto runClock(Clock& clock, std::size_t clock_index, schema::TableRows& rows, std::size_t& total_sink_groups,
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

  std::vector<Pin*> root_inputs;
  root_inputs.reserve(2U);

  auto build_non_empty_group = [&](const std::string& sink_group, const std::vector<Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    ++total_sink_groups;
    if (!buildSinkGroup(clock, clock_index, sink_group, sinks, root_inputs, valid_sinks, rows)) {
      ClockNetManager::restoreClockSourceNetToClockLoads(clock);
      clearClockCtsMembership(clock);
      return false;
    }
    return true;
  };

  if (!build_non_empty_group(kHardMacroSinkGroup, macro_sinks)) {
    return false;
  }
  if (!build_non_empty_group(kRegularSinkGroup, regular_sinks)) {
    return false;
  }

  ClockNetManager::reuseClockSourceNetAsSourceToRootBuffers(clock, clock_source, root_inputs);
  return true;
}

}  // namespace

auto FlowManager::readData() -> void
{
  ClockTreeEvaluator::resetSummary();
  ClockNetManager::readClockData();
}

auto FlowManager::run() -> void
{
  ClockTreeEvaluator::resetSummary();
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
    if (runClock(*clock, clock_index, rows, total_sink_groups, hard_macro_sinks, regular_sinks, skipped)) {
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
  emitFlowSummary(success, total_clocks, successful_clocks, skipped_clocks, failed_clocks, total_sink_groups, hard_macro_sinks,
                  regular_sinks, rows);
}

auto FlowManager::evaluate() -> void
{
  ClockTreeEvaluator::evaluate();
}

auto FlowManager::outputSummary() -> ClockTreeSummary
{
  return ClockTreeEvaluator::outputSummary();
}

auto FlowManager::reset() -> void
{
  ClockTreeEvaluator::resetSummary();
}

}  // namespace icts
