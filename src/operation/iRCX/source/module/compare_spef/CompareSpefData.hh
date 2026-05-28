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

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ircx {
namespace compare_spef {

struct NodePair
{
  std::string first;
  std::string second;

  static auto ordered(std::string node1, std::string node2) -> NodePair
  {
    if (node2 < node1) {
      std::swap(node1, node2);
    }
    return NodePair{std::move(node1), std::move(node2)};
  }

  friend auto operator<(const NodePair& lhs, const NodePair& rhs) -> bool
  {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  }
};

struct Pin
{
  std::string name;
  std::string direction;
  std::string driving_cell;
  bool is_external = false;
  double x = 0.0;
  double y = 0.0;
  bool has_coordinate = false;
};

struct Resistor
{
  std::string node1;
  std::string node2;
  double resistance = 0.0;
};

struct Net
{
  std::string name;
  double total_cap = 0.0;
  std::map<std::string, double> ground_caps;
  std::map<NodePair, double> coupling_caps;
  std::vector<Resistor> resistors;
  std::vector<Pin> pins;
};

struct Data
{
  std::string file_name;
  std::string cap_unit;
  std::string res_unit;
  std::map<std::string, Net> nets;
  std::map<std::string, std::size_t> net_order;
  std::map<std::string, std::string> node_to_net;
  std::map<NodePair, double> coupling_caps;
};

struct ValueRow
{
  std::string net;
  double reference = 0.0;
  double test = 0.0;
  double delta = 0.0;
  std::optional<double> relative_delta;
};

struct CcapRow
{
  std::string victim;
  std::string aggressor;
  double reference = 0.0;
  double test = 0.0;
  double delta = 0.0;
  double reference_victim_total_cap = 0.0;
  std::optional<double> relative_delta;
};

struct ResistanceRow : ValueRow
{
  std::string from_pin;
  std::string to_pin;
  bool reference_valid = false;
  bool test_valid = false;
};

struct Summary
{
  std::size_t reference_net_count = 0;
  std::size_t test_net_count = 0;
  std::size_t matched_net_count = 0;
  std::size_t reference_only_net_count = 0;
  std::size_t test_only_net_count = 0;
  std::size_t reference_coupling_count = 0;
  std::size_t test_coupling_count = 0;
  std::size_t reference_only_coupling_count = 0;
  std::size_t test_only_coupling_count = 0;
  std::size_t tcap_row_count = 0;
  std::size_t ccap_row_count = 0;
  std::size_t p2p_row_count = 0;
};

struct CcapMismatch
{
  NodePair nets;
  NodePair report_nets;
  std::size_t first_order = 0;
  std::size_t second_order = 0;
  bool first_external = false;
  bool second_external = false;
  double capacitance = 0.0;
};

struct Result
{
  std::vector<ValueRow> tcap_rows;
  std::vector<CcapRow> ccap_rows;
  std::vector<ResistanceRow> p2p_rows;
  std::vector<std::string> reference_only_nets;
  std::vector<std::string> test_only_nets;
  std::vector<CcapMismatch> reference_only_couplings;
  std::vector<CcapMismatch> test_only_couplings;
  Summary summary;
};

}  // namespace compare_spef
}  // namespace ircx
