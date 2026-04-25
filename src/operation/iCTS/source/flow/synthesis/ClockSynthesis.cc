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

#include "Clock.hh"
#include "Clustering.hh"
#include "Inst.hh"
#include "Log.hh"
#include "Net.hh"
#include "Pin.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "logger/Schema.hh"
#include "topology/TopologyGen.hh"

namespace icts {
namespace {

struct BufferPorts
{
  std::string input_pin;
  std::string output_pin;
};

struct BufferInstancePins
{
  Inst* inst = nullptr;
  Pin* input_pin = nullptr;
  Pin* output_pin = nullptr;
};

struct ResolvedClusterBufferMaster
{
  std::string cell_master;
  BufferPorts buffer_ports;
};

struct ClusterLeafDistanceSample
{
  std::size_t cluster_index = 0;
  std::size_t sink_count = 0;
  Point<int> cluster_center = Point<int>(-1, -1);
  Point<int> leaf_location = Point<int>(-1, -1);
  int manhattan_distance_dbu = 0;
};

struct ClusterLeafDistanceSummary
{
  std::size_t count = 0;
  int min_distance_dbu = 0;
  int max_distance_dbu = 0;
  double mean_distance_dbu = 0.0;
  double median_distance_dbu = 0.0;
};

auto resolveBufferPorts(const std::string& cell_master) -> std::optional<BufferPorts>
{
  if (cell_master.empty()) {
    return std::nullopt;
  }

  auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin.empty() || output_pin.empty()) {
    LOG_WARNING << "ClockSynthesis: skip buffer master \"" << cell_master << "\" because buffer ports are unresolved.";
    return std::nullopt;
  }

  return BufferPorts{
      .input_pin = std::move(input_pin),
      .output_pin = std::move(output_pin),
  };
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

auto makePinName(const std::string& inst_name, const std::string& port_name) -> std::string
{
  return inst_name + "/" + port_name;
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

auto resolveMinLegalClusterBufferMaster() -> std::optional<ResolvedClusterBufferMaster>
{
  std::optional<ResolvedClusterBufferMaster> resolved_master = std::nullopt;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (cell_master.empty()) {
      continue;
    }

    const double drive_cap_pf = resolveBufferDriveCap(cell_master);
    if (drive_cap_pf <= 0.0) {
      LOG_WARNING << "ClockSynthesis: skip clustered center buffer master \"" << cell_master
                  << "\" because output drive cap is unresolved.";
      continue;
    }

    const auto buffer_ports = resolveBufferPorts(cell_master);
    if (!buffer_ports.has_value()) {
      continue;
    }

    if (!resolved_master.has_value() || drive_cap_pf < best_drive_cap_pf
        || (drive_cap_pf == best_drive_cap_pf && cell_master < resolved_master->cell_master)) {
      resolved_master = ResolvedClusterBufferMaster{
          .cell_master = cell_master,
          .buffer_ports = *buffer_ports,
      };
      best_drive_cap_pf = drive_cap_pf;
    }
  }

  return resolved_master;
}

auto createBufferInstance(ClockSynthesis::BuildResult& result, const std::string& inst_name, const std::string& cell_master,
                          const BufferPorts& buffer_ports, const Point<int>& location) -> BufferInstancePins
{
  auto inst = std::make_unique<Inst>(inst_name, cell_master, InstType::kBuffer, location);
  auto* inst_ptr = inst.get();

  auto input_pin = std::make_unique<Pin>(makePinName(inst_name, buffer_ports.input_pin), PinType::kIn, location, inst_ptr, nullptr, false);
  auto output_pin
      = std::make_unique<Pin>(makePinName(inst_name, buffer_ports.output_pin), PinType::kOut, location, inst_ptr, nullptr, false);

  auto* input_pin_ptr = input_pin.get();
  auto* output_pin_ptr = output_pin.get();
  inst_ptr->insertDriverPin(output_pin_ptr);
  inst_ptr->add_pin(input_pin_ptr);

  result.inserted_insts.push_back(inst_ptr);
  result.inserted_pins.push_back(output_pin_ptr);
  result.inserted_pins.push_back(input_pin_ptr);

  result.inst_storage.push_back(std::move(inst));
  result.pin_storage.push_back(std::move(output_pin));
  result.pin_storage.push_back(std::move(input_pin));

  return BufferInstancePins{
      .inst = inst_ptr,
      .input_pin = input_pin_ptr,
      .output_pin = output_pin_ptr,
  };
}

auto createNet(ClockSynthesis::BuildResult& result, const std::string& net_name, Pin* driver, const std::vector<Pin*>& sinks) -> Net*
{
  auto net = std::make_unique<Net>(net_name);
  auto* net_ptr = net.get();
  net_ptr->set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(net_ptr);
  }

  for (auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    net_ptr->add_load(sink);
    sink->set_net(net_ptr);
  }

  result.inserted_nets.push_back(net_ptr);
  result.net_storage.push_back(std::move(net));
  return net_ptr;
}

auto collectClusterLeafDistanceSamples(const ClockSynthesis::BuildResult& result) -> std::vector<ClusterLeafDistanceSample>
{
  std::vector<ClusterLeafDistanceSample> samples;
  samples.reserve(result.cluster_buffers.size());
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

    samples.push_back(ClusterLeafDistanceSample{
        .cluster_index = cluster_buffer.cluster_index,
        .sink_count = cluster_buffer.sink_count,
        .cluster_center = cluster_buffer.location,
        .leaf_location = leaf_location,
        .manhattan_distance_dbu = calcManhattanDistance(cluster_buffer.location, leaf_location),
    });
  }
  return samples;
}

auto buildClusterLeafDistanceSummary(const std::vector<ClusterLeafDistanceSample>& samples) -> ClusterLeafDistanceSummary
{
  ClusterLeafDistanceSummary summary;
  summary.count = samples.size();
  if (samples.empty()) {
    return summary;
  }

  std::vector<int> distances;
  distances.reserve(samples.size());

  long long total_distance = 0;
  for (const auto& sample : samples) {
    distances.push_back(sample.manhattan_distance_dbu);
    total_distance += sample.manhattan_distance_dbu;
  }

  std::ranges::sort(distances);
  summary.min_distance_dbu = distances.front();
  summary.max_distance_dbu = distances.back();
  summary.mean_distance_dbu = static_cast<double>(total_distance) / static_cast<double>(distances.size());

  const std::size_t median_index = distances.size() / 2U;
  if ((distances.size() % 2U) == 0U) {
    summary.median_distance_dbu = (static_cast<double>(distances[median_index - 1U]) + static_cast<double>(distances[median_index])) / 2.0;
  } else {
    summary.median_distance_dbu = static_cast<double>(distances[median_index]);
  }

  return summary;
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
  const auto samples = collectClusterLeafDistanceSamples(result);

  auto& schema_writer = SCHEMA_WRITER_INST;
  const bool emit_to_active_writer = schema_writer.isOpen() && schema_writer.getActivePath() == report_path;
  if (samples.empty()) {
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

  const auto summary = buildClusterLeafDistanceSummary(samples);
  const schema::KeyValueFields summary_fields = {
      {"count", std::to_string(summary.count)},
      {"min_distance_dbu", std::to_string(summary.min_distance_dbu)},
      {"max_distance_dbu", std::to_string(summary.max_distance_dbu)},
      {"mean_distance_dbu", formatDecimal(summary.mean_distance_dbu)},
      {"median_distance_dbu", formatDecimal(summary.median_distance_dbu)},
  };

  schema::TableRows detail_rows;
  detail_rows.reserve(samples.size());
  for (const auto& sample : samples) {
    detail_rows.push_back({
        std::to_string(sample.cluster_index),
        std::to_string(sample.sink_count),
        formatPoint(sample.cluster_center),
        formatPoint(sample.leaf_location),
        std::to_string(sample.manhattan_distance_dbu),
    });
  }

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
  htree_options.htree_topology_tolerance = CONFIG_INST.get_htree_topology_tolerance();
  const double max_buf_tran = CONFIG_INST.get_max_buf_tran();
  if (max_buf_tran > 0.0) {
    htree_options.min_top_input_slew_ns = max_buf_tran * 0.5;
  }
  return htree_options;
}

auto appendHtreeInsertedObjects(ClockSynthesis::BuildResult& result) -> void
{
  result.inserted_insts.insert(result.inserted_insts.end(), result.htree_result.inserted_insts.begin(),
                               result.htree_result.inserted_insts.end());
  result.inserted_pins.insert(result.inserted_pins.end(), result.htree_result.inserted_pins.begin(),
                              result.htree_result.inserted_pins.end());
  result.inserted_nets.insert(result.inserted_nets.end(), result.htree_result.inserted_nets.begin(),
                              result.htree_result.inserted_nets.end());
}

auto absorbHtreeOwnedObjects(ClockSynthesis::BuildResult& result) -> void
{
  result.inst_storage.reserve(result.inst_storage.size() + result.htree_result.inst_storage.size());
  for (auto& inst : result.htree_result.inst_storage) {
    result.inst_storage.push_back(std::move(inst));
  }
  result.htree_result.inst_storage.clear();

  result.pin_storage.reserve(result.pin_storage.size() + result.htree_result.pin_storage.size());
  for (auto& pin : result.htree_result.pin_storage) {
    result.pin_storage.push_back(std::move(pin));
  }
  result.htree_result.pin_storage.clear();

  result.net_storage.reserve(result.net_storage.size() + result.htree_result.net_storage.size());
  for (auto& net : result.htree_result.net_storage) {
    result.net_storage.push_back(std::move(net));
  }
  result.htree_result.net_storage.clear();
}

auto materializeClusterBuffers(ClockSynthesis::BuildResult& result, const ClusterResult& cluster_result,
                               const ResolvedClusterBufferMaster& cluster_buffer_master, std::vector<Pin*>& htree_sinks) -> bool
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
    auto buffer_instance = createBufferInstance(result, "cts_cluster_buf_" + std::to_string(cluster_index),
                                                cluster_buffer_master.cell_master, cluster_buffer_master.buffer_ports, center);
    auto* sink_net = createNet(result, "cts_cluster_sink_net_" + std::to_string(cluster_index), buffer_instance.output_pin, cluster);
    result.cluster_buffers.push_back(ClockSynthesis::ClusterBufferMeta{
        .cluster_index = cluster_index,
        .location = center,
        .sink_count = cluster.size(),
        .inst = buffer_instance.inst,
        .input_pin = buffer_instance.input_pin,
        .output_pin = buffer_instance.output_pin,
        .sink_net = sink_net,
    });
    htree_sinks.push_back(buffer_instance.input_pin);
  }

  if (!htree_sinks.empty()) {
    return true;
  }

  LOG_ERROR << "ClockSynthesis: sink clustering generated no valid centroid buffers.";
  result.failure_reason = "no valid centroid buffers after clustering";
  return false;
}

}  // namespace

auto ClockSynthesis::build(Clock& clock) -> BuildResult
{
  return build(clock, BuildOptions{});
}

auto ClockSynthesis::build(Clock& clock, const BuildOptions& options) -> BuildResult
{
  clock.clearInsertedCtsObjects();
  BuildResult result = build(clock.get_clock_source(), clock.get_loads(), options);
  if (!result.success) {
    return result;
  }

  clock.adoptInsertedCtsOwnership(std::move(result.inst_storage), std::move(result.pin_storage), std::move(result.net_storage));
  for (auto* inst : result.inserted_insts) {
    if (inst != nullptr) {
      clock.add_inserted_inst(inst);
    }
  }
  for (auto* net : result.inserted_nets) {
    if (net != nullptr) {
      clock.add_inserted_net(net);
    }
  }
  return result;
}

auto ClockSynthesis::build(Pin* clock_source, const std::vector<Pin*>& sinks) -> BuildResult
{
  return build(clock_source, sinks, BuildOptions{});
}

auto ClockSynthesis::build(Pin* clock_source, const std::vector<Pin*>& sinks, const BuildOptions& options) -> BuildResult
{
  BuildResult result;
  if (clock_source == nullptr) {
    LOG_ERROR << "ClockSynthesis: clock source is null.";
    result.failure_reason = "clock source is null";
    return result;
  }
  if (sinks.empty()) {
    LOG_WARNING << "ClockSynthesis: sink list is empty.";
    result.failure_reason = "sink list is empty";
    return result;
  }

  const bool enable_sink_clustering = options.enable_sink_clustering.value_or(CONFIG_INST.is_enable_sink_clustering());
  result.sink_clustering_enabled = enable_sink_clustering;

  std::vector<Pin*> htree_sinks;
  htree_sinks.reserve(sinks.size());

  if (enable_sink_clustering) {
    const double max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
    auto clustering_config = TopologyGen::buildFastClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), max_cap);
    auto cluster_result = TopologyGen::defaultFastClustering(sinks, clustering_config);
    result.cluster_result = std::move(cluster_result);
    const auto cluster_buffer_master = resolveMinLegalClusterBufferMaster();
    if (!cluster_buffer_master.has_value()) {
      LOG_ERROR << "ClockSynthesis: failed to resolve a legal clustered center buffer master from configured buffer_types.";
      result.failure_reason = "failed to resolve clustered center buffer master";
      return result;
    }
    if (!materializeClusterBuffers(result, *result.cluster_result, *cluster_buffer_master, htree_sinks)) {
      return result;
    }
  } else {
    htree_sinks = sinks;
  }

  auto htree_options = buildHtreeOptions();
  result.htree_result = HTreeBuilder::build(htree_sinks, htree_options);
  appendHtreeInsertedObjects(result);
  if (!result.htree_result.success) {
    const std::string htree_failure
        = result.htree_result.failure_reason.empty() ? "unknown H-tree failure" : result.htree_result.failure_reason;
    LOG_ERROR << "ClockSynthesis: H-tree build failed: " << htree_failure;
    result.failure_reason = "H-tree build failed: " + htree_failure;
    return result;
  }
  absorbHtreeOwnedObjects(result);

  if (result.htree_result.root_input_pin == nullptr) {
    LOG_ERROR << "ClockSynthesis: H-tree root input pin is null after successful build.";
    result.failure_reason = "H-tree root input pin is null";
    return result;
  }

  result.source_to_root_net
      = createNet(result, "cts_clock_source_to_htree_root", clock_source, std::vector<Pin*>{result.htree_result.root_input_pin});
  if (enable_sink_clustering) {
    emitClusterLeafDistanceTables(result);
  }
  result.success = true;
  return result;
}

}  // namespace icts
