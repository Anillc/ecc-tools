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
 * @file ClockTreeVisualizationModel.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief Normalized readonly CTS clock-tree visualization model.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "report_data/ClockTreeReportData.hh"
#include "spatial/Point.hh"

namespace icts {

struct ClockTreeVisualizationSegment
{
  std::string clock_name;
  std::string net_name;
  Point<int> begin = Point<int>(-1, -1);
  Point<int> end = Point<int>(-1, -1);
  ClockTreeReportView view = ClockTreeReportView::kUnknown;
  CTSNetRole net_role = CTSNetRole::kUnknown;
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
  std::size_t clock_index = 0U;
  int topology_depth = -1;
  int topology_level = -1;
  bool routed = true;
  bool fallback = false;
};

struct ClockTreeVisualizationInst
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

struct ClockTreeVisualizationLogicCell
{
  std::string inst_name;
  Point<int> origin = Point<int>(-1, -1);
  int32_t width_dbu = 0;
  int32_t height_dbu = 0;
};

struct ClockTreeVisualizationPin
{
  Point<int> location = Point<int>(-1, -1);
  bool driver = false;
};

struct ClockTreeVisualizationModel
{
  bool has_clocks = false;
  int32_t dbu_per_um = 1;
  std::vector<ClockTreeVisualizationSegment> design_segments;
  std::vector<ClockTreeVisualizationSegment> flyline_segments;
  std::vector<ClockTreeVisualizationInst> insts;
  std::vector<ClockTreeVisualizationLogicCell> logic_cells;
  std::vector<ClockTreeVisualizationPin> pins;
};

class ClockTreeVisualizationModelBuilder
{
 public:
  ClockTreeVisualizationModelBuilder() = delete;

  static auto build(const ClockTreeReportData& report_data) -> ClockTreeVisualizationModel;
};

}  // namespace icts
