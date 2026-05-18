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
 * @file OptimizationPreparation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Preparation helpers for CTS post-synthesis optimization.
 */

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastStaAdapter.hh"
#include "FastStaBuilder.hh"
#include "FastStaTypes.hh"
#include "Log.hh"
#include "SteinerTree.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "optimization/OptimizationInternal.hh"
#include "optimization/OptimizationOptions.hh"
#include "optimization/OptimizationTypes.hh"
#include "router/Router.hh"

namespace icts::optimization_internal {

auto CaptureGraphProfile(const FastStaClockContext& context) -> OptimizationRuntimeProfile
{
  OptimizationRuntimeProfile profile;
  profile.node_count = context.nodes.size();
  profile.net_count = context.nets.size();
  for (const auto& node : context.nodes) {
    switch (node.kind) {
      case FastStaNodeKind::kSink:
        ++profile.sink_count;
        break;
      case FastStaNodeKind::kBufferInput:
        ++profile.buffer_input_count;
        break;
      case FastStaNodeKind::kBufferOutput:
        ++profile.buffer_output_count;
        break;
      case FastStaNodeKind::kSource:
        break;
    }
  }
  return profile;
}

auto CopyOuterProfile(OptimizationRuntimeProfile& destination, const OptimizationRuntimeProfile& source) -> void
{
  destination.build_route_tree_cache_s = source.build_route_tree_cache_s;
  destination.build_fast_sta_context_s = source.build_fast_sta_context_s;
  destination.inject_route_trees_s = source.inject_route_trees_s;
  destination.collect_optimizable_buffers_s = source.collect_optimizable_buffers_s;
  destination.collect_cap_baseline_s = source.collect_cap_baseline_s;
  destination.collect_slew_baseline_s = source.collect_slew_baseline_s;
  destination.solve_clock_s = source.solve_clock_s;
  destination.apply_mutations_s = source.apply_mutations_s;
  destination.node_count = source.node_count;
  destination.net_count = source.net_count;
  destination.sink_count = source.sink_count;
  destination.buffer_input_count = source.buffer_input_count;
  destination.buffer_output_count = source.buffer_output_count;
  destination.optimizable_buffer_count = source.optimizable_buffer_count;
}

namespace {

auto ResolveOutputCapLimit(const std::string& cell_master) -> double
{
  double max_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (max_cap_pf <= 0.0) {
    max_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return max_cap_pf;
}

}  // namespace

auto CollectBufferMasterInfos() -> std::vector<BufferMasterInfo>
{
  std::vector<BufferMasterInfo> infos;
  infos.reserve(CONFIG_INST.get_buffer_types().size());
  for (const auto& cell_master : CONFIG_INST.get_buffer_types()) {
    if (cell_master.empty()) {
      continue;
    }
    const double output_cap_limit_pf = ResolveOutputCapLimit(cell_master);
    const double input_cap_pf = STA_ADAPTER_INST.queryCharInputPinCap(cell_master);
    if (output_cap_limit_pf <= 0.0 || input_cap_pf <= 0.0) {
      continue;
    }
    infos.push_back(BufferMasterInfo{.cell_master = cell_master,
                                     .input_cap_pf = input_cap_pf,
                                     .output_cap_limit_pf = output_cap_limit_pf,
                                     .area_um2 = std::max(0.0, STA_ADAPTER_INST.queryCellAreaUm2(cell_master)),
                                     .drive_rank = 0U});
  }

  std::ranges::sort(infos, [](const BufferMasterInfo& lhs, const BufferMasterInfo& rhs) -> bool {
    if (std::abs(lhs.output_cap_limit_pf - rhs.output_cap_limit_pf) > kOptimizationEpsilon) {
      return lhs.output_cap_limit_pf < rhs.output_cap_limit_pf;
    }
    if (std::abs(lhs.area_um2 - rhs.area_um2) > kOptimizationEpsilon) {
      return lhs.area_um2 < rhs.area_um2;
    }
    return lhs.cell_master < rhs.cell_master;
  });
  for (std::size_t index = 0U; index < infos.size(); ++index) {
    infos.at(index).drive_rank = static_cast<unsigned>(index);
  }
  return infos;
}

auto BuildRouteTreeCache(const std::vector<Clock*>& clocks) -> RouteTreeCache
{
  RouteTreeCache route_tree_by_net;
  const auto& clock_dag = DESIGN_INST.get_clock_dag();
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    const auto* graph = clock_dag.graphForClock(clock);
    if (graph == nullptr) {
      continue;
    }
    for (auto* net : graph->nets) {
      if (net == nullptr || route_tree_by_net.contains(net)) {
        continue;
      }
      route_tree_by_net.emplace(net, Router::buildClockNetTree(*net));
    }
  }
  return route_tree_by_net;
}

auto FindMasterInfo(const std::vector<BufferMasterInfo>& master_infos, std::string_view cell_master) -> const BufferMasterInfo*
{
  const auto iter
      = std::ranges::find_if(master_infos, [cell_master](const BufferMasterInfo& info) -> bool { return info.cell_master == cell_master; });
  return iter == master_infos.end() ? nullptr : &(*iter);
}

auto CollectCapBaseline(FastStaClockId clock_id) -> std::vector<CapBaseline>
{
  std::vector<CapBaseline> baseline;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return baseline;
  }
  baseline.reserve(context->nets.size());
  for (FastStaNetId net_id = 0U; net_id < context->nets.size(); ++net_id) {
    const auto cap_status = FastStaAdapter::queryCapStatus(clock_id, net_id);
    baseline.push_back(CapBaseline{.load_cap_pf = cap_status.has_value() ? cap_status->load_cap_pf : 0.0,
                                   .max_cap_pf = cap_status.has_value() ? cap_status->max_cap_pf : 0.0,
                                   .violated = cap_status.has_value() && cap_status->violated});
  }
  return baseline;
}

auto CollectSlewBaseline(FastStaClockId clock_id) -> std::vector<SlewBaseline>
{
  std::vector<SlewBaseline> baseline;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr) {
    return baseline;
  }
  baseline.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto slew_status = FastStaAdapter::querySlewStatus(clock_id, node_id);
    baseline.push_back(SlewBaseline{.slew_ns = slew_status.has_value() ? slew_status->slew_ns : 0.0,
                                    .max_slew_ns = slew_status.has_value() ? slew_status->max_slew_ns : 0.0,
                                    .available = slew_status.has_value(),
                                    .violated = slew_status.has_value() && slew_status->violated});
  }
  return baseline;
}

auto CollectOptimizableBuffers(FastStaClockId clock_id, const std::vector<BufferMasterInfo>& master_infos) -> std::vector<OptimizableBuffer>
{
  std::vector<OptimizableBuffer> buffers;
  const auto* context = FastStaAdapter::queryClockContext(clock_id);
  if (context == nullptr || master_infos.empty()) {
    return buffers;
  }
  buffers.reserve(context->nodes.size());
  for (FastStaNodeId node_id = 0U; node_id < context->nodes.size(); ++node_id) {
    const auto& node = context->nodes.at(node_id);
    if (node.kind != FastStaNodeKind::kBufferOutput || node.inst_name.empty() || node.cell_master.empty()) {
      continue;
    }
    auto* inst = DESIGN_INST.findInst(node.inst_name);
    if (inst == nullptr || !inst->is_buffer() || FindMasterInfo(master_infos, node.cell_master) == nullptr) {
      continue;
    }
    buffers.push_back(OptimizableBuffer{
        .node_id = node_id, .inst = inst, .inst_name = node.inst_name, .current_master = node.cell_master, .candidates = master_infos});
  }
  return buffers;
}

auto InjectRouteTrees(FastStaClockId clock_id, const Clock& clock, const RouteTreeCache& route_tree_by_net) -> bool
{
  auto* context = FastStaAdapter::mutableClockContext(clock_id);
  const auto* graph = DESIGN_INST.get_clock_dag().graphForClock(&clock);
  if (context == nullptr || graph == nullptr) {
    return false;
  }
  const auto total_start = std::chrono::steady_clock::now();
  auto progress_start = total_start;
  const auto progress_interval = DefaultOptimizationOptions().route_tree_progress_interval;
  const auto initial_detail_net_count = DefaultOptimizationOptions().route_tree_initial_detail_net_count;
  const auto slow_net_threshold_s = DefaultOptimizationOptions().route_tree_slow_net_threshold_s;
  std::size_t visited_net_count = 0U;
  std::size_t injected_net_count = 0U;
  std::size_t rc_node_count = 0U;
  std::size_t rc_edge_count = 0U;
  LOG_INFO << "Optimization: start route-tree injection for clock \"" << clock.get_clock_name() << "\", dag_nets=" << graph->nets.size()
           << ", cached_route_trees=" << route_tree_by_net.size() << ".";
  for (auto* net : graph->nets) {
    if (net == nullptr) {
      continue;
    }
    ++visited_net_count;
    const auto route_iter = route_tree_by_net.find(net);
    if (route_iter == route_tree_by_net.end()) {
      LOG_ERROR << "Optimization: route tree is unavailable for net \"" << net->get_name() << "\".";
      return false;
    }
    if (visited_net_count <= initial_detail_net_count) {
      LOG_INFO << "Optimization: route-tree injection start net " << visited_net_count << "/" << graph->nets.size() << " \""
               << net->get_name() << "\", loads=" << net->get_loads().size() << ", route_nodes=" << route_iter->second.node_count()
               << ", route_edges=" << route_iter->second.edge_count() << ".";
    }
    const auto net_start = std::chrono::steady_clock::now();
    if (!FastStaBuilder::injectNetRouteTree(*context, *net, route_iter->second)) {
      LOG_ERROR << "Optimization: fast STA route-tree injection failed for net \"" << net->get_name() << "\".";
      return false;
    }
    const double net_runtime_s = ElapsedSeconds(net_start);
    ++injected_net_count;
    if (const auto net_iter = context->net_id_by_name.find(net->get_name());
        net_iter != context->net_id_by_name.end() && net_iter->second < context->nets.size()) {
      const auto& parasitic = context->nets.at(net_iter->second).parasitic;
      rc_node_count += parasitic.rc_nodes.size();
      rc_edge_count += parasitic.rc_edges.size();
    }
    if (visited_net_count <= initial_detail_net_count || net_runtime_s >= slow_net_threshold_s) {
      LOG_INFO << "Optimization: route-tree injection finish net " << visited_net_count << "/" << graph->nets.size() << " \""
               << net->get_name() << "\" in " << net_runtime_s << " s.";
    }
    if (injected_net_count % progress_interval == 0U) {
      LOG_INFO << "Optimization: route-tree injection progress for clock \"" << clock.get_clock_name()
               << "\": injected=" << injected_net_count << "/" << graph->nets.size() << ", visited=" << visited_net_count
               << ", elapsed=" << ElapsedSeconds(total_start) << " s, interval=" << ElapsedSeconds(progress_start) << " s.";
      progress_start = std::chrono::steady_clock::now();
    }
  }
  LOG_INFO << "Optimization: route-tree injection finished for clock \"" << clock.get_clock_name() << "\" in "
           << ElapsedSeconds(total_start) << " s, injected_nets=" << injected_net_count << ", rc_nodes=" << rc_node_count
           << ", rc_edges=" << rc_edge_count << ".";

  auto update_start = std::chrono::steady_clock::now();
  LOG_INFO << "Optimization: start post-injection timing update for clock \"" << clock.get_clock_name() << "\".";
  if (!FastStaAdapter::updateTiming(clock_id)) {
    return false;
  }
  LOG_INFO << "Optimization: post-injection timing update for clock \"" << clock.get_clock_name() << "\" finished in "
           << ElapsedSeconds(update_start) << " s.";

  update_start = std::chrono::steady_clock::now();
  LOG_INFO << "Optimization: start post-injection power update for clock \"" << clock.get_clock_name() << "\".";
  if (!FastStaAdapter::updatePower(clock_id)) {
    return false;
  }
  LOG_INFO << "Optimization: post-injection power update for clock \"" << clock.get_clock_name() << "\" finished in "
           << ElapsedSeconds(update_start) << " s.";
  return true;
}

}  // namespace icts::optimization_internal
