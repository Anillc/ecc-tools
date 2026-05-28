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
 * @file STAAdapterCharCircuit.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter characterization circuit construction implementation.
 */

#include <glog/logging.h>

#include <cmath>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "STAAdapter.hh"
#include "STAAdapterInternal.hh"
#include "Type.hh"
#include "api/TimingEngine.hh"
#include "liberty/Lib.hh"
#include "netlist/DesignObject.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sta/Sta.hh"
#include "sta/StaBuildGraph.hh"
#include "sta/StaClock.hh"
#include "sta/StaGraph.hh"

namespace icts {

auto STAAdapter::createCharInstance(const std::string& cell_master, const std::string& inst_name) -> std::string
{
  auto& adapter = getInst();
  LOG_FATAL_IF(!adapter._is_char_only_active) << "Characterization instance creation requires initCharOnly() first.";
  auto* lib_cell = sta_adapter_internal::GetStaEngine()->findLibertyCell(cell_master.c_str());
  LOG_FATAL_IF(lib_cell == nullptr) << "Failed to create characterization instance " << inst_name << ": liberty cell " << cell_master
                                    << " is not found.";

  ista::Instance char_inst(inst_name.c_str(), lib_cell);
  for (const auto& lib_port_ptr : lib_cell->get_cell_ports()) {
    auto* lib_port = lib_port_ptr.get();
    if (lib_port == nullptr) {
      continue;
    }
    char_inst.addPin(lib_port->get_port_name(), lib_port);
  }

  auto& created_inst = sta_adapter_internal::GetStaEngine()->get_netlist()->addInstance(std::move(char_inst));
  ista::StaBuildGraph build_graph;
  build_graph.buildInst(&(sta_adapter_internal::GetStaEngine()->get_ista()->get_graph()), &created_inst);
  return inst_name;
}

auto STAAdapter::createCharNet(const std::string& net_name) -> std::string
{
  auto& adapter = getInst();
  LOG_FATAL_IF(!adapter._is_char_only_active) << "Characterization net creation requires initCharOnly() first.";
  auto& created_net = sta_adapter_internal::GetStaEngine()->get_netlist()->addNet(ista::Net(net_name.c_str()));
  (void) created_net;
  return net_name;
}

auto STAAdapter::attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name) -> void
{
  auto* netlist = sta_adapter_internal::GetStaEngine()->get_netlist();
  auto* inst = netlist->findInstance(inst_name.c_str());
  LOG_FATAL_IF(inst == nullptr) << "Cannot attach characterization pin because instance is not found: " << inst_name;
  auto* net = netlist->findNet(net_name.c_str());
  LOG_FATAL_IF(net == nullptr) << "Cannot attach characterization pin because net is not found: " << net_name;

  auto pin = inst->getPin(port_name.c_str());
  LOG_FATAL_IF(!pin) << "Cannot attach characterization pin " << inst_name << "/" << port_name;
  net->addPinPort(*pin);
}

auto STAAdapter::buildCharNetGraph(const std::string& net_name) -> void
{
  auto* net = sta_adapter_internal::GetStaEngine()->get_netlist()->findNet(net_name.c_str());
  LOG_FATAL_IF(net == nullptr) << "Cannot build characterization graph because net is not found: " << net_name;
  ista::StaBuildGraph build_graph;
  build_graph.buildNet(&(sta_adapter_internal::GetStaEngine()->get_ista()->get_graph()), net);
}

auto STAAdapter::buildCharRcTree(const std::string& net_name, double wire_res, double wire_cap, double load_cap) -> void
{
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  auto* net = timing_engine->get_netlist()->findNet(net_name.c_str());
  LOG_FATAL_IF(net == nullptr) << "Characterization net " << net_name << " is not found for RC tree.";

  timing_engine->resetRcTree(net);
  timing_engine->initRcTree(net);

  auto* driver_pin = net->getDriver();
  LOG_FATAL_IF(driver_pin == nullptr) << "Characterization net " << net_name << " has no driver pin.";

  auto* driver_node = timing_engine->makeOrFindRCTreeNode(driver_pin);
  auto load_pins = net->getLoads();
  for (auto* load_pin : load_pins) {
    auto* load_node = timing_engine->makeOrFindRCTreeNode(load_pin);
    timing_engine->makeResistor(net, driver_node, load_node, wire_res);
    timing_engine->incrCap(driver_node, wire_cap / 2.0, true);
    timing_engine->incrCap(load_node, (wire_cap / 2.0) + load_cap, true);
  }
  timing_engine->updateRCTreeInfo(net);
}

auto STAAdapter::createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns) -> void
{
  const auto source_pin_objs = sta_adapter_internal::GetStaEngine()->get_netlist()->findPin(source_pin_full_name.c_str(), false, false);
  LOG_FATAL_IF(source_pin_objs.empty()) << "Cannot create characterization clock because source pin is not found: " << source_pin_full_name;
  auto* source_pin = dynamic_cast<ista::Pin*>(source_pin_objs.front());
  LOG_FATAL_IF(source_pin == nullptr) << "Cannot create characterization clock because source object is not a pin: "
                                      << source_pin_full_name;
  auto source_vertex = sta_adapter_internal::GetStaEngine()->get_ista()->get_graph().findVertex(source_pin);
  LOG_FATAL_IF(!source_vertex) << "Cannot create characterization clock because source vertex is not found: " << source_pin_full_name;

  const int period_ps = static_cast<int>(period_ns * ista::g_ns2ps);
  auto sta_clock = std::make_unique<ista::StaClock>(clock_name.c_str(), ista::StaClock::ClockType::kPropagated, period_ps);
  ista::StaWaveForm wave_form;
  wave_form.addWaveEdge(0);
  wave_form.addWaveEdge(period_ps / 2);
  sta_clock->set_wave_form(std::move(wave_form));
  sta_clock->addVertex(*source_vertex);

  sta_adapter_internal::GetStaEngine()->get_ista()->addClock(std::move(sta_clock));
}

auto STAAdapter::destroyCharClock() -> void
{
  sta_adapter_internal::GetStaEngine()->get_ista()->clearClocks();
  getInst().resetCharTimingState();
}

auto STAAdapter::resetCharContext() -> void
{
  auto& adapter = getInst();
  adapter.resetStaTransientState();
  auto* timing_engine = sta_adapter_internal::GetStaEngine();
  timing_engine->set_num_threads(adapter._is_char_only_active ? sta_adapter_internal::kCharStaThreadCount
                                                              : sta_adapter_internal::kStaThreadCount);
  sta_adapter_internal::ConfigureStaWorkspace(timing_engine, adapter._is_char_only_active ? "sta_char" : "sta");
}

}  // namespace icts
