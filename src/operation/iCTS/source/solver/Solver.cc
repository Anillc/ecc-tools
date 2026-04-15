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
 * @file Solver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */

#include "Solver.hh"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <ranges>
#include <sstream>

#include "BalanceClustering.hh"
#include "TimingPropagator.hh"
#include "TreeBuilder.hh"
#include "log/Log.hh"

namespace icts {

namespace {
constexpr double kMetricEpsilon = 1e-12;
constexpr double kConstraintEpsilon = 1e-9;

std::string formatBufferDepthSummary(const std::vector<std::vector<Inst*>>& buffers_by_depth)
{
  std::ostringstream oss;
  bool first = true;
  for (size_t depth = 0; depth < buffers_by_depth.size(); ++depth) {
    if (buffers_by_depth[depth].empty()) {
      continue;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << "d" << depth << "=" << buffers_by_depth[depth].size();
  }
  return first ? "none" : oss.str();
}

std::string formatSizingAssignmentSummary(const std::vector<size_t>& level_lib_indices, const std::vector<std::vector<Inst*>>& buffers_by_depth,
                                          const std::vector<CtsCellLib*>& libs)
{
  std::ostringstream oss;
  bool first = true;
  for (size_t depth = 0; depth < level_lib_indices.size(); ++depth) {
    if (depth >= buffers_by_depth.size() || buffers_by_depth[depth].empty()) {
      continue;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << "d" << depth << "=" << libs[level_lib_indices[depth]]->get_cell_master() << "x" << buffers_by_depth[depth].size();
  }
  return first ? "none" : oss.str();
}
}

void Solver::run()
{
  init();
  if (_sink_pins.empty()) {
    LOG_WARNING << "Net [" << _net_name << "] has no sink pins after initialization.";
    return;
  }

  buildLeafBuffers();
  buildTopology();
  breakLongWire();
  logTopologySummary();
  optimizeLevelSizing();
}

void Solver::init()
{
  auto* driver_inst = _cts_driver->get_instance();
  const auto driver_name = driver_inst == nullptr ? (_cts_driver->is_io() ? _cts_driver->get_pin_name() : _cts_driver->get_full_name())
                                                  : driver_inst->get_name();
  auto* inst = new Inst(driver_name, _cts_driver->get_location(), InstType::kBuffer);
  const auto driver_cell_master = driver_inst == nullptr ? std::string() : driver_inst->get_cell_master();
  if (!driver_cell_master.empty() && CTSAPIInst.cellLibExist(driver_cell_master)) {
    inst->set_cell_master(driver_cell_master);
  } else if (!driver_cell_master.empty()) {
    LOG_WARNING << "Net [" << _net_name << "] source driver cell master is not a valid CTS liberty cell: " << driver_cell_master
                << ", fallback to source-pin timing without source cell liberty.";
  }
  _driver = inst->get_driver_pin();
  _driver->set_name(_cts_driver->is_io() ? _cts_driver->get_pin_name() : _cts_driver->get_full_name());
  _driver->set_location(_cts_driver->get_location());

  std::ranges::for_each(_cts_pins, [&](CtsPin* cts_pin) {
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
      auto cell_exist = CTSAPIInst.cellLibExist(cell_master);
      load_inst->set_cell_master(cell_exist ? cell_master : TimingPropagator::getMinSizeCell());
    }

    TimingPropagator::updatePinCap(load_pin);
    _sink_pins.push_back(load_pin);
  });

  TreeBuilder::localPlace(_sink_pins);
}

void Solver::buildLeafBuffers()
{
  const size_t max_fanout = std::max<size_t>(2, TimingPropagator::getMaxFanout());
  std::vector<std::vector<Pin*>> clusters;

  if (_sink_pins.size() == 1) {
    clusters = {{_sink_pins.front()}};
  } else {
    clusters = BalanceClustering::iterClustering(_sink_pins, max_fanout, 20, 5, 0.9, false);
  }

  if (clusters.empty()) {
    clusters = {_sink_pins};
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
  sink_summary << "Net [" << _net_name << "] sink clustering summary => source: " << _driver->get_name()
               << ", sinks: " << _sink_pins.size() << ", leaf clusters: " << clusters.size() << ", cluster size(min/max/avg): "
               << min_cluster_size << "/" << max_cluster_size << "/" << avg_cluster_size;
  LOG_INFO << sink_summary.str();
  CTSAPIInst.saveToLog(sink_summary.str());

  for (size_t i = 0; i < clusters.size(); ++i) {
    auto& cluster = clusters[i];
    auto centroid = BalanceClustering::calcCentroid(cluster);
    auto leaf_name = CTSAPIInst.toString(_net_name, "_leaf_", i, "_", CTSAPIInst.genId());
    auto* leaf_buf = TreeBuilder::genBufInst(leaf_name, centroid);
    leaf_buf->set_cell_master(TimingPropagator::getMinSizeCell());
    auto* leaf_driver = leaf_buf->get_driver_pin();

    TreeBuilder::localPlace(leaf_driver, cluster);
    TreeBuilder::shallowLightTree(leaf_name, leaf_driver, cluster);

    auto* leaf_net = TimingPropagator::genNet(leaf_name, leaf_driver, cluster);
    TimingPropagator::update(leaf_net);

    _net_records.push_back({leaf_net, -1});
    _nets.push_back(leaf_net);
    _leaf_load_pins.push_back(leaf_buf->get_load_pin());
  }
}

void Solver::buildTopology()
{
  if (_leaf_load_pins.empty()) {
    return;
  }

  auto root_name = CTSAPIInst.toString(_net_name, "_root_", CTSAPIInst.genId());
  auto* root_buf = TreeBuilder::genBufInst(root_name, _driver->get_location());
  root_buf->set_cell_master(TimingPropagator::getMinSizeCell());
  registerBuffer(root_buf, 0);
  connectNet(_driver, {root_buf->get_load_pin()}, "root");
  buildSubTree(root_buf->get_driver_pin(), _leaf_load_pins, 1);
}

void Solver::buildSubTree(Pin* parent_driver, const std::vector<Pin*>& subtree_loads, int depth)
{
  if (subtree_loads.empty()) {
    return;
  }

  if (subtree_loads.size() == 1) {
    finalizeLeafDepth(subtree_loads.front(), depth);
    connectNet(parent_driver, subtree_loads, "attach");
    return;
  }

  auto child_clusters = BalanceClustering::balancedBiPartition(subtree_loads, 0.1, 8, false);
  LOG_FATAL_IF(child_clusters.size() != 2) << "Net [" << _net_name << "] failed to generate a binary H-tree split.";
  std::vector<Point> fixed_locs;
  fixed_locs.reserve(subtree_loads.size());
  std::ranges::for_each(subtree_loads, [&](Pin* pin) { fixed_locs.push_back(pin->get_location()); });

  std::vector<Point> branch_locs;
  for (size_t i = 0; i < child_clusters.size(); ++i) {
    if (child_clusters[i].size() > 1) {
      branch_locs.push_back(BalanceClustering::calcCentroid(child_clusters[i]));
    }
  }
  if (!branch_locs.empty()) {
    TreeBuilder::localPlace(branch_locs, fixed_locs);
  }

  struct RecursiveTask
  {
    Pin* driver = nullptr;
    std::vector<Pin*> loads;
    int depth = 0;
  };

  std::vector<Pin*> child_load_pins(child_clusters.size(), nullptr);
  std::vector<RecursiveTask> recursive_tasks;
  size_t branch_loc_idx = 0;

  for (size_t i = 0; i < child_clusters.size(); ++i) {
    auto& cluster = child_clusters[i];
    if (cluster.size() == 1) {
      finalizeLeafDepth(cluster.front(), depth);
      child_load_pins[i] = cluster.front();
      continue;
    }

    auto branch_name = CTSAPIInst.toString(_net_name, "_branch_", depth, "_", i, "_", CTSAPIInst.genId());
    auto* branch_buf = TreeBuilder::genBufInst(branch_name, branch_locs[branch_loc_idx++]);
    branch_buf->set_cell_master(TimingPropagator::getMinSizeCell());
    registerBuffer(branch_buf, depth);
    child_load_pins[i] = branch_buf->get_load_pin();
    recursive_tasks.push_back({branch_buf->get_driver_pin(), cluster, depth + 1});
  }

  connectNet(parent_driver, child_load_pins, "branch");

  std::ranges::for_each(recursive_tasks, [&](const RecursiveTask& task) { buildSubTree(task.driver, task.loads, task.depth); });
}

Net* Solver::connectNet(Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag)
{
  LOG_FATAL_IF(driver == nullptr) << "Net [" << _net_name << "] " << stage_tag << " has null driver.";
  LOG_FATAL_IF(loads.empty()) << "Net [" << _net_name << "] " << stage_tag << " has empty loads.";

  std::ranges::for_each(loads, [&](Pin* load) {
    LOG_FATAL_IF(load == nullptr) << "Net [" << _net_name << "] " << stage_tag << " has null load.";
    TreeBuilder::directConnectTree(driver, load);
  });

  return createNetRecord(driver, loads, stage_tag);
}

Net* Solver::createNetRecord(Pin* driver, const std::vector<Pin*>& loads, const std::string& stage_tag)
{
  LOG_FATAL_IF(driver == nullptr) << "Net [" << _net_name << "] " << stage_tag << " has null driver.";
  LOG_FATAL_IF(loads.empty()) << "Net [" << _net_name << "] " << stage_tag << " has empty loads.";
  auto net_name = CTSAPIInst.toString(_net_name, "_", stage_tag, "_", CTSAPIInst.genId());
  auto* net = TimingPropagator::genNet(net_name, driver, loads);
  TimingPropagator::update(net);
  _net_records.push_back({net, -1});
  _nets.push_back(net);
  return net;
}

void Solver::registerBuffer(Inst* buffer, int depth)
{
  LOG_FATAL_IF(buffer == nullptr) << "Net [" << _net_name << "] tried to register a null buffer.";
  auto [it, inserted] = _buffer_depths.emplace(buffer, depth);
  LOG_FATAL_IF(!inserted && it->second != depth) << "Net [" << _net_name << "] buffer " << buffer->get_name()
                                                 << " depth mismatch: " << it->second << " vs " << depth;

  if (depth >= static_cast<int>(_buffers_by_depth.size())) {
    _buffers_by_depth.resize(depth + 1);
  }
  if (inserted) {
    _buffers_by_depth[depth].push_back(buffer);
  }
  _max_depth = std::max(_max_depth, depth);
}

void Solver::finalizeLeafDepth(Pin* leaf_load, int depth)
{
  auto* leaf_inst = leaf_load->get_inst();
  registerBuffer(leaf_inst, depth);
}

int Solver::childDepth(Pin* child) const
{
  if (child == nullptr) {
    return -1;
  }
  auto* inst = child->get_inst();
  if (inst == nullptr) {
    return -1;
  }
  auto it = _buffer_depths.find(inst);
  return it == _buffer_depths.end() ? -1 : it->second;
}

void Solver::breakLongWire()
{
  if (_min_buffering_length <= 0.0) {
    LOG_WARNING << "Net [" << _net_name << "] skip long-wire buffering because min_buffering_length <= 0: "
                << _min_buffering_length;
    return;
  }

  const size_t original_record_count = _net_records.size();
  size_t affected_edges = 0;
  size_t inserted_buffers = 0;

  for (size_t record_idx = 0; record_idx < original_record_count; ++record_idx) {
    auto* net = _net_records[record_idx].net;
    auto* parent_driver = net->get_driver_pin();
    auto original_loads = net->get_load_pins();
    auto updated_loads = original_loads;
    bool net_changed = false;

    for (Pin* child_load : original_loads) {
      if (child_load == nullptr) {
        continue;
      }

      auto* child_inst = child_load->get_inst();
      if (child_inst == nullptr || !child_inst->isBuffer()) {
        continue;
      }

      const double wire_length = TimingPropagator::calcLen(parent_driver, child_load);
      if (wire_length <= _min_buffering_length + kMetricEpsilon) {
        continue;
      }

      const int sizing_depth = childDepth(child_load);
      LOG_FATAL_IF(sizing_depth < 0) << "Net [" << _net_name << "] breakLongWire missing sizing depth for child buffer "
                                     << child_inst->get_name();

      const int segment_count = std::max(2, static_cast<int>(std::ceil(wire_length / _min_buffering_length)));
      const int inserted_count = segment_count - 1;
      const auto parent_loc = parent_driver->get_location();
      const auto child_loc = child_load->get_location();

      TreeBuilder::disconnect(parent_driver, child_load);
      updated_loads.erase(std::remove(updated_loads.begin(), updated_loads.end(), child_load), updated_loads.end());

      Pin* upstream_driver = parent_driver;
      Pin* first_inserted_load = nullptr;
      const auto child_master = child_inst->get_cell_master();

      for (int segment_idx = 1; segment_idx <= inserted_count; ++segment_idx) {
        const double ratio = 1.0 * segment_idx / segment_count;
        const int x = static_cast<int>(std::lround(parent_loc.x() + (child_loc.x() - parent_loc.x()) * ratio));
        const int y = static_cast<int>(std::lround(parent_loc.y() + (child_loc.y() - parent_loc.y()) * ratio));
        auto break_name = CTSAPIInst.toString(_net_name, "_break_", sizing_depth, "_", segment_idx, "_", CTSAPIInst.genId());
        auto* break_buf = TreeBuilder::genBufInst(break_name, Point(x, y));
        break_buf->set_cell_master(child_master);
        registerBuffer(break_buf, sizing_depth);

        auto* break_load = break_buf->get_load_pin();
        auto* break_driver = break_buf->get_driver_pin();
        TreeBuilder::directConnectTree(upstream_driver, break_load);

        if (segment_idx == 1) {
          first_inserted_load = break_load;
        } else {
          createNetRecord(upstream_driver, {break_load}, "break");
        }

        upstream_driver = break_driver;
      }

      LOG_FATAL_IF(first_inserted_load == nullptr)
          << "Net [" << _net_name << "] breakLongWire failed to create inserted buffers for edge "
          << parent_driver->get_name() << " -> " << child_load->get_name();

      updated_loads.push_back(first_inserted_load);
      TreeBuilder::directConnectTree(upstream_driver, child_load);
      createNetRecord(upstream_driver, {child_load}, "break");

      net_changed = true;
      ++affected_edges;
      inserted_buffers += inserted_count;
    }

    if (net_changed) {
      net->set_load_pins(updated_loads);
      std::ranges::for_each(updated_loads, [net](Pin* load) { load->set_net(net); });
    }
  }

  std::ostringstream break_summary;
  break_summary << std::fixed << std::setprecision(3);
  break_summary << "Net [" << _net_name << "] long-wire buffering summary => threshold: " << _min_buffering_length
                << ", affected edges: " << affected_edges << ", inserted buffers: " << inserted_buffers;
  LOG_INFO << break_summary.str();
  CTSAPIInst.saveToLog(break_summary.str());
}

void Solver::optimizeLevelSizing()
{
  if (_buffers_by_depth.empty()) {
    return;
  }

  std::vector<SizingCandidate> feasible_candidates;
  std::vector<SizingCandidate> all_candidates;
  SizingSearchStats stats;
  std::vector<size_t> current_assignment(_buffers_by_depth.size(), 0);
  const auto& libs = TimingPropagator::getDelayLibs();
  LOG_FATAL_IF(libs.empty()) << "Net [" << _net_name << "] has no delay libraries for sizing.";
  const size_t max_lib_index = libs.size() - 1;
  enumerateLevelSizing(0, max_lib_index, current_assignment, feasible_candidates, all_candidates, stats);
  LOG_FATAL_IF(all_candidates.empty()) << "Net [" << _net_name << "] failed to enumerate any sizing candidates.";

  auto build_search_summary = [&](size_t feasible_count, size_t pareto_count, const std::string& mode_suffix) {
    std::ostringstream sizing_search_summary;
    sizing_search_summary << "Net [" << _net_name << "] level sizing search summary => depth count: " << _buffers_by_depth.size()
                          << ", library choices: " << libs.size() << ", evaluated: " << stats.evaluated << ", feasible: "
                          << feasible_count << ", pareto front: " << pareto_count
                          << ", rejected(skew/buf_slew/sink_slew/cap/length/fanout): " << stats.rejected_skew << "/"
                          << stats.rejected_buffer_slew << "/" << stats.rejected_sink_slew << "/" << stats.rejected_cap << "/"
                          << stats.rejected_length << "/" << stats.rejected_fanout << mode_suffix;
    return sizing_search_summary.str();
  };

  SizingCandidate best;
  size_t pareto_count = 0;
  if (feasible_candidates.empty()) {
    auto fallback_candidates = all_candidates;
    normalizeCandidates(fallback_candidates);
    pareto_count = countParetoCandidates(fallback_candidates);
    best = fallback_candidates[selectBalancedCandidate(fallback_candidates)];

    const auto summary = build_search_summary(0, pareto_count,
                                              ", fallback: global delay/area/power Pareto candidate selected because no feasible sizing exists");
    LOG_WARNING << summary;
    CTSAPIInst.saveToLog(summary);

    std::ostringstream fallback_summary;
    fallback_summary << std::fixed << std::setprecision(6);
    fallback_summary << "Net [" << _net_name << "] fallback sizing trigger => selected from global delay/area/power Pareto front"
                     << ", violation score: " << best.violation_score << ", violated constraints: " << best.violated_constraints
                     << ", details: "
                     << formatFeasibilitySummary(best.feasibility);
    LOG_WARNING << fallback_summary.str();
    CTSAPIInst.saveToLog(fallback_summary.str());
  } else {
    normalizeCandidates(feasible_candidates);
    pareto_count = countParetoCandidates(feasible_candidates);
    best = feasible_candidates[selectBalancedCandidate(feasible_candidates)];

    const auto summary = build_search_summary(stats.feasible, pareto_count, "");
    LOG_INFO << summary;
    CTSAPIInst.saveToLog(summary);
  }

  applyLevelSizing(best.level_lib_indices);
  reevaluateTree();
  logSizingSummary(best);
}

void Solver::enumerateLevelSizing(size_t depth, size_t max_lib_index, std::vector<size_t>& current_assignment,
                                  std::vector<SizingCandidate>& feasible_candidates, std::vector<SizingCandidate>& all_candidates,
                                  SizingSearchStats& stats)
{
  if (depth == current_assignment.size()) {
    auto candidate = evaluateSizing(current_assignment, stats);
    all_candidates.push_back(candidate);
    if (candidate.feasible) {
      feasible_candidates.push_back(candidate);
    }
    return;
  }

  for (size_t lib_index = 0; lib_index <= max_lib_index; ++lib_index) {
    current_assignment[depth] = lib_index;
    enumerateLevelSizing(depth + 1, lib_index, current_assignment, feasible_candidates, all_candidates, stats);
  }
}

Solver::SizingCandidate Solver::evaluateSizing(const std::vector<size_t>& level_lib_indices, SizingSearchStats& stats)
{
  ++stats.evaluated;
  applyLevelSizing(level_lib_indices);
  reevaluateTree();
  auto feasibility = checkSizingFeasibility();
  if (!feasibility.feasible) {
    stats.accumulate(feasibility);
  }

  SizingCandidate candidate;
  candidate.level_lib_indices = level_lib_indices;
  candidate.delay = _driver->get_max_delay();
  candidate.skew = _driver->get_max_delay() - _driver->get_min_delay();
  candidate.area = totalBufferArea();
  candidate.power = totalBufferPower();
  candidate.feasible = feasibility.feasible;
  candidate.violated_constraints = feasibility.violationCount();
  candidate.violation_score = feasibility.totalViolation();
  candidate.feasibility = feasibility;
  if (candidate.feasible) {
    ++stats.feasible;
  }
  return candidate;
}

Solver::FeasibilityResult Solver::checkSizingFeasibility() const
{
  FeasibilityResult result;
  const auto normalized_overflow = [](double actual, double limit) {
    if (actual <= limit + kConstraintEpsilon) {
      return 0.0;
    }
    return (actual - limit) / std::max(std::abs(limit), kConstraintEpsilon);
  };

  if (!TimingPropagator::skewFeasible(_driver)) {
    result.feasible = false;
    result.skew = true;
    result.skew_over = normalized_overflow(_driver->get_max_delay() - _driver->get_min_delay(), TimingPropagator::getSkewBound());
  }

  const double max_cap = TimingPropagator::getMaxCap();
  const double max_length = TimingPropagator::getMaxLength();
  const int max_fanout = TimingPropagator::getMaxFanout();
  const double max_buf_tran = TimingPropagator::getMaxBufTran();
  const double max_sink_tran = TimingPropagator::getMaxSinkTran();

  std::ranges::for_each(_net_records, [&](const NetRecord& record) {
    auto* net = record.net;
    if (net == nullptr) {
      result.feasible = false;
      result.fanout = true;
      return;
    }

    auto* driver_pin = net->get_driver_pin();
    if (driver_pin == nullptr) {
      result.feasible = false;
      result.fanout = true;
      return;
    }

    if (net->getFanout() > max_fanout) {
      result.feasible = false;
      result.fanout = true;
      result.fanout_over = std::max(result.fanout_over, 1.0 * (net->getFanout() - max_fanout) / std::max(max_fanout, 1));
    }
    if (driver_pin->get_cap_load() > max_cap + kConstraintEpsilon) {
      result.feasible = false;
      result.cap = true;
      result.cap_over = std::max(result.cap_over, normalized_overflow(driver_pin->get_cap_load(), max_cap));
    }
    if (driver_pin->get_sub_len() > max_length + kConstraintEpsilon) {
      result.feasible = false;
      result.length = true;
      result.length_over = std::max(result.length_over, normalized_overflow(driver_pin->get_sub_len(), max_length));
    }

    driver_pin->preOrder([&](Node* node) {
      if (!node->isPin()) {
        return;
      }
      auto* pin = dynamic_cast<Pin*>(node);
      if (pin == nullptr) {
        return;
      }
      if (pin->isSinkPin()) {
        if (pin->get_slew_in() > max_sink_tran + kConstraintEpsilon) {
          result.feasible = false;
          result.sink_slew = true;
          result.sink_slew_over = std::max(result.sink_slew_over, normalized_overflow(pin->get_slew_in(), max_sink_tran));
        }
        return;
      }
      if (pin->get_slew_in() > max_buf_tran + kConstraintEpsilon) {
        result.feasible = false;
        result.buffer_slew = true;
        result.buffer_slew_over = std::max(result.buffer_slew_over, normalized_overflow(pin->get_slew_in(), max_buf_tran));
      }
    });
  });

  return result;
}

void Solver::applyLevelSizing(const std::vector<size_t>& level_lib_indices)
{
  const auto& libs = TimingPropagator::getDelayLibs();
  LOG_FATAL_IF(level_lib_indices.size() != _buffers_by_depth.size()) << "Net [" << _net_name << "] sizing depth mismatch.";

  for (size_t depth = 0; depth < _buffers_by_depth.size(); ++depth) {
    LOG_FATAL_IF(level_lib_indices[depth] >= libs.size()) << "Net [" << _net_name << "] invalid library index "
                                                          << level_lib_indices[depth] << " at depth " << depth;
    const auto& cell_master = libs[level_lib_indices[depth]]->get_cell_master();
    std::ranges::for_each(_buffers_by_depth[depth], [&](Inst* buffer) { buffer->set_cell_master(cell_master); });
  }
}

void Solver::reevaluateTree()
{
  refreshNetEvaluationOrder();
  std::ranges::sort(_net_records, [](const NetRecord& lhs, const NetRecord& rhs) {
    if (lhs.evaluation_order != rhs.evaluation_order) {
      return lhs.evaluation_order < rhs.evaluation_order;
    }
    return lhs.net->get_name() < rhs.net->get_name();
  });

  std::ranges::for_each(_net_records, [](const NetRecord& record) { TimingPropagator::update(record.net); });
}

void Solver::refreshNetEvaluationOrder()
{
  std::unordered_map<Net*, int> order_cache;
  std::ranges::for_each(_net_records, [&](NetRecord& record) { record.evaluation_order = computeNetEvaluationOrder(record.net, order_cache); });
}

int Solver::computeNetEvaluationOrder(Net* net, std::unordered_map<Net*, int>& cache) const
{
  LOG_FATAL_IF(net == nullptr) << "Net [" << _net_name << "] encountered a null solver net during timing-order refresh.";

  auto cache_it = cache.find(net);
  if (cache_it != cache.end()) {
    return cache_it->second;
  }

  int max_child_order = -1;
  std::ranges::for_each(net->get_load_pins(), [&](Pin* load) {
    if (load == nullptr) {
      return;
    }
    auto* child_inst = load->get_inst();
    if (child_inst == nullptr || child_inst->isSink()) {
      return;
    }

    auto* child_driver = child_inst->get_driver_pin();
    auto* child_net = child_driver == nullptr ? nullptr : child_driver->get_net();
    LOG_FATAL_IF(child_net == nullptr) << "Net [" << _net_name << "] buffer child " << child_inst->get_name()
                                       << " is missing a downstream solver net.";
    max_child_order = std::max(max_child_order, computeNetEvaluationOrder(child_net, cache));
  });

  const int order = max_child_order + 1;
  cache[net] = order;
  return order;
}

void Solver::normalizeCandidates(std::vector<SizingCandidate>& candidates) const
{
  if (candidates.empty()) {
    return;
  }

  auto [delay_min_it, delay_max_it] = std::ranges::minmax_element(candidates, {}, &SizingCandidate::delay);
  auto [area_min_it, area_max_it] = std::ranges::minmax_element(candidates, {}, &SizingCandidate::area);
  auto [power_min_it, power_max_it] = std::ranges::minmax_element(candidates, {}, &SizingCandidate::power);

  const double delay_min = delay_min_it->delay;
  const double delay_max = delay_max_it->delay;
  const double area_min = area_min_it->area;
  const double area_max = area_max_it->area;
  const double power_min = power_min_it->power;
  const double power_max = power_max_it->power;

  auto normalize = [](double value, double min_value, double max_value) {
    auto delta = max_value - min_value;
    return delta < kMetricEpsilon ? 0.0 : (value - min_value) / delta;
  };

  std::ranges::for_each(candidates, [&](SizingCandidate& candidate) {
    candidate.delay_norm = normalize(candidate.delay, delay_min, delay_max);
    candidate.area_norm = normalize(candidate.area, area_min, area_max);
    candidate.power_norm = normalize(candidate.power, power_min, power_max);
    candidate.distance_to_ideal
        = std::sqrt(candidate.delay_norm * candidate.delay_norm + candidate.area_norm * candidate.area_norm
                    + candidate.power_norm * candidate.power_norm);
  });
}

bool Solver::dominates(const SizingCandidate& lhs, const SizingCandidate& rhs)
{
  const bool no_worse = lhs.delay_norm <= rhs.delay_norm + kMetricEpsilon && lhs.area_norm <= rhs.area_norm + kMetricEpsilon
                        && lhs.power_norm <= rhs.power_norm + kMetricEpsilon;
  const bool strictly_better = lhs.delay_norm + kMetricEpsilon < rhs.delay_norm || lhs.area_norm + kMetricEpsilon < rhs.area_norm
                               || lhs.power_norm + kMetricEpsilon < rhs.power_norm;
  return no_worse && strictly_better;
}

size_t Solver::selectBalancedCandidate(const std::vector<SizingCandidate>& candidates) const
{
  std::vector<size_t> pareto_indices;
  for (size_t i = 0; i < candidates.size(); ++i) {
    bool dominated = false;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j) {
        continue;
      }
      if (dominates(candidates[j], candidates[i])) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_indices.push_back(i);
    }
  }

  LOG_FATAL_IF(pareto_indices.empty()) << "Net [" << _net_name << "] failed to compute a Pareto front.";

  auto better = [&](size_t lhs_idx, size_t rhs_idx) {
    const auto& lhs = candidates[lhs_idx];
    const auto& rhs = candidates[rhs_idx];
    if (std::abs(lhs.distance_to_ideal - rhs.distance_to_ideal) > kMetricEpsilon) {
      return lhs.distance_to_ideal < rhs.distance_to_ideal;
    }
    const double lhs_worst = std::max({lhs.delay_norm, lhs.area_norm, lhs.power_norm});
    const double rhs_worst = std::max({rhs.delay_norm, rhs.area_norm, rhs.power_norm});
    if (std::abs(lhs_worst - rhs_worst) > kMetricEpsilon) {
      return lhs_worst < rhs_worst;
    }
    const double lhs_sum = lhs.delay_norm + lhs.area_norm + lhs.power_norm;
    const double rhs_sum = rhs.delay_norm + rhs.area_norm + rhs.power_norm;
    if (std::abs(lhs_sum - rhs_sum) > kMetricEpsilon) {
      return lhs_sum < rhs_sum;
    }
    return lhs.level_lib_indices < rhs.level_lib_indices;
  };

  size_t best_index = pareto_indices.front();
  for (size_t idx : pareto_indices) {
    if (better(idx, best_index)) {
      best_index = idx;
    }
  }
  return best_index;
}

size_t Solver::countParetoCandidates(const std::vector<SizingCandidate>& candidates) const
{
  size_t pareto_count = 0;
  for (size_t i = 0; i < candidates.size(); ++i) {
    bool dominated = false;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j) {
        continue;
      }
      if (dominates(candidates[j], candidates[i])) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      ++pareto_count;
    }
  }
  return pareto_count;
}

std::string Solver::formatFeasibilitySummary(const FeasibilityResult& result) const
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6);
  bool first = true;
  auto append = [&](const char* label, bool violated, double overflow) {
    if (!violated) {
      return;
    }
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << label << "=" << overflow;
  };
  append("skew", result.skew, result.skew_over);
  append("buf_slew", result.buffer_slew, result.buffer_slew_over);
  append("sink_slew", result.sink_slew, result.sink_slew_over);
  append("cap", result.cap, result.cap_over);
  append("length", result.length, result.length_over);
  append("fanout", result.fanout, result.fanout_over);
  return first ? "none" : oss.str();
}

double Solver::totalBufferArea() const
{
  double total_area = 0.0;
  std::ranges::for_each(_buffer_depths, [&](const auto& item) {
    total_area += CTSAPIInst.getCellArea(item.first->get_cell_master());
  });
  return total_area;
}

double Solver::totalBufferPower() const
{
  double total_power = 0.0;
  std::ranges::for_each(_buffer_depths, [&](const auto& item) {
    total_power += CTSAPIInst.getCellLeakagePower(item.first->get_cell_master());
  });
  return total_power;
}

void Solver::logTopologySummary() const
{
  const auto depth_summary = formatBufferDepthSummary(_buffers_by_depth);
  std::ostringstream topology_summary;
  topology_summary << "Net [" << _net_name << "] H-tree summary => depth: " << (_max_depth + 1) << ", inserted buffers: "
                   << _buffer_depths.size() << ", solver nets: " << _nets.size() << ", depth buffers: [" << depth_summary << "]";
  LOG_INFO << topology_summary.str();
  CTSAPIInst.saveToLog(topology_summary.str());
}

void Solver::logSizingSummary(const SizingCandidate& candidate) const
{
  const auto& libs = TimingPropagator::getDelayLibs();
  const auto assignment_summary = formatSizingAssignmentSummary(candidate.level_lib_indices, _buffers_by_depth, libs);

  std::ostringstream sizing_summary;
  sizing_summary << std::fixed << std::setprecision(6);
  sizing_summary << "Net [" << _net_name << "] selected sizing summary => assignment: [" << assignment_summary << "], delay: "
                 << candidate.delay << ", area: " << candidate.area << ", power: " << candidate.power << ", skew: " << candidate.skew
                 << ", normalized: (" << candidate.delay_norm << ", " << candidate.area_norm << ", " << candidate.power_norm << ")";
  LOG_INFO << sizing_summary.str();
  CTSAPIInst.saveToLog(sizing_summary.str());
}

}  // namespace icts
