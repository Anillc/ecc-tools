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

#include <string>
#include <unordered_map>
#include <vector>

namespace ircx::plot_spef {

struct Node
{
  std::string name;
  int layer = 0;
  int x = 0;
  int y = 0;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
  bool has_point = false;
  bool has_box = false;
};

struct Resistor
{
  std::string node1;
  std::string node2;
  double value = 0.0;
  int layer = 0;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
  bool has_layer = false;
  bool has_box = false;
};

struct Capacitor
{
  std::string node1;
  std::string node2;
  double value = 0.0;
};

struct Net
{
  std::string name;
  std::vector<Node> nodes;
  std::unordered_map<std::string, Node*> nodes_by_name;
  std::vector<Resistor> resistors;
  std::vector<Capacitor> coupling_caps;
  std::vector<Capacitor> ground_caps;
};

struct Model
{
  std::string design_name = "plot_spef";
  std::string cap_unit;
  std::string res_unit;
  int dbu = 1000;
  std::vector<Net> nets;
  std::unordered_map<std::string, Node*> nodes_by_name;
  std::unordered_map<int, std::string> layer_names;
};

}  // namespace ircx::plot_spef
