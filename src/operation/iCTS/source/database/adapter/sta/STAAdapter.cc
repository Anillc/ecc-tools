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
#include <array>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbPins.h"
#include "IdbUnits.h"
#include "PwrConfig.hh"
#include "PwrType.hh"
#include "TimingDBAdapter.hh"
#include "Type.hh"
#include "Vector.hh"
#include "api/Power.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "builder.h"
#include "config/Config.hh"
#include "core/PwrAnalysisData.hh"
#include "core/PwrCell.hh"
#include "core/PwrClock.hh"
#include "core/PwrGraph.hh"
#include "core/PwrVertex.hh"
#include "def_service.h"
#include "delay/ElmoreDelayCalc.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "dm_config.h"
#include "idm.h"
#include "liberty/Lib.hh"
#include "logger/Logger.hh"
#include "netlist/DesignObject.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sta/Sta.hh"
#include "sta/StaArc.hh"
#include "sta/StaBuildGraph.hh"
#include "sta/StaClock.hh"
#include "sta/StaData.hh"
#include "sta/StaGraph.hh"
#include "sta/StaVertex.hh"

namespace icts {
namespace {

constexpr int kStaThreadCount = 80;
constexpr int kCharStaThreadCount = 1;
constexpr int kWorstPathPerClock = 10;
constexpr double kHalfCapFactor = 2.0;
constexpr std::array<ista::LibTable::TableType, 4> kCharArcTableTypes = {
    ista::LibTable::TableType::kCellRise,
    ista::LibTable::TableType::kCellFall,
    ista::LibTable::TableType::kRiseTransition,
    ista::LibTable::TableType::kFallTransition,
};

auto GetDbConfig() -> decltype(auto)
{
  return (dmInst->get_config());
}

auto GetStaEngine() -> ista::TimingEngine*
{
  return ista::TimingEngine::getOrCreateTimingEngine();
}

auto GetTrackedCharClocks() -> std::unordered_set<ista::StaClock*>&
{
  static std::unordered_set<ista::StaClock*> tracked_char_clocks;
  return tracked_char_clocks;
}

auto ClearTrackedCharClocks() -> void
{
  GetTrackedCharClocks().clear();
}

auto RemoveTrackedCharClocks() -> void
{
  auto& tracked_char_clocks = GetTrackedCharClocks();
  if (tracked_char_clocks.empty()) {
    return;
  }

  auto* ista = GetStaEngine()->get_ista();
  if (ista == nullptr) {
    tracked_char_clocks.clear();
    return;
  }

  auto& clocks = ista->get_clocks();
  auto erase_result = std::ranges::remove_if(clocks, [&tracked_char_clocks](const std::unique_ptr<ista::StaClock>& clock) -> bool {
    return clock != nullptr && tracked_char_clocks.contains(clock.get());
  });
  clocks.erase(erase_result.begin(), clocks.end());
  tracked_char_clocks.clear();
}

auto ConfigureStaWorkspace(ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void
{
  auto sta_work_dir = std::filesystem::path(CONFIG_INST.get_work_dir()).append(workspace_dir_name).string();
  if (!std::filesystem::exists(sta_work_dir)) {
    std::filesystem::create_directories(sta_work_dir);
  }
  timing_engine->set_design_work_space(sta_work_dir.c_str());
}

auto BuildStaLibPathList() -> std::vector<const char*>
{
  std::vector<const char*> lib_paths;
  std::ranges::transform(GetDbConfig().get_lib_paths(), std::back_inserter(lib_paths),
                         [](const std::string& lib_path) -> auto { return lib_path.c_str(); });
  return lib_paths;
}

auto BuildConfiguredBufferCellSet() -> std::set<std::string>
{
  return std::set<std::string>(std::ranges::begin(CONFIG_INST.get_buffer_types()), std::ranges::end(CONFIG_INST.get_buffer_types()));
}

auto InstallTimingIDBAdapter(ista::TimingEngine* timing_engine) -> void
{
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(timing_engine->get_ista());
  db_adapter->set_idb(dmInst->get_idb_builder());
  timing_engine->set_db_adapter(std::move(db_adapter));
}

auto LoadConfiguredLiberty(ista::TimingEngine* timing_engine) -> void
{
  timing_engine->get_ista()->addLinkCells(BuildConfiguredBufferCellSet());
  auto lib_paths = BuildStaLibPathList();
  timing_engine->readLiberty(lib_paths);
}

auto LoadConfiguredSdc(ista::TimingEngine* timing_engine) -> void
{
  const auto& sdc_path = GetDbConfig().get_sdc_path();
  if (sdc_path.empty()) {
    CTS_LOG_WARNING << "STA SDC path is empty; skip readSdc during initialization.";
    return;
  }
  timing_engine->readSdc(sdc_path.c_str());
}

auto RebuildCharPowerContext() -> ipower::Power*
{
  ipower::Power::destroyPower();

  auto* ista = GetStaEngine()->get_ista();
  if (ista == nullptr) {
    CTS_LOG_ERROR << "Characterization power setup failed: iSTA is not ready.";
    return nullptr;
  }

  auto* fastest_clock = ista->getFastestClock();
  if (fastest_clock == nullptr) {
    CTS_LOG_WARNING << "Characterization power setup skipped: no propagated clock is available.";
    return nullptr;
  }

  auto clocks = ista->getClocks();
  if (clocks.empty()) {
    CTS_LOG_WARNING << "Characterization power setup skipped: iSTA has no clock objects.";
    return nullptr;
  }

  auto* power = ipower::Power::getOrCreatePower(&(ista->get_graph()));
  ipower::PwrClock pwr_fastest_clock(fastest_clock->get_clock_name(), fastest_clock->getPeriodNs());
  power->setupClock(std::move(pwr_fastest_clock), std::move(clocks));
  power->buildGraph();
  power->buildSeqGraph();
  power->initToggleSPData();
  return power;
}

auto PrimeCharPower(ipower::Power* power) -> bool
{
  return power != nullptr && power->isBuildGraph() != 0U;
}

auto FindStaPins(const std::string& pin_full_name) -> std::vector<ista::DesignObject*>
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    CTS_LOG_WARNING << "STA netlist is null when querying pin " << pin_full_name;
    return {};
  }
  return sta_netlist->findPin(pin_full_name.c_str(), false, false);
}

auto FindStaPin(const std::string& pin_full_name) -> ista::Pin*
{
  auto match_pins = FindStaPins(pin_full_name);
  if (match_pins.empty()) {
    return nullptr;
  }
  return dynamic_cast<ista::Pin*>(match_pins.front());
}

auto FindStaVertex(const std::string& pin_full_name) -> ista::StaVertex*
{
  auto* pin = FindStaPin(pin_full_name);
  if (pin == nullptr) {
    return nullptr;
  }

  auto& the_graph = GetStaEngine()->get_ista()->get_graph();
  const auto the_vertex = the_graph.findVertex(pin);
  return the_vertex.has_value() ? *the_vertex : nullptr;
}

template <class PowerDataT>
auto SumInstPowerData(const std::vector<std::unique_ptr<PowerDataT>>& power_datas, const std::unordered_set<std::string>& inst_names)
    -> double
{
  if (inst_names.empty()) {
    return 0.0;
  }

  double power_sum_w = 0.0;
  for (const auto& power_data : power_datas) {
    if (power_data == nullptr) {
      continue;
    }
    auto* inst = dynamic_cast<ista::Instance*>(power_data->get_design_obj());
    if (inst != nullptr && inst_names.contains(inst->get_name())) {
      power_sum_w += power_data->getPowerDataValue();
    }
  }
  return power_sum_w;
}

auto CalcSelectedNetSwitchPower(ipower::Power* power, const std::unordered_set<std::string>& net_names) -> double
{
  if (power == nullptr || net_names.empty()) {
    return 0.0;
  }

  auto& power_graph = power->get_power_graph();
  auto* sta_graph = power_graph.get_sta_graph();
  if (sta_graph == nullptr) {
    return 0.0;
  }

  auto* netlist = sta_graph->get_nl();
  if (netlist == nullptr) {
    return 0.0;
  }

  double switch_power_w = 0.0;
  for (const auto& net_name : net_names) {
    auto* net = netlist->findNet(net_name.c_str());
    if (net == nullptr || net->getLoads().empty()) {
      continue;
    }

    auto* driver_obj = net->getDriver();
    if (driver_obj == nullptr) {
      continue;
    }
    if (driver_obj->isPort() != 0U && net->getLoads().size() == 1U && net->getLoads().front()->isPort() != 0U) {
      continue;
    }

    auto driver_sta_vertex = sta_graph->findVertex(driver_obj);
    if (!driver_sta_vertex.has_value()) {
      continue;
    }

    auto* driver_pwr_vertex = power_graph.staToPwrVertex(*driver_sta_vertex);
    if (driver_pwr_vertex == nullptr) {
      continue;
    }

    const auto driver_voltage = driver_pwr_vertex->getDriveVoltage();
    if (!driver_voltage.has_value()) {
      continue;
    }

    const double toggle = driver_pwr_vertex->getToggleData(std::nullopt);
    const double net_cap = (*driver_sta_vertex)->getNetLoad();
    const double switch_power_mw = c_switch_power_K * toggle * net_cap * driver_voltage.value() * driver_voltage.value();
    switch_power_w += switch_power_mw / static_cast<double>(ipower::g_mw2w);
  }

  return switch_power_w;
}

auto FilterPowerCells(ipower::Power* power, const std::unordered_set<std::string>& inst_names) -> void
{
  if (power == nullptr || inst_names.empty()) {
    return;
  }

  auto& power_cells = power->get_power_graph().get_cells();
  auto erase_result = std::ranges::remove_if(power_cells, [&inst_names](const std::unique_ptr<ipower::PwrCell>& cell) -> bool {
    return cell == nullptr || !inst_names.contains(cell->get_design_inst()->get_name());
  });
  power_cells.erase(erase_result.begin(), erase_result.end());
}

auto AnnotateCharSourceInputPower(ipower::Power* power, const std::optional<std::string>& source_input_pin_full_name) -> void
{
  if (power == nullptr || !source_input_pin_full_name.has_value() || source_input_pin_full_name->empty()) {
    return;
  }

  auto* source_vertex = FindStaVertex(*source_input_pin_full_name);
  if (source_vertex == nullptr) {
    CTS_LOG_WARNING << "Characterization power source pin is not found: " << *source_input_pin_full_name;
    return;
  }

  auto* pwr_vertex = power->get_power_graph().staToPwrVertex(source_vertex);
  if (pwr_vertex == nullptr) {
    CTS_LOG_WARNING << "Characterization power source vertex is not found in iPA graph: " << *source_input_pin_full_name;
    return;
  }

  const double clock_period_ns = power->get_power_graph().get_fastest_clock().get_clock_period_ns();
  if (clock_period_ns <= 0.0) {
    CTS_LOG_WARNING << "Characterization power source pin cannot be annotated because clock period is invalid.";
    return;
  }

  auto* fastest_clock = &(power->get_power_graph().get_fastest_clock());
  pwr_vertex->addData(c_default_clock_toggle / clock_period_ns, c_default_clock_sp, ipower::PwrDataSource::kClockPropagation,
                      fastest_clock);
}

auto RemoveGraphArc(ista::StaGraph& the_graph, ista::StaArc* the_arc) -> void
{
  if (the_arc == nullptr) {
    return;
  }
  the_arc->get_src()->removeSrcArc(the_arc);
  the_arc->get_snk()->removeSnkArc(the_arc);
  the_graph.removeArc(the_arc);
}

auto FindBufferArcSet(ista::LibCell* lib_cell) -> std::optional<ista::LibArcSet*>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr || output == nullptr) {
    return std::nullopt;
  }

  auto timing_arc_set = lib_cell->findLibertyArcSet(input->get_port_name(), output->get_port_name(), ista::LibArc::TimingType::kComb);
  if (!timing_arc_set.has_value()) {
    timing_arc_set = lib_cell->findLibertyArcSet(input->get_port_name(), output->get_port_name());
  }
  return timing_arc_set;
}

auto ConvertAxisValue(ista::LibLibrary* owner_lib, ista::LibLutTableTemplate::Variable variable, double axis_value) -> double
{
  if (owner_lib == nullptr) {
    return 0.0;
  }

  switch (variable) {
    case ista::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
    case ista::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE:
      return ista::ConvertCapUnit(owner_lib->get_cap_unit(), ista::CapacitiveUnit::kPF, axis_value);
    case ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
    case ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
    case ista::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
    case ista::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
      return owner_lib->convert_time_unit_to_ns(axis_value);
    default:
      return 0.0;
  }
}

auto ConvertLibCapToPf(ista::LibCell* lib_cell, double cap_value) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return cap_value;
  }
  return ista::ConvertCapUnit(owner_lib->get_cap_unit(), ista::CapacitiveUnit::kPF, cap_value);
}

auto ConvertLibTimeToNs(ista::LibCell* lib_cell, double time_value) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return time_value;
  }
  return owner_lib->convert_time_unit_to_ns(time_value);
}

auto ConvertPfLoadToLibUnit(ista::LibCell* lib_cell, double load_pf) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return load_pf;
  }

  if (owner_lib->get_cap_unit() == ista::CapacitiveUnit::kFF) {
    return PF_TO_FF(load_pf);
  }
  if (owner_lib->get_cap_unit() == ista::CapacitiveUnit::kPF) {
    return load_pf;
  }
  if (owner_lib->get_cap_unit() == ista::CapacitiveUnit::kF) {
    return PF_TO_F(load_pf);
  }
  return load_pf;
}

auto QueryOutputNetLoadPf(ista::Pin* output_pin, ista::TransType trans_type) -> double
{
  auto* output_net = output_pin != nullptr ? output_pin->get_net() : nullptr;
  if (output_net == nullptr) {
    return 0.0;
  }

  auto* rc_net = GetStaEngine()->get_ista()->getRcNet(output_net);
  return rc_net != nullptr ? rc_net->load(ista::AnalysisMode::kMax, trans_type) : output_net->getLoad(ista::AnalysisMode::kMax, trans_type);
}

auto AddCharSlewData(ista::StaVertex* vertex, ista::TransType trans_type, double slew_ns,
                     std::unique_ptr<ista::LibCurrentData> output_current_data = nullptr) -> void
{
  CTS_LOG_FATAL_IF(vertex == nullptr) << "Null STA vertex when installing characterization slew data.";
  const int slew_fs = static_cast<int>(NS_TO_FS(slew_ns));
  auto slew_data = std::make_unique<ista::StaSlewData>(ista::AnalysisMode::kMax, trans_type, vertex, slew_fs);
  slew_data->set_output_current_data(std::move(output_current_data));
  vertex->addData(slew_data.release());
}

auto QueryBufferTableAxisMax(const std::string& cell_master, std::initializer_list<ista::LibLutTableTemplate::Variable> target_variables)
    -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }

  auto timing_arc_set = FindBufferArcSet(lib_cell);
  if (!timing_arc_set.has_value() || timing_arc_set.value() == nullptr || timing_arc_set.value()->get_arcs().empty()) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " has no buffer timing arc set.";
    return 0.0;
  }

  auto* owner_lib = lib_cell->get_owner_lib();
  CTS_LOG_WARNING_IF(owner_lib == nullptr) << "Liberty cell " << cell_master << " has no owner liberty.";
  if (owner_lib == nullptr) {
    return 0.0;
  }

  auto is_target_variable = [&target_variables](ista::LibLutTableTemplate::Variable variable) -> bool {
    return std::ranges::find(target_variables, variable) != target_variables.end();
  };

  double min_axis_max = std::numeric_limits<double>::infinity();
  bool found_axis = false;

  for (const auto& timing_arc_holder : timing_arc_set.value()->get_arcs()) {
    auto* timing_arc = timing_arc_holder.get();
    auto* delay_model = timing_arc != nullptr ? dynamic_cast<ista::LibDelayTableModel*>(timing_arc->get_table_model()) : nullptr;
    if (delay_model == nullptr) {
      continue;
    }

    for (const auto table_type : kCharArcTableTypes) {
      auto* table = delay_model->getTable(static_cast<int>(table_type));
      if (table == nullptr || table->getAxesSize() == 0U) {
        continue;
      }

      auto* table_template = table->get_table_template();
      if (table_template == nullptr) {
        continue;
      }

      auto inspect_axis = [&](std::optional<ista::LibLutTableTemplate::Variable> variable, unsigned axis_index) -> void {
        if (!variable.has_value() || !is_target_variable(*variable) || axis_index >= table->getAxesSize()) {
          return;
        }

        auto& axis_values = table->getAxis(axis_index).get_axis_values();
        if (axis_values.empty()) {
          return;
        }

        const double converted_axis_max = ConvertAxisValue(owner_lib, *variable, axis_values.back()->getFloatValue());
        if (converted_axis_max > 0.0) {
          min_axis_max = std::min(min_axis_max, converted_axis_max);
          found_axis = true;
        }
      };

      inspect_axis(table_template->get_template_variable1(), 0U);
      inspect_axis(table_template->get_template_variable2(), 1U);
    }
  }

  if (!found_axis) {
    return 0.0;
  }

  return min_axis_max;
}

}  // namespace

auto STAAdapter::init() -> void
{
  auto& adapter = getInst();
  adapter._is_char_only_active = false;
  adapter._char_power_inst_names.clear();
  adapter._char_power_net_names.clear();
  adapter._char_power_source_input_pin_full_name.reset();
  adapter._last_char_power_w = 0.0;
  ClearTrackedCharClocks();
  ipower::Power::destroyPower();
  ista::TimingEngine::destroyTimingEngine();
  auto* timing_engine = GetStaEngine();

  InstallTimingIDBAdapter(timing_engine);
  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
  LoadConfiguredLiberty(timing_engine);

  timing_engine->resetNetlist();
  timing_engine->resetGraph();
  timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  LoadConfiguredSdc(timing_engine);
  timing_engine->buildGraph();

  timing_engine->initRcTree();
  timing_engine->get_ista()->set_n_worst_path_per_clock(kWorstPathPerClock);
}

auto STAAdapter::initCharOnly() -> void
{
  auto& adapter = getInst();
  if (adapter._is_char_only_active) {
    CTS_LOG_WARNING << "initCharOnly called while char-only mode is already active; "
                    << "resetting the previous characterization state first.";
    finishCharOnly();
  }
  adapter._is_char_only_active = true;
  adapter._char_power_inst_names.clear();
  adapter._char_power_net_names.clear();
  adapter._char_power_source_input_pin_full_name.reset();
  adapter._last_char_power_w = 0.0;
  ClearTrackedCharClocks();
  ipower::Power::destroyPower();
  ista::TimingEngine::destroyTimingEngine();
  auto* timing_engine = GetStaEngine();

  CTS_LOG_WARNING << "initCharOnly rebuilds the global STA singleton for characterization. "
                     "Any previously cached full-design timing state is discarded.";

  InstallTimingIDBAdapter(timing_engine);
  LoadConfiguredLiberty(timing_engine);

  timing_engine->set_num_threads(kCharStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta_char");

  timing_engine->resetNetlist();
  timing_engine->resetGraph();
  timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  timing_engine->get_ista()->resetConstraint();
  LoadConfiguredSdc(timing_engine);
  timing_engine->resetGraphData();
  timing_engine->resetPathData();
  timing_engine->initRcTree();
  timing_engine->get_ista()->set_n_worst_path_per_clock(kWorstPathPerClock);
}

auto STAAdapter::queryInstType(const std::string& inst_name) -> icts::InstType
{
  auto inst_type = icts::InstType::kUnknown;

  auto name = inst_name;
  std::erase(name, '\\');
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

  constexpr std::ptrdiff_t clock_input_pin_limit = 1;
  const auto clock_input_pins = std::ranges::count_if(pin_list->get_pin_list(), [&](auto* idb_pin) -> bool {
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      return false;
    }
    auto* idb_net = idb_pin->get_net();
    CTS_LOG_FATAL_IF(idb_net == nullptr) << "Instance " << name << " pin " << idb_pin->get_pin_name()
                                         << " is not connected to any net, error in inst type judgement, which cell is " << cell_master;

    if (idb_net->is_clock() == 0U) {
      CTS_LOG_WARNING << "Instance " << name << " pin " << idb_pin->get_pin_name() << " connected net " << idb_net->get_net_name()
                      << " is not clock net, warning in inst type judgement, which cell is " << cell_master;
      return false;
    }
    return true;
  });
  if (clock_input_pins > clock_input_pin_limit) {
    inst_type = icts::InstType::kMux;
    return inst_type;
  }

  CTS_LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master;
  return inst_type;
}

auto STAAdapter::isFlipFlop(const std::string& inst_name) -> bool
{
  auto name = inst_name;
  std::erase(name, '\\');
  return GetStaEngine()->isSequentialCell(name.c_str()) != 0U;
}

auto STAAdapter::isClockNet(const std::string& net_name) -> bool
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

auto STAAdapter::collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>
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

auto STAAdapter::queryWireResistance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getResistance(routing_layer, length, wire_width);
}

auto STAAdapter::queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    CTS_LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getCapacitance(routing_layer, length, wire_width);
}

auto STAAdapter::queryCellOutPinCapLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (output == nullptr) {
    return 0.0;
  }

  auto cap_limit = output->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value()) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " output pin has no cap limit defined.";
    return 0.0;
  }
  return ConvertLibCapToPf(lib_cell, *cap_limit);
}

auto STAAdapter::queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(cell_master, {ista::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE,
                                               ista::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE});
}

auto STAAdapter::queryCellInPinSlewLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " has no input pin defined.";
    return 0.0;
  }

  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (!slew_limit.has_value()) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " input pin has no slew limit defined.";
    return 0.0;
  }
  return ConvertLibTimeToNs(lib_cell, *slew_limit);
}

auto STAAdapter::queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(
      cell_master,
      {ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION, ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION,
       ista::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME, ista::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION});
}

auto STAAdapter::queryCellHeightUm(const std::string& cell_master) -> double
{
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr || idb_layout->get_units() == nullptr) {
    CTS_LOG_WARNING << "iDB layout is not ready when querying cell height for " << cell_master;
    return 0.0;
  }

  auto* idb_master = idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    CTS_LOG_WARNING << "iDB cell master " << cell_master << " not found when querying cell height.";
    return 0.0;
  }

  const int dbu_per_micron = idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    CTS_LOG_WARNING << "Invalid DBU-per-micron value when querying cell height for " << cell_master;
    return 0.0;
  }

  return static_cast<double>(idb_master->get_height()) / static_cast<double>(dbu_per_micron);
}

auto STAAdapter::createCharInstance(const std::string& cell_master, const std::string& inst_name) -> std::string
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  CTS_LOG_FATAL_IF(lib_cell == nullptr) << "Cannot find liberty cell: " << cell_master;
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when creating characterization instance.";
  auto* inst = adapter->createInstance(lib_cell, inst_name.c_str());
  CTS_LOG_FATAL_IF(inst == nullptr) << "Failed to create instance: " << inst_name;
  auto* ista = GetStaEngine()->get_ista();
  auto& the_graph = ista->get_graph();
  ista::StaBuildGraph build_graph;
  build_graph.buildInst(&the_graph, inst);
  return inst_name;
}

auto STAAdapter::createCharNet(const std::string& net_name) -> std::string
{
  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when creating characterization net.";
  auto* net = adapter->createNet(net_name.c_str(), nullptr);
  CTS_LOG_FATAL_IF(net == nullptr) << "Failed to create net: " << net_name;
  return net_name;
}

auto STAAdapter::attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name) -> void
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

auto STAAdapter::buildCharNetGraph(const std::string& net_name) -> void
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  CTS_LOG_FATAL_IF(sta_netlist == nullptr) << "STA netlist is null when building characterization net graph.";

  auto* net = sta_netlist->findNet(net_name.c_str());
  CTS_LOG_FATAL_IF(net == nullptr) << "Cannot find net for characterization graph build: " << net_name;

  auto& the_graph = GetStaEngine()->get_ista()->get_graph();
  ista::StaBuildGraph build_graph;
  build_graph.buildNet(&the_graph, net);
}

auto STAAdapter::buildCharRcTree(const std::string& net_name, const CharRcTreeConfig& rc_tree_config) -> void
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
    GetStaEngine()->makeResistor(net, driver_node, load_node, rc_tree_config.wire_res);
    GetStaEngine()->incrCap(driver_node, rc_tree_config.wire_cap / kHalfCapFactor, true);
    GetStaEngine()->incrCap(load_node, (rc_tree_config.wire_cap / kHalfCapFactor) + rc_tree_config.load_cap, true);
  }

  GetStaEngine()->updateRCTreeInfo(net);
}

auto STAAdapter::createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns) -> void
{
  RemoveTrackedCharClocks();

  auto* ista = GetStaEngine()->get_ista();
  auto& the_graph = ista->get_graph();

  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto match_pins = sta_netlist->findPin(source_pin_full_name.c_str(), false, false);
  CTS_LOG_FATAL_IF(match_pins.empty()) << "Cannot find pin for clock source: " << source_pin_full_name;

  auto the_vertex = the_graph.findVertex(match_pins.front());
  CTS_LOG_FATAL_IF(!the_vertex) << "Cannot find vertex for clock source: " << source_pin_full_name;

  const int period_ps = static_cast<int>(NS_TO_PS(period_ns));
  auto sta_clock = std::make_unique<ista::StaClock>(clock_name.c_str(), ista::StaClock::ClockType::kPropagated, period_ps);

  ista::StaWaveForm wave_form;
  wave_form.addWaveEdge(0);
  wave_form.addWaveEdge(period_ps / 2);
  sta_clock->set_wave_form(std::move(wave_form));
  if (the_vertex.has_value()) {
    sta_clock->addVertex(*the_vertex);
  }
  ista->addClock(std::move(sta_clock));

  auto& clocks = ista->get_clocks();
  if (!clocks.empty() && clocks.back() != nullptr) {
    GetTrackedCharClocks().insert(clocks.back().get());
  }
}

auto STAAdapter::destroyCharClock() -> void
{
  RemoveTrackedCharClocks();
  GetStaEngine()->resetGraphData();
  GetStaEngine()->resetPathData();
}

auto STAAdapter::setCharInputSlew(const std::string& pin_full_name, double slew_ns) -> void
{
  auto* vertex = FindStaVertex(pin_full_name);
  CTS_LOG_FATAL_IF(vertex == nullptr) << "Cannot find vertex for pin: " << pin_full_name;
  AddCharSlewData(vertex, ista::TransType::kRise, slew_ns);
  AddCharSlewData(vertex, ista::TransType::kFall, slew_ns);
}

auto STAAdapter::setCharBufferInputSlew(const std::string& input_pin_full_name, const std::string& output_pin_full_name, double slew_ns)
    -> void
{
  setCharInputSlew(input_pin_full_name, slew_ns);

  auto* input_pin = FindStaPin(input_pin_full_name);
  auto* output_pin = FindStaPin(output_pin_full_name);
  CTS_LOG_FATAL_IF(input_pin == nullptr) << "Cannot find source input pin: " << input_pin_full_name;
  CTS_LOG_FATAL_IF(output_pin == nullptr) << "Cannot find source output pin: " << output_pin_full_name;

  auto* output_vertex = FindStaVertex(output_pin_full_name);
  CTS_LOG_FATAL_IF(output_vertex == nullptr) << "Cannot find source output vertex: " << output_pin_full_name;

  auto* source_inst = output_pin->get_own_instance();
  CTS_LOG_FATAL_IF(source_inst == nullptr) << "Source output pin has no owning instance: " << output_pin_full_name;
  CTS_LOG_FATAL_IF(input_pin->get_own_instance() != source_inst)
      << "Source input/output pins do not belong to the same instance: " << input_pin_full_name << " / " << output_pin_full_name;
  auto* lib_cell = source_inst->get_inst_cell();
  CTS_LOG_FATAL_IF(lib_cell == nullptr) << "Source instance has no liberty cell: " << source_inst->get_name();

  auto timing_arc_set = FindBufferArcSet(lib_cell);
  auto* source_arc_set = timing_arc_set.value_or(nullptr);
  CTS_LOG_FATAL_IF(source_arc_set == nullptr || source_arc_set->get_arcs().empty())
      << "Cannot resolve buffer timing arc for source instance " << source_inst->get_name();
  auto* lib_arc = source_arc_set->front();
  CTS_LOG_FATAL_IF(lib_arc == nullptr) << "Source buffer liberty arc is null for instance " << source_inst->get_name();
  const bool is_negative_arc = source_arc_set->isNegativeArc() != 0U;

  const auto apply_output_model = [&](ista::TransType input_trans_type) -> void {
    auto output_trans_type = input_trans_type;
    if (is_negative_arc) {
      output_trans_type = (input_trans_type == ista::TransType::kRise) ? ista::TransType::kFall : ista::TransType::kRise;
    }
    if (!source_arc_set->isMatchTimingType(output_trans_type)) {
      return;
    }

    const double output_load_pf = QueryOutputNetLoadPf(output_pin, output_trans_type);
    const double output_load = ConvertPfLoadToLibUnit(lib_cell, output_load_pf);
    const auto slew_values = source_arc_set->getSlewNs(input_trans_type, output_trans_type, slew_ns, output_load);
    if (slew_values.empty()) {
      CTS_LOG_WARNING << "Characterization source output slew lookup is empty for " << output_pin_full_name;
      return;
    }

    const double output_slew_ns = slew_values.front();
    auto output_current = lib_arc->getOutputCurrent(output_trans_type, slew_ns, output_load);
    AddCharSlewData(output_vertex, output_trans_type, output_slew_ns, std::move(output_current));
  };

  apply_output_model(ista::TransType::kRise);
  apply_output_model(ista::TransType::kFall);
}

auto STAAdapter::prepareCharTiming() -> void
{
  GetStaEngine()->prepareCharTiming();
}

auto STAAdapter::updateCharTiming() -> void
{
  GetStaEngine()->updateCharTiming();
}

auto STAAdapter::prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                                  std::optional<std::string> source_input_pin_full_name) -> bool
{
  auto& adapter = getInst();
  adapter._char_power_inst_names = inst_names;
  adapter._char_power_net_names = net_names;
  adapter._char_power_source_input_pin_full_name = std::move(source_input_pin_full_name);
  adapter._last_char_power_w = 0.0;

  auto* ista = GetStaEngine()->get_ista();
  if (ista == nullptr) {
    CTS_LOG_WARNING << "Characterization power setup skipped: iSTA is not ready.";
    return false;
  }

  if (adapter._char_power_inst_names.empty()) {
    CTS_LOG_WARNING << "Characterization power setup skipped: no selected instances are provided.";
    return false;
  }

  auto* fastest_clock = ista->getFastestClock();
  if (fastest_clock == nullptr) {
    CTS_LOG_WARNING << "Characterization power setup skipped: no propagated clock is available.";
    return false;
  }

  auto clocks = ista->getClocks();
  if (clocks.empty()) {
    CTS_LOG_WARNING << "Characterization power setup skipped: iSTA has no clock objects.";
    return false;
  }

  return true;
}

auto STAAdapter::updateCharPower() -> bool
{
  auto& adapter = getInst();
  auto* power = RebuildCharPowerContext();
  if (!PrimeCharPower(power)) {
    CTS_LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
    return false;
  }

  const std::unordered_set<std::string> inst_name_set(adapter._char_power_inst_names.begin(), adapter._char_power_inst_names.end());
  const std::unordered_set<std::string> net_name_set(adapter._char_power_net_names.begin(), adapter._char_power_net_names.end());

  AnnotateCharSourceInputPower(power, adapter._char_power_source_input_pin_full_name);
  FilterPowerCells(power, inst_name_set);

  if (power->calcLeakagePower() == 0U || power->calcInternalPower() == 0U) {
    CTS_LOG_WARNING << "Characterization power update skipped: selected-instance power calculation failed.";
    return false;
  }

  const double leakage_power_w = SumInstPowerData(power->get_leakage_powers(), inst_name_set);
  const double internal_power_w = SumInstPowerData(power->get_internal_powers(), inst_name_set);
  const double switch_power_w = CalcSelectedNetSwitchPower(power, net_name_set);
  adapter._last_char_power_w = leakage_power_w + internal_power_w + switch_power_w;
  return true;
}

auto STAAdapter::queryCharPower() -> double
{
  return getInst()._last_char_power_w;
}

auto STAAdapter::destroyCharPower() -> void
{
  auto& adapter = getInst();
  adapter._char_power_inst_names.clear();
  adapter._char_power_net_names.clear();
  adapter._char_power_source_input_pin_full_name.reset();
  adapter._last_char_power_w = 0.0;
  ipower::Power::destroyPower();
}

auto STAAdapter::finishCharOnly() -> void
{
  auto& adapter = getInst();
  if (!adapter._is_char_only_active) {
    return;
  }

  adapter._is_char_only_active = false;
  adapter._char_power_inst_names.clear();
  adapter._char_power_net_names.clear();
  adapter._char_power_source_input_pin_full_name.reset();
  adapter._last_char_power_w = 0.0;
  ipower::Power::destroyPower();
  RemoveTrackedCharClocks();
  ClearTrackedCharClocks();
  init();
}

auto STAAdapter::updateTiming() -> void
{
  GetStaEngine()->updateTiming();
}

auto STAAdapter::queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name) -> double
{
  const auto pick_max_value = [](const std::optional<double>& lhs, const std::optional<double>& rhs) -> double {
    const double lhs_value = lhs.has_value() ? lhs.value() : 0.0;
    const double rhs_value = rhs.has_value() ? rhs.value() : 0.0;
    return std::max(lhs_value, rhs_value);
  };

  const auto rise_clock_at
      = GetStaEngine()->getClockAT(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise, clock_name);
  const auto fall_clock_at
      = GetStaEngine()->getClockAT(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall, clock_name);
  double delay_ns = pick_max_value(rise_clock_at, fall_clock_at);
  if (delay_ns > 0.0) {
    return delay_ns;
  }

  const auto rise_at = GetStaEngine()->getAT(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise);
  const auto fall_at = GetStaEngine()->getAT(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall);
  delay_ns = pick_max_value(rise_at, fall_at);
  if (delay_ns > 0.0) {
    return delay_ns;
  }

  CTS_LOG_WARNING << "No characterization arrival time at pin: " << pin_full_name << " for clock: " << clock_name;
  return 0.0;
}

auto STAAdapter::queryCharSlew(const std::string& pin_full_name) -> double
{
  const double rise_slew = GetStaEngine()->getSlew(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kRise);
  const double fall_slew = GetStaEngine()->getSlew(pin_full_name.c_str(), ista::AnalysisMode::kMax, ista::TransType::kFall);
  if (rise_slew > 0.0 && fall_slew > 0.0) {
    return std::max(rise_slew, fall_slew);
  }
  const double slew_ns = rise_slew > 0.0 ? rise_slew : fall_slew;
  if (slew_ns == 0.0) {
    CTS_LOG_WARNING << "No characterization slew is available at pin: " << pin_full_name;
  }
  return slew_ns;
}

auto STAAdapter::queryCharInputPinCap(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return 0.0;
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    return 0.0;
  }
  return ConvertLibCapToPf(lib_cell, input->get_port_cap());
}

auto STAAdapter::queryPinCapacitance(const Pin* pin) -> double
{
  if (pin == nullptr) {
    CTS_LOG_WARNING << "Null pin provided when querying pin capacitance.";
    return 0.0;
  }

  auto* inst = pin->get_inst();
  const std::string pin_full_name = inst != nullptr ? (inst->get_name() + "/" + pin->get_name()) : pin->get_name();
  const double pin_cap = GetStaEngine()->getInstPinCapacitance(pin_full_name.c_str());

  return pin_cap;
}

auto STAAdapter::queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    CTS_LOG_WARNING << "Liberty cell " << cell_master << " not found.";
    return {"", ""};
  }
  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  const std::string in_name = input != nullptr ? input->get_port_name() : "";
  const std::string out_name = output != nullptr ? output->get_port_name() : "";
  return {in_name, out_name};
}

auto STAAdapter::destroyCharInstance(const std::string& inst_name) -> void
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  CTS_LOG_FATAL_IF(sta_netlist == nullptr) << "STA netlist is null when destroying characterization instance.";

  auto* inst = sta_netlist->findInstance(inst_name.c_str());
  if (inst == nullptr) {
    return;
  }

  auto* ista = GetStaEngine()->get_ista();
  if (ista->isBuildGraph()) {
    auto& the_graph = ista->get_graph();
    std::set<ista::StaArc*> arcs_to_remove;
    std::vector<ista::Pin*> inst_pins;

    ista::Pin* pin = nullptr;
    FOREACH_INSTANCE_PIN(inst, pin)
    {
      inst_pins.push_back(pin);
      auto the_vertex = the_graph.findVertex(pin);
      if (!the_vertex.has_value()) {
        continue;
      }

      for (auto* src_arc : (*the_vertex)->get_src_arcs()) {
        arcs_to_remove.insert(src_arc);
      }
      for (auto* snk_arc : (*the_vertex)->get_snk_arcs()) {
        arcs_to_remove.insert(snk_arc);
      }
    }

    for (auto* the_arc : arcs_to_remove) {
      RemoveGraphArc(the_graph, the_arc);
    }

    for (auto* pin_ptr : inst_pins) {
      auto the_vertex = the_graph.findVertex(pin_ptr);
      if (the_vertex.has_value()) {
        the_graph.removePinVertex(pin_ptr, *the_vertex);
      }
    }
  }

  auto* adapter = GetStaEngine()->getIDBAdapter();
  CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when destroying characterization instance.";
  adapter->deleteInstance(inst_name.c_str());
}

auto STAAdapter::destroyCharNet(const std::string& net_name) -> void
{
  auto* sta_netlist = GetStaEngine()->get_netlist();
  auto* net = sta_netlist->findNet(net_name.c_str());
  if (net != nullptr) {
    auto* ista = GetStaEngine()->get_ista();
    if (ista->isBuildGraph()) {
      auto& the_graph = ista->get_graph();
      std::set<ista::StaArc*> arcs_to_remove;

      auto collect_net_arcs = [&](auto* pin_or_port) -> void {
        if (pin_or_port == nullptr) {
          return;
        }
        auto the_vertex = the_graph.findVertex(pin_or_port);
        if (!the_vertex.has_value()) {
          return;
        }

        for (auto* src_arc : (*the_vertex)->get_src_arcs()) {
          auto* net_arc = dynamic_cast<ista::StaNetArc*>(src_arc);
          if (net_arc != nullptr && net_arc->get_net() == net) {
            arcs_to_remove.insert(net_arc);
          }
        }
        for (auto* snk_arc : (*the_vertex)->get_snk_arcs()) {
          auto* net_arc = dynamic_cast<ista::StaNetArc*>(snk_arc);
          if (net_arc != nullptr && net_arc->get_net() == net) {
            arcs_to_remove.insert(net_arc);
          }
        }
      };

      collect_net_arcs(net->getDriver());
      for (auto* load_pin : net->getLoads()) {
        collect_net_arcs(load_pin);
      }

      for (auto* the_arc : arcs_to_remove) {
        RemoveGraphArc(the_graph, the_arc);
      }

      ista->removeRcNet(net);
    }

    auto* adapter = GetStaEngine()->getIDBAdapter();
    CTS_LOG_FATAL_IF(adapter == nullptr) << "STA IDB adapter is not ready when destroying characterization net.";
    adapter->deleteNet(net);
  }
}

}  // namespace icts
