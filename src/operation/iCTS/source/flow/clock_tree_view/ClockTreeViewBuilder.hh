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
 * @file ClockTreeViewBuilder.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS clock-tree view construction helper.
 */

#pragma once

#include <cstddef>
#include <vector>

#include "clock_tree_view/ClockTreeView.hh"
#include "clock_tree_view/ClockTreeViewSynthesisInput.hh"

namespace icts {

class Clock;
class Inst;
class Net;
class Pin;

struct ClockSinkDomainViewTopology
{
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  const Inst* root_buffer = nullptr;
  const Net* downstream_net = nullptr;
};

class ClockTreeViewBuilder
{
 public:
  ClockTreeViewBuilder() = delete;

  static auto appendSinkInsts(ClockTreeView& clock_tree_view, const Clock& clock, std::size_t clock_index, const std::vector<Pin*>& sinks,
                              CTSSinkDomain sink_domain) -> void;
  static auto appendDirectSinkDomain(ClockTreeView& clock_tree_view, const Clock& clock, std::size_t clock_index,
                                     const ClockSinkDomainViewTopology& sink_domain_topology) -> void;
  static auto makeSinkDomainView(const Clock& clock, std::size_t clock_index, const ClockSinkDomainViewTopology& sink_domain_topology,
                                 const ClockSinkDomainViewInput& view_input) -> ClockTreeView;
  static auto makeSourceToRootView(const Clock& clock, std::size_t clock_index, const Net& source_net,
                                   const ClockSourceToRootViewInput& view_input, ClockTreeSynthesisPhase synthesis_phase) -> ClockTreeView;
  static auto merge(ClockTreeView& target, const ClockTreeView& source) -> void;
};

}  // namespace icts
