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
 * @file SinkLoadClustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements clustered or direct HTree sink-load preparation for Topology.
 */

#include "synthesis/topology/sink/SinkLoadClustering.hh"

#include <glog/logging.h>

#include <compare>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "Log.hh"
#include "Pin.hh"
#include "Point.hh"
#include "TopologyConfig.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "topology/TopologyGen.hh"

namespace icts::topology {
namespace {

auto resolveBufferPinNames(const std::string& cell_master) -> std::optional<std::pair<std::string, std::string>>
{
  if (cell_master.empty()) {
    return std::nullopt;
  }

  auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin.empty() || output_pin.empty()) {
    LOG_WARNING << "Topology: skip buffer master \"" << cell_master << "\" because buffer ports are unresolved.";
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
      LOG_WARNING << "Topology: skip clustered center buffer master \"" << candidate_cell_master
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

auto buildClusteringConfigFromRuntimeConfig() -> ClusterConfig
{
  const double max_cap = CONFIG_INST.has_max_cap() ? CONFIG_INST.get_max_cap() : std::numeric_limits<double>::infinity();
  auto clustering_config = TopologyGen::buildFastClusteringElectricalConfig(CONFIG_INST.get_max_fanout(), max_cap);
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  clustering_config.routing_layer = routing_layers.empty() ? 1 : static_cast<int>(routing_layers.front());
  clustering_config.wire_width = CONFIG_INST.get_wire_width();
  return clustering_config;
}

auto buildClusterBufferObjects(Topology::BuildResult& result, const ClusterResult& cluster_result,
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
      LOG_WARNING << "Topology: skip empty cluster at index " << cluster_index << ".";
      continue;
    }

    const auto center = resolveClusterCenter(cluster_result.centers, cluster, cluster_index);
    const auto cluster_inst_name = MakeObjectName(object_name_prefix, "cluster_buf_" + std::to_string(cluster_index));
    const auto buffer
        = CreateBufferInstance(result, cluster_inst_name, cluster_buffer_cell_master, input_pin_name, output_pin_name, center);
    const auto sink_net_name = MakeObjectName(object_name_prefix, "cluster_sink_net_" + std::to_string(cluster_index));
    auto* sink_net = CreateNet(result, sink_net_name, buffer.output_pin, cluster);
    result.cluster_buffers.push_back(Topology::ClusterBufferMeta{
        .cluster_index = cluster_index,
        .location = center,
        .sink_count = cluster.size(),
        .inst = buffer.inst,
        .input_pin = buffer.input_pin,
        .output_pin = buffer.output_pin,
        .sink_net = sink_net,
    });
    htree_sinks.push_back(buffer.input_pin);
  }

  if (!htree_sinks.empty()) {
    return true;
  }

  LOG_ERROR << "Topology: sink clustering generated no valid centroid buffers.";
  result.failure_reason = "no valid centroid buffers after clustering";
  return false;
}

}  // namespace

auto PrepareSinkTreeLoads(Topology::BuildResult& result, const std::vector<Pin*>& root_loads, const Topology::BuildOptions& options)
    -> SinkTreeLoadPreparation
{
  const bool enable_sink_clustering = options.enable_sink_clustering.value_or(CONFIG_INST.is_enable_sink_clustering());
  result.sink_clustering_enabled = enable_sink_clustering;

  SinkTreeLoadPreparation preparation;
  if (!enable_sink_clustering) {
    preparation.success = true;
    preparation.htree_sinks = root_loads;
    return preparation;
  }

  auto clustering_config = buildClusteringConfigFromRuntimeConfig();
  auto cluster_result = TopologyGen::defaultFastClustering(root_loads, clustering_config);
  result.cluster_result = std::move(cluster_result);

  std::string cluster_buffer_cell_master;
  std::string cluster_buffer_input_pin;
  std::string cluster_buffer_output_pin;
  if (!resolveMinLegalClusterBufferCell(cluster_buffer_cell_master, cluster_buffer_input_pin, cluster_buffer_output_pin)) {
    LOG_ERROR << "Topology: failed to resolve a legal clustered center buffer master from configured buffer_types.";
    result.failure_reason = "failed to resolve clustered center buffer master";
    return preparation;
  }
  preparation.success = buildClusterBufferObjects(result, *result.cluster_result, cluster_buffer_cell_master, cluster_buffer_input_pin,
                                                  cluster_buffer_output_pin, options.object_name_prefix, preparation.htree_sinks);
  return preparation;
}

}  // namespace icts::topology
