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
 * @file ClockTreeReportData.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Clock-tree report data store implementation.
 */

#include "report_data/ClockTreeReportData.hh"

#include <algorithm>
#include <cstdint>
#include <string>

namespace icts {

auto ClockTreeReportData::reset() -> void
{
  _clocks.clear();
  _design_dbu_per_um = 1;
  _synthesis_complete = false;
  _writeback_done = false;
}

auto ClockTreeReportData::set_design_dbu_per_um(int32_t dbu_per_um) -> void
{
  _design_dbu_per_um = std::max(dbu_per_um, int32_t{1});
}

auto ClockTreeReportData::ensureClock(const std::string& clock_name, const std::string& clock_net_name, std::size_t clock_index)
    -> ClockTreeReportClock&
{
  if (auto* report_clock = findClock(clock_index); report_clock != nullptr) {
    if (report_clock->clock_name.empty()) {
      report_clock->clock_name = clock_name;
    }
    if (report_clock->clock_net_name.empty()) {
      report_clock->clock_net_name = clock_net_name;
    }
    return *report_clock;
  }

  _clocks.push_back(ClockTreeReportClock{
      .clock_name = clock_name,
      .clock_net_name = clock_net_name,
      .clock_index = clock_index,
      .nets = {},
      .insts = {},
  });
  return _clocks.back();
}

auto ClockTreeReportData::findClock(std::size_t clock_index) -> ClockTreeReportClock*
{
  auto iter = std::ranges::find_if(
      _clocks, [clock_index](const ClockTreeReportClock& report_clock) -> bool { return report_clock.clock_index == clock_index; });
  return iter == _clocks.end() ? nullptr : &(*iter);
}

auto ClockTreeReportData::findClock(std::size_t clock_index) const -> const ClockTreeReportClock*
{
  auto iter = std::ranges::find_if(
      _clocks, [clock_index](const ClockTreeReportClock& report_clock) -> bool { return report_clock.clock_index == clock_index; });
  return iter == _clocks.end() ? nullptr : &(*iter);
}

auto ClockTreeReportData::findNet(std::size_t clock_index, const std::string& net_name) const -> const ClockTreeReportNet*
{
  auto clock_iter = std::ranges::find_if(
      _clocks, [clock_index](const ClockTreeReportClock& report_clock) -> bool { return report_clock.clock_index == clock_index; });
  if (clock_iter == _clocks.end()) {
    return nullptr;
  }
  auto net_iter = std::ranges::find_if(
      clock_iter->nets, [&net_name](const ClockTreeReportNet& report_net) -> bool { return report_net.net_name == net_name; });
  return net_iter == clock_iter->nets.end() ? nullptr : &(*net_iter);
}

auto ClockTreeReportData::findInst(std::size_t clock_index, const std::string& inst_name) const -> const ClockTreeReportInst*
{
  auto clock_iter = std::ranges::find_if(
      _clocks, [clock_index](const ClockTreeReportClock& report_clock) -> bool { return report_clock.clock_index == clock_index; });
  if (clock_iter == _clocks.end()) {
    return nullptr;
  }
  auto inst_iter = std::ranges::find_if(
      clock_iter->insts, [&inst_name](const ClockTreeReportInst& report_inst) -> bool { return report_inst.inst_name == inst_name; });
  return inst_iter == clock_iter->insts.end() ? nullptr : &(*inst_iter);
}

auto ClockTreeReportData::addNet(const ClockTreeReportNet& report_net) -> void
{
  auto& report_clock = ensureClock(report_net.clock_name, "", report_net.clock_index);
  report_clock.nets.push_back(report_net);
}

auto ClockTreeReportData::addInst(const ClockTreeReportInst& report_inst) -> void
{
  auto& report_clock = ensureClock(report_inst.clock_name, "", report_inst.clock_index);
  report_clock.insts.push_back(report_inst);
}

auto ToString(CTSNetRole role) -> const char*
{
  switch (role) {
    case CTSNetRole::kClockSource:
      return "clock_source";
    case CTSNetRole::kDownstream:
      return "downstream";
    case CTSNetRole::kSinkTree:
      return "sink_tree";
    case CTSNetRole::kSourceToRoot:
      return "source_to_root";
    case CTSNetRole::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ToString(CTSInstRole role) -> const char*
{
  switch (role) {
    case CTSInstRole::kLogicCell:
      return "logic_cell";
    case CTSInstRole::kClockSource:
      return "clock_source";
    case CTSInstRole::kClockLoad:
      return "clock_load";
    case CTSInstRole::kRootBuffer:
      return "root_buffer";
    case CTSInstRole::kHTreeBuffer:
      return "htree_buffer";
    case CTSInstRole::kSourceRootBuffer:
      return "source_root_buffer";
    case CTSInstRole::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ToString(ClockTreeSynthesisPhase stage) -> const char*
{
  switch (stage) {
    case ClockTreeSynthesisPhase::kReadData:
      return "read_data";
    case ClockTreeSynthesisPhase::kDownstreamHTree:
      return "downstream_htree";
    case ClockTreeSynthesisPhase::kSourceToRootSegment:
      return "source_to_root_segment";
    case ClockTreeSynthesisPhase::kSourceToRootHTree:
      return "source_to_root_htree";
    case ClockTreeSynthesisPhase::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ToString(CTSSinkDomain domain) -> const char*
{
  switch (domain) {
    case CTSSinkDomain::kHardMacro:
      return "hard_macro";
    case CTSSinkDomain::kRegular:
      return "regular";
    case CTSSinkDomain::kSourceToRoot:
      return "source_to_root";
    case CTSSinkDomain::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ToString(ClockTreeReportView view) -> const char*
{
  switch (view) {
    case ClockTreeReportView::kDesign:
      return "design";
    case ClockTreeReportView::kFlyline:
      return "flyline";
    case ClockTreeReportView::kUnknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace icts
