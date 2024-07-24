// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file CmdLinkDesign.cc
 * @author Wang Hao (harry0789@qq.com)
 * @brief
 * @version 0.1
 * @date 2021-10-12
 */
#include "ShellCmd.hh"
#include "sta/Sta.hh"
#include "sta/StaClusterTiming.hh"

namespace ista {

std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename);
CmdLinkDesign::CmdLinkDesign(const char* cmd_name) : TclCmd(cmd_name) {
  auto* cell_name_option = new TclStringOption("cell_name", 1, nullptr);
  addOption(cell_name_option);
}

unsigned CmdLinkDesign::check() {
  TclOption* cell_name_option = getOptionOrArg("cell_name");
  LOG_FATAL_IF(!cell_name_option);
  return 1;
}

unsigned CmdLinkDesign::exec() {
  if (!check()) {
    return 0;
  }

  TclOption* cell_name_option = getOptionOrArg("cell_name");
  auto* cell_name = cell_name_option->getStringVal();

  Sta* ista = Sta::getOrCreateSta();
  ista->set_top_module_name(cell_name);

  ista->linkDesignWithRustParser(cell_name);
  std::set<std::string> exclude_cell_names = {};
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write1.v",
      exclude_cell_names);
  std::vector<std::set<std::string>> clusters = {
      {"r1", "u2"}, {"r2", "u1"}, {"r3"}};
  // std::vector<std::set<std::string>> clusters = readClusterFromFile(
  //     "/home/longshuaiying/cluster_timing_model/ariane133/"
  //     "cluster_instances.txt");
  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write2.v",
      exclude_cell_names);
  int sub_netlist_index = 1;
  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    std::set<std::string> exclude_cell_names1 = {};
    std::string cluster_verilog_file =
        std::string(
            "/home/longshuaiying/cluster_timing_model/example1/verilog/") +
        std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
        ".v";
    hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
                                   exclude_cell_names1);
    sub_netlist_index++;
  }
  sta_cluster_timing.buildSubnetlistToInst();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/example1/verilog/"
      "example1_write3.v",
      exclude_cell_names);
  return 1;
}

std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename) {
  std::vector<std::set<std::string>> allData;
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return allData;
  }

  std::string line;
  while (getline(file, line)) {
    std::set<std::string> lineData;
    std::stringstream ss(line);
    std::string item;

    while (getline(ss, item, ',')) {
      lineData.insert(item);
    }

    allData.push_back(lineData);
  }

  file.close();
  return allData;
}

}  // namespace ista