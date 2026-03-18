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
 * @file STAAdapter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter implementation over iSTA.
 */

#include "STAAdapter.hh"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "Type.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "config/Config.hh"
#include "design/Inst.hh"
#include "idm.h"
#include "liberty/Lib.hh"
#include "logger/Logger.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sta/StaBuildGraph.hh"
#include "sta/StaClock.hh"
#include "sta/StaData.hh"
#include "sta/StaGraph.hh"
#include "sta/StaVertex.hh"

namespace icts {
namespace {

decltype(auto) GetDbConfig()
{
  return (dmInst->get_config());
}

ista::TimingEngine* GetStaEngine()
{
  return ista::TimingEngine::getOrCreateTimingEngine();
}

}  // namespace

void STAAdapter::init()
{
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(GetStaEngine()->get_ista());
  db_adapter->set_idb(dmInst->get_idb_builder());
  GetStaEngine()->set_db_adapter(std::move(db_adapter));
  auto sta_work_dir = std::filesystem::path(ConfigInst.get_work_dir()).append("sta").string();
  if (!std::filesystem::exists(sta_work_dir)) {
    std::filesystem::create_directories(sta_work_dir);
  }
  std::vector<const char*> lib_paths;
  std::ranges::transform(GetDbConfig().get_lib_paths(), std::back_inserter(lib_paths),
                         [](const std::string& lib_path) { return lib_path.c_str(); });
  GetStaEngine()->set_num_threads(80);
  GetStaEngine()->set_design_work_space(sta_work_dir.c_str());
  auto cell_set = std::set<std::string>(std::ranges::begin(ConfigInst.get_buffer_types()), std::ranges::end(ConfigInst.get_buffer_types()));
  GetStaEngine()->get_ista()->addLinkCells(cell_set);
  GetStaEngine()->readLiberty(lib_paths);

  GetStaEngine()->resetNetlist();
  GetStaEngine()->resetGraph();
  GetStaEngine()->get_db_adapter()->convertDBToTimingNetlist();
  const char* sdc_path = GetDbConfig().get_sdc_path().c_str();
  GetStaEngine()->readSdc(sdc_path);
  GetStaEngine()->buildGraph();

  GetStaEngine()->initRcTree();
  GetStaEngine()->get_ista()->set_n_worst_path_per_clock(10);
}

icts::InstType STAAdapter::queryInstType(const std::string& inst_name) const
{
  auto inst_type = icts::InstType::kUnknown;

  auto name = inst_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  auto* sta_netlist = GetStaEngine()->get_netlist();
  CTS_LOG_FATAL_IF(sta_netlist == nullptr) << "STA netlist is null.";
  auto* sta_inst = sta_netlist->findInstance(name.c_str());
  auto* lib_cell = sta_inst != nullptr ? sta_inst->get_inst_cell() : nullptr;
  if (sta_inst == nullptr) {
    CTS_LOG_ERROR << "Instance " << name << " is not found in the STA netlist.";
  } else if (lib_cell == nullptr) {
    CTS_LOG_ERROR << "Instance " << name << " has no liberty cell in the STA netlist.";
  } else if (lib_cell->isSequentialCell()) {
    inst_type = icts::InstType::kFlipFlop;
  } else if (lib_cell->isBuffer()) {
    inst_type = icts::InstType::kBuffer;
  } else if (lib_cell->isInverter()) {
    inst_type = icts::InstType::kInverter;
  } else if (lib_cell->isICG()) {
    inst_type = icts::InstType::kClockGate;
  }
  if (inst_type != icts::InstType::kUnknown) {
    return inst_type;
  }

  const std::string cell_master = lib_cell != nullptr ? lib_cell->get_cell_name() : "unknown";
  auto* idb_builder = dmInst->get_idb_builder();
  CTS_LOG_FATAL_IF(idb_builder == nullptr) << "iDB builder is null when querying instance type for " << name;
  auto* def_service = idb_builder->get_def_service();
  CTS_LOG_FATAL_IF(def_service == nullptr) << "DEF service is null when querying instance type for " << name;
  auto* idb_design = def_service->get_design();
  CTS_LOG_FATAL_IF(idb_design == nullptr) << "iDB design is null when querying instance type for " << name;
  auto* inst_list = idb_design->get_instance_list();
  CTS_LOG_FATAL_IF(inst_list == nullptr) << "iDB instance list is null when querying instance type for " << name;
  idb::IdbInstance* idb_inst = inst_list->find_instance(inst_name);
  CTS_LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name << " type is unknown (not found instance in iDB) which cell is "
                                        << cell_master;

  auto* pin_list = idb_inst->get_pin_list();
  CTS_LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name << " type is unknown (none pin in iDB) which cell is " << cell_master;

  std::size_t clock_input_pins = 0;
  for (auto* idb_pin : pin_list->get_pin_list()) {
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      continue;
    }
    auto* idb_net = idb_pin->get_net();
    CTS_LOG_FATAL_IF(idb_net == nullptr) << "Instance " << name << " pin " << idb_pin->get_pin_name()
                                         << " is not connected to any net, error in inst type judgement, which cell is " << cell_master;

    if (!idb_net->is_clock()) {
      CTS_LOG_WARNING << "Instance " << name << " pin " << idb_pin->get_pin_name() << " connected net " << idb_net->get_net_name()
                      << " is not clock net, warning in inst type judgement, which cell is " << cell_master;
      continue;
    }
    ++clock_input_pins;
    if (clock_input_pins > 1) {
      inst_type = icts::InstType::kMux;
      return inst_type;
    }
  }

  CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master;
  return inst_type;
}

bool STAAdapter::isFlipFlop(const std::string& inst_name) const
{
  auto name = inst_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  return GetStaEngine()->isSequentialCell(name.c_str());
}

bool STAAdapter::isClockNet(const std::string& net_name) const
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    CTS_LOG_ERROR << "STA netlist is not ready.";
    return false;
  }
  auto* sta_net = sta_netlist->findNet(net_name.c_str());
  if (sta_net == nullptr) {
    CTS_LOG_ERROR << "Net " << net_name << " is not found in the STA netlist.";
    return false;
  }
  return sta_net->isClockNet();
}

std::vector<std::pair<std::string, std::string>> STAAdapter::collectClockNetPairs() const
{
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    CTS_LOG_ERROR << "STA netlist is null when collecting clock nets.";
    return clock_net_pairs;
  }

  ista::Net* sta_net = nullptr;
  FOREACH_NET(sta_netlist, sta_net)
  {
    if (!sta_net->isClockNet()) {
      continue;
    }
    auto* sta_clock = GetStaEngine()->getPropClockOfNet(sta_net);
    if (sta_clock == nullptr) {
      CTS_LOG_WARNING << "Clock net \"" << sta_net->get_name() << "\" has no propagated clock in STA.";
      continue;
    }
    clock_net_pairs.emplace_back(sta_clock->get_clock_name(), sta_net->get_name());
  }
  return clock_net_pairs;
}

double STAAdapter::queryWireResistance(int routing_layer, double length, std::optional<double> wire_width) const
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (!idb_adapter) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getResistance(routing_layer, length, wire_width);
}

double STAAdapter::queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) const
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (!idb_adapter) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getCapacitance(routing_layer, length, wire_width);
}

double STAAdapter::queryCellOutPinCapLimit(const std::string& cell_master) const
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (!output) {
    return 0.0;
  }

  double cap = output->get_port_cap();
  auto cap_limit = output->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value()) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " output pin has no cap limit defined.";
    return cap;
  }
  cap = *cap_limit;

  auto* lib = lib_cell->get_owner_lib();
  CTS_LOG_WARNING_IF(lib == nullptr) << "Liberty cell " << cell_master << " has no owner liberty.";
  if (lib) {
    cap = ista::ConvertCapUnit(lib->get_cap_unit(), ista::CapacitiveUnit::kPF, cap);
  }
  return cap;
}

double STAAdapter::queryCellInPinSlewLimit(const std::string& cell_master) const
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (!input) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " has no input pin defined.";
    return 0.0;
  }

  double slew = 0.0;
  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (!slew_limit.has_value()) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " input pin has no slew limit defined.";
    return slew;
  }
  slew = *slew_limit;

  auto* lib = lib_cell->get_owner_lib();
  CTS_LOG_WARNING_IF(lib == nullptr) << "Liberty cell " << cell_master << " has no owner liberty.";
  if (lib) {
    slew = lib->convert_time_unit_to_ns(slew);
    slew = NS_TO_PS(slew);
  }
  return slew;
}

std::string STAAdapter::createCharInstance(const std::string& cell_master, const std::string& inst_name)
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  CTS_LOG_FATAL_IF(lib_cell == nullptr) << "Cannot find liberty cell: " << cell_master;
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when creating characterization instance.";
  auto* inst = adapter->createInstance(lib_cell, inst_name.c_str());
  CTS_LOG_FATAL_IF(inst == nullptr) << "Failed to create instance: " << inst_name;
  auto* ista = GetStaEngine()->get_ista();
  if (ista->isBuildGraph()) {
    auto& the_graph = ista->get_graph();
    ista::StaBuildGraph build_graph;
    build_graph.buildInst(&the_graph, inst);
  }
  return inst_name;
}

std::string STAAdapter::createCharNet(const std::string& net_name)
{
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when creating characterization net.";
  auto* net = adapter->createNet(net_name.c_str(), nullptr);
  CTS_LOG_FATAL_IF(net == nullptr) << "Failed to create net: " << net_name;
  return net_name;
}

void STAAdapter::attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name)
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto* inst = sta_netlist->findInstance(inst_name.c_str());
  CTS_LOG_FATAL_IF(inst == nullptr) << "Cannot find instance: " << inst_name;
  auto* net = sta_netlist->findNet(net_name.c_str());
  CTS_LOG_FATAL_IF(net == nullptr) << "Cannot find net: " << net_name;
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when attaching characterization pin.";
  adapter->attach(inst, port_name.c_str(), net);
}

void STAAdapter::buildCharRcTree(const std::string& net_name, double wire_res, double wire_cap, double load_cap)
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto* net = sta_netlist->findNet(net_name.c_str());
  CTS_LOG_FATAL_IF(net == nullptr) << "Cannot find net for RC tree: " << net_name;

  GetStaEngine()->initRcTree(net);

  auto* driver_pin = net->getDriver();
  CTS_LOG_FATAL_IF(driver_pin == nullptr) << "Net " << net_name << " has no driver pin";

  auto* driver_node = GetStaEngine()->makeOrFindRCTreeNode(driver_pin);
  auto load_pins = net->getLoads();
  for (auto* load_pin : load_pins) {
    auto* load_node = GetStaEngine()->makeOrFindRCTreeNode(load_pin);
    GetStaEngine()->makeResistor(net, driver_node, load_node, wire_res);
    GetStaEngine()->incrCap(driver_node, wire_cap / 2.0, true);
    GetStaEngine()->incrCap(load_node, wire_cap / 2.0 + load_cap, true);
  }

  GetStaEngine()->updateRCTreeInfo(net);
}

void STAAdapter::createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns)
{
  auto* ista = GetStaEngine()->get_ista();
  auto& the_graph = ista->get_graph();

  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto match_pins = sta_netlist->findPin(source_pin_full_name.c_str(), false, false);
  CTS_LOG_FATAL_IF(match_pins.empty()) << "Cannot find pin for clock source: " << source_pin_full_name;

  auto the_vertex = the_graph.findVertex(match_pins.front());
  CTS_LOG_FATAL_IF(!the_vertex) << "Cannot find vertex for clock source: " << source_pin_full_name;

  int period_ps = NS_TO_PS(period_ns);
  auto sta_clock = std::make_unique<ista::StaClock>(clock_name.c_str(), ista::StaClock::ClockType::kPropagated, period_ps);

  ista::StaWaveForm wave_form;
  wave_form.addWaveEdge(0);
  wave_form.addWaveEdge(period_ps / 2);
  sta_clock->set_wave_form(std::move(wave_form));
  sta_clock->addVertex(*the_vertex);
  ista->addClock(std::move(sta_clock));
}

void STAAdapter::destroyCharClock()
{
  auto* ista = GetStaEngine()->get_ista();
  ista->resetSdcConstrain();
  ista->resetConstraint();
  const char* sdc_path = GetDbConfig().get_sdc_path().c_str();
  ista->readSdc(sdc_path);
}

void STAAdapter::setCharInputSlew(const std::string& pin_full_name, double slew_ns)
{
  auto* ista = GetStaEngine()->get_ista();
  auto& the_graph = ista->get_graph();

  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto match_pins = sta_netlist->findPin(pin_full_name.c_str(), false, false);
  CTS_LOG_FATAL_IF(match_pins.empty()) << "Cannot find pin: " << pin_full_name;

  auto the_vertex = the_graph.findVertex(match_pins.front());
  CTS_LOG_FATAL_IF(!the_vertex) << "Cannot find vertex for pin: " << pin_full_name;

  auto* vertex = *the_vertex;
  int slew_fs = NS_TO_FS(slew_ns);
  auto* rise_slew_data = new ista::StaSlewData(ista::AnalysisMode::kMax, ista::TransType::kRise, vertex, slew_fs);
  vertex->addData(rise_slew_data);
  auto* fall_slew_data = new ista::StaSlewData(ista::AnalysisMode::kMax, ista::TransType::kFall, vertex, slew_fs);
  vertex->addData(fall_slew_data);
}

void STAAdapter::updateTiming()
{
  GetStaEngine()->updateTiming();
}

double STAAdapter::queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name) const
{
  auto result = GetStaEngine()->getClockAT(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall, clock_name);
  if (!result.has_value()) {
    CTS_LOG_WARNING << "No clock arrival time at pin: " << pin_full_name << " for clock: " << clock_name;
    return 0.0;
  }
  return result.value();
}

double STAAdapter::queryCharSlew(const std::string& pin_full_name) const
{
  double rise_slew = GetStaEngine()->getSlew(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise);
  double fall_slew = GetStaEngine()->getSlew(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall);
  return (rise_slew + fall_slew) / 2.0;
}

double STAAdapter::queryCharInputPinCap(const std::string& cell_master) const
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (!input) {
    return 0.0;
  }
  double cap = input->get_port_cap();
  auto* lib = lib_cell->get_owner_lib();
  if (lib) {
    cap = ista::ConvertCapUnit(lib->get_cap_unit(), ista::CapacitiveUnit::kPF, cap);
  }
  return cap;
}

std::pair<std::string, std::string> STAAdapter::queryBufferPorts(const std::string& cell_master) const
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return {"", ""};
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  std::string in_name = input ? input->get_port_name() : "";
  std::string out_name = output ? output->get_port_name() : "";
  return {in_name, out_name};
}

void STAAdapter::destroyCharInstance(const std::string& inst_name)
{
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when destroying characterization instance.";
  adapter->deleteInstance(inst_name.c_str());
}

void STAAdapter::destroyCharNet(const std::string& net_name)
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto* net = sta_netlist->findNet(net_name.c_str());
  if (net) {
    auto* adapter = GetStaEngine()->getIDBAdapter();
    CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when destroying characterization net.";
    adapter->deleteNet(net);
  }
}

}  // namespace icts
