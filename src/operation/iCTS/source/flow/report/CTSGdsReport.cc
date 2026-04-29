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
 * @file CTSGdsReport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Report-stage CTS GDSII report generation implementation.
 */

#include "report/CTSGdsReport.hh"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "Log.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "report/CTSGdsWriter.hh"
#include "report_data/ClockTreeReportData.hh"

namespace icts::report {
namespace {

constexpr const char* kDesignGdsLabel = "cts_design.gds";
constexpr const char* kFlylineGdsLabel = "cts_flyline.gds";
constexpr const char* kLayerPropertiesLabel = "cts_layers.lyp";
constexpr int16_t kFirstSemanticLayer = 1;

enum class CTSGdsSemanticLayerKind
{
  kLogicCell,
  kRegularSink,
  kMacroSink,
  kRootBuffer,
  kHTreeBuffer,
  kRouteSegment
};

struct CTSGdsSemanticLayerKey
{
  CTSGdsSemanticLayerKind kind = CTSGdsSemanticLayerKind::kLogicCell;
  ClockTreeReportView view = ClockTreeReportView::kUnknown;
  ClockTreeSynthesisPhase synthesis_phase = ClockTreeSynthesisPhase::kUnknown;
  CTSNetRole net_role = CTSNetRole::kUnknown;
  CTSSinkDomain sink_domain = CTSSinkDomain::kUnknown;
  std::size_t clock_index = 0U;
  int topology_level = -1;
};

struct CTSGdsSemanticLayerDescriptor
{
  CTSGdsSemanticLayerKey semantic_key;
  std::string display_name;
  std::string color;
};

struct CTSGdsRegisteredLayer
{
  CTSGdsSemanticLayerKey semantic_key;
  CTSGdsLayerProperty property;
};

auto palette() -> const std::vector<std::string>&
{
  static const std::vector<std::string> colors = {
      "#0072B2", "#D55E00", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#C44E52", "#000000", "#F0E442",
  };
  return colors;
}

auto semanticKeysEqual(const CTSGdsSemanticLayerKey& lhs, const CTSGdsSemanticLayerKey& rhs) -> bool
{
  return lhs.kind == rhs.kind && lhs.view == rhs.view && lhs.synthesis_phase == rhs.synthesis_phase && lhs.net_role == rhs.net_role
         && lhs.sink_domain == rhs.sink_domain && lhs.clock_index == rhs.clock_index && lhs.topology_level == rhs.topology_level;
}

class CTSGdsSemanticLayerRegistry
{
 public:
  auto getLayer(const CTSGdsSemanticLayerDescriptor& descriptor) -> CTSGdsLayerKey
  {
    auto iter = std::ranges::find_if(_layers, [&descriptor](const CTSGdsRegisteredLayer& layer) -> bool {
      return semanticKeysEqual(layer.semantic_key, descriptor.semantic_key);
    });
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
    _layers.push_back(CTSGdsRegisteredLayer{
        .semantic_key = descriptor.semantic_key,
        .property = property,
    });
    return property.key;
  }

  auto getLayerProperties() const -> std::vector<CTSGdsLayerProperty>
  {
    std::vector<CTSGdsLayerProperty> properties;
    properties.reserve(_layers.size());
    for (const auto& layer : _layers) {
      properties.push_back(layer.property);
    }
    return properties;
  }

 private:
  std::vector<CTSGdsRegisteredLayer> _layers;
  int16_t _next_layer = kFirstSemanticLayer;
};

auto resolveVisualizationDir(const std::filesystem::path& visualization_dir) -> std::filesystem::path
{
  if (!visualization_dir.empty()) {
    return visualization_dir;
  }
  return std::filesystem::path(CONFIG_INST.get_visualization_dir());
}

auto isValidLocation(const Point<int>& point) -> bool
{
  return point.get_x() >= 0 && point.get_y() >= 0;
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

auto logicCellLayerDescriptor() -> CTSGdsSemanticLayerDescriptor
{
  return CTSGdsSemanticLayerDescriptor{
      .semantic_key = CTSGdsSemanticLayerKey{
          .kind = CTSGdsSemanticLayerKind::kLogicCell,
      },
      .display_name = "logic cells",
      .color = "#AEB4BC",
  };
}

auto instLayerDescriptor(const ClockTreeReportClock& clock, const ClockTreeReportInst& inst) -> std::optional<CTSGdsSemanticLayerDescriptor>
{
  switch (inst.role) {
    case CTSInstRole::kClockLoad:
      if (inst.sink_domain == CTSSinkDomain::kRegular) {
        return CTSGdsSemanticLayerDescriptor{
            .semantic_key = CTSGdsSemanticLayerKey{
                .kind = CTSGdsSemanticLayerKind::kRegularSink,
                .clock_index = clock.clock_index,
            },
            .display_name = clock.clock_name + " regular sinks",
            .color = colorFor(clock.clock_index, 0, 0U),
        };
      }
      if (inst.sink_domain == CTSSinkDomain::kHardMacro) {
        return CTSGdsSemanticLayerDescriptor{
            .semantic_key = CTSGdsSemanticLayerKey{
                .kind = CTSGdsSemanticLayerKind::kMacroSink,
                .clock_index = clock.clock_index,
            },
            .display_name = clock.clock_name + " macro sinks",
            .color = colorFor(clock.clock_index, 0, 1U),
        };
      }
      return std::nullopt;
    case CTSInstRole::kRootBuffer:
      return CTSGdsSemanticLayerDescriptor{
          .semantic_key = CTSGdsSemanticLayerKey{
              .kind = CTSGdsSemanticLayerKind::kRootBuffer,
              .clock_index = clock.clock_index,
          },
          .display_name = clock.clock_name + " root buffers",
          .color = colorFor(clock.clock_index, 0, 2U),
      };
    case CTSInstRole::kHTreeBuffer:
    case CTSInstRole::kSourceRootBuffer: {
      const int level = normalizeTopologyLevel(inst.topology_level, inst.topology_depth);
      return CTSGdsSemanticLayerDescriptor{
          .semantic_key = CTSGdsSemanticLayerKey{
              .kind = CTSGdsSemanticLayerKind::kHTreeBuffer,
              .clock_index = clock.clock_index,
              .topology_level = level,
          },
          .display_name = clock.clock_name + " htree_buffer level " + std::to_string(level),
          .color = colorFor(clock.clock_index, level, 3U),
      };
    }
    case CTSInstRole::kLogicCell:
    case CTSInstRole::kClockSource:
    case CTSInstRole::kUnknown:
      return std::nullopt;
  }
  return std::nullopt;
}

auto routeSemanticLabel(const ClockTreeReportSegment& segment) -> std::string
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

auto routeUsesTopologyLevel(const ClockTreeReportSegment& segment) -> bool
{
  if (segment.net_role == CTSNetRole::kSinkTree) {
    return true;
  }
  return segment.net_role == CTSNetRole::kSourceToRoot
         && (segment.synthesis_phase == ClockTreeSynthesisPhase::kSourceToRootHTree
             || segment.synthesis_phase == ClockTreeSynthesisPhase::kSourceToRootSegment);
}

auto routeLayerDescriptor(const ClockTreeReportClock& clock, const ClockTreeReportSegment& segment, ClockTreeReportView view)
    -> CTSGdsSemanticLayerDescriptor
{
  const int level = normalizeTopologyLevel(segment.topology_level, segment.topology_depth);
  const bool has_meaningful_level = routeUsesTopologyLevel(segment);
  const bool flyline = view == ClockTreeReportView::kFlyline;
  const auto level_suffix = has_meaningful_level ? " level " + std::to_string(level) : std::string{};
  return CTSGdsSemanticLayerDescriptor{
      .semantic_key = CTSGdsSemanticLayerKey{
          .kind = CTSGdsSemanticLayerKind::kRouteSegment,
          .view = view,
          .synthesis_phase = segment.synthesis_phase,
          .net_role = segment.net_role,
          .sink_domain = segment.sink_domain,
          .clock_index = clock.clock_index,
          .topology_level = has_meaningful_level ? level : 0,
      },
      .display_name = clock.clock_name + (flyline ? " flyline " : " routing ") + ToString(segment.synthesis_phase) + " "
                      + routeSemanticLabel(segment) + level_suffix,
      .color = colorFor(clock.clock_index, level, flyline ? 6U : 4U),
  };
}

auto makeBoundary(const CTSGdsLayerKey& key, const Point<int>& origin, int32_t width, int32_t height) -> CTSGdsBoundary
{
  const int32_t safe_width = std::max(width, int32_t{1});
  const int32_t safe_height = std::max(height, int32_t{1});
  const int32_t lx = origin.get_x();
  const int32_t ly = origin.get_y();
  const int32_t ux = lx + safe_width;
  const int32_t uy = ly + safe_height;
  return CTSGdsBoundary{
      .key = key,
      .points = {
          Point<int>(lx, ly),
          Point<int>(ux, ly),
          Point<int>(ux, uy),
          Point<int>(lx, uy),
          Point<int>(lx, ly),
      },
  };
}

auto resolvePathWidthDbu(int32_t dbu_per_um) -> int32_t
{
  if (CONFIG_INST.get_wire_width() > 0.0) {
    return std::max(static_cast<int32_t>(CONFIG_INST.get_wire_width() * static_cast<double>(std::max(dbu_per_um, int32_t{1}))), int32_t{1});
  }
  return std::max(std::max(dbu_per_um, int32_t{1}) / 20, int32_t{1});
}

auto collectCtsInstNames(const ClockTreeReportData& report_data) -> std::unordered_set<std::string>
{
  std::unordered_set<std::string> names;
  for (const auto& clock : report_data.get_clocks()) {
    for (const auto& inst : clock.insts) {
      if (!inst.inst_name.empty()) {
        names.insert(inst.inst_name);
      }
    }
  }
  return names;
}

auto geometryOriginOrReportOrigin(const std::optional<WrapperCellGeometry>& geometry, const ClockTreeReportInst& inst) -> Point<int>
{
  if (geometry.has_value() && isValidLocation(geometry->origin)) {
    return geometry->origin;
  }
  return inst.origin;
}

auto geometryWidthOrDefault(const std::optional<WrapperCellGeometry>& geometry, int32_t dbu_per_um) -> int32_t
{
  if (geometry.has_value() && geometry->width_dbu > 0) {
    return geometry->width_dbu;
  }
  return std::max(dbu_per_um / 5, int32_t{1});
}

auto geometryHeightOrDefault(const std::optional<WrapperCellGeometry>& geometry, int32_t dbu_per_um) -> int32_t
{
  if (geometry.has_value() && geometry->height_dbu > 0) {
    return geometry->height_dbu;
  }
  return std::max(dbu_per_um / 5, int32_t{1});
}

auto appendLogicCellBoundaries(CTSGdsLibrary& library, CTSGdsSemanticLayerRegistry& registry, const ClockTreeReportData& report_data,
                               const std::vector<WrapperCellGeometry>& logic_cell_geometries) -> void
{
  const auto cts_inst_names = collectCtsInstNames(report_data);
  for (const auto& geometry : logic_cell_geometries) {
    if (cts_inst_names.contains(geometry.name) || !isValidLocation(geometry.origin)) {
      continue;
    }
    const auto key = registry.getLayer(logicCellLayerDescriptor());
    library.boundaries.push_back(makeBoundary(key, geometry.origin, geometry.width_dbu, geometry.height_dbu));
  }
}

auto appendClockInstBoundaries(CTSGdsLibrary& library, CTSGdsSemanticLayerRegistry& registry, const ClockTreeReportData& report_data)
    -> void
{
  const int32_t dbu_per_um = std::max(report_data.get_design_dbu_per_um(), int32_t{1});
  for (const auto& clock : report_data.get_clocks()) {
    for (const auto& inst : clock.insts) {
      const auto layer_descriptor = instLayerDescriptor(clock, inst);
      if (!layer_descriptor.has_value()) {
        continue;
      }
      const auto geometry = WRAPPER_INST.queryInstGeometry(inst.inst_name);
      const auto origin = geometryOriginOrReportOrigin(geometry, inst);
      if (!isValidLocation(origin)) {
        continue;
      }
      const auto key = registry.getLayer(*layer_descriptor);
      library.boundaries.push_back(
          makeBoundary(key, origin, geometryWidthOrDefault(geometry, dbu_per_um), geometryHeightOrDefault(geometry, dbu_per_um)));
    }
  }
}

auto appendInstBoundaries(CTSGdsLibrary& library, CTSGdsSemanticLayerRegistry& registry, const ClockTreeReportData& report_data,
                          const std::vector<WrapperCellGeometry>& logic_cell_geometries) -> void
{
  appendLogicCellBoundaries(library, registry, report_data, logic_cell_geometries);
  appendClockInstBoundaries(library, registry, report_data);
}

auto appendSegments(CTSGdsLibrary& library, CTSGdsSemanticLayerRegistry& registry, const ClockTreeReportData& report_data,
                    ClockTreeReportView view) -> void
{
  const bool flyline = view == ClockTreeReportView::kFlyline;
  const int32_t path_width = resolvePathWidthDbu(report_data.get_design_dbu_per_um());
  for (const auto& clock : report_data.get_clocks()) {
    for (const auto& net : clock.nets) {
      const auto& segments = flyline ? net.flyline_segments : net.routed_segments;
      for (const auto& segment : segments) {
        if (!isValidLocation(segment.begin) || !isValidLocation(segment.end)) {
          continue;
        }
        const auto key = registry.getLayer(routeLayerDescriptor(clock, segment, view));
        library.paths.push_back(CTSGdsPath{
            .key = key,
            .width_dbu = flyline ? std::max(path_width / 2, int32_t{1}) : path_width,
            .points = {segment.begin, segment.end},
        });
      }
    }
  }
}

auto buildLibrary(const std::string& library_name, const std::string& structure_name, const ClockTreeReportData& report_data,
                  const std::vector<WrapperCellGeometry>& logic_cell_geometries, ClockTreeReportView view,
                  CTSGdsSemanticLayerRegistry& registry) -> CTSGdsLibrary
{
  CTSGdsLibrary library{
      .library_name = library_name,
      .structure_name = structure_name,
      .dbu_per_um = report_data.get_design_dbu_per_um(),
      .paths = {},
      .boundaries = {},
      .texts = {},
  };
  appendInstBoundaries(library, registry, report_data, logic_cell_geometries);
  appendSegments(library, registry, report_data, view);
  return library;
}

auto emitReportTable(const std::filesystem::path& design_gds_path, bool design_success, const std::filesystem::path& flyline_gds_path,
                     bool flyline_success, const std::filesystem::path& lyp_path, bool lyp_success) -> void
{
  schema::TableRows rows = {
      {kDesignGdsLabel, design_gds_path.string(), "gds/design", design_success ? "generated" : "failed"},
      {kFlylineGdsLabel, flyline_gds_path.string(), "gds/flyline", flyline_success ? "generated" : "failed"},
      {kLayerPropertiesLabel, lyp_path.string(), "lyp/layers", lyp_success ? "generated" : "failed"},
  };
  schema::EmitTable("CTS GDS Reports", {"Report", "Path", "View", "Status"}, rows);
}

}  // namespace

auto EmitCTSGdsReports(const std::filesystem::path& visualization_dir, const ClockTreeReportData& report_data) -> CTSGdsReportResult
{
  const auto output_dir = resolveVisualizationDir(visualization_dir) / "gds";
  const auto design_gds_path = output_dir / kDesignGdsLabel;
  const auto flyline_gds_path = output_dir / kFlylineGdsLabel;
  const auto lyp_path = output_dir / kLayerPropertiesLabel;

  if (report_data.get_clocks().empty()) {
    LOG_WARNING << "CTS GDS report skipped because no clock-tree report data is available.";
    emitReportTable(design_gds_path, false, flyline_gds_path, false, lyp_path, false);
    return CTSGdsReportResult{.success = false};
  }

  auto logic_cell_geometries = WRAPPER_INST.collectLogicCellGeometries();
  CTSGdsSemanticLayerRegistry layer_registry;
  auto design_library
      = buildLibrary("CTS_DESIGN", "CTS_DESIGN", report_data, logic_cell_geometries, ClockTreeReportView::kDesign, layer_registry);
  auto flyline_library
      = buildLibrary("CTS_FLYLINE", "CTS_FLYLINE", report_data, logic_cell_geometries, ClockTreeReportView::kFlyline, layer_registry);
  const auto layers = layer_registry.getLayerProperties();

  const bool design_success = CTSGdsWriter::writeBinary(design_gds_path, design_library);
  const bool flyline_success = CTSGdsWriter::writeBinary(flyline_gds_path, flyline_library);
  const bool lyp_success = CTSGdsWriter::writeLayerProperties(lyp_path, layers);
  emitReportTable(design_gds_path, design_success, flyline_gds_path, flyline_success, lyp_path, lyp_success);
  return CTSGdsReportResult{.success = design_success && flyline_success && lyp_success};
}

}  // namespace icts::report
