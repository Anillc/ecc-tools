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
#include "py_ista.h"

#include <tool_manager.h>

#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "log/Log.hh"
#include "sta/Sta.hh"

namespace python_interface {
bool staRun(const std::string& output)
{
  bool run_ok = iplf::tmInst->autoRunSTA(output);
  return run_ok;
}

bool staInit(const std::string& output)
{
  bool run_ok = iplf::tmInst->initSTA(output);
  return run_ok;
}

bool staReport(const std::string& output)
{
  bool run_ok = iplf::tmInst->runSTA(output);
  return run_ok;
}

bool setDesignWorkSpace(const std::string& design_workspace)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->set_design_work_space(design_workspace.c_str());
  return true;
}

bool read_lef_def(std::vector<std::string>& lef_files, const std::string& def_file)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();

  timing_engine->readDefDesign(def_file, lef_files);

  return 1;
}

bool readVerilog(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();

  ista->readVerilogWithRustParser(file_name.c_str());
  return true;
}

bool readLiberty(std::vector<std::string>& lib_files)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->readLiberty(lib_files);
  return true;
}

bool linkDesign(const std::string& cell_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->set_top_module_name(cell_name.c_str());
  ista->linkDesignWithRustParser(cell_name.c_str());
  return true;
}

bool readSpef(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->readSpef(file_name.c_str());
  return true;
}

bool readSdc(const std::string& file_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  return ista->readSdc(file_name.c_str());
}

std::string getNetName(const std::string& pin_port_name)
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto objs = ista->get_netlist()->findObj(pin_port_name.c_str(), false, false);
  LOG_FATAL_IF(objs.size() != 1);

  auto* pin_or_port = objs[0];
  std::string net_name = pin_or_port->get_net()->get_name();

  return net_name;
}

double getSegmentResistance(int layer_id, double segment_length, int route_layer_id) {
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* idb_adapter = dynamic_cast<ista::TimingIDBAdapter*>(timing_engine->get_db_adapter());
  double resistance = idb_adapter->getResistance(layer_id, segment_length, std::nullopt, route_layer_id);

  return resistance;
}

double getSegmentCapacitance(int layer_id, double segment_length, int route_layer_id) {
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* idb_adapter = dynamic_cast<ista::TimingIDBAdapter*>(timing_engine->get_db_adapter());
  double capacitance = idb_adapter->getCapacitance(layer_id, segment_length, std::nullopt, route_layer_id);

  return capacitance;
}

std::string makeRCTreeInnerNode(const std::string& net_name, int id, float cap)
{
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  auto* rc_node = timing_engine->makeOrFindRCTreeNode(the_net, id);
  rc_node->incrCap(cap);

  return rc_node->get_name();
}

std::string makeRCTreeObjNode(const std::string& pin_port_name, float cap) {
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();

  auto the_pin_ports = ista->get_netlist()->findObj(pin_port_name.c_str(), false, false);
  assert(the_pin_ports.size() == 1);

  auto* rc_node = timing_engine->makeOrFindRCTreeNode(the_pin_ports.front());
  rc_node->incrCap(cap);

  return rc_node->get_name();
}

bool makeRCTreeEdge(const std::string& net_name, std::string& node1, std::string& node2, float res) {
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  auto* rc_node1 = timing_engine->findRCTreeNode(the_net, node1);
  auto* rc_node2 = timing_engine->findRCTreeNode(the_net, node2);

  timing_engine->makeResistor(the_net, rc_node1, rc_node2, res);

  return true;
}

bool updateRCTreeInfo(const std::string& net_name) {
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  auto* ista = ista::Sta::getOrCreateSta();
  auto* the_net = ista->get_netlist()->findNet(net_name.c_str());
  timing_engine->updateRCTreeInfo(the_net);

  return true;
}


bool updateTiming()
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->buildGraph();
  ista->updateTiming();
  return true;
}

bool reportSta()
{
  auto* ista = ista::Sta::getOrCreateSta();
  ista->reportTiming();
  return true;
}

std::vector<PathWireTimingData> getWireTimingData(unsigned n_worst_path_per_clock)
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto path_wire_timing_data = ista->reportTimingData(n_worst_path_per_clock);

  std::vector<PathWireTimingData> ret_timing_data;

  for (auto& one_path_wire_timing_data : path_wire_timing_data) {
    PathWireTimingData ret_one_path_data;
    for (auto& wire_timing_data : one_path_wire_timing_data) {
      WireTimingData ret_wire_data;
      ret_wire_data._from_node_name = std::move(wire_timing_data._from_node_name);
      ret_wire_data._to_node_name = std::move(wire_timing_data._to_node_name);
      ret_wire_data._wire_resistance = wire_timing_data._wire_resistance;
      ret_wire_data._wire_capacitance = wire_timing_data._wire_capacitance;
      ret_wire_data._wire_delay = wire_timing_data._wire_delay;
      ret_wire_data._wire_from_slew = wire_timing_data._wire_from_slew;
      ret_wire_data._wire_to_slew = wire_timing_data._wire_to_slew;

      ret_one_path_data.emplace_back(std::move(ret_wire_data));
    }

    ret_timing_data.emplace_back(std::move(ret_one_path_data));

  }

  LOG_INFO << "get wire data size: " << ret_timing_data.size();

  return ret_timing_data;
}

bool reportTiming(int digits, 
  const std::string& delay_type, 
  std::vector<std::string> exclude_cell_names, 
  bool derate, 
  bool is_clock_cap, 
  bool is_not_bak_rpt, 
  int max_path, 
  int nworst, 
  std::vector<std::string> from_list, 
  std::vector<std::vector<std::string>> through, 
  std::vector<std::string> to_list, 
  bool is_json)
{
  // Get Sta instance
  auto* ista = ista::Sta::getOrCreateSta();

  // Set significant digits
  ista->set_significant_digits(digits);

  // Set analysis mode
  if (!delay_type.empty()) {
    if (delay_type == "max_min") {
      ista->set_analysis_mode(ista::AnalysisMode::kMaxMin);
    } else if (delay_type == "max") {
      ista->set_analysis_mode(ista::AnalysisMode::kMax);
    } else if (delay_type == "min") {
      ista->set_analysis_mode(ista::AnalysisMode::kMin);
    }
  }

  // Set max path per clock
  ista->set_n_worst_path_per_clock(max_path);

  // Set max path per endpoint
  ista->set_n_worst_path_per_endpoint(nworst);

  // Set report spec
  ista->setReportSpec(std::move(from_list), std::move(through), std::move(to_list));

  // Enable json report if needed
  if (is_json) {
    ista->enableJsonReport();
  }

  // Build graph
  ista->buildGraph();

  // Update timing
  ista->updateTiming();

  // Convert exclude_cell_names to set
  std::set<std::string> exclude_cell_names_set;
  for (const auto& exclude_cell_name : exclude_cell_names) {
    exclude_cell_names_set.insert(exclude_cell_name);
  }

  // Report timing
  ista->reportTiming(std::move(exclude_cell_names_set), derate, is_clock_cap, is_not_bak_rpt);

  return true;
}

std::vector<std::string> get_used_libs()
{
  auto* ista = ista::Sta::getOrCreateSta();
  auto used_libs = ista->getUsedLibs();

  std::vector<std::string> ret;
  for (auto& lib : used_libs) {
    ret.push_back(lib->get_file_name());
  }

  return ret;
}

bool initLog(std::string log_dir)
{
  char config[] = "test";
  char* argv[] = {config};

  ieda::Log::makeSureDirectoryExist(log_dir);
  ieda::Log::init(argv, log_dir);

  return true;
}

}  // namespace python_interface