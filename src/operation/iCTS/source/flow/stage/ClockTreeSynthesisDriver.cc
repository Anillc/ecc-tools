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

#include <cstddef>
#include <ostream>
#include <utility>
#include <vector>

#include "Log.hh"
#include "clock_tree_view/ClockTreeViewBuilder.hh"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "htree/CharacterizationLibrary.hh"
#include "stage/ClockSinkDomainBuilder.hh"
#include "stage/ClockTreeSynthesisStatusTable.hh"
#include "stage/ClockTreeSynthesisTransaction.hh"

namespace icts {

auto ClockTreeSynthesisDriver::run(Clock& clock, std::size_t clock_index, ClockTreeView& clock_tree_view, CTSClockTreeRunSummary& summary,
                                   schema::TableRows& rows, std::size_t& total_sink_domains, std::size_t& hard_macro_sink_count,
                                   std::size_t& regular_sink_count) -> ClockTreeSynthesisResult
{
  ClockTreeSynthesisStatusTable status_table(rows);
  ClockTreeSynthesisTransaction::rollbackClock(clock);
  ClockTreeView per_clock_view;
  per_clock_view.ensureClock(clock.get_clock_name(), clock.get_clock_net_name(), clock_index);

  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source == nullptr) {
    status_table.appendNoDomain(clock, ClockTreeSynthesisStatus::kSkipped, 0U, 0U, "clock source is null");
    LOG_WARNING << "ClockTreeSynthesisDriver: skip clock \"" << clock.get_clock_name() << "\" because clock source is null.";
    return ClockTreeSynthesisResult{.success = false, .skipped = true};
  }
  if (clock_source_net == nullptr) {
    status_table.appendNoDomain(clock, ClockTreeSynthesisStatus::kFailed, 0U, 0U, "clock source net is null");
    LOG_ERROR << "ClockTreeSynthesisDriver: clock \"" << clock.get_clock_name() << "\" failed because the clock source net is null.";
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }

  const auto sink_partition = ClockSinkDomainBuilder::partitionSinkDomains(clock);
  const auto valid_sinks = sink_partition.valid_sink_count;
  hard_macro_sink_count += sink_partition.macro_sinks.size();
  regular_sink_count += sink_partition.regular_sinks.size();
  if (valid_sinks == 0U) {
    status_table.appendNoDomain(clock, ClockTreeSynthesisStatus::kSkipped, 0U, 0U, "no valid sinks");
    LOG_WARNING << "ClockTreeSynthesisDriver: skip clock \"" << clock.get_clock_name() << "\" because no valid sinks are available.";
    return ClockTreeSynthesisResult{.success = false, .skipped = true};
  }
  ClockTreeViewBuilder::appendSinkInsts(per_clock_view, clock, clock_index, sink_partition.macro_sinks, CTSSinkDomain::kHardMacro);
  ClockTreeViewBuilder::appendSinkInsts(per_clock_view, clock, clock_index, sink_partition.regular_sinks, CTSSinkDomain::kRegular);

  CharacterizationLibrary char_library;
  ClockTreeSynthesisTransaction transaction(clock, clock_index, per_clock_view, summary, status_table, char_library, valid_sinks);
  std::vector<ClockSinkDomainContext> sink_domain_contexts;
  sink_domain_contexts.reserve(2U);

  auto prepare_non_empty_domain = [&](CTSSinkDomain sink_domain, const std::vector<Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    ++total_sink_domains;
    ClockSinkDomainContext context;
    if (!ClockSinkDomainBuilder::prepare(clock, clock_index, sink_domain, sinks, valid_sinks, status_table, context)) {
      ClockTreeSynthesisTransaction::rollbackClock(clock);
      return false;
    }
    sink_domain_contexts.push_back(std::move(context));
    return true;
  };

  if (!prepare_non_empty_domain(CTSSinkDomain::kHardMacro, sink_partition.macro_sinks)) {
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }
  if (!prepare_non_empty_domain(CTSSinkDomain::kRegular, sink_partition.regular_sinks)) {
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }

  auto root_inputs = ClockTreeSynthesisTransaction::collectRootInputs(sink_domain_contexts);
  const auto source_to_root_lengths_um = ClockTreeSynthesisTransaction::collectSourceToRootLengthsUm(clock_source, root_inputs);
  for (const auto& context : sink_domain_contexts) {
    if (!transaction.synthesizeSinkDomain(context, source_to_root_lengths_um)) {
      return ClockTreeSynthesisResult{.success = false, .skipped = false};
    }
  }

  if (!transaction.synthesizeSourceToRoot(root_inputs)) {
    return ClockTreeSynthesisResult{.success = false, .skipped = false};
  }
  ClockTreeViewBuilder::merge(clock_tree_view, per_clock_view);
  return ClockTreeSynthesisResult{.success = true, .skipped = false};
}

}  // namespace icts
