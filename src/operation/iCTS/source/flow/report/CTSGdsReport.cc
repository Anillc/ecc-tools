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
#include <string>
#include <vector>

#include "Log.hh"
#include "config/Config.hh"
#include "logger/Schema.hh"
#include "report/CTSGdsWriter.hh"
#include "report/ClockTreeVisualizationLayerPolicy.hh"
#include "report_data/ClockTreeReportData.hh"
#include "report_data/ClockTreeVisualizationModel.hh"

namespace icts::report {
namespace {

constexpr const char* kDesignGdsLabel = "cts_design.gds";
constexpr const char* kFlylineGdsLabel = "cts_flyline.gds";
constexpr const char* kDesignLayerPropertiesLabel = "cts_design.lyp";
constexpr const char* kFlylineLayerPropertiesLabel = "cts_flyline.lyp";

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

auto appendLogicCellBoundaries(CTSGdsLibrary& library, ClockTreeVisualizationLayerPolicy& layer_policy,
                               const ClockTreeVisualizationModel& model) -> void
{
  for (const auto& logic_cell : model.logic_cells) {
    if (!isValidLocation(logic_cell.origin)) {
      continue;
    }
    const auto key = layer_policy.logicCellLayer();
    library.boundaries.push_back(makeBoundary(key, logic_cell.origin, logic_cell.width_dbu, logic_cell.height_dbu));
  }
}

auto appendClockInstBoundaries(CTSGdsLibrary& library, ClockTreeVisualizationLayerPolicy& layer_policy,
                               const ClockTreeVisualizationModel& model) -> void
{
  for (const auto& inst : model.insts) {
    const auto layer_key = layer_policy.instLayer(inst);
    if (!layer_key.has_value() || !isValidLocation(inst.origin)) {
      continue;
    }
    library.boundaries.push_back(makeBoundary(*layer_key, inst.origin, inst.width_dbu, inst.height_dbu));
  }
}

auto appendInstBoundaries(CTSGdsLibrary& library, ClockTreeVisualizationLayerPolicy& layer_policy, const ClockTreeVisualizationModel& model)
    -> void
{
  appendLogicCellBoundaries(library, layer_policy, model);
  appendClockInstBoundaries(library, layer_policy, model);
}

auto appendSegments(CTSGdsLibrary& library, ClockTreeVisualizationLayerPolicy& layer_policy, const ClockTreeVisualizationModel& model,
                    ClockTreeReportView view) -> void
{
  const bool flyline = view == ClockTreeReportView::kFlyline;
  const int32_t path_width = resolvePathWidthDbu(model.dbu_per_um);
  const auto& segments = flyline ? model.flyline_segments : model.design_segments;
  for (const auto& segment : segments) {
    if (!isValidLocation(segment.begin) || !isValidLocation(segment.end)) {
      continue;
    }
    const auto key = layer_policy.segmentLayer(segment);
    library.paths.push_back(CTSGdsPath{
        .key = key,
        .width_dbu = flyline ? std::max(path_width / 2, int32_t{1}) : path_width,
        .points = {segment.begin, segment.end},
    });
  }
}

auto buildLibrary(const std::string& library_name, const std::string& structure_name, const ClockTreeVisualizationModel& model,
                  ClockTreeReportView view, ClockTreeVisualizationLayerPolicy& layer_policy) -> CTSGdsLibrary
{
  CTSGdsLibrary library{
      .library_name = library_name,
      .structure_name = structure_name,
      .dbu_per_um = model.dbu_per_um,
      .paths = {},
      .boundaries = {},
      .texts = {},
  };
  appendInstBoundaries(library, layer_policy, model);
  appendSegments(library, layer_policy, model, view);
  return library;
}

auto emitReportTable(const std::filesystem::path& design_gds_path, bool design_success, const std::filesystem::path& design_lyp_path,
                     bool design_lyp_success, const std::filesystem::path& flyline_gds_path, bool flyline_success,
                     const std::filesystem::path& flyline_lyp_path, bool flyline_lyp_success) -> void
{
  schema::TableRows rows = {
      {kDesignGdsLabel, design_gds_path.string(), "gds/design", design_success ? "generated" : "failed"},
      {kDesignLayerPropertiesLabel, design_lyp_path.string(), "lyp/design", design_lyp_success ? "generated" : "failed"},
      {kFlylineGdsLabel, flyline_gds_path.string(), "gds/flyline", flyline_success ? "generated" : "failed"},
      {kFlylineLayerPropertiesLabel, flyline_lyp_path.string(), "lyp/flyline", flyline_lyp_success ? "generated" : "failed"},
  };
  schema::EmitTable("CTS GDS Reports", {"Report", "Path", "View", "Status"}, rows);
}

}  // namespace

auto EmitCTSGdsReports(const std::filesystem::path& visualization_dir, const ClockTreeReportData& report_data) -> CTSGdsReportResult
{
  const auto output_dir = resolveVisualizationDir(visualization_dir) / "gds";
  const auto design_gds_path = output_dir / kDesignGdsLabel;
  const auto flyline_gds_path = output_dir / kFlylineGdsLabel;
  const auto design_lyp_path = output_dir / kDesignLayerPropertiesLabel;
  const auto flyline_lyp_path = output_dir / kFlylineLayerPropertiesLabel;

  const auto model = ClockTreeVisualizationModelBuilder::build(report_data);
  if (!model.has_clocks || (model.design_segments.empty() && model.flyline_segments.empty())) {
    LOG_WARNING << "CTS GDS report skipped because no clock-tree report data is available.";
    emitReportTable(design_gds_path, false, design_lyp_path, false, flyline_gds_path, false, flyline_lyp_path, false);
    return CTSGdsReportResult{.success = false};
  }

  ClockTreeVisualizationLayerPolicy design_layer_policy;
  auto design_library = buildLibrary("CTS_DESIGN", "CTS_DESIGN", model, ClockTreeReportView::kDesign, design_layer_policy);
  ClockTreeVisualizationLayerPolicy flyline_layer_policy;
  auto flyline_library = buildLibrary("CTS_FLYLINE", "CTS_FLYLINE", model, ClockTreeReportView::kFlyline, flyline_layer_policy);

  const bool design_success = CTSGdsWriter::writeBinary(design_gds_path, design_library);
  const bool flyline_success = CTSGdsWriter::writeBinary(flyline_gds_path, flyline_library);
  const bool design_lyp_success = CTSGdsWriter::writeLayerProperties(design_lyp_path, design_layer_policy.getLayerProperties());
  const bool flyline_lyp_success = CTSGdsWriter::writeLayerProperties(flyline_lyp_path, flyline_layer_policy.getLayerProperties());
  emitReportTable(design_gds_path, design_success, design_lyp_path, design_lyp_success, flyline_gds_path, flyline_success, flyline_lyp_path,
                  flyline_lyp_success);
  return CTSGdsReportResult{.success = design_success && flyline_success && design_lyp_success && flyline_lyp_success};
}

}  // namespace icts::report
