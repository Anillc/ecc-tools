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
 * @file ClockTraceResolve.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Net resolution and traversal logic for SDC clock tracing.
 */

#include <algorithm>
#include <deque>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "SdcClockReader.hh"
#include "SdcClockTraceAlgorithm.hh"

namespace icts::clock_trace {

auto ObjectKindName(SdcObjectKind kind) -> std::string
{
  switch (kind) {
    case SdcObjectKind::kPort:
      return "port";
    case SdcObjectKind::kPin:
      return "pin";
    case SdcObjectKind::kNet:
      return "net";
    case SdcObjectKind::kClock:
      return "clock";
    case SdcObjectKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto ResolvePortNet(idb::IdbDesign* idb_design, const std::string& port_name) -> idb::IdbNet*
{
  auto* io_pin_list = idb_design == nullptr ? nullptr : idb_design->get_io_pin_list();
  if (io_pin_list != nullptr) {
    for (auto* pin : io_pin_list->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      if (pin->get_pin_name() == port_name || TermName(pin) == port_name) {
        return pin->get_net();
      }
    }
  }
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  return net_list == nullptr ? nullptr : net_list->find_net(port_name);
}

auto ResolvePinNet(idb::IdbDesign* idb_design, const std::string& pin_name) -> idb::IdbNet*
{
  auto separator_pos = pin_name.rfind('/');
  if (separator_pos == std::string::npos) {
    separator_pos = pin_name.rfind(':');
  }
  if (separator_pos == std::string::npos || separator_pos == 0U || separator_pos + 1U >= pin_name.size()) {
    auto* io_pin_list = idb_design == nullptr ? nullptr : idb_design->get_io_pin_list();
    if (io_pin_list == nullptr) {
      return nullptr;
    }
    for (auto* pin : io_pin_list->get_pin_list()) {
      if (pin != nullptr && (pin->get_pin_name() == pin_name || TermName(pin) == pin_name)) {
        return pin->get_net();
      }
    }
    return nullptr;
  }
  auto* inst_list = idb_design == nullptr ? nullptr : idb_design->get_instance_list();
  if (inst_list == nullptr) {
    return nullptr;
  }
  const auto inst_name = pin_name.substr(0U, separator_pos);
  const auto port_name = pin_name.substr(separator_pos + 1U);
  auto* inst = inst_list->find_instance(inst_name);
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return nullptr;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (pin != nullptr && (pin->get_pin_name() == port_name || TermName(pin) == port_name)) {
      return pin->get_net();
    }
  }
  return nullptr;
}

auto ResolveRefNets(idb::IdbDesign* idb_design, const SdcObjectRef& ref) -> std::vector<idb::IdbNet*>
{
  std::vector<idb::IdbNet*> nets;
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  auto append_net = [&nets](idb::IdbNet* net) -> void {
    if (net != nullptr && std::ranges::find(nets, net) == nets.end()) {
      nets.push_back(net);
    }
  };

  switch (ref.kind) {
    case SdcObjectKind::kPort:
      append_net(ResolvePortNet(idb_design, ref.pattern));
      break;
    case SdcObjectKind::kPin:
      append_net(ResolvePinNet(idb_design, ref.pattern));
      break;
    case SdcObjectKind::kNet:
      append_net(net_list == nullptr ? nullptr : net_list->find_net(ref.pattern));
      break;
    case SdcObjectKind::kClock:
      break;
    case SdcObjectKind::kUnknown:
      append_net(net_list == nullptr ? nullptr : net_list->find_net(ref.pattern));
      append_net(ResolvePortNet(idb_design, ref.pattern));
      append_net(ResolvePinNet(idb_design, ref.pattern));
      break;
  }
  return nets;
}

auto BuildCaseConstraintSet(const SdcClockData& clock_data) -> CaseConstraintSet
{
  CaseConstraintSet constraints;
  for (const auto& analysis : clock_data.case_analyses) {
    for (const auto& object : analysis.objects) {
      if (object.kind == SdcObjectKind::kPin) {
        constraints.pin_names.insert(object.pattern);
      } else if (object.kind == SdcObjectKind::kNet) {
        constraints.net_names.insert(object.pattern);
      } else if (object.kind == SdcObjectKind::kUnknown) {
        constraints.pin_names.insert(object.pattern);
        constraints.net_names.insert(object.pattern);
      }
    }
  }
  return constraints;
}

auto BuildGeneratedBoundaryOwners(idb::IdbDesign* idb_design, const SdcClockData& clock_data)
    -> std::unordered_map<idb::IdbNet*, std::string>
{
  std::unordered_map<idb::IdbNet*, std::string> boundary_owner_by_net;
  for (const auto& clock : clock_data.clocks) {
    if (clock.kind != SdcClockDecl::Kind::kGenerated) {
      continue;
    }
    for (const auto& target : clock.targets) {
      for (auto* net : ResolveRefNets(idb_design, target)) {
        boundary_owner_by_net[net] = clock.clock_name;
      }
    }
  }
  return boundary_owner_by_net;
}

auto TraceClock(idb::IdbDesign* idb_design, const SdcClockDecl& clock, const CaseConstraintSet& case_constraints,
                const std::unordered_map<idb::IdbNet*, std::string>& generated_boundary_owner_by_net) -> std::vector<ClockTraceRecord>
{
  std::vector<ClockTraceRecord> records;
  if (clock.is_virtual || clock.targets.empty()) {
    records.push_back({clock.clock_name, "", "skipped", "virtual_clock", 0U, 0U, "", "no_netlist_source"});
    return records;
  }

  std::vector<idb::IdbNet*> seed_nets;
  for (const auto& target : clock.targets) {
    auto resolved_nets = ResolveRefNets(idb_design, target);
    seed_nets.insert(seed_nets.end(), resolved_nets.begin(), resolved_nets.end());
    if (resolved_nets.empty()) {
      records.push_back(
          {clock.clock_name, target.pattern, "rejected", ObjectKindName(target.kind), 0U, 0U, target.pattern, "unresolved_sdc_object"});
    }
  }
  std::ranges::sort(seed_nets);
  const auto unique_seed_nets = std::ranges::unique(seed_nets);
  seed_nets.erase(unique_seed_nets.begin(), unique_seed_nets.end());
  if (seed_nets.empty()) {
    records.push_back({clock.clock_name, "", "rejected", "sdc_source", 0U, 0U, "", "no_resolved_seed_net"});
    return records;
  }

  std::deque<TraceNode> queue;
  std::unordered_set<idb::IdbNet*> visited;
  visited.reserve(1024U);
  for (auto* seed_net : seed_nets) {
    if (seed_net != nullptr) {
      queue.push_back({seed_net, seed_net->get_net_name(), 0U});
    }
  }

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop_front();
    if (node.net == nullptr || !visited.insert(node.net).second) {
      continue;
    }
    if (visited.size() > kTraceNetVisitLimit) {
      records.push_back({clock.clock_name, NetName(node.net), "rejected", "trace_limit", 0U, 0U, node.path, "trace_net_visit_limit"});
      break;
    }

    if (const auto boundary_iter = generated_boundary_owner_by_net.find(node.net);
        boundary_iter != generated_boundary_owner_by_net.end() && boundary_iter->second != clock.clock_name) {
      records.push_back(
          {clock.clock_name, NetName(node.net), "trace_stop", "generated_clock_boundary", 0U, 0U, node.path, boundary_iter->second});
      continue;
    }

    const auto stats = CountDirectClockSinks(node.net);
    if (IsClockTarget(stats)) {
      records.push_back({clock.clock_name, node.net->get_net_name(), "accepted", TargetKind(stats), stats.sequential_clock_sinks,
                         stats.macro_clock_sinks, node.path, "sdc_reachable_clock_sink_net"});
    }

    if (node.depth >= kTraceDepthLimit) {
      records.push_back({clock.clock_name, NetName(node.net), "trace_stop", "depth_limit", stats.sequential_clock_sinks,
                         stats.macro_clock_sinks, node.path, "trace_depth_limit"});
      continue;
    }

    for (const auto& transition : CollectSafeTransitions(node.net, case_constraints)) {
      if (transition.net == nullptr || visited.contains(transition.net)) {
        continue;
      }
      auto next_path = node.path;
      if (!next_path.empty()) {
        next_path += " -> ";
      }
      next_path += transition.path_step;
      queue.push_back({transition.net, std::move(next_path), node.depth + 1U});
      records.push_back(
          {clock.clock_name, transition.net->get_net_name(), "trace_through", transition.reason, 0U, 0U, node.path, transition.reason});
    }
  }
  return records;
}

}  // namespace icts::clock_trace
