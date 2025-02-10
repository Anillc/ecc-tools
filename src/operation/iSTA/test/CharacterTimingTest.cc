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
#include "sta/StaCharacterTiming.hh"
#include "usage/usage.hh"

using namespace ista;
using namespace ieda;

namespace {

class CharacterTimingTest : public testing::Test {
  void SetUp() final {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  void TearDown() final { Log::end(); }
};

TEST_F(CharacterTimingTest, example1) {
  Stats stats;

  auto* timing_engine = TimingEngine::getOrCreateTimingEngine();
  timing_engine->set_num_threads(48);
  const char* design_work_space = "/home/longshuaiying/cluster_timing_model";
  timing_engine->set_design_work_space(design_work_space);

  std::vector<const char*> lib_files{
      "/home/taosimin/nangate45/lib/NangateOpenCellLibrary_typical.lib"};
  std::vector<const char*> t28_lib_files = {
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
  std::vector<const char*> cluster_lib_file{
      "/home/longshuaiying/cluster_timing_model/asic_top/liberty/cluster1.lib"};
  timing_engine->readLiberty(t28_lib_files);

  // auto* inst_cell =
  //     timing_engine->get_ista()->findLibertyCell(liberty_cell_name);
  // const char* port_name;
  // auto* library_port_or_port_bus =
  //     inst_cell->get_cell_port_or_port_bus(port_name);
  // int a = 0;

  timing_engine->get_ista()->set_analysis_mode(ista::AnalysisMode::kMaxMin);
  timing_engine->get_ista()->set_n_worst_path_per_clock(1);

  for (int sub_netlist_index = 2; sub_netlist_index <= 200;
       ++sub_netlist_index) {
    // std::string output_lib_path =
    //     "/home/longshuaiying/cluster_timing_model/asic_top/liberty/cluster" +
    //     std::to_string(sub_netlist_index) + ".lib";
    // std::cout << output_lib_path << " \\" << std::endl;

    std::string top_module_name = "cluster" + std::to_string(sub_netlist_index);
    timing_engine->get_ista()->set_top_module_name(top_module_name.c_str());
    std::string asic_top_hier_verilog_file =
        "/home/longshuaiying/cluster_timing_model/asic_top/verilog/"
        "hier_sub_netlist" +
        std::to_string(sub_netlist_index) + ".v";
    timing_engine->readDesign(asic_top_hier_verilog_file.c_str());
    timing_engine->buildGraph();
    std::string model_path = std::string("asic_top.lib") + std::to_string(sub_netlist_index);
    timing_engine->extractTimingModel(
        AnalysisMode::kMax, model_path.c_str());
    timing_engine->get_ista()->resetNetlist();
    timing_engine->get_ista()->resetGraph();
  }

  // timing_engine->readSdc(
  //     "/home/taosimin/nangate45/design/example/example1.sdc");

  // timing_engine->extractTimingModel(AnalysisMode::kMin,
  // "macro_model_min.lib");

  double memory_delta = stats.memoryDelta();
  LOG_INFO << "extract timing lib memory usage " << memory_delta << "MB";
  double time_delta = stats.elapsedRunTime();
  LOG_INFO << "extract timing lib time elapsed " << time_delta << "s";
}

}  // namespace
