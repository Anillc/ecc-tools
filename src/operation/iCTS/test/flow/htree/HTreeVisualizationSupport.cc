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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file HTreeVisualizationSupport.cc
 * @author OpenAI Codex
 * @date 2026-04-14
 * @brief SVG artifact emission helpers for HTreeBuilder tests.
 */

#include "flow/htree/HTreeVisualizationSupport.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <compare>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Inst.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "Tree.hh"
#include "common/io/TestArtifactIO.hh"
#include "common/visualization/TestVisualization.hh"
#include "common/visualization/core/SvgCommon.hh"
#include "htree/HTreeBuilder.hh"
#include "utils/logger/Schema.hh"

namespace icts_test::htree {
namespace {

using common::visualization::detail::ComputeBounds;
using common::visualization::detail::MakeTransform;
using common::visualization::detail::MapX;
using common::visualization::detail::MapY;

constexpr double kBufferHalfSize = 6.0;
constexpr double kRootRingRadius = 10.0;

struct BufferMasterSummary
{
  std::string cell_master;
  std::size_t count = 0;
  int drive_strength = -1;
};

struct BufferRenderStyle
{
  std::string fill_color = "#ffbf69";
  std::string stroke_color = "#8c4f00";
  double half_size = kBufferHalfSize;
};

struct DelayPowerPoint
{
  const icts::HTreeTopologyChar* entry = nullptr;
  double normalized_delay = 0.0;
  double normalized_power = 0.0;
  bool is_pareto = false;
  bool is_selected = false;
};

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
  for (const char value : text) {
    switch (value) {
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
        escaped.push_back(value);
        break;
    }
  }
  return escaped;
}

auto HasValidLocation(const icts::Point<int>& location) -> bool
{
  return location.get_x() >= 0 && location.get_y() >= 0;
}

auto ParseDriveStrength(const std::string& cell_master) -> int
{
  const std::size_t x_pos = cell_master.find('X');
  if (x_pos == std::string::npos || x_pos + 1 >= cell_master.size()) {
    return -1;
  }

  int drive_strength = 0;
  bool saw_digit = false;
  for (std::size_t index = x_pos + 1; index < cell_master.size(); ++index) {
    const auto ch = static_cast<unsigned char>(cell_master[index]);
    if (std::isdigit(ch) == 0) {
      break;
    }

    saw_digit = true;
    drive_strength = (drive_strength * 10) + static_cast<int>(cell_master[index] - '0');
  }

  return saw_digit ? drive_strength : -1;
}

auto FindRenderableLocation(const icts::Pin* pin) -> icts::Point<int>
{
  if (pin == nullptr) {
    return {-1, -1};
  }
  if (HasValidLocation(pin->get_location())) {
    return pin->get_location();
  }
  if (pin->get_inst() != nullptr) {
    return pin->get_inst()->get_location();
  }
  return {-1, -1};
}

auto CollectExtraPoints(const icts::HTreeBuilder::BuildResult& result) -> std::vector<icts::Point<int>>
{
  std::vector<icts::Point<int>> extra_points;
  extra_points.reserve(result.topology.get_size() + result.inserted_insts.size() + result.inserted_pins.size());

  for (std::size_t node_id = 0; node_id < result.topology.get_size(); ++node_id) {
    const auto* node = result.topology.get_node(node_id);
    if (node == nullptr || !HasValidLocation(node->get_position())) {
      continue;
    }
    extra_points.push_back(node->get_position());
  }

  for (const auto* inst : result.inserted_insts) {
    if (inst == nullptr || !HasValidLocation(inst->get_location())) {
      continue;
    }
    extra_points.push_back(inst->get_location());
  }

  for (const auto* pin : result.inserted_pins) {
    const auto location = FindRenderableLocation(pin);
    if (!HasValidLocation(location)) {
      continue;
    }
    extra_points.push_back(location);
  }

  return extra_points;
}

auto WriteTooltip(std::ofstream& output_stream, const std::string& text) -> void
{
  output_stream << "<title>" << EscapeXml(text) << "</title>";
}

auto IsOriginalLoadPin(const icts::Pin* pin, const std::unordered_set<const icts::Pin*>& original_loads) -> bool
{
  return pin != nullptr && original_loads.contains(pin);
}

auto ResolveNetStrokeColor(bool is_root_net, bool reaches_sink) -> std::string
{
  if (is_root_net) {
    return "#6a3d9a";
  }
  if (reaches_sink) {
    return "#2ca25f";
  }
  return "#ff8c42";
}

auto ResolveNetStrokeWidth(bool is_root_net, bool reaches_sink) -> double
{
  if (is_root_net) {
    return 2.4;
  }
  if (reaches_sink) {
    return 2.0;
  }
  return 1.6;
}

auto CollectBufferMasterSummaries(const std::vector<icts::Inst*>& inserted_insts) -> std::vector<BufferMasterSummary>
{
  std::map<std::string, std::size_t> master_counts;
  for (const auto* inst : inserted_insts) {
    if (inst == nullptr || inst->get_cell_master().empty()) {
      continue;
    }
    ++master_counts[inst->get_cell_master()];
  }

  std::vector<BufferMasterSummary> summaries;
  summaries.reserve(master_counts.size());
  for (const auto& [cell_master, count] : master_counts) {
    summaries.push_back(BufferMasterSummary{cell_master, count, ParseDriveStrength(cell_master)});
  }

  std::ranges::sort(summaries, [](const BufferMasterSummary& lhs, const BufferMasterSummary& rhs) -> bool {
    const bool lhs_has_strength = lhs.drive_strength >= 0;
    const bool rhs_has_strength = rhs.drive_strength >= 0;
    if (lhs_has_strength != rhs_has_strength) {
      return lhs_has_strength;
    }
    if (lhs_has_strength && lhs.drive_strength != rhs.drive_strength) {
      return lhs.drive_strength < rhs.drive_strength;
    }
    return lhs.cell_master < rhs.cell_master;
  });

  return summaries;
}

auto BuildBufferRenderStyles(const std::vector<BufferMasterSummary>& summaries) -> std::unordered_map<std::string, BufferRenderStyle>
{
  static constexpr std::array<const char*, 6> fill_palette = {
      "#ffe0b2", "#ffbf69", "#f4a261", "#e76f51", "#d1495b", "#9c6644",
  };
  static constexpr std::array<const char*, 6> stroke_palette = {
      "#b36a00", "#8c4f00", "#9a4d1f", "#91361f", "#7a1f30", "#5d4037",
  };

  std::unordered_map<std::string, BufferRenderStyle> styles;
  styles.reserve(summaries.size());
  if (summaries.empty()) {
    return styles;
  }

  constexpr double min_half_size = kBufferHalfSize - 1.5;
  constexpr double max_half_size = kBufferHalfSize + 2.5;
  const std::size_t rank_count = summaries.size() > 1 ? summaries.size() - 1 : 1;

  for (std::size_t index = 0; index < summaries.size(); ++index) {
    const double rank_ratio = summaries.size() == 1 ? 0.5 : static_cast<double>(index) / static_cast<double>(rank_count);
    const double half_size = min_half_size + ((max_half_size - min_half_size) * rank_ratio);
    BufferRenderStyle style;
    style.fill_color = fill_palette[index % fill_palette.size()];
    style.stroke_color = stroke_palette[index % stroke_palette.size()];
    style.half_size = half_size;
    styles.emplace(summaries[index].cell_master, style);
  }

  return styles;
}

auto NormalizeMetric(double value, double min_value, double max_value) -> double
{
  if (max_value <= min_value) {
    return 0.0;
  }
  return std::clamp((value - min_value) / (max_value - min_value), 0.0, 1.0);
}

auto DelayPowerDominates(const icts::HTreeTopologyChar& lhs, const icts::HTreeTopologyChar& rhs) -> bool
{
  const bool not_worse = lhs.get_delay() <= rhs.get_delay() && lhs.get_power() <= rhs.get_power();
  const bool strictly_better = lhs.get_delay() < rhs.get_delay() || lhs.get_power() < rhs.get_power();
  return not_worse && strictly_better;
}

auto BuildDelayPowerPoints(const icts::HTreeBuilder::BuildResult& result) -> std::vector<DelayPowerPoint>
{
  const auto& display_chars = result.feasible_chars.empty() ? result.candidate_chars : result.feasible_chars;
  if (display_chars.empty()) {
    return {};
  }

  const auto [min_delay_it, max_delay_it] = std::ranges::minmax_element(display_chars, {}, &icts::HTreeTopologyChar::get_delay);
  const auto [min_power_it, max_power_it] = std::ranges::minmax_element(display_chars, {}, &icts::HTreeTopologyChar::get_power);
  const double min_delay = min_delay_it->get_delay();
  const double max_delay = max_delay_it->get_delay();
  const double min_power = min_power_it->get_power();
  const double max_power = max_power_it->get_power();

  std::vector<bool> pareto_flags(display_chars.size(), true);
  for (std::size_t index = 0; index < display_chars.size(); ++index) {
    for (std::size_t other_index = 0; other_index < display_chars.size(); ++other_index) {
      if (index == other_index) {
        continue;
      }
      if (DelayPowerDominates(display_chars[other_index], display_chars[index])) {
        pareto_flags[index] = false;
        break;
      }
    }
  }

  const auto selected_pattern_id
      = result.best_char.has_value() ? std::optional<icts::PatternId>(result.best_char->get_pattern_id()) : std::nullopt;

  std::vector<DelayPowerPoint> points;
  points.reserve(display_chars.size());
  for (std::size_t index = 0; index < display_chars.size(); ++index) {
    const auto& entry = display_chars[index];
    points.push_back(DelayPowerPoint{
        .entry = &entry,
        .normalized_delay = NormalizeMetric(entry.get_delay(), min_delay, max_delay),
        .normalized_power = NormalizeMetric(entry.get_power(), min_power, max_power),
        .is_pareto = pareto_flags[index],
        .is_selected = selected_pattern_id.has_value() && entry.get_pattern_id() == *selected_pattern_id,
    });
  }

  std::ranges::sort(points, [](const DelayPowerPoint& lhs, const DelayPowerPoint& rhs) -> bool {
    if (lhs.normalized_delay != rhs.normalized_delay) {
      return lhs.normalized_delay < rhs.normalized_delay;
    }
    if (lhs.normalized_power != rhs.normalized_power) {
      return lhs.normalized_power < rhs.normalized_power;
    }
    return lhs.entry->get_pattern_id().pack() < rhs.entry->get_pattern_id().pack();
  });
  return points;
}

auto BuildDelayPowerTooltip(const DelayPowerPoint& point) -> std::string
{
  std::ostringstream tooltip;
  tooltip.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  tooltip << std::setprecision(6);
  tooltip << "pattern_id=" << point.entry->get_pattern_id().local_id;
  tooltip << ", delay=" << point.entry->get_delay();
  tooltip << ", power=" << point.entry->get_power();
  tooltip << ", norm_delay=" << point.normalized_delay;
  tooltip << ", norm_power=" << point.normalized_power;
  tooltip << ", in_slew_idx=" << point.entry->get_input_slew_idx();
  tooltip << ", out_slew_idx=" << point.entry->get_output_slew_idx();
  tooltip << ", driven_cap_idx=" << point.entry->get_driven_cap_idx();
  tooltip << ", load_cap_idx=" << point.entry->get_load_cap_idx();
  if (point.is_pareto) {
    tooltip << ", pareto=true";
  }
  if (point.is_selected) {
    tooltip << ", selected=true";
  }
  return tooltip.str();
}

auto WriteStraightConnection(std::ofstream& output_stream, const std::string& source_x, const std::string& source_y,
                             const std::string& target_x, const std::string& target_y, const std::string& stroke_color, double stroke_width,
                             const std::string& dash_array, const std::string& tooltip) -> void
{
  output_stream << R"(<line x1=")" << source_x << R"(" y1=")" << source_y << R"(" x2=")" << target_x << R"(" y2=")" << target_y
                << R"(" stroke=")" << stroke_color << R"(" stroke-width=")" << stroke_width << R"(" stroke-linecap="round")";
  if (!dash_array.empty()) {
    output_stream << R"( stroke-dasharray=")" << dash_array << '"';
  }
  output_stream << '>';
  WriteTooltip(output_stream, tooltip);
  output_stream << R"(</line>
)";
}

auto WriteLegend(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                 const std::vector<BufferMasterSummary>& buffer_summaries,
                 const std::unordered_map<std::string, BufferRenderStyle>& buffer_styles) -> void
{
  const double legend_x = 18.0;
  constexpr double legend_row_height = 18.0;
  const std::size_t base_row_count = 5U;
  const std::size_t total_row_count = base_row_count + buffer_summaries.size();
  const double legend_height = 24.0 + (legend_row_height * static_cast<double>(total_row_count));
  const double legend_y = std::max(22.0, static_cast<double>(transform.height) - legend_height - 18.0);

  output_stream << R"(<g font-family="monospace" font-size="12" fill="#222222">)";
  output_stream << R"(<rect x=")" << FormatSvgNumber(legend_x - 8.0) << R"(" y=")" << FormatSvgNumber(legend_y - 16.0)
                << R"(" width="280" height=")" << FormatSvgNumber(legend_height)
                << R"(" rx="6" fill="#ffffff" fill-opacity="0.88" stroke="#d0d0d0" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x) << R"(" y=")" << FormatSvgNumber(legend_y) << R"(">Legend</text>)";

  const double row_1_y = legend_y + legend_row_height;
  const double row_2_y = legend_y + (2.0 * legend_row_height);
  const double row_3_y = legend_y + (3.0 * legend_row_height);
  const double row_4_y = legend_y + (4.0 * legend_row_height);
  const double row_5_y = legend_y + (5.0 * legend_row_height);

  output_stream << R"(<circle cx=")" << FormatSvgNumber(legend_x + 6.0) << R"(" cy=")" << FormatSvgNumber(row_1_y - 4.0)
                << R"(" r="4" fill="#1f77b4" fill-opacity="0.75" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_1_y) << R"(">sink load</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_2_y - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 12.0) << R"(" y2=")" << FormatSvgNumber(row_2_y - 4.0)
                << R"(" stroke="#9aa0a6" stroke-width="1.4" stroke-dasharray="6,4" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_2_y)
                << R"(">H-tree topology</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_3_y - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 12.0) << R"(" y2=")" << FormatSvgNumber(row_3_y - 4.0)
                << R"(" stroke="#2ca25f" stroke-width="2.0" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_3_y)
                << R"(">sink-reaching net</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_4_y - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 12.0) << R"(" y2=")" << FormatSvgNumber(row_4_y - 4.0)
                << R"(" stroke="#ff8c42" stroke-width="1.6" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_4_y)
                << R"(">internal fanout net</text>)";

  output_stream << R"(<line x1=")" << FormatSvgNumber(legend_x) << R"(" y1=")" << FormatSvgNumber(row_5_y - 4.0) << R"(" x2=")"
                << FormatSvgNumber(legend_x + 12.0) << R"(" y2=")" << FormatSvgNumber(row_5_y - 4.0)
                << R"(" stroke="#6a3d9a" stroke-width="2.4" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_5_y) << R"(">root net</text>)";

  for (std::size_t index = 0; index < buffer_summaries.size(); ++index) {
    const auto& summary = buffer_summaries[index];
    const auto style_itr = buffer_styles.find(summary.cell_master);
    const BufferRenderStyle style = style_itr != buffer_styles.end() ? style_itr->second : BufferRenderStyle{};
    const double row_y = legend_y + (legend_row_height * static_cast<double>(base_row_count + index + 1));
    const double symbol_size = 2.0 * std::min(style.half_size, 6.0);

    output_stream << R"(<rect x=")" << FormatSvgNumber(legend_x + 1.0) << R"(" y=")" << FormatSvgNumber(row_y - 11.0) << R"(" width=")"
                  << FormatSvgNumber(symbol_size) << R"(" height=")" << FormatSvgNumber(symbol_size) << R"(" fill=")" << style.fill_color
                  << R"(" stroke=")" << style.stroke_color << R"(" stroke-width="1.2" />)";

    std::ostringstream label;
    label << summary.cell_master;
    label << " x" << summary.count;
    output_stream << R"(<text x=")" << FormatSvgNumber(legend_x + 18.0) << R"(" y=")" << FormatSvgNumber(row_y) << R"(">)"
                  << EscapeXml(label.str()) << R"(</text>)";
  }
  output_stream << R"(</g>
)";
}

auto WriteTopologyOverlay(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                          const icts::Tree& topology) -> void
{
  for (std::size_t node_id = 0; node_id < topology.get_size(); ++node_id) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr || node->get_parent() == common::visualization::detail::kInvalidNodeId || !HasValidLocation(node->get_position())) {
      continue;
    }

    const auto* parent = topology.get_node(node->get_parent());
    if (parent == nullptr || !HasValidLocation(parent->get_position())) {
      continue;
    }

    const auto source_x = FormatSvgNumber(MapX(transform, parent->get_position().get_x()));
    const auto source_y = FormatSvgNumber(MapY(transform, parent->get_position().get_y()));
    const auto target_x = FormatSvgNumber(MapX(transform, node->get_position().get_x()));
    const auto target_y = FormatSvgNumber(MapY(transform, node->get_position().get_y()));
    const std::string tooltip = "tree edge " + std::to_string(parent->get_id()) + " -> " + std::to_string(node->get_id());
    WriteStraightConnection(output_stream, source_x, source_y, target_x, target_y, "#9aa0a6", 1.4, "6,4", tooltip);
  }
}

auto WriteMaterializedNets(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                           const std::unordered_set<const icts::Pin*>& original_loads, const icts::HTreeBuilder::BuildResult& result)
    -> void
{
  for (const auto* net : result.inserted_nets) {
    if (net == nullptr || net->get_driver() == nullptr) {
      continue;
    }

    const auto driver_location = FindRenderableLocation(net->get_driver());
    if (!HasValidLocation(driver_location)) {
      continue;
    }

    const bool is_root_net = net->get_driver() == result.root_output_pin;
    const bool reaches_sink = std::ranges::any_of(
        net->get_loads(), [&original_loads](const icts::Pin* pin) -> bool { return IsOriginalLoadPin(pin, original_loads); });

    const std::string stroke_color = ResolveNetStrokeColor(is_root_net, reaches_sink);
    const double stroke_width = ResolveNetStrokeWidth(is_root_net, reaches_sink);

    const auto source_x = FormatSvgNumber(MapX(transform, driver_location.get_x()));
    const auto source_y = FormatSvgNumber(MapY(transform, driver_location.get_y()));
    for (const auto* load : net->get_loads()) {
      const auto target_location = FindRenderableLocation(load);
      if (!HasValidLocation(target_location)) {
        continue;
      }

      const auto target_x = FormatSvgNumber(MapX(transform, target_location.get_x()));
      const auto target_y = FormatSvgNumber(MapY(transform, target_location.get_y()));
      const std::string tooltip = "net " + net->get_name() + ": "
                                  + (net->get_driver() != nullptr ? net->get_driver()->get_name() : std::string("<null-driver>")) + " -> "
                                  + (load != nullptr ? load->get_name() : std::string("<null-load>"));
      WriteStraightConnection(output_stream, source_x, source_y, target_x, target_y, stroke_color, stroke_width, "", tooltip);
    }
  }
}

auto WriteTopologyNodes(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                        const icts::Tree& topology) -> void
{
  for (std::size_t node_id = 0; node_id < topology.get_size(); ++node_id) {
    const auto* node = topology.get_node(node_id);
    if (node == nullptr || !HasValidLocation(node->get_position())) {
      continue;
    }

    const bool is_root = node->get_parent() == common::visualization::detail::kInvalidNodeId;
    const auto position_x = FormatSvgNumber(MapX(transform, node->get_position().get_x()));
    const auto position_y = FormatSvgNumber(MapY(transform, node->get_position().get_y()));

    const double radius = is_root ? 6.0 : 4.0;
    const std::string fill_color = is_root ? "#d62728" : "#3d3d3d";
    output_stream << R"(<circle cx=")" << position_x << R"(" cy=")" << position_y << R"(" r=")" << radius << R"(" fill=")" << fill_color
                  << R"(" fill-opacity="0.92" stroke="#ffffff" stroke-width="1">)";
    WriteTooltip(output_stream, "tree node " + std::to_string(node->get_id()));
    output_stream << "</circle>\n";
  }
}

auto WriteBuffers(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                  const std::vector<icts::Inst*>& inserted_insts, const std::unordered_map<std::string, BufferRenderStyle>& buffer_styles)
    -> void
{
  for (const auto* inst : inserted_insts) {
    if (inst == nullptr || !HasValidLocation(inst->get_location())) {
      continue;
    }

    const auto style_itr = buffer_styles.find(inst->get_cell_master());
    const BufferRenderStyle style = style_itr != buffer_styles.end() ? style_itr->second : BufferRenderStyle{};
    const double center_x = MapX(transform, inst->get_location().get_x());
    const double center_y = MapY(transform, inst->get_location().get_y());
    output_stream << R"(<rect x=")" << FormatSvgNumber(center_x - style.half_size) << R"(" y=")"
                  << FormatSvgNumber(center_y - style.half_size) << R"(" width=")" << FormatSvgNumber(2.0 * style.half_size)
                  << R"(" height=")" << FormatSvgNumber(2.0 * style.half_size) << R"(" fill=")" << style.fill_color << R"(" stroke=")"
                  << style.stroke_color << R"(" stroke-width="1.2">)";
    WriteTooltip(output_stream, "buffer " + inst->get_name() + " [" + inst->get_cell_master() + "]");
    output_stream << "</rect>\n";
  }
}

auto WriteLoads(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                const std::vector<icts::Pin*>& loads) -> void
{
  for (const auto* load : loads) {
    if (load == nullptr || !HasValidLocation(load->get_location())) {
      continue;
    }

    const auto location_x = FormatSvgNumber(MapX(transform, load->get_location().get_x()));
    const auto location_y = FormatSvgNumber(MapY(transform, load->get_location().get_y()));
    const std::string load_label
        = load->get_name() + (load->get_inst() != nullptr ? " [" + load->get_inst()->get_name() + "]" : std::string{});

    output_stream << R"(<circle cx=")" << location_x << R"(" cy=")" << location_y
                  << R"(" r="4.5" fill="#1f77b4" fill-opacity="0.75" stroke="#0c4068" stroke-width="0.8">)";
    WriteTooltip(output_stream, load_label);
    output_stream << "</circle>\n";
  }
}

auto WriteRootMarker(std::ofstream& output_stream, const common::visualization::detail::SvgTransform& transform,
                     const icts::HTreeBuilder::BuildResult& result) -> void
{
  const auto root_location = FindRenderableLocation(result.root_output_pin);
  if (!HasValidLocation(root_location)) {
    return;
  }

  const auto center_x = FormatSvgNumber(MapX(transform, root_location.get_x()));
  const auto center_y = FormatSvgNumber(MapY(transform, root_location.get_y()));

  output_stream << R"(<circle cx=")" << center_x << R"(" cy=")" << center_y << R"(" r=")" << FormatSvgNumber(kRootRingRadius)
                << R"(" fill="none" stroke="#6a3d9a" stroke-width="2.0">)";
  WriteTooltip(output_stream, "root output pin");
  output_stream << "</circle>\n";
}

auto WriteMaterializedSvg(const std::filesystem::path& path, const std::vector<icts::Pin*>& loads,
                          const icts::HTreeBuilder::BuildResult& result) -> bool
{
  const auto extra_points = CollectExtraPoints(result);
  const auto bounds = ComputeBounds(loads, extra_points);
  const auto transform = MakeTransform(bounds);
  const auto buffer_summaries = CollectBufferMasterSummaries(result.inserted_insts);
  const auto buffer_styles = BuildBufferRenderStyles(buffer_summaries);

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }

  output_stream << common::visualization::detail::kSvgOpenTagPrefix << FormatSvgNumber(transform.width)
                << common::visualization::detail::kSvgHeightTag << FormatSvgNumber(transform.height)
                << common::visualization::detail::kSvgViewBoxPrefix << FormatSvgNumber(transform.width) << ' '
                << FormatSvgNumber(transform.height) << common::visualization::detail::kSvgOpenTagSuffix;
  output_stream << common::visualization::detail::kSvgBackgroundRect;

  const std::unordered_set<const icts::Pin*> original_loads(loads.begin(), loads.end());
  WriteTopologyOverlay(output_stream, transform, result.topology);
  WriteMaterializedNets(output_stream, transform, original_loads, result);
  WriteTopologyNodes(output_stream, transform, result.topology);
  WriteBuffers(output_stream, transform, result.inserted_insts, buffer_styles);
  WriteLoads(output_stream, transform, loads);
  WriteRootMarker(output_stream, transform, result);
  WriteLegend(output_stream, transform, buffer_summaries, buffer_styles);

  output_stream << common::visualization::detail::kSvgClosingTag;
  return true;
}

auto WriteDelayPowerParetoSvg(const std::filesystem::path& path, const icts::HTreeBuilder::BuildResult& result) -> bool
{
  const auto points = BuildDelayPowerPoints(result);
  if (points.empty()) {
    std::ofstream empty_stream(path);
    if (!empty_stream.is_open()) {
      return false;
    }
    empty_stream << R"(<svg xmlns="http://www.w3.org/2000/svg" width="720" height="520" viewBox="0 0 720 520"></svg>)";
    return true;
  }

  constexpr double width = 720.0;
  constexpr double height = 520.0;
  constexpr double plot_left = 88.0;
  constexpr double plot_right = 660.0;
  constexpr double plot_top = 54.0;
  constexpr double plot_bottom = 434.0;
  const double plot_width = plot_right - plot_left;
  const double plot_height = plot_bottom - plot_top;

  auto map_x = [plot_width](double normalized_delay) -> double { return plot_left + (normalized_delay * plot_width); };
  auto map_y = [plot_height](double normalized_power) -> double { return plot_bottom - (normalized_power * plot_height); };

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }

  output_stream << common::visualization::detail::kSvgOpenTagPrefix << FormatSvgNumber(width)
                << common::visualization::detail::kSvgHeightTag << FormatSvgNumber(height)
                << common::visualization::detail::kSvgViewBoxPrefix << FormatSvgNumber(width) << ' ' << FormatSvgNumber(height)
                << common::visualization::detail::kSvgOpenTagSuffix;
  output_stream << common::visualization::detail::kSvgBackgroundRect;

  output_stream << R"(<g font-family="monospace" fill="#1f2933">)";
  output_stream << R"(<text x="36" y="28" font-size="16" font-weight="700">Normalized Delay-Power Frontier</text>)";
  output_stream
      << R"(<text x="36" y="46" font-size="12">Lower-left is better. Orange points are Pareto-optimal. Purple point is selected.</text>)";
  output_stream << R"(<rect x=")" << FormatSvgNumber(plot_left) << R"(" y=")" << FormatSvgNumber(plot_top) << R"(" width=")"
                << FormatSvgNumber(plot_width) << R"(" height=")" << FormatSvgNumber(plot_height)
                << R"(" fill="#ffffff" fill-opacity="0.85" stroke="#d0d7de" />)";

  for (int tick = 0; tick <= 5; ++tick) {
    const double normalized_value = static_cast<double>(tick) / 5.0;
    const double grid_x = map_x(normalized_value);
    const double grid_y = map_y(normalized_value);

    output_stream << R"(<line x1=")" << FormatSvgNumber(grid_x) << R"(" y1=")" << FormatSvgNumber(plot_top) << R"(" x2=")"
                  << FormatSvgNumber(grid_x) << R"(" y2=")" << FormatSvgNumber(plot_bottom) << R"(" stroke="#edf2f7" stroke-width="1" />)";
    output_stream << R"(<line x1=")" << FormatSvgNumber(plot_left) << R"(" y1=")" << FormatSvgNumber(grid_y) << R"(" x2=")"
                  << FormatSvgNumber(plot_right) << R"(" y2=")" << FormatSvgNumber(grid_y) << R"(" stroke="#edf2f7" stroke-width="1" />)";

    output_stream << R"(<text x=")" << FormatSvgNumber(grid_x - 8.0) << R"(" y="456" font-size="11">)" << FormatSvgNumber(normalized_value)
                  << R"(</text>)";
    output_stream << R"(<text x="52" y=")" << FormatSvgNumber(grid_y + 4.0) << R"(" font-size="11">)" << FormatSvgNumber(normalized_value)
                  << R"(</text>)";
  }

  output_stream << R"(<line x1=")" << FormatSvgNumber(plot_left) << R"(" y1=")" << FormatSvgNumber(plot_bottom) << R"(" x2=")"
                << FormatSvgNumber(plot_right) << R"(" y2=")" << FormatSvgNumber(plot_bottom)
                << R"(" stroke="#4a5568" stroke-width="1.4" />)";
  output_stream << R"(<line x1=")" << FormatSvgNumber(plot_left) << R"(" y1=")" << FormatSvgNumber(plot_bottom) << R"(" x2=")"
                << FormatSvgNumber(plot_left) << R"(" y2=")" << FormatSvgNumber(plot_top) << R"(" stroke="#4a5568" stroke-width="1.4" />)";
  output_stream << R"(<text x=")" << FormatSvgNumber((plot_left + plot_right) * 0.5 - 58.0)
                << R"(" y="488" font-size="13">normalized delay</text>)";
  const double power_label_y = (plot_top + plot_bottom) * 0.5;
  output_stream << R"SVG(<text x="22" y=")SVG" << FormatSvgNumber(power_label_y) << R"SVG(" font-size="13" transform="rotate(-90 22 )SVG"
                << FormatSvgNumber(power_label_y) << R"SVG()">normalized power</text>)SVG";

  std::vector<const DelayPowerPoint*> pareto_points;
  pareto_points.reserve(points.size());
  for (const auto& point : points) {
    if (point.is_pareto) {
      pareto_points.push_back(&point);
    }
  }

  if (!pareto_points.empty()) {
    std::ranges::sort(pareto_points, [](const DelayPowerPoint* lhs, const DelayPowerPoint* rhs) -> bool {
      if (lhs->normalized_delay != rhs->normalized_delay) {
        return lhs->normalized_delay < rhs->normalized_delay;
      }
      return lhs->normalized_power < rhs->normalized_power;
    });

    output_stream << R"(<polyline fill="none" stroke="#e76f51" stroke-width="2.2" points=")";
    for (const auto* point : pareto_points) {
      output_stream << FormatSvgNumber(map_x(point->normalized_delay)) << "," << FormatSvgNumber(map_y(point->normalized_power)) << " ";
    }
    output_stream << R"("/>)";
  }

  for (const auto& point : points) {
    const double center_x = map_x(point.normalized_delay);
    const double center_y = map_y(point.normalized_power);
    std::string fill_color = "#94a3b8";
    std::string stroke_color = "#64748b";
    double radius = 3.8;
    if (point.is_selected) {
      fill_color = "#6a3d9a";
      stroke_color = "#2d1b69";
      radius = 6.5;
    } else if (point.is_pareto) {
      fill_color = "#e76f51";
      stroke_color = "#91361f";
      radius = 5.0;
    }

    output_stream << R"(<circle cx=")" << FormatSvgNumber(center_x) << R"(" cy=")" << FormatSvgNumber(center_y) << R"(" r=")"
                  << FormatSvgNumber(radius) << R"(" fill=")" << fill_color << R"(" stroke=")" << stroke_color
                  << R"(" stroke-width="1.2">)";
    WriteTooltip(output_stream, BuildDelayPowerTooltip(point));
    output_stream << "</circle>\n";
  }

  output_stream << R"(<g font-size="12">)";
  output_stream << R"(<rect x="478" y="60" width="200" height="92" rx="6" fill="#ffffff" fill-opacity="0.9" stroke="#d0d7de" />)";
  output_stream << R"(<circle cx="494" cy="84" r="4.5" fill="#94a3b8" stroke="#64748b" stroke-width="1.2" />)";
  output_stream << R"(<text x="508" y="88">feasible solution</text>)";
  output_stream << R"(<circle cx="494" cy="108" r="5.0" fill="#e76f51" stroke="#91361f" stroke-width="1.2" />)";
  output_stream << R"(<text x="508" y="112">Pareto front</text>)";
  output_stream << R"(<circle cx="494" cy="132" r="6.5" fill="#6a3d9a" stroke="#2d1b69" stroke-width="1.2" />)";
  output_stream << R"(<text x="508" y="136">selected solution</text>)";
  output_stream << R"(</g>)";
  output_stream << R"(</g>)";

  output_stream << common::visualization::detail::kSvgClosingTag;
  return true;
}

auto BuildReport(const std::string& scenario_name, const std::string& input_summary, const HTreeArtifactPaths& paths,
                 const std::vector<icts::Pin*>& loads, const icts::HTreeBuilder::BuildResult& result) -> std::string
{
  const auto delay_power_points = BuildDelayPowerPoints(result);

  std::ostringstream report;
  report.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  report << std::setprecision(3);
  report << "scenario=" << scenario_name << "\n";
  report << "input=" << input_summary << "\n";
  report << "load_count=" << loads.size() << "\n";
  report << "topology_nodes=" << result.topology.get_size() << "\n";
  report << "htree_levels=" << result.levels.size() << "\n";
  report << "inserted_buffers=" << result.inserted_insts.size() << "\n";
  report << "inserted_nets=" << result.inserted_nets.size() << "\n";
  report << "char_grid_unit_um=" << result.char_wire_length_unit_um << ", iterations=" << result.char_wire_length_iterations
         << ", distinct_level_bins=" << result.char_unique_level_bins << ", adapted=" << (result.char_grid_adapted ? "true" : "false")
         << "\n";
  report << "char_limits max_slew_ns=" << result.char_max_slew_ns << ", max_cap_pf=" << result.char_max_cap_pf << "\n";
  report << "frontier_solution_count=" << delay_power_points.size() << ", pareto_solution_count="
         << std::ranges::count_if(delay_power_points, [](const DelayPowerPoint& point) -> bool { return point.is_pareto; }) << "\n";
  for (const auto& summary : CollectBufferMasterSummaries(result.inserted_insts)) {
    report << "buffer_master=" << summary.cell_master << ", count=" << summary.count;
    if (summary.drive_strength >= 0) {
      report << ", drive_strength=" << summary.drive_strength;
    }
    report << "\n";
  }
  report << "root_output_pin=" << (result.root_output_pin != nullptr ? result.root_output_pin->get_name() : "<null>") << "\n";
  for (const auto& point : delay_power_points) {
    if (!point.is_pareto && !point.is_selected) {
      continue;
    }

    report << (point.is_selected ? "selected_solution" : "pareto_solution") << " pattern_id=" << point.entry->get_pattern_id().local_id
           << ", delay=" << point.entry->get_delay() << ", power=" << point.entry->get_power() << ", norm_delay=" << point.normalized_delay
           << ", norm_power=" << point.normalized_power << ", driven_cap_idx=" << point.entry->get_driven_cap_idx()
           << ", output_slew_idx=" << point.entry->get_output_slew_idx() << ", load_cap_idx=" << point.entry->get_load_cap_idx() << "\n";
  }
  for (std::size_t level_index = 0; level_index < result.levels.size(); ++level_index) {
    const auto& level = result.levels.at(level_index);
    report << "level[" << level_index << "] requested_dbu=" << level.requested_length_dbu << ", requested_um=" << level.requested_length_um
           << ", aligned_idx=" << level.aligned_length_idx << ", aligned_um=" << level.aligned_length_um
           << ", segment_pattern_id=" << level.segment_pattern_id.local_id << "\n";
  }
  report << "artifacts=cts.log, topology.svg, materialized_htree.svg, pareto_delay_power.svg\n";
  report << "output_dir=" << paths.output_dir.string() << "\n";
  return report.str();
}

}  // namespace

auto PrepareHTreeArtifactPaths(const std::string& case_name) -> HTreeArtifactPaths
{
  HTreeArtifactPaths paths;
  paths.output_dir
      = common::io::PrepareCleanOutputDir(common::io::ResolveOutputDir() / "flow" / "htree" / common::io::SanitizeOutputName(case_name));
  if (paths.output_dir.empty()) {
    return paths;
  }

  paths.cts_log = paths.output_dir / "cts.log";
  paths.topology_svg = paths.output_dir / "topology.svg";
  paths.materialized_svg = paths.output_dir / "materialized_htree.svg";
  paths.pareto_svg = paths.output_dir / "pareto_delay_power.svg";
  paths.report_log = paths.output_dir / "report.log";
  return paths;
}

auto WriteHTreeArtifacts(const HTreeArtifactPaths& paths, const std::string& scenario_name, const std::string& input_summary,
                         const std::vector<icts::Pin*>& loads, const icts::HTreeBuilder::BuildResult& result) -> bool
{
  if (paths.output_dir.empty()) {
    return false;
  }

  const bool wrote_topology = common::visualization::WriteTopologySvg(paths.topology_svg.string(), result.topology, loads);
  const bool wrote_materialized = WriteMaterializedSvg(paths.materialized_svg, loads, result);
  const bool wrote_pareto = WriteDelayPowerParetoSvg(paths.pareto_svg, result);
  const bool wrote_report = common::io::WriteTextLog(paths.report_log, BuildReport(scenario_name, input_summary, paths, loads, result));

  if (wrote_topology) {
    icts::schema::EmitArtifact("HTree topology svg", paths.topology_svg);
  }
  if (wrote_materialized) {
    icts::schema::EmitArtifact("HTree materialized svg", paths.materialized_svg);
  }
  if (wrote_pareto) {
    icts::schema::EmitArtifact("HTree delay-power pareto svg", paths.pareto_svg);
  }
  if (wrote_report) {
    icts::schema::EmitArtifact("HTree report", paths.report_log);
  }

  return wrote_topology && wrote_materialized && wrote_pareto && wrote_report;
}

}  // namespace icts_test::htree
