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
 * @file ClockSynthesisReporter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements ClockSynthesis-specific schema report sections.
 */

#include "synthesis/ClockSynthesisReporter.hh"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "synthesis/ClockSynthesisNetEditor.hh"

namespace icts::clock_synthesis {
namespace {

auto calcManhattanDistance(const Point<int>& lhs, const Point<int>& rhs) -> int
{
  const int delta_x = std::abs(lhs.get_x() - rhs.get_x());
  const int delta_y = std::abs(lhs.get_y() - rhs.get_y());
  return delta_x + delta_y;
}

auto formatDecimal(double value) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream.precision(2);
  stream << value;
  return stream.str();
}

auto resolveDbuPerUm() -> double
{
  return static_cast<double>(std::max(WRAPPER_INST.queryDbUnit(), 1));
}

auto dbuToUm(double value_dbu) -> double
{
  return value_dbu / resolveDbuPerUm();
}

auto resolveDistanceReportPath() -> std::filesystem::path
{
  auto& schema_writer = SCHEMA_WRITER_INST;
  if (schema_writer.isOpen()) {
    return schema_writer.getActivePath();
  }

  const auto& log_file = CONFIG_INST.get_log_file();
  return log_file.empty() ? std::filesystem::path{} : std::filesystem::path(log_file);
}

}  // namespace

auto EmitClusterLeafDistanceTables(const ClockSynthesis::BuildResult& result) -> std::optional<ClockSynthesis::ClusterLeafDistanceSummary>
{
  if (result.cluster_buffers.empty()) {
    return std::nullopt;
  }

  const auto report_path = resolveDistanceReportPath();
  if (report_path.empty()) {
    return std::nullopt;
  }

  constexpr const char* run_title = "iCTS Report";
  constexpr const char* summary_title = "Cluster Center vs H-Tree Leaf Distance Summary";
  std::vector<double> distances;
  distances.reserve(result.cluster_buffers.size());

  double total_distance = 0.0;
  for (const auto& cluster_buffer : result.cluster_buffers) {
    if (!HasValidLocation(cluster_buffer.location) || cluster_buffer.input_pin == nullptr) {
      continue;
    }

    const auto* leaf_net = cluster_buffer.input_pin->get_net();
    if (leaf_net == nullptr || leaf_net->get_driver() == nullptr) {
      continue;
    }
    const auto leaf_location = FindRenderableLocation(leaf_net->get_driver());
    if (!HasValidLocation(leaf_location)) {
      continue;
    }

    const int distance_dbu = calcManhattanDistance(cluster_buffer.location, leaf_location);
    const double distance_um = dbuToUm(distance_dbu);
    distances.push_back(distance_um);
    total_distance += distance_um;
  }

  auto& schema_writer = SCHEMA_WRITER_INST;
  const bool emit_to_active_writer = schema_writer.isOpen() && schema_writer.getActivePath() == report_path;
  if (distances.empty()) {
    const schema::KeyValueFields summary_fields = {
        {"count", "0"},
        {"status", "no_renderable_htree_leaf_locations"},
    };
    if (emit_to_active_writer) {
      schema_writer.emitSection("### Cluster Distance Summary");
      schema_writer.emitKeyValueTable(summary_title, summary_fields);
    } else {
      schema::SchemaWriter::appendStandaloneKeyValueTable(report_path, run_title, summary_title, summary_fields);
    }
    return std::nullopt;
  }

  std::ranges::sort(distances);
  const std::size_t median_index = distances.size() / 2U;
  const double median_distance
      = (distances.size() % 2U) == 0U
            ? (static_cast<double>(distances.at(median_index - 1U)) + static_cast<double>(distances.at(median_index))) / 2.0
            : static_cast<double>(distances.at(median_index));
  ClockSynthesis::ClusterLeafDistanceSummary summary{
      .count = distances.size(),
      .min_distance_um = distances.front(),
      .max_distance_um = distances.back(),
      .mean_distance_um = total_distance / static_cast<double>(distances.size()),
      .median_distance_um = median_distance,
  };
  const schema::KeyValueFields summary_fields = {
      {"count", std::to_string(summary.count)},
      {"min_distance", formatDecimal(summary.min_distance_um) + " um"},
      {"max_distance", formatDecimal(summary.max_distance_um) + " um"},
      {"mean_distance", formatDecimal(summary.mean_distance_um) + " um"},
      {"median_distance", formatDecimal(summary.median_distance_um) + " um"},
  };

  if (emit_to_active_writer) {
    schema_writer.emitSection("### Cluster Distance Summary");
    schema_writer.emitKeyValueTable(summary_title, summary_fields);
    return summary;
  }

  schema::SchemaWriter::appendStandaloneKeyValueTable(report_path, run_title, summary_title, summary_fields);
  return summary;
}

}  // namespace icts::clock_synthesis
