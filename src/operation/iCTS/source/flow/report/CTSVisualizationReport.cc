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
 * @file CTSVisualizationReport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Report-stage CTS SVG visualization report generation implementation.
 */

#include "report/CTSVisualizationReport.hh"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.hh"
#include "SteinerTree.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/Schema.hh"
#include "report_data/ClockTreeReportData.hh"
#include "router/Router.hh"
#include "spatial/Point.hh"
#include "visualization/core/SvgCommon.hh"

namespace icts::report {
namespace {

constexpr const char* kDesignSvgLabel = "cts_design.svg";
constexpr const char* kFlylineSvgLabel = "cts_flyline.svg";

enum class WireKind
{
  kRouted,
  kSourceToRoot,
  kFlyline,
  kFallback
};

struct VisualizationSegment
{
  Point<int> begin;
  Point<int> end;
  WireKind kind = WireKind::kRouted;
  bool fallback = false;
};

struct SegmentCollection
{
  std::vector<VisualizationSegment> segments;
  std::size_t routed_net_count = 0U;
  std::size_t fallback_net_count = 0U;
  std::size_t skipped_net_count = 0U;
};

struct VisualizationReportStatus
{
  std::string label;
  std::filesystem::path path;
  std::string view_label;
  bool success = false;
  std::string reason;
};

auto ResolveVisualizationDir(const std::filesystem::path& visualization_dir) -> std::filesystem::path
{
  if (!visualization_dir.empty()) {
    return visualization_dir;
  }
  return std::filesystem::path(CONFIG_INST.get_visualization_dir());
}

auto HasValidLocation(const Point<int>& point) -> bool
{
  return point.get_x() >= 0 && point.get_y() >= 0;
}

auto wireKindFromRole(CTSNetRole role, bool flyline) -> WireKind
{
  if (flyline) {
    return WireKind::kFlyline;
  }
  if (role == CTSNetRole::kSourceToRoot || role == CTSNetRole::kClockSource) {
    return WireKind::kSourceToRoot;
  }
  return WireKind::kRouted;
}

auto EnsureParentDirectory(const std::filesystem::path& path, std::string& detail) -> bool
{
  const auto parent_path = path.parent_path();
  if (parent_path.empty()) {
    return true;
  }

  std::error_code error;
  std::filesystem::create_directories(parent_path, error);
  if (error) {
    detail = "failed to create report directory " + parent_path.string() + ": " + error.message();
    return false;
  }
  return true;
}

auto MakeFailure(std::string label, const std::filesystem::path& path, std::string view_label, std::string reason)
    -> VisualizationReportStatus
{
  LOG_WARNING << "CTS visualization report " << label << " failed: " << reason;
  return VisualizationReportStatus{
      .label = std::move(label), .path = path, .view_label = std::move(view_label), .success = false, .reason = std::move(reason)};
}

auto MakeSuccess(std::string label, const std::filesystem::path& path, std::string view_label, std::string reason)
    -> VisualizationReportStatus
{
  return VisualizationReportStatus{
      .label = std::move(label), .path = path, .view_label = std::move(view_label), .success = true, .reason = std::move(reason)};
}

auto CollectClockNets() -> std::vector<Net*>
{
  std::vector<Net*> nets;
  std::unordered_set<Net*> seen_nets;
  auto append_net = [&nets, &seen_nets](Net* net) -> void {
    if (net == nullptr || seen_nets.contains(net)) {
      return;
    }
    seen_nets.insert(net);
    nets.push_back(net);
  };

  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    append_net(clock->get_clock_source_net());
    for (auto* net : clock->get_nets()) {
      append_net(net);
    }
  }
  return nets;
}

auto CollectClockInsts() -> std::vector<Inst*>
{
  std::vector<Inst*> insts;
  std::unordered_set<Inst*> seen_insts;
  for (auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    for (auto* inst : clock->get_insts()) {
      if (inst == nullptr || seen_insts.contains(inst)) {
        continue;
      }
      seen_insts.insert(inst);
      insts.push_back(inst);
    }
  }
  return insts;
}

auto MakeSegment(const Point<int>& begin, const Point<int>& end, WireKind kind, bool fallback) -> VisualizationSegment
{
  return VisualizationSegment{
      .begin = begin,
      .end = end,
      .kind = kind,
      .fallback = fallback,
  };
}

auto AppendFlylineSegmentsForNet(const Net& net, std::vector<VisualizationSegment>& segments) -> bool
{
  auto* driver = net.get_driver();
  if (driver == nullptr || !HasValidLocation(driver->get_location())) {
    return false;
  }

  bool appended = false;
  const auto driver_location = driver->get_location();
  for (auto* load : net.get_loads()) {
    if (load == nullptr || !HasValidLocation(load->get_location())) {
      continue;
    }
    segments.push_back(MakeSegment(driver_location, load->get_location(), WireKind::kFlyline, false));
    appended = true;
  }
  return appended;
}

auto AppendFallbackSegmentsForNet(const Net& net, std::vector<VisualizationSegment>& segments) -> bool
{
  auto* driver = net.get_driver();
  if (driver == nullptr || !HasValidLocation(driver->get_location())) {
    return false;
  }

  bool appended = false;
  const auto driver_location = driver->get_location();
  for (auto* load : net.get_loads()) {
    if (load == nullptr || !HasValidLocation(load->get_location())) {
      continue;
    }
    segments.push_back(MakeSegment(driver_location, load->get_location(), WireKind::kFallback, true));
    appended = true;
  }
  return appended;
}

auto AppendRouteTreeSegmentsForNet(const Net& net, std::vector<VisualizationSegment>& segments) -> bool
{
  auto route_tree = Router::buildClockNetTree(net);
  if (route_tree.node_count() == 0U || route_tree.edge_count() == 0U) {
    return false;
  }

  bool appended = false;
  for (const auto& edge : route_tree.get_edges()) {
    const auto* source = route_tree.get_node(edge.source_node_id);
    const auto* target = route_tree.get_node(edge.target_node_id);
    if (source == nullptr || target == nullptr || !HasValidLocation(source->location) || !HasValidLocation(target->location)) {
      continue;
    }
    segments.push_back(MakeSegment(source->location, target->location, WireKind::kRouted, false));
    appended = true;
  }
  return appended;
}

auto AppendReportSegments(const std::vector<ClockTreeReportSegment>& report_segments, std::vector<VisualizationSegment>& segments,
                          bool flyline) -> void
{
  for (const auto& segment : report_segments) {
    if (!HasValidLocation(segment.begin) || !HasValidLocation(segment.end)) {
      continue;
    }
    segments.push_back(MakeSegment(segment.begin, segment.end, wireKindFromRole(segment.net_role, flyline), segment.fallback));
  }
}

auto CollectDesignReportSegments(const ClockTreeReportData& report_data) -> SegmentCollection
{
  SegmentCollection collection;
  for (const auto& clock : report_data.get_clocks()) {
    for (const auto& net : clock.nets) {
      const auto before_count = collection.segments.size();
      AppendReportSegments(net.routed_segments, collection.segments, false);
      if (collection.segments.size() > before_count) {
        ++collection.routed_net_count;
      } else {
        ++collection.skipped_net_count;
      }
    }
  }
  return collection;
}

auto CollectFlylineReportSegments(const ClockTreeReportData& report_data) -> SegmentCollection
{
  SegmentCollection collection;
  for (const auto& clock : report_data.get_clocks()) {
    for (const auto& net : clock.nets) {
      const auto before_count = collection.segments.size();
      AppendReportSegments(net.flyline_segments, collection.segments, true);
      if (collection.segments.size() > before_count) {
        ++collection.routed_net_count;
      } else {
        ++collection.skipped_net_count;
      }
    }
  }
  return collection;
}

auto CollectDesignSegments(const std::vector<Net*>& nets) -> SegmentCollection
{
  SegmentCollection collection;
  for (auto* net : nets) {
    if (net == nullptr) {
      ++collection.skipped_net_count;
      continue;
    }

    if (AppendRouteTreeSegmentsForNet(*net, collection.segments)) {
      ++collection.routed_net_count;
      continue;
    }

    if (AppendFallbackSegmentsForNet(*net, collection.segments)) {
      ++collection.fallback_net_count;
      LOG_WARNING << "CTS report visualization: design view falls back to driver-to-load segments for net " << net->get_name();
      continue;
    }

    ++collection.skipped_net_count;
  }
  return collection;
}

auto CollectFlylineSegments(const std::vector<Net*>& nets) -> SegmentCollection
{
  SegmentCollection collection;
  for (auto* net : nets) {
    if (net == nullptr) {
      ++collection.skipped_net_count;
      continue;
    }
    if (AppendFlylineSegmentsForNet(*net, collection.segments)) {
      ++collection.routed_net_count;
      continue;
    }
    ++collection.skipped_net_count;
  }
  return collection;
}

auto CollectDrawablePoints(const std::vector<VisualizationSegment>& segments, const std::vector<Net*>& nets,
                           const std::vector<Inst*>& insts) -> std::vector<Point<int>>
{
  std::vector<Point<int>> points;
  points.reserve((segments.size() * 2U) + nets.size() + insts.size());
  for (const auto& segment : segments) {
    if (HasValidLocation(segment.begin)) {
      points.push_back(segment.begin);
    }
    if (HasValidLocation(segment.end)) {
      points.push_back(segment.end);
    }
  }
  for (const auto* net : nets) {
    if (net == nullptr) {
      continue;
    }
    if (net->get_driver() != nullptr && HasValidLocation(net->get_driver()->get_location())) {
      points.push_back(net->get_driver()->get_location());
    }
    for (const auto* load : net->get_loads()) {
      if (load != nullptr && HasValidLocation(load->get_location())) {
        points.push_back(load->get_location());
      }
    }
  }
  for (const auto* inst : insts) {
    if (inst != nullptr && HasValidLocation(inst->get_location())) {
      points.push_back(inst->get_location());
    }
  }
  return points;
}

auto FormatSvgNumber(double value) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream << std::setprecision(2) << value;
  return stream.str();
}

auto EscapeXml(const std::string& text) -> std::string
{
  std::string escaped;
  escaped.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

auto ResolveSegmentStyle(WireKind kind, bool fallback, bool flyline_view) -> std::tuple<const char*, double, double, std::string>
{
  const char* stroke_color = visualization::detail::kSvgColorRoutedSinkNet;
  double stroke_width = 1.8;
  double stroke_opacity = 0.78;
  std::string dash_array;
  if (flyline_view || kind == WireKind::kFlyline) {
    stroke_color = visualization::detail::kSvgColorFlylineRootNet;
    stroke_width = 1.2;
    stroke_opacity = 0.62;
    dash_array = "8,5";
  } else if (fallback || kind == WireKind::kFallback) {
    stroke_color = visualization::detail::kSvgColorFallbackInternalNet;
    stroke_width = 1.4;
    stroke_opacity = 0.74;
    dash_array = "5,3";
  } else if (kind == WireKind::kSourceToRoot) {
    stroke_color = visualization::detail::kSvgColorFlylineRootNet;
    stroke_width = 1.9;
    stroke_opacity = 0.82;
  }
  return {stroke_color, stroke_width, stroke_opacity, dash_array};
}

auto WriteSvgSegments(std::ofstream& output_stream, const visualization::detail::SvgTransform& transform,
                      const std::vector<VisualizationSegment>& segments, bool flyline_view) -> void
{
  output_stream << R"(<g fill="none" stroke-linecap="round">
)";
  for (const auto& segment : segments) {
    const auto begin_x = FormatSvgNumber(visualization::detail::MapX(transform, segment.begin.get_x()));
    const auto begin_y = FormatSvgNumber(visualization::detail::MapY(transform, segment.begin.get_y()));
    const auto end_x = FormatSvgNumber(visualization::detail::MapX(transform, segment.end.get_x()));
    const auto end_y = FormatSvgNumber(visualization::detail::MapY(transform, segment.end.get_y()));
    const auto [stroke_color, stroke_width, stroke_opacity, dash_array] = ResolveSegmentStyle(segment.kind, segment.fallback, flyline_view);
    output_stream << R"(<line x1=")" << begin_x << R"(" y1=")" << begin_y << R"(" x2=")" << end_x << R"(" y2=")" << end_y << R"(" stroke=")"
                  << stroke_color << R"(" stroke-width=")" << FormatSvgNumber(stroke_width) << R"(" stroke-opacity=")"
                  << FormatSvgNumber(stroke_opacity) << '"';
    if (!dash_array.empty()) {
      output_stream << R"( stroke-dasharray=")" << dash_array << '"';
    }
    output_stream << R"( />
)";
  }
  output_stream << R"(</g>
)";
}

auto WriteSvgPins(std::ofstream& output_stream, const visualization::detail::SvgTransform& transform, const std::vector<Net*>& nets) -> void
{
  output_stream << R"(<g stroke-width="0.7">
)";
  std::unordered_set<const Pin*> seen_loads;
  for (const auto* net : nets) {
    if (net == nullptr) {
      continue;
    }
    if (const auto* driver = net->get_driver(); driver != nullptr && HasValidLocation(driver->get_location())) {
      const auto location = driver->get_location();
      output_stream << R"(<circle cx=")" << FormatSvgNumber(visualization::detail::MapX(transform, location.get_x())) << R"(" cy=")"
                    << FormatSvgNumber(visualization::detail::MapY(transform, location.get_y())) << R"(" r=")"
                    << FormatSvgNumber(visualization::detail::kReportDriverRadius) << R"(" fill=")"
                    << visualization::detail::kSvgColorDriverRoot << R"(" stroke=")" << visualization::detail::kSvgColorNodeStroke
                    << R"(" />
)";
    }
    for (const auto* load : net->get_loads()) {
      if (load == nullptr || !HasValidLocation(load->get_location()) || seen_loads.contains(load)) {
        continue;
      }
      seen_loads.insert(load);
      const auto location = load->get_location();
      output_stream << R"(<circle cx=")" << FormatSvgNumber(visualization::detail::MapX(transform, location.get_x())) << R"(" cy=")"
                    << FormatSvgNumber(visualization::detail::MapY(transform, location.get_y())) << R"(" r=")"
                    << FormatSvgNumber(visualization::detail::kReportSinkLoadRadius) << R"(" fill=")"
                    << visualization::detail::kSvgColorSinkLoad << R"(" fill-opacity="0.78" stroke=")"
                    << visualization::detail::kSvgColorLoadStroke << R"(" />
)";
    }
  }
  output_stream << R"(</g>
)";
}

auto WriteSvgInsts(std::ofstream& output_stream, const visualization::detail::SvgTransform& transform, const std::vector<Inst*>& insts)
    -> void
{
  output_stream << R"(<g fill=")" << visualization::detail::kSvgColorBufferFillDefault << R"(" fill-opacity="0.86" stroke=")"
                << visualization::detail::kSvgColorBufferStrokeDefault << R"(" stroke-width="0.8">
)";
  for (const auto* inst : insts) {
    if (inst == nullptr || !HasValidLocation(inst->get_location())) {
      continue;
    }
    const auto location = inst->get_location();
    const auto center_x = visualization::detail::MapX(transform, location.get_x());
    const auto center_y = visualization::detail::MapY(transform, location.get_y());
    const double marker_size = visualization::detail::kReportCtsBufferSize;
    output_stream << R"(<rect x=")" << FormatSvgNumber(center_x - marker_size / 2.0) << R"(" y=")"
                  << FormatSvgNumber(center_y - marker_size / 2.0) << R"(" width=")" << FormatSvgNumber(marker_size) << R"(" height=")"
                  << FormatSvgNumber(marker_size) << R"(" />
)";
  }
  output_stream << R"(</g>
)";
}

auto WriteSvgLegend(std::ofstream& output_stream, const visualization::detail::SvgTransform& transform) -> void
{
  const double legend_x = visualization::detail::kSvgLegendX;
  const double legend_row_height = visualization::detail::kSvgLegendRowHeight;
  constexpr std::size_t row_count = 6U;
  const double legend_height = 24.0 + (legend_row_height * static_cast<double>(row_count));
  const double legend_y = std::max(22.0, static_cast<double>(transform.height) - legend_height - 18.0);
  auto row_y = [legend_y, legend_row_height](double row_index) -> double { return legend_y + (row_index * legend_row_height); };

  output_stream << R"(<g font-family="monospace" font-size="12" fill=")" << visualization::detail::kSvgColorLegendText << R"(">)";
  output_stream << R"(<rect x=")" << FormatSvgNumber(legend_x - 8.0) << R"(" y=")" << FormatSvgNumber(legend_y - 16.0)
                << R"(" width="260" height=")" << FormatSvgNumber(legend_height) << R"(" rx="6" fill=")"
                << visualization::detail::kSvgColorLegendFill << R"(" fill-opacity=")"
                << FormatSvgNumber(visualization::detail::kSvgLegendFrameOpacity) << R"(" stroke=")"
                << visualization::detail::kSvgColorLegendStroke << R"(" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x) << R"(" y=")" << FormatSvgNumber(legend_y) << R"(">Legend</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_y(1.0) - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 14.0) << R"(" y2=")" << FormatSvgNumber(row_y(1.0) - 4.0) << R"(" stroke=")"
                << visualization::detail::kSvgColorRoutedSinkNet << R"(" stroke-width="1.8" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(1.0))
                << R"(">routed segment</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_y(2.0) - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 14.0) << R"(" y2=")" << FormatSvgNumber(row_y(2.0) - 4.0) << R"(" stroke=")"
                << visualization::detail::kSvgColorFlylineRootNet << R"(" stroke-width="1.2" stroke-dasharray="8,5" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(2.0))
                << R"(">flyline segment</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_y(3.0) - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 14.0) << R"(" y2=")" << FormatSvgNumber(row_y(3.0) - 4.0) << R"(" stroke=")"
                << visualization::detail::kSvgColorFallbackInternalNet << R"(" stroke-width="1.4" stroke-dasharray="5,3" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(3.0))
                << R"(">fallback segment</text>)";

  output_stream << R"(<circle cx=")" << FormatSvgNumber(legend_x + 7.0) << R"(" cy=")" << FormatSvgNumber(row_y(4.0) - 4.0) << R"(" r=")"
                << FormatSvgNumber(visualization::detail::kReportDriverRadius) << R"(" fill=")"
                << visualization::detail::kSvgColorDriverRoot << R"(" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(4.0))
                << R"(">driver/root</text>)";

  output_stream << R"(<circle cx=")" << FormatSvgNumber(legend_x + 7.0) << R"(" cy=")" << FormatSvgNumber(row_y(5.0) - 4.0) << R"(" r=")"
                << FormatSvgNumber(visualization::detail::kReportSinkLoadRadius) << R"(" fill=")"
                << visualization::detail::kSvgColorSinkLoad << R"(" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(5.0))
                << R"(">sink load</text>)";

  output_stream << R"(<rect x=")" << FormatSvgNumber(legend_x + 4.0) << R"(" y=")" << FormatSvgNumber(row_y(6.0) - 8.0) << R"(" width=")"
                << FormatSvgNumber(visualization::detail::kReportCtsBufferSize) << R"(" height=")"
                << FormatSvgNumber(visualization::detail::kReportCtsBufferSize) << R"(" fill=")"
                << visualization::detail::kSvgColorBufferFillDefault << R"(" stroke=")"
                << visualization::detail::kSvgColorBufferStrokeDefault << R"(" stroke-width="1.0" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 20.0) << R"(" y=")" << FormatSvgNumber(row_y(6.0))
                << R"(">CTS buffer</text>)";
  output_stream << R"(</g>
)";
}

auto WriteSvgFile(const std::filesystem::path& path, const std::string& label, const std::string& view_label,
                  const std::vector<VisualizationSegment>& segments, const std::vector<Net*>& nets, const std::vector<Inst*>& insts,
                  bool flyline_view) -> VisualizationReportStatus
{
  if (segments.empty()) {
    return MakeFailure(label, path, view_label, "no drawable CTS net segments are available");
  }

  const auto points = CollectDrawablePoints(segments, nets, insts);
  const auto bounds = visualization::detail::ComputeBounds({}, points);
  if (!bounds.valid) {
    return MakeFailure(label, path, view_label, "no valid CTS locations are available for SVG bounds");
  }
  const auto transform = visualization::detail::MakeTransform(bounds);

  std::string detail;
  if (!EnsureParentDirectory(path, detail)) {
    return MakeFailure(label, path, view_label, detail);
  }

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return MakeFailure(label, path, view_label, "failed to open report file for writing");
  }

  output_stream << visualization::detail::kSvgOpenTagPrefix << FormatSvgNumber(transform.width) << visualization::detail::kSvgHeightTag
                << FormatSvgNumber(transform.height) << visualization::detail::kSvgViewBoxPrefix << FormatSvgNumber(transform.width) << ' '
                << FormatSvgNumber(transform.height) << visualization::detail::kSvgOpenTagSuffix;
  output_stream << visualization::detail::kSvgBackgroundRect;
  output_stream << R"(<title>)" << EscapeXml(label) << R"(</title>
)";
  WriteSvgSegments(output_stream, transform, segments, flyline_view);
  WriteSvgInsts(output_stream, transform, insts);
  WriteSvgPins(output_stream, transform, nets);
  WriteSvgLegend(output_stream, transform);
  output_stream << visualization::detail::kSvgClosingTag;
  output_stream.close();
  if (!output_stream) {
    return MakeFailure(label, path, view_label, "failed while flushing report file to disk");
  }
  return MakeSuccess(label, path, view_label, "generated");
}

auto BuildUnavailableStatuses(const std::filesystem::path& output_dir, const std::string& reason) -> std::vector<VisualizationReportStatus>
{
  return {
      MakeFailure(kDesignSvgLabel, output_dir / kDesignSvgLabel, "svg/design", reason),
      MakeFailure(kFlylineSvgLabel, output_dir / kFlylineSvgLabel, "svg/flyline", reason),
  };
}

auto EmitReportStatusTable(const std::vector<VisualizationReportStatus>& statuses) -> void
{
  schema::TableRows rows;
  rows.reserve(statuses.size());
  for (const auto& status : statuses) {
    rows.push_back({
        status.label,
        status.path.string(),
        status.view_label,
        status.success ? "generated" : "failed",
        status.reason,
    });
    if (!status.success) {
      schema::EmitDiagnostic(
          schema::DiagnosticLevel::kWarning, "CTS Report Visualization", "visualization report generation failed",
          {{"report", status.label}, {"path", status.path.string()}, {"view", status.view_label}, {"reason", status.reason}});
    }
  }
  schema::EmitTable("CTS Visualization Reports", {"Report", "Path", "View", "Status", "Detail"}, rows);
}

}  // namespace

auto EmitCTSVisualizationReports(const std::filesystem::path& visualization_dir, const ClockTreeReportData& report_data)
    -> CTSVisualizationReportResult
{
  const auto output_dir = ResolveVisualizationDir(visualization_dir) / "svg";
  auto nets = CollectClockNets();
  auto insts = CollectClockInsts();

  std::vector<VisualizationReportStatus> statuses;
  if (DESIGN_INST.get_clocks().empty()) {
    statuses = BuildUnavailableStatuses(output_dir, "CTS design contains no clocks; run CTS or initialize clock data before report");
    EmitReportStatusTable(statuses);
    return CTSVisualizationReportResult{.success = false};
  }
  if (nets.empty()) {
    statuses = BuildUnavailableStatuses(output_dir, "CTS design contains no clock nets to visualize");
    EmitReportStatusTable(statuses);
    return CTSVisualizationReportResult{.success = false};
  }

  auto design_collection = CollectDesignReportSegments(report_data);
  auto flyline_collection = CollectFlylineReportSegments(report_data);
  if (design_collection.segments.empty()) {
    design_collection = CollectDesignSegments(nets);
  }
  if (flyline_collection.segments.empty()) {
    flyline_collection = CollectFlylineSegments(nets);
  }

  statuses.push_back(
      WriteSvgFile(output_dir / kDesignSvgLabel, kDesignSvgLabel, "svg/design", design_collection.segments, nets, insts, false));
  statuses.push_back(
      WriteSvgFile(output_dir / kFlylineSvgLabel, kFlylineSvgLabel, "svg/flyline", flyline_collection.segments, nets, insts, true));

  EmitReportStatusTable(statuses);
  const bool success = std::ranges::all_of(statuses, [](const auto& status) -> bool { return status.success; });
  return CTSVisualizationReportResult{.success = success};
}

}  // namespace icts::report
