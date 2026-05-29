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
#include "reader/SpefReader.hh"

#include <set>
#include <string>
#include <utility>

#include "SpefParser.hh"

namespace ircx {
namespace compare_spef {
namespace {

auto directionName(ista::spef::ConnectionDirection direction) -> std::string
{
  switch (direction) {
    case ista::spef::ConnectionDirection::kInput:
      return "I";
    case ista::spef::ConnectionDirection::kOutput:
      return "O";
    case ista::spef::ConnectionDirection::kInout:
      return "B";
    case ista::spef::ConnectionDirection::kInternal:
      return "N";
    case ista::spef::ConnectionDirection::kUninitialized:
      break;
  }
  return "";
}

}  // namespace

void SpefReader::rememberNodeNet(Data& data, const std::string& node_name, const std::string& net_name) const
{
  if (!node_name.empty() && !net_name.empty()) {
    data.node_to_net.try_emplace(node_name, net_name);
  }
}

auto SpefReader::resolveNodeNet(const Data& data, const std::string& node_name) const -> std::string
{
  const auto node_it = data.node_to_net.find(node_name);
  if (node_it != data.node_to_net.end()) {
    return node_it->second;
  }

  const auto colon = node_name.find(':');
  if (colon != std::string::npos) {
    const auto prefix = node_name.substr(0, colon);
    if (data.nets.contains(prefix)) {
      return prefix;
    }
  }

  if (data.nets.contains(node_name)) {
    return node_name;
  }
  return {};
}

void SpefReader::buildNetCouplingCaps(Data& data) const
{
  std::set<NodePair> seen_node_pairs;
  for (const auto& [net_name, net] : data.nets) {
    for (const auto& [node_pair, capacitance] : net.coupling_caps) {
      if (!seen_node_pairs.insert(node_pair).second) {
        continue;
      }

      const auto net1 = resolveNodeNet(data, node_pair.first);
      const auto net2 = resolveNodeNet(data, node_pair.second);
      if (net1.empty() || net2.empty() || net1 == net2) {
        continue;
      }
      data.coupling_caps[NodePair::ordered(net1, net2)] += capacitance;
    }
  }
}

auto SpefReader::read(const std::string& path, Data& data) const -> bool
{
  ista::spef::SpefReader reader;
  if (!reader.read(path)) {
    return false;
  }
  reader.expandName();

  const auto* spef_file = reader.getSpefFile();
  if (spef_file == nullptr) {
    return false;
  }

  data = Data{};
  data.file_name = path;
  data.cap_unit = reader.getSpefCapUnit();
  data.res_unit = reader.getSpefResUnit();

  for (const auto& spef_net : spef_file->nets) {
    Net net;
    net.name = spef_net.name;
    net.total_cap = spef_net.lcap;

    for (const auto& conn : spef_net.conns) {
      Pin pin;
      pin.name = conn.pin_port_name;
      pin.direction = directionName(conn.conn_direction);
      pin.driving_cell = conn.driving_cell;
      pin.is_external = conn.conn_type == ista::spef::ConnectionType::kExternal;
      pin.x = conn.coordinate.x;
      pin.y = conn.coordinate.y;
      pin.has_coordinate = conn.coordinate.x >= 0.0 && conn.coordinate.y >= 0.0;
      rememberNodeNet(data, pin.name, net.name);
      net.pins.push_back(std::move(pin));
    }

    for (const auto& cap : spef_net.caps) {
      if (cap.node2.empty()) {
        net.ground_caps[cap.node1] += cap.res_or_cap;
        rememberNodeNet(data, cap.node1, net.name);
      } else {
        rememberNodeNet(data, cap.node1, net.name);
        net.coupling_caps[NodePair::ordered(cap.node1, cap.node2)] += cap.res_or_cap;
      }
    }

    for (const auto& res : spef_net.ress) {
      if (res.node1.empty() || res.node2.empty()) {
        continue;
      }
      rememberNodeNet(data, res.node1, net.name);
      rememberNodeNet(data, res.node2, net.name);
      net.resistors.push_back(Resistor{res.node1, res.node2, res.res_or_cap});
    }

    const std::string net_name = net.name;
    data.net_order.try_emplace(net_name, data.net_order.size());
    data.nets[net_name] = std::move(net);
  }

  buildNetCouplingCaps(data);
  return true;
}

}  // namespace compare_spef
}  // namespace ircx
