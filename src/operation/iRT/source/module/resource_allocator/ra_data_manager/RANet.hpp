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
#pragma once

#include "Net.hpp"
#include "RAPin.hpp"
#include "RAGCellNode.hpp"


namespace irt {

class RANet
{
 public:
  RANet() = default;
  ~RANet() = default;
  // getter
  Net* get_origin_net() { return _origin_net; }
  int32_t get_net_idx() const { return _net_idx; }
  ConnectType get_connect_type() const { return _connect_type; }
  std::vector<RAPin>& get_ra_pin_list() { return _ra_pin_list; }
  BoundingBox& get_bounding_box() { return _bounding_box; }
  double get_routing_demand() const { return _routing_demand; }
  std::vector<RAGCellNode>& get_ra_gcell_node_list() { return _ra_gcell_node_list; }
  GridMap<double>& get_resource_cost_map() { return _resource_cost_map; }
  // setter
  void set_origin_net(Net* origin_net) { _origin_net = origin_net; }
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_connect_type(const ConnectType& connect_type) { _connect_type = connect_type; }
  void set_ra_pin_list(const std::vector<RAPin>& ra_pin_list) { _ra_pin_list = ra_pin_list; }
  void set_bounding_box(const BoundingBox& bounding_box) { _bounding_box = bounding_box; }
  void set_routing_demand(const double routing_demand) { _routing_demand = routing_demand; }
  void set_resource_cost_map(const GridMap<double>& resource_cost_map) { _resource_cost_map = resource_cost_map; }
  // function

 private:
  Net* _origin_net = nullptr;
  int32_t _net_idx = -1;
  ConnectType _connect_type = ConnectType::kNone;
  std::vector<RAPin> _ra_pin_list;
  BoundingBox _bounding_box;
  double _routing_demand = 0;
  std::vector<RAGCellNode> _ra_gcell_node_list;
  GridMap<double> _resource_cost_map;
};
}  // namespace irt
