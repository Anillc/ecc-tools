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
 * @file ClockSinkDomainBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Prepares CTS sink-domain root buffers and downstream nets.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "clock_tree_view/ClockTreeView.hh"
#include "clock_tree_view/ClockTreeViewBuilder.hh"
#include "stage/ClockTreeSynthesisStatusTable.hh"

namespace icts {

class Clock;
class Inst;
class Net;
class Pin;

struct ClockSinkDomainRootBufferSpec
{
  std::string cell_master;
  std::string input_pin_name;
  std::string output_pin_name;
};

struct ClockSinkDomainPartition
{
  std::vector<Pin*> macro_sinks;
  std::vector<Pin*> regular_sinks;
  std::size_t valid_sink_count = 0U;
};

struct ClockSinkDomainContext
{
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  std::string domain_prefix;
  std::vector<Pin*> sinks;
  Inst* root_buffer = nullptr;
  Pin* root_input = nullptr;
  Pin* root_output = nullptr;
  Net* downstream_net = nullptr;

  auto makeViewTopology() const -> ClockSinkDomainViewTopology
  {
    return ClockSinkDomainViewTopology{
        .sink_domain = sink_domain,
        .root_buffer = root_buffer,
        .downstream_net = downstream_net,
    };
  }
};

class ClockSinkDomainBuilder
{
 public:
  ClockSinkDomainBuilder() = delete;

  static auto partitionSinkDomains(const Clock& clock) -> ClockSinkDomainPartition;
  static auto prepare(Clock& clock, std::size_t clock_index, CTSSinkDomain sink_domain, const std::vector<Pin*>& sinks,
                      std::size_t valid_sinks, ClockTreeSynthesisStatusTable& status_table, ClockSinkDomainContext& context,
                      const ClockSinkDomainRootBufferSpec* root_buffer_spec = nullptr) -> bool;
};

}  // namespace icts
