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
 * @file ClockTreeVisualizationLayerPolicy.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS visualization semantic layer and palette policy.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "report/CTSGdsWriter.hh"
#include "report_data/ClockTreeVisualizationModel.hh"

namespace icts::report {

class ClockTreeVisualizationLayerPolicy
{
 public:
  ClockTreeVisualizationLayerPolicy() = default;

  auto logicCellLayer() -> CTSGdsLayerKey;
  auto instLayer(const ClockTreeVisualizationInst& inst) -> std::optional<CTSGdsLayerKey>;
  auto segmentLayer(const ClockTreeVisualizationSegment& segment) -> CTSGdsLayerKey;
  auto getLayerProperties() const -> std::vector<CTSGdsLayerProperty>;

  enum class LayerKind
  {
    kLogicCell,
    kRegularSink,
    kMacroSink,
    kRootBuffer,
    kHTreeBuffer,
    kRouteSegment
  };

  struct LayerKey
  {
    LayerKind kind = LayerKind::kLogicCell;
    ClockTreeReportView view = ClockTreeReportView::kUnknown;
    ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
    CTSNetRole net_role = CTSNetRole::kUnknown;
    CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
    std::size_t clock_index = 0U;
    int topology_level = -1;
  };

  struct LayerDescriptor
  {
    LayerKey key;
    std::string display_name;
    std::string color;
  };

  struct RegisteredLayer
  {
    LayerKey key;
    CTSGdsLayerProperty property;
  };

 private:
  auto getLayer(const LayerDescriptor& descriptor) -> CTSGdsLayerKey;

  std::vector<RegisteredLayer> _layers;
  int16_t _next_layer = 1;
};

}  // namespace icts::report
