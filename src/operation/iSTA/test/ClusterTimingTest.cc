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
#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"
#include "sta/StaClusterTiming.hh"
#include "usage/usage.hh"

using namespace ista;
using namespace ieda;

namespace {
std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename);

class ClusterTimingTest : public testing::Test {
  void SetUp() final {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() final { Log::end(); }
};

TEST_F(ClusterTimingTest, example1) {
  Stats stats;

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(48);
  const char* design_work_space = "/home/longshuaiying/cluster_timing_model";
  timing_engine->set_design_work_space(design_work_space);

  std::vector<const char*> lib_files{
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib"};
  timing_engine->readLiberty(lib_files);

  timing_engine->get_ista()->set_analysis_mode(ista::AnalysisMode::kMaxMin);
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);

  timing_engine->get_ista()->set_top_module_name("cluster1");

  timing_engine->readDesign(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "hier_sub_netlist1.v");

  // timing_engine->readSdc(
  //     "/home/taosimin/nangate45/design/example/example1.sdc");

  timing_engine->buildGraph();

  // timing_engine->extractTimingModel(AnalysisMode::kMax, "asic_top.lib");
  // timing_engine->extractTimingModel(AnalysisMode::kMin,
  // "macro_model_min.lib");

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
}

TEST_F(ClusterTimingTest, asic_top) {
  Stats stats;
  Sta* ista = Sta::getOrCreateSta();

  ista->set_num_threads(48);
  const char* design_work_space =
      "/home/longshuaiying/cluster_timing_model/asic_top/rpt";
  ista->set_design_work_space(design_work_space);

  std::vector<std::string> t28_lib_files = {
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140mblvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140oppulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp30p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp30p140ulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mbhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mblvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140oppulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp35p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp35p140ulvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140ehvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140hvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140lvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140mbhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140mbssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppehvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140opphvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140opplvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140oppuhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/tcbn28hpcplusbwp40p140ssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "tcbn28hpcplusbwp40p140uhvtssg0p81v125c_ccs.lib",
      "/home/taosimin/T28/ccslib/"
      "ts5n28hpcplvta256x32m4fw_130a_ssg0p81v125c.lib",
      "/home/taosimin/T28/ccslib/"
      "ts5n28hpcplvta64x128m2fw_130a_ssg0p81v125c.lib",
      "/home/taosimin/T28/ccslib/tphn28hpcpgv18ssg0p81v1p62v125c.lib",
      "/home/taosimin/T28/ccslib/PLLTS28HPMLAINT_SS_0P81_125C.lib"};
  ista->readLiberty(t28_lib_files);

  ista->readVerilogWithRustParser(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "asic_top_flatten.v");
  const char* top_module_name = "asic_top";
  ista->set_top_module_name(top_module_name);
  ista->linkDesignWithRustParser(top_module_name);

  std::set<std::string> exclude_cell_names = {};

  std::vector<std::set<std::string>> clusters = readClusterFromFile(
      "/home/longshuaiying/cluster_timing_model/asic_top/"
      "cluster_instances.txt");

  StaClusterTiming sta_cluster_timing(clusters);

  sta_cluster_timing.addHierSubNetlist();

  std::vector<Netlist*> hier_sub_netlists =
      ista->get_netlist()->get_hier_sub_netlists();
  auto& top_insts = ista->get_netlist()->get_instances();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "asic_top_write2.v",
      exclude_cell_names);
  int sub_netlist_index = 1;

  auto findInstanceByName = [&top_insts](const std::string& name) -> Instance* {
    for (auto& inst : top_insts) {
      if (inst.get_name() == name) {
        return &inst;
      }
    }
    return nullptr;
  };

  for (const auto& hier_sub_netlist : hier_sub_netlists) {
    std::set<std::string> exclude_cell_names1 = {};
    std::string cluster_verilog_file =
        std::string(
            "/home/longshuaiying/cluster_timing_model/asic_top/verilog/") +
        std::string("hier_sub_netlist") + std::to_string(sub_netlist_index) +
        ".v";
    int cluster_inst_nums = hier_sub_netlist->getInstanceNum();
    std::cout << "cluster_inst_nums" << cluster_inst_nums << std::endl;
    int hier_inst_index = 1;
    for (auto& hier_inst : hier_sub_netlist->get_instances()) {
      auto top_inst = findInstanceByName(hier_inst.get_name());
      LOG_FATAL_IF(!top_inst);
      std::cout << "hier_inst "
                << ":" << hier_inst_index << ":" << hier_inst.get_name()
                << std::endl;
      hier_inst.isEqual(*top_inst);
      ++hier_inst_index;
    }
    int hier_vitual_port_index = 1;
    std::vector<std::string> hier_virtual_port_name_vec;
    std::set<std::string> hier_virtual_port_name_set;

    for (auto& hier_port : hier_sub_netlist->get_ports()) {
      auto top_port = ista->get_netlist()->findPort(hier_port.get_name());
      if (!top_port) {
        // std::cout << "hier_port "
        //           << ":" << hier_vitual_port_index << ":"
        //           << hier_port.get_name() << std::endl;
        hier_virtual_port_name_vec.emplace_back(hier_port.get_name());
        hier_virtual_port_name_set.insert(hier_port.get_name());
      }

      // ++hier_vitual_port_index;
    }

    std::cout << "hier_virtual_port_name_vec: "
              << hier_virtual_port_name_vec.size() << std::endl;
    std::cout << "hier_virtual_port_name_set: "
              << hier_virtual_port_name_set.size() << std::endl;

    hier_sub_netlist->writeVerilog(cluster_verilog_file.c_str(),
                                   exclude_cell_names1);

    std::unordered_map<std::string, int> count_map;
    for (const auto& item : hier_virtual_port_name_vec) {
      count_map[item]++;
    }
    std::cout << "Duplicate elements and their counts in "
                 "hier_virtual_port_name_vec : "
              << std::endl;
    bool has_duplicates = false;
    int duplicate_count = 0;
    for (const auto& pair : count_map) {
      if (pair.second > 1) {
        std::cout << pair.first << ": " << pair.second - 1 << " times"
                  << std::endl;
        int count = pair.second - 1;
        duplicate_count = duplicate_count + count;
        has_duplicates = true;
      }
    }
    std::cout << "Duplicate elements: " << duplicate_count << std::endl;

    if (!has_duplicates) {
      std::cout << "No duplicate elements in hier_virtual_port_name_vec."
                << std::endl;
    }

    sub_netlist_index++;
    if (sub_netlist_index == 2) {
      break;
    }
  }
  //   return 1;
  sta_cluster_timing.buildSubnetlistToInst();
  ista->get_netlist()->writeVerilog(
      "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
      "asic_top_write3.v",
      exclude_cell_names);

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
}

std::vector<std::set<std::string>> readClusterFromFile(
    const std::string& filename) {
  std::vector<std::set<std::string>> allData;
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << "Unable to open file." << std::endl;
    return allData;
  }

  std::string line;
  while (std::getline(file, line)) {
    std::set<std::string> lineData;
    size_t start = line.find('{');
    size_t end = line.find('}');

    if (start != std::string::npos && end != std::string::npos && start < end) {
      std::string content = line.substr(start + 1, end - start - 1);
      std::stringstream ss(content);
      std::string item;

      while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
          lineData.insert(item);
        }
      }
    }

    allData.push_back(lineData);
  }

  file.close();

  size_t inst_num = 0;
  bool first = true;
  for (const auto& set : allData) {
    if (first) {
      std::cout << "Total number of cluster1 instances: " << set.size()
                << std::endl;
      first = false;
    }

    inst_num += set.size();
  }

  std::cout << "Total number of instances: " << inst_num << std::endl;
  return allData;
}

}  // namespace
