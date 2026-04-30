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
 * @file ClockTreeVisualizationLayerPolicy.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-29
 * @brief CTS visualization semantic layer and palette policy implementation.
 */

#include "report/ClockTreeVisualizationLayerPolicy.hh"

#include <algorithm>
#include <limits>
#include <ostream>

#include "Log.hh"

namespace icts::report {
namespace {

auto palette() -> const std::vector<std::string>&
{
  static const std::vector<std::string> colors = {
      "#0072B2", "#D55E00", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#C44E52", "#000000", "#F0E442",
  };
  return colors;
}

auto normalizeTopologyLevel(int topology_level, int topology_depth) -> int
{
  if (topology_level >= 0) {
    return topology_level;
  }
  if (topology_depth >= 0) {
    return topology_depth;
  }
  return 0;
}

auto colorFor(std::size_t clock_index, int level, std::size_t offset) -> std::string
{
  const auto& colors = palette();
  const auto safe_level = static_cast<std::size_t>(std::max(level, 0));
  return colors.at((clock_index * 3U + safe_level + offset) % colors.size());
}

auto layerKeysEqual(const ClockTreeVisualizationLayerPolicy::LayerKey& lhs, const ClockTreeVisualizationLayerPolicy::LayerKey& rhs) -> bool
{
  return lhs.kind == rhs.kind && lhs.view == rhs.view && lhs.synthesis_phase == rhs.synthesis_phase && lhs.net_role == rhs.net_role
         && lhs.sink_domain == rhs.sink_domain && lhs.clock_index == rhs.clock_index && lhs.topology_level == rhs.topology_level;
}

auto routeSemanticLabel(const ClockTreeVisualizationSegment& segment) -> std::string
{
  switch (segment.net_role) {
    case CTSNetRole::kDownstream:
      if (segment.sink_domain == CTSSinkDomain::kRegular) {
        return "regular_downstream_root";
      }
      if (segment.sink_domain == CTSSinkDomain::kHardMacro) {
        return "macro_downstream_root";
      }
      return "downstream_root";
    case CTSNetRole::kSinkTree:
      return "htree_buffer";
    case CTSNetRole::kSourceToRoot:
      return "source_to_root";
    case CTSNetRole::kClockSource:
      return "clock_source";
    case CTSNetRole::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto routeUsesTopologyLevel(const ClockTreeVisualizationSegment& segment) -> bool
{
  if (segment.net_role == CTSNetRole::kSinkTree) {
    return true;
  }
  return segment.net_role == CTSNetRole::kSourceToRoot
         && (segment.synthesis_phase == ClockTreeSynthesisPhase::kSourceToRootHTree
             || segment.synthesis_phase == ClockTreeSynthesisPhase::kSourceToRootSegment);
}

}  // namespace

auto ClockTreeVisualizationLayerPolicy::getLayer(const LayerDescriptor& descriptor) -> CTSGdsLayerKey
{
  auto iter = std::ranges::find_if(
      _layers, [&descriptor](const RegisteredLayer& layer) -> bool { return layerKeysEqual(layer.key, descriptor.key); });
  if (iter != _layers.end()) {
    return iter->property.key;
  }

  if (_next_layer >= std::numeric_limits<int16_t>::max()) {
    LOG_WARNING << "CTS GDS report exceeded available semantic layer ids; reusing the last layer id.";
    return CTSGdsLayerKey{.layer = std::numeric_limits<int16_t>::max(), .datatype = 0};
  }

  CTSGdsLayerProperty property{
      .key = CTSGdsLayerKey{.layer = _next_layer++, .datatype = 0},
      .name = descriptor.display_name,
      .color = descriptor.color,
  };
  _layers.push_back(RegisteredLayer{
      .key = descriptor.key,
      .property = property,
  });
  return property.key;
}

auto ClockTreeVisualizationLayerPolicy::logicCellLayer() -> CTSGdsLayerKey
{
  return getLayer(LayerDescriptor{
      .key = LayerKey{.kind = LayerKind::kLogicCell},
      .display_name = "logic cells",
      .color = "#AEB4BC",
  });
}

auto ClockTreeVisualizationLayerPolicy::instLayer(const ClockTreeVisualizationInst& inst) -> std::optional<CTSGdsLayerKey>
{
  switch (inst.role) {
    case CTSInstRole::kClockLoad:
      if (inst.sink_domain == CTSSinkDomain::kRegular) {
        return getLayer(LayerDescriptor{
            .key = LayerKey{.kind = LayerKind::kRegularSink, .clock_index = inst.clock_index},
            .display_name = inst.clock_name + " regular sinks",
            .color = colorFor(inst.clock_index, 0, 0U),
        });
      }
      if (inst.sink_domain == CTSSinkDomain::kHardMacro) {
        return getLayer(LayerDescriptor{
            .key = LayerKey{.kind = LayerKind::kMacroSink, .clock_index = inst.clock_index},
            .display_name = inst.clock_name + " macro sinks",
            .color = colorFor(inst.clock_index, 0, 1U),
        });
      }
      return std::nullopt;
    case CTSInstRole::kRootBuffer:
      return getLayer(LayerDescriptor{
          .key = LayerKey{.kind = LayerKind::kRootBuffer, .clock_index = inst.clock_index},
          .display_name = inst.clock_name + " root buffers",
          .color = colorFor(inst.clock_index, 0, 2U),
      });
    case CTSInstRole::kHTreeBuffer:
    case CTSInstRole::kSourceRootBuffer: {
      const int level = normalizeTopologyLevel(inst.topology_level, inst.topology_depth);
      return getLayer(LayerDescriptor{
          .key = LayerKey{.kind = LayerKind::kHTreeBuffer, .clock_index = inst.clock_index, .topology_level = level},
          .display_name = inst.clock_name + " htree_buffer level " + std::to_string(level),
          .color = colorFor(inst.clock_index, level, 3U),
      });
    }
    case CTSInstRole::kLogicCell:
    case CTSInstRole::kClockSource:
    case CTSInstRole::kUnknown:
      return std::nullopt;
  }
  return std::nullopt;
}

auto ClockTreeVisualizationLayerPolicy::segmentLayer(const ClockTreeVisualizationSegment& segment) -> CTSGdsLayerKey
{
  const int level = normalizeTopologyLevel(segment.topology_level, segment.topology_depth);
  const bool has_meaningful_level = routeUsesTopologyLevel(segment);
  const bool flyline = segment.view == ClockTreeReportView::kFlyline;
  const auto level_suffix = has_meaningful_level ? " level " + std::to_string(level) : std::string{};
  return getLayer(LayerDescriptor{
      .key = LayerKey{
          .kind = LayerKind::kRouteSegment,
          .view = segment.view,
          .synthesis_phase = segment.synthesis_phase,
          .net_role = segment.net_role,
          .sink_domain = segment.sink_domain,
          .clock_index = segment.clock_index,
          .topology_level = has_meaningful_level ? level : 0,
      },
      .display_name = segment.clock_name + (flyline ? " flyline " : " routing ") + ToString(segment.synthesis_phase) + " "
                      + routeSemanticLabel(segment) + level_suffix,
      .color = colorFor(segment.clock_index, level, flyline ? 6U : 4U),
  });
}

auto ClockTreeVisualizationLayerPolicy::getLayerProperties() const -> std::vector<CTSGdsLayerProperty>
{
  std::vector<CTSGdsLayerProperty> properties;
  properties.reserve(_layers.size());
  for (const auto& layer : _layers) {
    properties.push_back(layer.property);
  }
  return properties;
}

}  // namespace icts::report
