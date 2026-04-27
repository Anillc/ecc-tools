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
 * @file ClockSynthesis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Orchestrates optional sink clustering and H-tree synthesis for one clock distribution.
 */

#include "synthesis/ClockSynthesis.hh"

#include <glog/logging.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "Pin.hh"
#include "TopologyConfig.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Design.hh"
#include "logger/Schema.hh"
#include "topology/TopologyGen.hh"

namespace icts {
namespace {

auto resolveBufferPinNames(const std::string& cell_master) -> std::optional<std::pair<std::string, std::string>>
{
  if (cell_master.empty()) {
    return std::nullopt;
  }

  auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin.empty() || output_pin.empty()) {
    LOG_WARNING << "ClockSynthesis: skip buffer master \"" << cell_master << "\" because buffer ports are unresolved.";
    return std::nullopt;
  }

  return std::make_pair(std::move(input_pin), std::move(output_pin));
}

auto resolveClusterCenter(const std::vector<Point<int>>& centers, const std::vector<Pin*>& cluster, std::size_t index) -> Point<int>
{
  if (index < centers.size()) {
    return centers.at(index);
  }
  if (cluster.empty()) {
    return Point<int>(0, 0);
  }

  int sum_x = 0;
  int sum_y = 0;
  for (auto* pin : cluster) {
    if (pin == nullptr) {
      continue;
    }
    sum_x += pin->get_location().get_x();
    sum_y += pin->get_location().get_y();
  }

  const int count = static_cast<int>(cluster.size());
  if (count <= 0) {
    return Point<int>(0, 0);
  }
  return Point<int>(sum_x / count, sum_y / count);
}

auto makeObjectName(const std::string& prefix, const std::string& suffix) -> std::string
{
  if (prefix.empty()) {
    return "cts_" + suffix;
  }
  return prefix + "_" + suffix;
}

auto hasValidLocation(const Point<int>& location) -> bool
{
  return location.get_x() >= 0 && location.get_y() >= 0;
}

auto findRenderableLocation(const Pin* pin) -> Point<int>
{
  if (pin == nullptr) {
    return Point<int>(-1, -1);
  }

  const auto pin_location = pin->get_location();
  if (hasValidLocation(pin_location)) {
    return pin_location;
  }
  if (pin->get_inst() != nullptr && hasValidLocation(pin->get_inst()->get_location())) {
    return pin->get_inst()->get_location();
  }
  return Point<int>(-1, -1);
}

auto calcManhattanDistance(const Point<int>& lhs, const Point<int>& rhs) -> int
{
  const int delta_x = std::abs(lhs.get_x() - rhs.get_x());
  const int delta_y = std::abs(lhs.get_y() - rhs.get_y());
  return delta_x + delta_y;
}

auto formatPoint(const Point<int>& point) -> std::string
{
  std::ostringstream stream;
  stream << "(" << point.get_x() << ", " << point.get_y() << ")";
  return stream.str();
}

auto formatDecimal(double value) -> std::string
{
  std::ostringstream stream;
  stream.setf(std::ostringstream::fixed, std::ostringstream::floatfield);
  stream.precision(2);
  stream << value;
  return stream.str();
}

auto resolveBufferDriveCap(const std::string& cell_master) -> double
{
  double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (drive_cap_pf <= 0.0) {
    drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return drive_cap_pf;
}

auto resolveMinLegalClusterBufferCell(std::string& cell_master, std::string& input_pin_name, std::string& output_pin_name) -> bool
{
  bool has_resolved_master = false;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& candidate_cell_master : CONFIG_INST.get_buffer_types()) {
    if (candidate_cell_master.empty()) {
      continue;
    }

    const double drive_cap_pf = resolveBufferDriveCap(candidate_cell_master);
    if (drive_cap_pf <= 0.0) {
      LOG_WARNING << "ClockSynthesis: skip clustered center buffer master \"" << candidate_cell_master
                  << "\" because output drive cap is unresolved.";
      continue;
    }

    const auto buffer_pin_names = resolveBufferPinNames(candidate_cell_master);
    if (!buffer_pin_names.has_value()) {
      continue;
    }

    if (!has_resolved_master || drive_cap_pf < best_drive_cap_pf
        || (drive_cap_pf == best_drive_cap_pf && candidate_cell_master < cell_master)) {
      cell_master = candidate_cell_master;
      input_pin_name = buffer_pin_names->first;
      output_pin_name = buffer_pin_names->second;
      best_drive_cap_pf = drive_cap_pf;
      has_resolved_master = true;
    }
  }

  return has_resolved_master;
}

auto createBufferInstance(ClockSynthesis::BuildResult& result, const std::string& inst_name, const std::string& cell_master,
                          const std::string& input_pin_name, const std::string& output_pin_name, const Point<int>& location,
                          Inst*& inst_ptr, Pin*& input_pin_ptr, Pin*& output_pin_ptr) -> void
{
  auto inst = std::make_unique<Inst>(inst_name, cell_master, InstType::kBuffer, location);
  inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(input_pin_name, PinType::kIn, location, inst_ptr, nullptr, false);
  input_pin_ptr = input_pin.get();
  result.inserted_pins.push_back(std::move(input_pin));

  auto output_pin = std::make_unique<Pin>(output_pin_name, PinType::kOut, location, inst_ptr, nullptr, false);
  output_pin_ptr = output_pin.get();
  result.inserted_pins.push_back(std::move(output_pin));

  inst_ptr->add_pin(input_pin_ptr);
  inst_ptr->insertDriverPin(output_pin_ptr);

  result.inserted_insts.push_back(std::move(inst));
}

auto connectNet(Net& net, Pin* driver, const std::vector<Pin*>& sinks) -> void
{
  net.set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(&net);
  }

  net.set_loads({});
  for (auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    net.add_load(sink);
    sink->set_net(&net);
  }
}

auto createNet(ClockSynthesis::BuildResult& result, const std::string& net_name, Pin* driver, const std::vector<Pin*>& sinks) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();
  connectNet(*net_ptr, driver, sinks);
  result.inserted_nets.push_back(std::move(net));
  return net_ptr;
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

auto emitClusterLeafDistanceTables(const ClockSynthesis::BuildResult& result) -> void
{
  if (result.cluster_buffers.empty()) {
    return;
  }

  const auto report_path = resolveDistanceReportPath();
  if (report_path.empty()) {
    return;
  }

  constexpr const char* run_title = "iCTS Report";
  constexpr const char* summary_title = "Cluster Center vs H-Tree Leaf Distance Summary";
  constexpr const char* detail_title = "Cluster Center vs H-Tree Leaf Distance Details";
  std::vector<int> distances;
  schema::TableRows detail_rows;
  distances.reserve(result.cluster_buffers.size());
  detail_rows.reserve(result.cluster_buffers.size());

  long long total_distance = 0;
  for (const auto& cluster_buffer : result.cluster_buffers) {
    if (!hasValidLocation(cluster_buffer.location) || cluster_buffer.input_pin == nullptr) {
      continue;
    }

    const auto* leaf_net = cluster_buffer.input_pin->get_net();
    if (leaf_net == nullptr || leaf_net->get_driver() == nullptr) {
      continue;
    }
    const auto leaf_location = findRenderableLocation(leaf_net->get_driver());
    if (!hasValidLocation(leaf_location)) {
      continue;
    }

    const int distance = calcManhattanDistance(cluster_buffer.location, leaf_location);
    distances.push_back(distance);
    total_distance += distance;
    detail_rows.push_back({
        std::to_string(cluster_buffer.cluster_index),
        std::to_string(cluster_buffer.sink_count),
        formatPoint(cluster_buffer.location),
        formatPoint(leaf_location),
        std::to_string(distance),
    });
  }

  auto& schema_writer = SCHEMA_WRITER_INST;
  const bool emit_to_active_writer = schema_writer.isOpen() && schema_writer.getActivePath() == report_path;
  if (detail_rows.empty()) {
    const std::vector<std::string> summary_lines = {
        "clustered flow emitted no renderable H-tree leaf locations for distance reporting",
    };
    if (emit_to_active_writer) {
      schema_writer.emitDetailBlock(summary_title, summary_lines);
    } else {
      schema::SchemaWriter::appendStandaloneDetailBlock(report_path, run_title, summary_title, summary_lines);
    }
    return;
  }

  std::ranges::sort(distances);
  const std::size_t median_index = distances.size() / 2U;
  const double median_distance
      = (distances.size() % 2U) == 0U
            ? (static_cast<double>(distances.at(median_index - 1U)) + static_cast<double>(distances.at(median_index))) / 2.0
            : static_cast<double>(distances.at(median_index));
  const schema::KeyValueFields summary_fields = {
      {"count", std::to_string(distances.size())},
      {"min_distance_dbu", std::to_string(distances.front())},
      {"max_distance_dbu", std::to_string(distances.back())},
      {"mean_distance_dbu", formatDecimal(static_cast<double>(total_distance) / static_cast<double>(distances.size()))},
      {"median_distance_dbu", formatDecimal(median_distance)},
  };

  if (emit_to_active_writer) {
    schema_writer.emitKeyValueTable(summary_title, summary_fields);
    schema_writer.emitTable(
        detail_title, {"cluster_index", "sink_count", "cluster_center", "htree_leaf_location", "manhattan_distance_dbu"}, detail_rows);
    return;
  }

  schema::SchemaWriter::appendStandaloneKeyValueTable(report_path, run_title, summary_title, summary_fields);
  schema::SchemaWriter::appendStandaloneTable(
      report_path, run_title, detail_title,
      {"cluster_index", "sink_count", "cluster_center", "htree_leaf_location", "manhattan_distance_dbu"}, detail_rows);
}

auto buildHtreeOptions() -> HTreeBuilder::BuildOptions
{
  HTreeBuilder::BuildOptions htree_options;
  const double max_buf_tran = CONFIG_INST.get_max_buf_tran();
  if (max_buf_tran > 0.0) {
    htree_options.min_top_input_slew_ns = max_buf_tran * 0.5;
  }
  return htree_options;
}

auto buildClusteringConfigFromRuntimeConfig() -> ClusterConfig
{
  const double max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  auto clustering_config = TopologyGen::buildFastClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), max_cap);
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  clustering_config.routing_layer = routing_layers.empty() ? 1 : static_cast<int>(routing_layers.front());
  clustering_config.wire_width = CONFIG_INST.get_wire_width();
  return clustering_config;
}

auto collectValidLoads(const Net& root_net) -> std::vector<Pin*>
{
  std::vector<Pin*> loads;
  loads.reserve(root_net.get_loads().size());
  for (auto* load : root_net.get_loads()) {
    if (load != nullptr) {
      loads.push_back(load);
    }
  }
  return loads;
}

auto isDesignPin(const Pin* pin) -> bool
{
  if (pin == nullptr) {
    return false;
  }

  const auto pins = DESIGN_INST.get_pins();
  return std::ranges::find_if(pins, [pin](const auto* candidate) -> bool { return candidate == pin; }) != pins.end();
}

template <typename T>
auto absorbOwnedObjects(std::vector<std::unique_ptr<T>>& target, std::vector<std::unique_ptr<T>>& source) -> void
{
  target.reserve(target.size() + source.size());
  for (auto& object : source) {
    target.push_back(std::move(object));
  }
  source.clear();
}

auto absorbHtreeInsertedObjects(ClockSynthesis::BuildResult& result) -> void
{
  absorbOwnedObjects(result.inserted_insts, result.htree_result.inserted_insts);
  absorbOwnedObjects(result.inserted_pins, result.htree_result.inserted_pins);
  absorbOwnedObjects(result.inserted_nets, result.htree_result.inserted_nets);
}

auto materializeClusterBuffers(ClockSynthesis::BuildResult& result, const ClusterResult& cluster_result,
                               const std::string& cluster_buffer_cell_master, const std::string& input_pin_name,
                               const std::string& output_pin_name, const std::string& object_name_prefix, std::vector<Pin*>& htree_sinks)
    -> bool
{
  const auto& clusters = cluster_result.clusters;
  result.cluster_buffers.reserve(clusters.size());
  htree_sinks.reserve(clusters.size());
  for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
    const auto& cluster = clusters.at(cluster_index);
    if (cluster.empty()) {
      LOG_WARNING << "ClockSynthesis: skip empty cluster at index " << cluster_index << ".";
      continue;
    }

    const auto center = resolveClusterCenter(cluster_result.centers, cluster, cluster_index);
    Inst* cluster_inst = nullptr;
    Pin* cluster_input_pin = nullptr;
    Pin* cluster_output_pin = nullptr;
    createBufferInstance(result, makeObjectName(object_name_prefix, "cluster_buf_" + std::to_string(cluster_index)),
                         cluster_buffer_cell_master, input_pin_name, output_pin_name, center, cluster_inst, cluster_input_pin,
                         cluster_output_pin);
    auto* sink_net = createNet(result, makeObjectName(object_name_prefix, "cluster_sink_net_" + std::to_string(cluster_index)),
                               cluster_output_pin, cluster);
    result.cluster_buffers.push_back(ClockSynthesis::ClusterBufferMeta{
        .cluster_index = cluster_index,
        .location = center,
        .sink_count = cluster.size(),
        .inst = cluster_inst,
        .input_pin = cluster_input_pin,
        .output_pin = cluster_output_pin,
        .sink_net = sink_net,
    });
    htree_sinks.push_back(cluster_input_pin);
  }

  if (!htree_sinks.empty()) {
    return true;
  }

  LOG_ERROR << "ClockSynthesis: sink clustering generated no valid centroid buffers.";
  result.failure_reason = "no valid centroid buffers after clustering";
  return false;
}

}  // namespace

auto ClockSynthesis::build(Net& root_net) -> BuildResult
{
  return build(root_net, BuildOptions{});
}

auto ClockSynthesis::build(Net& root_net, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  auto* root_driver = root_net.get_driver();
  if (root_driver == nullptr) {
    LOG_ERROR << "ClockSynthesis: root net \"" << root_net.get_name() << "\" driver is null.";
    result.failure_reason = "root net driver is null";
    return result;
  }

  const auto root_loads = collectValidLoads(root_net);
  if (root_loads.empty()) {
    LOG_WARNING << "ClockSynthesis: root net \"" << root_net.get_name() << "\" has no loads.";
    result.failure_reason = "root net load list is empty";
    return result;
  }

  const auto original_root_loads = root_net.get_loads();
  std::vector<std::pair<Pin*, Net*>> pin_nets;
  auto append_pin_net = [&pin_nets](Pin* pin) -> void {
    if (pin == nullptr) {
      return;
    }
    const auto iter = std::ranges::find_if(pin_nets, [pin](const auto& pin_net) -> bool { return pin_net.first == pin; });
    if (iter == pin_nets.end()) {
      pin_nets.emplace_back(pin, pin->get_net());
    }
  };
  append_pin_net(root_driver);
  for (auto* sink : original_root_loads) {
    append_pin_net(sink);
  }

  auto* root_driver_inst = root_driver->get_inst();
  std::string root_driver_cell_master;
  std::vector<std::pair<Pin*, std::string>> root_pin_names;
  if (root_driver_inst != nullptr) {
    root_driver_cell_master = root_driver_inst->get_cell_master();
    for (auto* pin : root_driver_inst->get_pins()) {
      if (pin == nullptr) {
        continue;
      }
      root_pin_names.emplace_back(pin, pin->get_name());
      append_pin_net(pin);
    }
  } else {
    root_pin_names.emplace_back(root_driver, root_driver->get_name());
  }

  auto restore_algorithm_side_effects = [&]() -> void {
    if (root_driver_inst != nullptr) {
      root_driver_inst->set_cell_master(root_driver_cell_master);
    }
    for (const auto& [pin, name] : root_pin_names) {
      if (pin == nullptr || pin->get_name() == name) {
        continue;
      }
      if (isDesignPin(pin)) {
        (void) DESIGN_INST.renamePin(pin, name);
      } else {
        pin->set_name(name);
      }
    }

    connectNet(root_net, root_driver, original_root_loads);
    for (const auto& [pin, net] : pin_nets) {
      if (pin != nullptr) {
        pin->set_net(net);
      }
    }
  };

  connectNet(root_net, root_driver, root_loads);

  const bool enable_sink_clustering = options.enable_sink_clustering.value_or(CONFIG_INST.is_enable_sink_clustering());
  result.sink_clustering_enabled = enable_sink_clustering;

  std::vector<Pin*> htree_sinks;
  htree_sinks.reserve(root_loads.size());

  if (enable_sink_clustering) {
    auto clustering_config = buildClusteringConfigFromRuntimeConfig();
    auto cluster_result = TopologyGen::defaultFastClustering(root_loads, clustering_config);
    result.cluster_result = std::move(cluster_result);
    std::string cluster_buffer_cell_master;
    std::string cluster_buffer_input_pin;
    std::string cluster_buffer_output_pin;
    if (!resolveMinLegalClusterBufferCell(cluster_buffer_cell_master, cluster_buffer_input_pin, cluster_buffer_output_pin)) {
      LOG_ERROR << "ClockSynthesis: failed to resolve a legal clustered center buffer master from configured buffer_types.";
      result.failure_reason = "failed to resolve clustered center buffer master";
      restore_algorithm_side_effects();
      return result;
    }
    if (!materializeClusterBuffers(result, *result.cluster_result, cluster_buffer_cell_master, cluster_buffer_input_pin,
                                   cluster_buffer_output_pin, options.object_name_prefix, htree_sinks)) {
      restore_algorithm_side_effects();
      return result;
    }
    connectNet(root_net, root_driver, htree_sinks);
  } else {
    htree_sinks = root_loads;
  }

  auto htree_options = buildHtreeOptions();
  result.htree_result = HTreeBuilder::build(root_net, htree_options);
  if (!result.htree_result.success) {
    const std::string htree_failure
        = result.htree_result.failure_reason.empty() ? "unknown H-tree failure" : result.htree_result.failure_reason;
    LOG_ERROR << "ClockSynthesis: H-tree build failed: " << htree_failure;
    result.failure_reason = "H-tree build failed: " + htree_failure;
    restore_algorithm_side_effects();
    return result;
  }
  absorbHtreeInsertedObjects(result);

  if (result.htree_result.root_net != &root_net) {
    LOG_ERROR << "ClockSynthesis: H-tree result root net does not match input root net \"" << root_net.get_name() << "\".";
    result.failure_reason = "H-tree result root net mismatch";
    restore_algorithm_side_effects();
    return result;
  }

  if (enable_sink_clustering) {
    emitClusterLeafDistanceTables(result);
  }
  result.success = true;
  return result;
}

}  // namespace icts
