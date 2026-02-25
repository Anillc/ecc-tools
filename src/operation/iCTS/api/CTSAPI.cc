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
 * @file CTSAPI.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "CTSAPI.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

#include "Type.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/io/Wrapper.hh"
#include "feature_icts.h"
#include "idm.h"
#include "liberty/Lib.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "netlist/Port.hh"
#include "sdc/SdcSetInputTransition.hh"
#include "sta/StaGraph.hh"
#include "sta/StaVertex.hh"
#include "usage/usage.hh"
#include "utils/logger/Logger.hh"

namespace icts {
#define DBConfig (dmInst->get_config())
#define STAInst (ista::TimingEngine::getOrCreateTimingEngine())

void CTSAPI::runCTS()
{
  ieda::Stats stats;
  readData();
  // ctsFlow();
  // evaluate();

  CTS_LOG_INFO << "**Flow memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Flow elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::readData()
{
  ieda::Stats stats;

  // Get clock netlist from Config / STA
  if (CTSConfigInst.is_use_netlist()) {
    auto& net_list = CTSConfigInst.get_clock_netlist();
    for (auto& [clock_name, net_name] : net_list) {
      auto* clock = new icts::Clock(clock_name, net_name);
      CTSDesignInst.add_clock(clock);
    }
  } else {
    STAInst->updateTiming();
    auto* sta_netlist = STAInst->get_netlist();
    ista::Net* sta_net;
    FOREACH_NET(sta_netlist, sta_net)
    {
      if (sta_net->isClockNet()) {
        auto* sta_clock = STAInst->getPropClockOfNet(sta_net);
        auto* clock = new icts::Clock(sta_clock->get_clock_name(), sta_net->get_name());
        CTSDesignInst.add_clock(clock);
        CTS_LOG_INFO << "Clock [" << sta_clock->get_clock_name() << "] have net \"" << sta_net->get_name() << "\"";
      }
    }
  }
  // Get clock instance from DB
  CTSWrapperInst.read();
  // Summary clock distribution
  summaryClockDistribution();

  CTS_LOG_INFO << "**Read Data memory usage " << stats.memoryDelta() << "MB";
  CTS_LOG_INFO << "**Read Data elapsed time " << stats.elapsedRunTime() << "s";
}

void CTSAPI::summaryClockDistribution()
{
  CTS_LOG_INFO << "======== Clock Distribution Summary ========";
  std::unordered_map<std::string, std::vector<Clock*>> clock_map;
  for (auto* clock : CTSDesignInst.get_clocks()) {
    clock_map[clock->get_clock_name()].push_back(clock);
  }

  for (const auto& [clock_name, clocks] : clock_map) {
    const std::size_t num_nets = clocks.size();

    std::size_t num_ff_sinks = 0;
    std::size_t num_total_sinks = 0;
    std::size_t num_buffer_sinks = 0;
    std::size_t num_none_inst_sinks = 0;

    std::vector<Pin*> pins;
    for (const auto* clock : clocks) {
      std::ranges::copy(clock->get_loads(), std::back_inserter(pins));
    }

    for (const Pin* pin : pins) {
      ++num_total_sinks;

      const auto* inst = pin->get_inst();
      if (!inst) {
        ++num_none_inst_sinks;
      } else if (inst->is_flipflop()) {
        ++num_ff_sinks;
      } else {
        ++num_buffer_sinks;
      }
    }

    CTS_LOG_INFO << "Clock: " << clock_name << ", #Net: " << num_nets << ", #Total Sinks: " << num_total_sinks
                 << ", #FlipFlop Sinks: " << num_ff_sinks << ", #Buffer Sinks: " << num_buffer_sinks
                 << ", #None Inst Sinks: " << num_none_inst_sinks;
  }
  CTS_LOG_INFO << "============================================";
}

void CTSAPI::report(const std::string&)
{
  // TBD
}

void CTSAPI::resetAPI()
{
  CTSConfigInst.reset();
  CTSDesignInst.reset();
  CTSWrapperInst.reset();
  CTSLogInst.close();
}

void CTSAPI::init(const std::string& config_file, const std::string& work_dir)
{
  resetAPI();
  // Config
  CTSConfigInst.init(config_file);
  auto dir_str = work_dir.empty() ? CTSConfigInst.get_work_dir() : work_dir;
  auto dir = std::filesystem::path(dir_str);
  if (!std::filesystem::exists(dir)) {
    std::filesystem::create_directories(dir);
  }
  CTSConfigInst.set_work_dir(dir_str);
  CTSConfigInst.set_log_file(dir.append("cts.log").string());
  CTSConfigInst.set_gds_file(dir.append("cts.gds").string());
  auto def_path = dir.append("output");
  if (!std::filesystem::exists(def_path)) {
    std::filesystem::create_directories(def_path);
  }
  CTSConfigInst.set_output_def_path(def_path.string());

  // Logger
  CTSLogInst.set_log_file(CTSConfigInst.get_log_file());
  CTS_LOG_INFO << "Generate the report at " << ieda::Time::getNowWallTime();

  // DB Wrapper
  auto* idb_builder = dmInst->get_idb_builder();
  CTS_LOG_FATAL_IF(idb_builder == nullptr) << "idb builder is null";
  CTSWrapperInst.init(idb_builder);

  // STA
  initSTA();
}

ieda_feature::CTSSummary CTSAPI::outputSummary()
{
  // TBD
  return ieda_feature::CTSSummary{};
}

void CTSAPI::initSTA()
{
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(STAInst->get_ista());
  db_adapter->set_idb(dmInst->get_idb_builder());
  STAInst->set_db_adapter(std::move(db_adapter));
  auto sta_work_dir = std::filesystem::path(CTSConfigInst.get_work_dir()).append("sta").string();
  if (!std::filesystem::exists(sta_work_dir)) {
    std::filesystem::create_directories(sta_work_dir);
  }
  std::vector<const char*> lib_paths;
  std::ranges::transform(DBConfig.get_lib_paths(), std::back_inserter(lib_paths),
                         [](const std::string& lib_path) { return lib_path.c_str(); });
  STAInst->set_num_threads(80);
  STAInst->set_design_work_space(sta_work_dir.c_str());
  auto cell_set
      = std::set<std::string>(std::ranges::begin(CTSConfigInst.get_buffer_types()), std::ranges::end(CTSConfigInst.get_buffer_types()));
  STAInst->get_ista()->addLinkCells(cell_set);
  STAInst->readLiberty(lib_paths);

  STAInst->resetNetlist();
  STAInst->resetGraph();
  STAInst->get_db_adapter()->convertDBToTimingNetlist();
  const char* sdc_path = DBConfig.get_sdc_path().c_str();
  STAInst->readSdc(sdc_path);
  STAInst->buildGraph();

  STAInst->initRcTree();
  STAInst->get_ista()->set_n_worst_path_per_clock(10);
}

icts::InstType CTSAPI::queryInstType(const std::string& inst_name) const
{
  auto inst_type = icts::InstType::kUnknown;

  // Clock type query by STA
  auto name = inst_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  auto* sta_netlist = STAInst->get_netlist();
  auto* sta_inst = sta_netlist->findInstance(name.c_str());
  CTS_LOG_ERROR_IF(sta_inst == nullptr) << "Instance " << name << " is not found in the STA netlist.";
  auto* lib_cell = sta_inst->get_inst_cell();
  if (lib_cell->isSequentialCell()) {
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

  // TBD: judge MUX type
  auto* idb_design = CTSWrapperInst.get_idb_design();
  auto* inst_list = idb_design->get_instance_list();
  idb::IdbInstance* idb_inst = inst_list->find_instance(inst_name);
  CTS_LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name << " type is unknown (not found instance in iDB) which cell is "
                                        << lib_cell->get_cell_name();

  auto* pin_list = idb_inst->get_pin_list();
  CTS_LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name << " type is unknown (none pin in iDB) which cell is "
                                        << lib_cell->get_cell_name();

  std::size_t clock_input_pins = 0;
  for (auto* idb_pin : pin_list->get_pin_list()) {
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      continue;
    }
    auto* idb_net = idb_pin->get_net();
    CTS_LOG_FATAL_IF(idb_net == nullptr) << "Instance " << name << " pin " << idb_pin->get_pin_name()
                                         << " is not connected to any net, error in inst type judgement, which "
                                            "cell is "
                                         << lib_cell->get_cell_name();

    if (idb_net->is_clock() == false) {
      CTS_LOG_WARNING << "Instance " << name << " pin " << idb_pin->get_pin_name() << " connected net " << idb_net->get_net_name()
                      << " is not clock net, warning in inst type judgement, which cell is " << lib_cell->get_cell_name();
      continue;
    }
    ++clock_input_pins;
    if (clock_input_pins > 1) {
      inst_type = icts::InstType::kMux;
      return inst_type;
    }
  }

  CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown)
      << "Instance " << name << " type is unknown which cell is " << lib_cell->get_cell_name();
  return inst_type;
}

bool CTSAPI::isFlipFlop(const std::string& inst_name) const
{
  // remove all "\" in inst_name
  auto name = inst_name;
  name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
  return STAInst->isSequentialCell(name.c_str());
}

bool CTSAPI::isClockNet(const std::string& net_name) const
{
  auto* sta_net = STAInst->get_netlist()->findNet(net_name.c_str());
  CTS_LOG_ERROR_IF(sta_net == nullptr) << "Net " << net_name << " is not found in the STA netlist.";
  return sta_net->isClockNet();
}

double CTSAPI::queryWireResistance(int routing_layer, double length, double wire_width) const
{
  auto* idb_adapter = STAInst->getIDBAdapter();
  if (!idb_adapter) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  std::optional<double> width_opt = wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
  return idb_adapter->getResistance(routing_layer, length, width_opt);
}

double CTSAPI::queryWireCapacitance(int routing_layer, double length, double wire_width) const
{
  auto* idb_adapter = STAInst->getIDBAdapter();
  if (!idb_adapter) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  std::optional<double> width_opt = wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
  return idb_adapter->getCapacitance(routing_layer, length, width_opt);
}

double CTSAPI::queryCellOutPortCapLimit(const std::string& cell_name) const
{
  auto* lib_cell = STAInst->findLibertyCell(cell_name.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_name << " not found.";
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
  if (cap_limit.has_value() == false) {
    CTS_LOG_WARNING << "Liberty cell " << cell_name << " output port has no cap limit defined.";
    return cap;
  }
  cap = *cap_limit;

  // Convert to pF
  auto* lib = lib_cell->get_owner_lib();
  CTS_LOG_WARNING_IF(lib == nullptr) << "Liberty cell " << cell_name << " has no owner liberty.";
  if (lib) {
    cap = ista::ConvertCapUnit(lib->get_cap_unit(), ista::CapacitiveUnit::kPF, cap);
  }
  return cap;
}

double CTSAPI::queryCellInPortSlewLimit(const std::string& cell_name) const
{
  auto* lib_cell = STAInst->findLibertyCell(cell_name.c_str());
  if (!lib_cell) {
    CTS_LOG_WARNING << "Liberty cell " << cell_name << " not found.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (!input) {
    CTS_LOG_WARNING << "Liberty cell " << cell_name << " has no input port defined.";
    return 0.0;
  }

  double slew = 0.0;
  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (slew_limit.has_value() == false) {
    CTS_LOG_WARNING << "Liberty cell " << cell_name << " input port has no slew limit defined.";
    return slew;
  }
  slew = *slew_limit;

  // Convert to ps
  auto* lib = lib_cell->get_owner_lib();
  CTS_LOG_WARNING_IF(lib == nullptr) << "Liberty cell " << cell_name << " has no owner liberty.";
  if (lib) {
    slew = lib->convert_time_unit_to_ns(slew);
    slew = NS_TO_PS(slew);
  }

  return slew;
}

}  // namespace icts
