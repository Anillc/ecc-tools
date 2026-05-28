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
 * @file Visualization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS clock-tree visualization report output implementation.
 */

#include "report/visualization/Visualization.hh"

#include "report/visualization/gds/GdsVisualization.hh"
#include "report/visualization/svg/SvgVisualization.hh"

namespace icts {

auto Visualization::emit(const std::filesystem::path& visualization_dir, const ClockLayout& clock_layout) -> VisualizationResult
{
  const auto svg_result = visualization::EmitSvgVisualizations(visualization_dir, clock_layout);
  const auto gds_result = visualization::EmitGdsVisualizations(visualization_dir, clock_layout);
  return VisualizationResult{
      .svg_success = svg_result.success,
      .gds_success = gds_result.success,
      .success = svg_result.success && gds_result.success,
  };
}

}  // namespace icts
