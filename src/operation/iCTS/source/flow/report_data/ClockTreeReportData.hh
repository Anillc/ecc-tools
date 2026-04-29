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
 * @file ClockTreeReportData.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Clock-tree report data consumed by report and visualization steps.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "spatial/Point.hh"

namespace icts {

enum class CTSNetRole
{
  kUnknown = 0,
  kClockSource = 1,
  kSourceToRoot = 2,
  kDownstream = 3,
  kSinkTree = 4
};

enum class CTSInstRole
{
  kUnknown = 0,
  kLogicCell = 1,
  kClockSource = 2,
  kClockLoad = 3,
  kRootBuffer = 4,
  kHTreeBuffer = 5,
  kSourceRootBuffer = 6
};

enum class ClockTreeSynthesisPhase
{
  kUnknown,
  kReadData,
  kDownstreamHTree,
  kSourceToRootSegment,
  kSourceToRootHTree
};

enum class CTSSinkDomain
{
  kUnknown,
  kHardMacro,
  kRegular,
  kSourceToRoot
};

enum class ClockTreeReportView
{
  kUnknown,
  kDesign,
  kFlyline
};

struct ClockTreeReportSegment
{
  std::string clock_name;
  std::string net_name;
  Point<int> begin = Point<int>(-1, -1);
  Point<int> end = Point<int>(-1, -1);
  CTSNetRole net_role = CTSNetRole::kUnknown;
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
  std::size_t clock_index = 0U;
  int topology_depth = -1;
  int topology_level = -1;
  bool routed = true;
  bool fallback = false;
};

struct ClockTreeReportNet
{
  std::string clock_name;
  std::string net_name;
  CTSNetRole role = CTSNetRole::kUnknown;
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
  std::size_t clock_index = 0U;
  int selected_depth = -1;
  int topology_level = -1;
  int topology_level_count = 0;
  std::vector<ClockTreeReportSegment> routed_segments;
  std::vector<ClockTreeReportSegment> flyline_segments;
};

struct ClockTreeReportInst
{
  std::string clock_name;
  std::string inst_name;
  std::string cell_master;
  Point<int> origin = Point<int>(-1, -1);
  int32_t width_dbu = 0;
  int32_t height_dbu = 0;
  CTSInstRole role = CTSInstRole::kUnknown;
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
  std::size_t clock_index = 0U;
  int topology_depth = -1;
  int topology_level = -1;
};

struct ClockTreeReportClock
{
  std::string clock_name;
  std::string clock_net_name;
  std::size_t clock_index = 0U;
  std::vector<ClockTreeReportNet> nets;
  std::vector<ClockTreeReportInst> insts;
};

class ClockTreeReportData
{
 public:
  auto reset() -> void;
  auto set_design_dbu_per_um(int32_t dbu_per_um) -> void;
  auto get_design_dbu_per_um() const -> int32_t { return _design_dbu_per_um; }

  auto markSynthesisComplete(bool complete) -> void { _synthesis_complete = complete; }
  auto isSynthesisComplete() const -> bool { return _synthesis_complete; }
  auto markWritebackDone(bool done) -> void { _writeback_done = done; }
  auto isWritebackDone() const -> bool { return _writeback_done; }

  auto ensureClock(const std::string& clock_name, const std::string& clock_net_name, std::size_t clock_index) -> ClockTreeReportClock&;
  auto findClock(std::size_t clock_index) -> ClockTreeReportClock*;
  auto findClock(std::size_t clock_index) const -> const ClockTreeReportClock*;
  auto findNet(std::size_t clock_index, const std::string& net_name) const -> const ClockTreeReportNet*;
  auto findInst(std::size_t clock_index, const std::string& inst_name) const -> const ClockTreeReportInst*;
  auto addNet(const ClockTreeReportNet& report_net) -> void;
  auto addInst(const ClockTreeReportInst& report_inst) -> void;

  auto get_clocks() const -> const std::vector<ClockTreeReportClock>& { return _clocks; }
  auto get_clocks() -> std::vector<ClockTreeReportClock>& { return _clocks; }

 private:
  std::vector<ClockTreeReportClock> _clocks;
  int32_t _design_dbu_per_um = 1;
  bool _synthesis_complete = false;
  bool _writeback_done = false;
};

auto ToString(CTSNetRole role) -> const char*;
auto ToString(CTSInstRole role) -> const char*;
auto ToString(ClockTreeSynthesisPhase stage) -> const char*;
auto ToString(CTSSinkDomain domain) -> const char*;
auto ToString(ClockTreeReportView view) -> const char*;

}  // namespace icts
