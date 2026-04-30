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
 * @file ClockSinkDomainBuilder.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS sink-domain preparation implementation.
 */

#include "stage/ClockSinkDomainBuilder.hh"

#include <ostream>

#include "Log.hh"
#include "design/Clock.hh"
#include "netlist/ClockNetEditor.hh"

namespace icts {
namespace {

auto addRootBuffer(Clock& clock, const std::string& domain_prefix, const std::vector<Pin*>& sinks,
                   const ClockSinkDomainRootBufferSpec* root_buffer_spec, Inst*& root_buffer, Pin*& root_input, Pin*& root_output) -> bool
{
  if (root_buffer_spec == nullptr) {
    return ClockNetEditor::addRootBufferForSinkDomain(clock, domain_prefix, sinks, root_buffer, root_input, root_output);
  }
  return ClockNetEditor::addRootBufferForSinkDomain(clock, domain_prefix, root_buffer_spec->cell_master, root_buffer_spec->input_pin_name,
                                                    root_buffer_spec->output_pin_name, sinks, root_buffer, root_input, root_output);
}

}  // namespace

auto ClockSinkDomainBuilder::partitionSinkDomains(const Clock& clock) -> ClockSinkDomainPartition
{
  ClockSinkDomainPartition partition;
  ClockNetEditor::partitionClockSinks(clock.get_loads(), partition.macro_sinks, partition.regular_sinks);
  partition.valid_sink_count = partition.macro_sinks.size() + partition.regular_sinks.size();
  return partition;
}

auto ClockSinkDomainBuilder::prepare(Clock& clock, std::size_t clock_index, CTSSinkDomain sink_domain, const std::vector<Pin*>& sinks,
                                     std::size_t valid_sinks, ClockTreeSynthesisStatusTable& status_table, ClockSinkDomainContext& context,
                                     const ClockSinkDomainRootBufferSpec* root_buffer_spec) -> bool
{
  if (sinks.empty()) {
    return true;
  }

  const auto* const sink_domain_label = ToString(sink_domain);
  context = ClockSinkDomainContext{};
  context.sink_domain = sink_domain;
  context.domain_prefix = ClockNetEditor::makeSinkDomainPrefix(clock, clock_index, sink_domain);
  context.sinks = sinks;

  if (!addRootBuffer(clock, context.domain_prefix, sinks, root_buffer_spec, context.root_buffer, context.root_input, context.root_output)) {
    status_table.append(clock, ClockTreeSynthesisStatus::kFailed, sink_domain, valid_sinks, sinks.size(), "failed to insert root buffer");
    LOG_ERROR << "ClockSinkDomainBuilder: clock \"" << clock.get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed because root-buffer insertion failed.";
    return false;
  }

  context.downstream_net = ClockNetEditor::connectSinkDomainDownstreamNet(clock, context.domain_prefix, context.root_output, sinks);
  if (context.downstream_net == nullptr) {
    status_table.append(clock, ClockTreeSynthesisStatus::kFailed, sink_domain, valid_sinks, sinks.size(),
                        "failed to create downstream net");
    LOG_ERROR << "ClockSinkDomainBuilder: clock \"" << clock.get_clock_name() << "\" sink domain " << sink_domain_label
              << " failed because downstream net creation failed.";
    return false;
  }
  return true;
}

}  // namespace icts
