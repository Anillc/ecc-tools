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
 * @file SinkClusteringOperator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "SinkClusteringOperator.hh"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>

#include "BalanceClustering.hh"
#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "log/Log.hh"

namespace icts {

bool SinkClusteringOperator::run(SolverPipelineState& state) const
{
  state.min_buffering_length = _runtime.minBufferingLength();
  init(state);
  if (state.sink_pins.empty()) {
    LOG_WARNING << "Net [" << state.net_name << "] has no sink pins after initialization.";
    return false;
  }
  buildLeafBuffers(state);
  return true;
}

void SinkClusteringOperator::init(SolverPipelineState& state) const
{
  auto* driver_inst = state.cts_driver->get_instance();
  const auto driver_name = driver_inst == nullptr
                               ? (state.cts_driver->is_io() ? state.cts_driver->get_pin_name() : state.cts_driver->get_full_name())
                               : driver_inst->get_name();
  auto* inst = new Inst(driver_name, state.cts_driver->get_location(), InstType::kBuffer);
  const auto driver_cell_master = driver_inst == nullptr ? std::string() : driver_inst->get_cell_master();
  if (!driver_cell_master.empty() && _runtime.cellLibExist(driver_cell_master)) {
    inst->set_cell_master(driver_cell_master);
  } else if (!driver_cell_master.empty()) {
    LOG_WARNING << "Net [" << state.net_name << "] source driver cell master is not a valid CTS liberty cell: " << driver_cell_master
                << ", fallback to source-pin timing without source cell liberty.";
  }
  state.driver = inst->get_driver_pin();
  state.driver->set_name(state.cts_driver->is_io() ? state.cts_driver->get_pin_name() : state.cts_driver->get_full_name());
  state.driver->set_location(state.cts_driver->get_location());

  std::ranges::for_each(state.cts_pins, [&](CtsPin* cts_pin) {
    auto* cts_inst = cts_pin->get_instance();
    auto type = cts_inst != nullptr && cts_inst->get_type() != CtsInstanceType::kSink ? InstType::kBuffer : InstType::kSink;
    auto inst_name = cts_inst == nullptr ? (cts_pin->is_io() ? cts_pin->get_pin_name() : cts_pin->get_full_name()) : cts_inst->get_name();
    auto inst_loc = cts_inst == nullptr ? cts_pin->get_location() : cts_inst->get_location();
    auto* load_inst = new Inst(inst_name, inst_loc, type);
    auto* load_pin = load_inst->get_load_pin();
    load_pin->set_name(cts_pin->is_io() ? cts_pin->get_pin_name() : cts_pin->get_full_name());
    LOG_FATAL_IF(cts_pin->get_location().x() < 0 || cts_pin->get_location().y() < 0)
        << "Load pin location is invalid: " << load_pin->get_name() << " loc: " << cts_pin->get_location();
    load_pin->set_location(cts_pin->get_location());

    if (load_inst->isBuffer() && cts_inst != nullptr) {
      auto cell_master = cts_inst->get_cell_master();
      auto cell_exist = _runtime.cellLibExist(cell_master);
      load_inst->set_cell_master(cell_exist ? cell_master : TimingPropagator::getMinSizeCell());
    }

    TimingPropagator::updatePinCap(load_pin);
    state.sink_pins.push_back(load_pin);
  });

  TreeBuilder::localPlace(state.sink_pins);
}

void SinkClusteringOperator::buildLeafBuffers(SolverPipelineState& state) const
{
  const size_t max_fanout = std::max<size_t>(2, TimingPropagator::getMaxFanout());
  std::vector<std::vector<Pin*>> clusters;

  if (state.sink_pins.size() == 1) {
    clusters = {{state.sink_pins.front()}};
  } else {
    clusters = BalanceClustering::iterClustering(state.sink_pins, max_fanout, 20, 5, 0.9, false);
  }

  if (clusters.empty()) {
    clusters = {state.sink_pins};
  }

  size_t min_cluster_size = std::numeric_limits<size_t>::max();
  size_t max_cluster_size = 0;
  size_t total_cluster_size = 0;
  std::ranges::for_each(clusters, [&](const std::vector<Pin*>& cluster) {
    min_cluster_size = std::min(min_cluster_size, cluster.size());
    max_cluster_size = std::max(max_cluster_size, cluster.size());
    total_cluster_size += cluster.size();
  });
  const double avg_cluster_size = clusters.empty() ? 0.0 : 1.0 * total_cluster_size / clusters.size();

  std::ostringstream sink_summary;
  sink_summary << std::fixed << std::setprecision(3);
  sink_summary << "Net [" << state.net_name << "] sink clustering summary => source: " << state.driver->get_name()
               << ", sinks: " << state.sink_pins.size() << ", leaf clusters: " << clusters.size()
               << ", cluster size(min/max/avg): " << min_cluster_size << "/" << max_cluster_size << "/" << avg_cluster_size;
  LOG_INFO << sink_summary.str();
  _runtime.saveToLog(sink_summary.str());

  for (size_t i = 0; i < clusters.size(); ++i) {
    auto& cluster = clusters[i];
    auto centroid = BalanceClustering::calcCentroid(cluster);
    auto leaf_name = ComposeSolverName(state.net_name, "_leaf_", i, "_", _runtime.genId());
    auto* leaf_buf = TreeBuilder::genBufInst(leaf_name, centroid);
    leaf_buf->set_cell_master(TimingPropagator::getMinSizeCell());
    auto* leaf_driver = leaf_buf->get_driver_pin();

    TreeBuilder::localPlace(leaf_driver, cluster);
    TreeBuilder::shallowLightTree(leaf_name, leaf_driver, cluster);

    auto* leaf_net = TimingPropagator::genNet(leaf_name, leaf_driver, cluster);
    TimingPropagator::update(leaf_net);

    state.net_records.push_back({leaf_net, -1});
    state.nets.push_back(leaf_net);
    state.leaf_load_pins.push_back(leaf_buf->get_load_pin());
  }
}

}  // namespace icts
