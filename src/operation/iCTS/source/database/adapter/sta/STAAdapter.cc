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

#include <glog/logging.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
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
#include "Log.hh"
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
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "netlist/DesignObject.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "sta/Sta.hh"
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
constexpr double kMilliOhmPerOhm = 1000.0;
constexpr const char* kStaAdapterOwner = "STAAdapter";
constexpr std::array<ista::LibTable::TableType, 4> kCharArcTableTypes = {
    ista::LibTable::TableType::kCellRise,
    ista::LibTable::TableType::kCellFall,
    ista::LibTable::TableType::kRiseTransition,
    ista::LibTable::TableType::kFallTransition,
};

struct WireRcProbe
{
  int routing_layer = 0;
  std::optional<double> wire_width_um = std::nullopt;
  double query_length_um = 1.0;
  double resistance_per_um_ohm = 0.0;
  double capacitance_per_um_pf = 0.0;
  bool queried = false;
  bool has_diagnostic = false;
  schema::DiagnosticLevel diagnostic_level = schema::DiagnosticLevel::kInfo;
  std::string diagnostic_summary;
  std::string status = "not_queried";
  std::string detail;
};

auto GetDbConfig() -> decltype(auto)
{
  return (dmInst->get_config());
}

auto GetStaEngine() -> ista::TimingEngine*
{
  return ista::TimingEngine::getOrCreateTimingEngine();
}

auto BuildCharPower() -> IctsCharPowerPtr
{
  auto* timing_engine = GetStaEngine();
  auto* ista = timing_engine != nullptr ? timing_engine->get_ista() : nullptr;
  auto* char_graph = ista != nullptr ? &(ista->get_graph()) : nullptr;
  if (char_graph == nullptr) {
    LOG_WARNING << "Characterization power setup skipped: char-only STA graph is not ready.";
    return nullptr;
  }

  auto clocks = ista->getClocks();
  if (clocks.empty()) {
    LOG_WARNING << "Characterization power setup skipped: char-only STA has no clock objects.";
    return nullptr;
  }

  ista::StaClock* fastest_clock = nullptr;
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    if (fastest_clock == nullptr || clock->getPeriodNs() < fastest_clock->getPeriodNs()) {
      fastest_clock = clock;
    }
  }
  if (fastest_clock == nullptr) {
    LOG_WARNING << "Characterization power setup skipped: propagated char-only clock is null.";
    return nullptr;
  }

  ipower::Power::destroyPower();
  auto* power = ipower::Power::getOrCreatePower(char_graph);
  if (power == nullptr) {
    LOG_WARNING << "Characterization power setup skipped: failed to create iPA context.";
    return nullptr;
  }

  ipower::PwrClock pwr_fastest_clock(fastest_clock->get_clock_name(), fastest_clock->getPeriodNs());
  power->setupClock(std::move(pwr_fastest_clock), std::move(clocks));
  power->buildGraph();
  power->buildSeqGraph();
  if (power->initToggleSPData() == 0U) {
    return nullptr;
  }
  return IctsCharPowerPtr(power, [](ipower::Power*) -> void {});
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

auto InstallTimingIDBAdapter(ista::TimingEngine* timing_engine) -> void
{
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(timing_engine->get_ista());
  db_adapter->set_idb(dmInst->get_idb_builder());
  timing_engine->set_db_adapter(std::move(db_adapter));
}

auto LoadConfiguredLiberty(ista::TimingEngine* timing_engine) -> void
{
  auto lib_paths = BuildStaLibPathList();
  timing_engine->readLiberty(lib_paths);
  // Link all liberty cells once during base STA setup so lightweight cell queries
  // can use liberty metadata without forcing full-design DB conversion.
  timing_engine->get_ista()->linkLibertys();
}

auto LoadConfiguredSdcIfPresent(ista::TimingEngine* timing_engine) -> void
{
  const auto& sdc_path = GetDbConfig().get_sdc_path();
  if (sdc_path.empty()) {
    LOG_WARNING << "STA SDC path is empty; skip readSdc during full-design timing preparation.";
    return;
  }
  if (!std::filesystem::exists(sdc_path)) {
    LOG_WARNING << "STA SDC path does not exist: " << sdc_path << ". Skip readSdc during full-design timing preparation.";
    return;
  }
  timing_engine->readSdc(sdc_path.c_str());
}

auto HasFullDesignTimingContext() -> bool
{
  auto* timing_engine = GetStaEngine();
  return timing_engine->get_db_adapter() != nullptr && timing_engine->isBuildGraph();
}

auto HasStaBaseContext() -> bool
{
  auto* timing_engine = GetStaEngine();
  auto* ista = timing_engine->get_ista();
  return timing_engine->get_db_adapter() != nullptr && ista != nullptr && !ista->getAllLib().empty();
}

auto PrimeCharPower(ipower::Power* power) -> bool
{
  return power != nullptr && power->isBuildGraph() != 0U;
}

auto FindStaVertex(const std::string& pin_full_name) -> ista::StaVertex*
{
  return GetStaEngine()->findVertex(pin_full_name.c_str());
}

auto NormalizeInstName(std::string inst_name) -> std::string
{
  std::erase(inst_name, '\\');
  return inst_name;
}

auto NormalizePortName(const std::string& pin_name) -> std::string
{
  const auto separator_pos = pin_name.rfind('/');
  return separator_pos == std::string::npos ? pin_name : pin_name.substr(separator_pos + 1);
}

auto FindIdbInstance(const std::string& inst_name) -> idb::IdbInstance*
{
  auto* idb_builder = dmInst->get_idb_builder();
  LOG_FATAL_IF(idb_builder == nullptr) << "iDB builder is null when querying instance metadata for " << inst_name;
  auto* def_service = idb_builder->get_def_service();
  LOG_FATAL_IF(def_service == nullptr) << "DEF service is null when querying instance metadata for " << inst_name;
  auto* idb_design = def_service->get_design();
  LOG_FATAL_IF(idb_design == nullptr) << "iDB design is null when querying instance metadata for " << inst_name;
  auto* inst_list = idb_design->get_instance_list();
  LOG_FATAL_IF(inst_list == nullptr) << "iDB instance list is null when querying instance metadata for " << inst_name;
  return inst_list->find_instance(inst_name);
}

auto FindLibertyCellForInstName(const std::string& inst_name, std::string& cell_master) -> ista::LibCell*
{
  auto normalized_inst_name = NormalizeInstName(inst_name);
  auto* idb_inst = FindIdbInstance(normalized_inst_name);
  LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << normalized_inst_name << " is not found in iDB when querying liberty metadata.";
  auto* idb_master = idb_inst->get_cell_master();
  LOG_FATAL_IF(idb_master == nullptr) << "Instance " << normalized_inst_name << " has no iDB cell master.";
  cell_master = idb_master->get_name();
  return GetStaEngine()->findLibertyCell(cell_master.c_str());
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
    LOG_WARNING << "Characterization power source pin is not found: " << *source_input_pin_full_name;
    return;
  }

  auto* pwr_vertex = power->get_power_graph().staToPwrVertex(source_vertex);
  if (pwr_vertex == nullptr) {
    LOG_WARNING << "Characterization power source vertex is not found in iPA graph: " << *source_input_pin_full_name;
    return;
  }

  const double clock_period_ns = power->get_power_graph().get_fastest_clock().get_clock_period_ns();
  if (clock_period_ns <= 0.0) {
    LOG_WARNING << "Characterization power source pin cannot be annotated because clock period is invalid.";
    return;
  }

  auto* fastest_clock = &(power->get_power_graph().get_fastest_clock());
  pwr_vertex->addData(c_default_clock_toggle / clock_period_ns, c_default_clock_sp, ipower::PwrDataSource::kClockPropagation,
                      fastest_clock);
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

auto ResolveConfiguredRoutingLayer() -> int
{
  const auto& routing_layers = CONFIG_INST.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  return static_cast<int>(routing_layers.front());
}

auto ResolveConfiguredWireWidth() -> std::optional<double>
{
  const double wire_width = CONFIG_INST.get_wire_width();
  return wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
}

auto FormatOptionalWireWidth(std::optional<double> wire_width_um) -> std::string
{
  return wire_width_um.has_value() ? logformat::FormatWithUnit(*wire_width_um, "um") : "library_default";
}

auto QueryWireRcProbe(int routing_layer, std::optional<double> wire_width_um) -> WireRcProbe
{
  WireRcProbe probe;
  probe.routing_layer = routing_layer;
  probe.wire_width_um = wire_width_um;

  if (routing_layer <= 0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = schema::DiagnosticLevel::kError;
    probe.diagnostic_summary = "effective routing layer is invalid for unit RC probing.";
    probe.status = "invalid_layer";
    probe.detail = "routing layer must be positive";
    return probe;
  }

  if (GetStaEngine()->getIDBAdapter() == nullptr) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = schema::DiagnosticLevel::kError;
    probe.diagnostic_summary = "STA IDB adapter is not ready for unit RC probing.";
    probe.status = "adapter_unavailable";
    probe.detail = "STA IDB adapter must be initialized before RC probing";
    return probe;
  }

  probe.queried = true;
  probe.resistance_per_um_ohm = STA_ADAPTER_INST.queryWireResistance(routing_layer, probe.query_length_um, wire_width_um) / kMilliOhmPerOhm;
  probe.capacitance_per_um_pf = STA_ADAPTER_INST.queryWireCapacitance(routing_layer, probe.query_length_um, wire_width_um);

  if (!std::isfinite(probe.resistance_per_um_ohm) || !std::isfinite(probe.capacitance_per_um_pf)) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = schema::DiagnosticLevel::kError;
    probe.diagnostic_summary = "unit wire RC query returned non-finite values.";
    probe.status = "invalid_nonfinite";
    probe.detail = "queried unit RC must be finite";
    return probe;
  }

  if (probe.resistance_per_um_ohm < 0.0 || probe.capacitance_per_um_pf < 0.0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = schema::DiagnosticLevel::kError;
    probe.diagnostic_summary = "unit wire RC query returned negative values.";
    probe.status = "invalid_negative";
    probe.detail = "negative resistance/capacitance is physically invalid";
    return probe;
  }

  if (probe.resistance_per_um_ohm == 0.0 || probe.capacitance_per_um_pf == 0.0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = schema::DiagnosticLevel::kWarning;
    probe.diagnostic_summary = "unit wire RC query returned zero on at least one metric.";
    probe.status = "warning_zero";
    probe.detail = "zero unit RC is suspicious; flow continues";
    return probe;
  }

  probe.status = "ok";
  probe.detail = "positive finite unit RC";
  return probe;
}

auto EmitWireRcProbeDiagnostic(const WireRcProbe& probe) -> void
{
  if (!probe.has_diagnostic) {
    return;
  }

  if (probe.diagnostic_level == schema::DiagnosticLevel::kError) {
    LOG_ERROR << kStaAdapterOwner << ": " << probe.diagnostic_summary << " layer=" << probe.routing_layer
              << ", wire_width=" << FormatOptionalWireWidth(probe.wire_width_um) << ", resistance="
              << (probe.queried ? logformat::FormatEngineering(probe.resistance_per_um_ohm, "Ohm/um") : std::string{"n/a"})
              << ", capacitance=" << (probe.queried ? logformat::FormatWithUnit(probe.capacitance_per_um_pf, "pF/um") : std::string{"n/a"});
  } else {
    LOG_WARNING << kStaAdapterOwner << ": " << probe.diagnostic_summary << " layer=" << probe.routing_layer
                << ", wire_width=" << FormatOptionalWireWidth(probe.wire_width_um) << ", resistance="
                << (probe.queried ? logformat::FormatEngineering(probe.resistance_per_um_ohm, "Ohm/um") : std::string{"n/a"})
                << ", capacitance="
                << (probe.queried ? logformat::FormatWithUnit(probe.capacitance_per_um_pf, "pF/um") : std::string{"n/a"});
  }

  schema::EmitDiagnostic(
      probe.diagnostic_level, kStaAdapterOwner, probe.diagnostic_summary,
      {
          {"routing_layer", std::to_string(probe.routing_layer)},
          {"wire_width_um", FormatOptionalWireWidth(probe.wire_width_um)},
          {"query_length_um", logformat::FormatWithUnit(probe.query_length_um, "um")},
          {"unit_resistance", probe.queried ? logformat::FormatEngineering(probe.resistance_per_um_ohm, "Ohm/um") : std::string{"n/a"}},
          {"unit_capacitance", probe.queried ? logformat::FormatWithUnit(probe.capacitance_per_um_pf, "pF/um") : std::string{"n/a"}},
          {"status", probe.status},
      });
}

auto BuildWireRcRows(const WireRcProbe& probe) -> logformat::TableRows
{
  return {
      {"routing_layer", std::to_string(probe.routing_layer), "effective routing layer for unit RC probe"},
      {"wire_width_um", FormatOptionalWireWidth(probe.wire_width_um), "explicit width override or technology default"},
      {"query_length_um", logformat::FormatWithUnit(probe.query_length_um, "um"), "single-unit probe length"},
      {"unit_resistance", probe.queried ? logformat::FormatEngineering(probe.resistance_per_um_ohm, "Ohm/um") : std::string{"n/a"},
       "derived from STAAdapter::queryWireResistance"},
      {"unit_capacitance", probe.queried ? logformat::FormatWithUnit(probe.capacitance_per_um_pf, "pF/um") : std::string{"n/a"},
       "derived from STAAdapter::queryWireCapacitance"},
      {"status", probe.status, probe.detail},
  };
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

  auto* ista = GetStaEngine()->get_ista();
  auto* rc_net = ista != nullptr ? ista->getRcNet(output_net) : nullptr;
  if (rc_net != nullptr) {
    return rc_net->load(ista::AnalysisMode::kMax, trans_type);
  }
  return output_net->getLoad(ista::AnalysisMode::kMax, trans_type);
}

auto QueryLibPortCapacitancePf(ista::LibCell* lib_cell, ista::LibPort* lib_port) -> double
{
  if (lib_cell == nullptr || lib_port == nullptr) {
    return 0.0;
  }
  if (lib_port->isInput() == 0U) {
    return 0.0;
  }

  double cap_value = lib_port->get_port_cap();
  cap_value = std::max(cap_value, lib_port->get_port_cap(ista::AnalysisMode::kMax, ista::TransType::kRise).value_or(0.0));
  cap_value = std::max(cap_value, lib_port->get_port_cap(ista::AnalysisMode::kMax, ista::TransType::kFall).value_or(0.0));
  cap_value = std::max(cap_value, lib_port->get_port_cap(ista::AnalysisMode::kMin, ista::TransType::kRise).value_or(0.0));
  cap_value = std::max(cap_value, lib_port->get_port_cap(ista::AnalysisMode::kMin, ista::TransType::kFall).value_or(0.0));
  return ConvertLibCapToPf(lib_cell, cap_value);
}

auto AddCharSlewData(ista::StaVertex* vertex, ista::TransType trans_type, double slew_ns,
                     std::unique_ptr<ista::LibCurrentData> output_current_data = nullptr) -> void
{
  LOG_FATAL_IF(vertex == nullptr) << "Null STA vertex when installing characterization slew data.";
  const int slew_fs = static_cast<int>(NS_TO_FS(slew_ns));
  auto slew_data = std::make_unique<ista::StaSlewData>(ista::AnalysisMode::kMax, trans_type, vertex, slew_fs);
  slew_data->set_output_current_data(std::move(output_current_data));
  vertex->addData(slew_data.release());
}

auto ApplyCharBufferInputSlew(ista::StaVertex* input_vertex, ista::Pin* output_pin, ista::StaVertex* output_vertex,
                              ista::Instance* source_inst, ista::LibCell* lib_cell, ista::LibArcSet* source_arc_set, ista::LibArc* lib_arc,
                              double slew_ns) -> void
{
  LOG_FATAL_IF(input_vertex == nullptr) << "Null source input STA vertex in characterization slew update.";
  LOG_FATAL_IF(output_pin == nullptr) << "Null source output STA pin in characterization slew update.";
  LOG_FATAL_IF(output_vertex == nullptr) << "Null source output STA vertex in characterization slew update.";
  LOG_FATAL_IF(source_inst == nullptr) << "Null source STA instance in characterization slew update.";
  LOG_FATAL_IF(lib_cell == nullptr) << "Null source liberty cell in characterization slew update.";
  LOG_FATAL_IF(source_arc_set == nullptr || source_arc_set->get_arcs().empty())
      << "Missing source buffer timing arc set for characterization slew update.";
  LOG_FATAL_IF(lib_arc == nullptr) << "Missing source liberty arc in characterization slew update.";

  AddCharSlewData(input_vertex, ista::TransType::kRise, slew_ns);
  AddCharSlewData(input_vertex, ista::TransType::kFall, slew_ns);

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
      LOG_WARNING << "Characterization source output slew lookup is empty for " << output_pin->getFullName();
      return;
    }

    const double output_slew_ns = slew_values.front();
    auto output_current = lib_arc->getOutputCurrent(output_trans_type, slew_ns, output_load);
    AddCharSlewData(output_vertex, output_trans_type, output_slew_ns, std::move(output_current));
  };

  apply_output_model(ista::TransType::kRise);
  apply_output_model(ista::TransType::kFall);
}

auto QueryCharClockATFromVertex(ista::StaVertex* vertex, const std::string& clock_name) -> double
{
  LOG_FATAL_IF(vertex == nullptr) << "Null sink STA vertex in characterization clock-arrival query.";

  const auto pick_max_value = [](const std::optional<int64_t>& lhs, const std::optional<int64_t>& rhs) -> double {
    const auto to_ns = [](const std::optional<int64_t>& value) -> double {
      return value.has_value() ? static_cast<double>(value.value()) / static_cast<double>(ista::g_ns2fs) : 0.0;
    };
    const double lhs_value = to_ns(lhs);
    const double rhs_value = to_ns(rhs);
    return std::max(lhs_value, rhs_value);
  };

  double delay_ns = pick_max_value(vertex->getClockArriveTime(ista::AnalysisMode::kMax, ista::TransType::kRise, clock_name),
                                   vertex->getClockArriveTime(ista::AnalysisMode::kMax, ista::TransType::kFall, clock_name));
  if (delay_ns > 0.0) {
    return delay_ns;
  }

  delay_ns = pick_max_value(vertex->getArriveTime(ista::AnalysisMode::kMax, ista::TransType::kRise),
                            vertex->getArriveTime(ista::AnalysisMode::kMax, ista::TransType::kFall));
  if (delay_ns > 0.0) {
    return delay_ns;
  }

  LOG_WARNING << "No characterization arrival time at pin: " << vertex->getName() << " for clock: " << clock_name;
  return 0.0;
}

auto QueryCharSlewFromVertex(ista::StaVertex* vertex) -> double
{
  LOG_FATAL_IF(vertex == nullptr) << "Null sink STA vertex in characterization slew query.";

  const double rise_slew = vertex->getSlewNs(ista::AnalysisMode::kMax, ista::TransType::kRise).value_or(0.0);
  const double fall_slew = vertex->getSlewNs(ista::AnalysisMode::kMax, ista::TransType::kFall).value_or(0.0);
  if (rise_slew > 0.0 && fall_slew > 0.0) {
    return std::max(rise_slew, fall_slew);
  }

  const double slew_ns = rise_slew > 0.0 ? rise_slew : fall_slew;
  if (slew_ns == 0.0) {
    LOG_WARNING << "No characterization slew is available at pin: " << vertex->getName();
  }
  return slew_ns;
}

auto MakeCharQueryContext(const char* query_name, const std::string& cell_master) -> std::string
{
  return "Characterization query [" + std::string(query_name) + "] for liberty cell \"" + cell_master + "\"";
}

auto QueryBufferTableAxisMax(const std::string& cell_master, const char* query_name,
                             std::initializer_list<ista::LibLutTableTemplate::Variable> target_variables) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext(query_name, cell_master) << " failed: liberty cell not found; return 0.0.";
    return 0.0;
  }

  auto timing_arc_set = FindBufferArcSet(lib_cell);
  if (!timing_arc_set.has_value() || timing_arc_set.value() == nullptr || timing_arc_set.value()->get_arcs().empty()) {
    LOG_WARNING << MakeCharQueryContext(query_name, cell_master) << " failed: no buffer timing arcs are available; return 0.0.";
    return 0.0;
  }

  auto* owner_lib = lib_cell->get_owner_lib();
  LOG_WARNING_IF(owner_lib == nullptr) << MakeCharQueryContext(query_name, cell_master) << " failed: owner liberty is null; return 0.0.";
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
    LOG_WARNING << MakeCharQueryContext(query_name, cell_master) << " found no matching non-empty table axis values; return 0.0.";
    return 0.0;
  }

  return min_axis_max;
}

}  // namespace

STAAdapter::~STAAdapter() = default;

STAAdapter::CharPowerState::~CharPowerState() = default;

auto STAAdapter::init() -> void
{
  auto& adapter = getInst();
  adapter.resetCharTimingState();
  adapter.resetCharPowerState();
  adapter._is_char_only_active = false;
  auto* timing_engine = GetStaEngine();
  if (!HasStaBaseContext()) {
    ipower::Power::destroyPower();
    ista::TimingEngine::destroyTimingEngine();
    timing_engine = GetStaEngine();
    InstallTimingIDBAdapter(timing_engine);
    LoadConfiguredLiberty(timing_engine);
  }

  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
  adapter.resetStaTransientState();
  timing_engine->get_ista()->set_n_worst_path_per_clock(kWorstPathPerClock);
}

auto STAAdapter::initCharOnly() -> void
{
  auto& adapter = getInst();
  if (adapter._is_char_only_active) {
    LOG_WARNING << "initCharOnly called while char-only mode is already active; "
                << "resetting the previous characterization state first.";
    finishCharOnly();
  }

  if (!HasStaBaseContext()) {
    init();
  }

  adapter._is_char_only_active = true;
  auto* timing_engine = GetStaEngine();
  timing_engine->set_num_threads(kCharStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta_char");
  adapter.resetStaTransientState();
}

auto STAAdapter::queryInstType(const std::string& inst_name) -> icts::InstType
{
  auto inst_type = icts::InstType::kUnknown;
  const auto name = NormalizeInstName(inst_name);
  std::string cell_master = "unknown";
  auto* idb_inst = FindIdbInstance(name);
  LOG_FATAL_IF(idb_inst == nullptr) << "Instance " << name << " is not found in iDB when querying instance type.";
  auto* pin_list = idb_inst->get_pin_list();
  LOG_FATAL_IF(pin_list == nullptr) << "Instance " << name << " type is unknown (none pin in iDB) which cell is " << cell_master;

  auto* lib_cell = FindLibertyCellForInstName(name, cell_master);
  if (lib_cell == nullptr) {
    LOG_WARNING << "Instance " << name << " liberty cell \"" << cell_master << "\" is not found; fallback to iDB-only type heuristics.";
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

  constexpr std::ptrdiff_t clock_input_pin_limit = 1;
  const auto clock_input_pins = std::ranges::count_if(pin_list->get_pin_list(), [&](auto* idb_pin) -> bool {
    if (idb_pin == nullptr || idb_pin->get_term() == nullptr) {
      return false;
    }
    auto* term = idb_pin->get_term();
    auto direction = term->get_direction();
    if (direction != idb::IdbConnectDirection::kInput && direction != idb::IdbConnectDirection::kInOut) {
      return false;
    }
    auto* idb_net = idb_pin->get_net();
    LOG_FATAL_IF(idb_net == nullptr) << "Instance " << name << " pin " << idb_pin->get_pin_name()
                                     << " is not connected to any net, error in inst type judgement, which cell is " << cell_master;

    if (idb_net->is_clock() == 0U) {
      LOG_WARNING << "Instance " << name << " pin " << idb_pin->get_pin_name() << " connected net " << idb_net->get_net_name()
                  << " is not clock net, warning in inst type judgement, which cell is " << cell_master;
      return false;
    }
    return true;
  });
  if (clock_input_pins > clock_input_pin_limit) {
    inst_type = icts::InstType::kMux;
    return inst_type;
  }

  LOG_WARNING_IF(inst_type == icts::InstType::kUnknown) << "Instance " << name << " type is unknown which cell is " << cell_master;
  return inst_type;
}

auto STAAdapter::isFlipFlop(const std::string& inst_name) -> bool
{
  return queryInstType(inst_name) == icts::InstType::kFlipFlop;
}

auto STAAdapter::isClockNet(const std::string& net_name) -> bool
{
  getInst().prepareFullDesignTiming();
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    LOG_ERROR << "STA netlist is not ready.";
    return false;
  }
  auto* sta_net = sta_netlist->findNet(net_name.c_str());
  if (sta_net == nullptr) {
    LOG_ERROR << "Net " << net_name << " is not found in the STA netlist.";
    return false;
  }
  return sta_net->isClockNet();
}

auto STAAdapter::collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>
{
  getInst().prepareFullDesignTiming();
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  auto* sta_netlist = GetStaEngine()->get_netlist();
  if (sta_netlist == nullptr) {
    LOG_ERROR << "STA netlist is null when collecting clock nets.";
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
      LOG_WARNING << "Clock net \"" << sta_net->get_name() << "\" has no propagated clock in STA.";
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
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getResistance(routing_layer, length, wire_width);
}

auto STAAdapter::queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width) -> double
{
  auto* idb_adapter = GetStaEngine()->getIDBAdapter();
  if (idb_adapter == nullptr) {
    LOG_ERROR << "STA IDB adapter is not ready.";
    return 0.0;
  }
  return idb_adapter->getCapacitance(routing_layer, length, wire_width);
}

auto STAAdapter::emitUnitWireRcReport(const std::string& title, int routing_layer, std::optional<double> wire_width) -> void
{
  const WireRcProbe probe = QueryWireRcProbe(routing_layer, wire_width);
  EmitWireRcProbeDiagnostic(probe);
  schema::EmitTable(title, {"Item", "Value", "Detail"}, BuildWireRcRows(probe));
}

auto STAAdapter::emitConfiguredUnitWireRcReport(const std::string& title) -> void
{
  emitUnitWireRcReport(title, ResolveConfiguredRoutingLayer(), ResolveConfiguredWireWidth());
}

auto STAAdapter::queryCellOutPinCapLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: liberty cell not found; caller may fallback to table-axis max.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (output == nullptr) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: output pin is unavailable; caller may fallback to table-axis max.";
    return 0.0;
  }

  auto cap_limit = output->get_port_cap_limit(ista::AnalysisMode::kMax);
  if (!cap_limit.has_value()) {
    LOG_WARNING << MakeCharQueryContext("output pin cap limit", cell_master)
                << " failed: max cap limit is not defined on output pin; caller may fallback to table-axis max.";
    return 0.0;
  }
  return ConvertLibCapToPf(lib_cell, *cap_limit);
}

auto STAAdapter::queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(cell_master, "output pin cap table-axis max",
                                 {ista::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE,
                                  ista::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE});
}

auto STAAdapter::queryCellInPinSlewLimit(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: liberty cell not found; caller may fallback to table-axis max.";
    return 0.0;
  }

  ista::LibPort* input = nullptr;
  ista::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: input pin is unavailable; caller may fallback to table-axis max.";
    return 0.0;
  }

  auto slew_limit = input->get_port_slew_limit(ista::AnalysisMode::kMax);
  if (!slew_limit.has_value()) {
    LOG_WARNING << MakeCharQueryContext("input pin slew limit", cell_master)
                << " failed: max slew limit is not defined on input pin; caller may fallback to table-axis max.";
    return 0.0;
  }
  return ConvertLibTimeToNs(lib_cell, *slew_limit);
}

auto STAAdapter::queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double
{
  return QueryBufferTableAxisMax(
      cell_master, "input pin slew table-axis max",
      {ista::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION, ista::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION,
       ista::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME, ista::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION});
}

auto STAAdapter::queryCellHeightUm(const std::string& cell_master) -> double
{
  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_cell_master_list() == nullptr || idb_layout->get_units() == nullptr) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB layout metadata is not ready; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  auto* idb_master = idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: iDB cell master is not found; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  const int dbu_per_micron = idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    LOG_WARNING << MakeCharQueryContext("cell height", cell_master)
                << " failed: invalid DBU-per-micron in iDB units; auto-derived characterization unit may be unavailable.";
    return 0.0;
  }

  return static_cast<double>(idb_master->get_height()) / static_cast<double>(dbu_per_micron);
}

auto STAAdapter::createCharInstance(const std::string& cell_master, const std::string& inst_name) -> std::string
{
  auto& adapter = getInst();
  LOG_FATAL_IF(!adapter._is_char_only_active) << "Characterization instance creation requires initCharOnly() first.";
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
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

  auto& created_inst = GetStaEngine()->get_netlist()->addInstance(std::move(char_inst));
  ista::StaBuildGraph build_graph;
  build_graph.buildInst(&(GetStaEngine()->get_ista()->get_graph()), &created_inst);
  return inst_name;
}

auto STAAdapter::createCharNet(const std::string& net_name) -> std::string
{
  auto& adapter = getInst();
  LOG_FATAL_IF(!adapter._is_char_only_active) << "Characterization net creation requires initCharOnly() first.";
  auto& created_net = GetStaEngine()->get_netlist()->addNet(ista::Net(net_name.c_str()));
  (void) created_net;
  return net_name;
}

auto STAAdapter::attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name) -> void
{
  auto* netlist = GetStaEngine()->get_netlist();
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
  auto* net = GetStaEngine()->get_netlist()->findNet(net_name.c_str());
  LOG_FATAL_IF(net == nullptr) << "Cannot build characterization graph because net is not found: " << net_name;
  ista::StaBuildGraph build_graph;
  build_graph.buildNet(&(GetStaEngine()->get_ista()->get_graph()), net);
}

auto STAAdapter::buildCharRcTree(const std::string& net_name, const CharRcTreeConfig& rc_tree_config) -> void
{
  auto* timing_engine = GetStaEngine();
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
    timing_engine->makeResistor(net, driver_node, load_node, rc_tree_config.wire_res);
    timing_engine->incrCap(driver_node, rc_tree_config.wire_cap / 2.0, true);
    timing_engine->incrCap(load_node, (rc_tree_config.wire_cap / 2.0) + rc_tree_config.load_cap, true);
  }
  timing_engine->updateRCTreeInfo(net);
}

auto STAAdapter::createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns) -> void
{
  const auto source_pin_objs = GetStaEngine()->get_netlist()->findPin(source_pin_full_name.c_str(), false, false);
  LOG_FATAL_IF(source_pin_objs.empty()) << "Cannot create characterization clock because source pin is not found: " << source_pin_full_name;
  auto* source_pin = dynamic_cast<ista::Pin*>(source_pin_objs.front());
  LOG_FATAL_IF(source_pin == nullptr) << "Cannot create characterization clock because source object is not a pin: "
                                      << source_pin_full_name;
  auto source_vertex = GetStaEngine()->get_ista()->get_graph().findVertex(source_pin);
  LOG_FATAL_IF(!source_vertex) << "Cannot create characterization clock because source vertex is not found: " << source_pin_full_name;

  const int period_ps = static_cast<int>(NS_TO_PS(period_ns));
  auto sta_clock = std::make_unique<ista::StaClock>(clock_name.c_str(), ista::StaClock::ClockType::kPropagated, period_ps);
  ista::StaWaveForm wave_form;
  wave_form.addWaveEdge(0);
  wave_form.addWaveEdge(period_ps / 2);
  sta_clock->set_wave_form(std::move(wave_form));
  sta_clock->addVertex(*source_vertex);

  GetStaEngine()->get_ista()->addClock(std::move(sta_clock));
}

auto STAAdapter::destroyCharClock() -> void
{
  GetStaEngine()->get_ista()->clearClocks();
  getInst().resetCharTimingState();
}

auto STAAdapter::resetCharContext() -> void
{
  auto& adapter = getInst();
  adapter.resetStaTransientState();
  auto* timing_engine = GetStaEngine();
  timing_engine->set_num_threads(adapter._is_char_only_active ? kCharStaThreadCount : kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, adapter._is_char_only_active ? "sta_char" : "sta");
}

auto STAAdapter::prepareCharTimingContext(const std::string& input_pin_full_name, const std::string& output_pin_full_name,
                                          const std::string& sink_pin_full_name) -> void
{
  auto& adapter = getInst();
  adapter.resetCharTimingState();

  auto& runtime = adapter._char_timing_state;
  auto* netlist = GetStaEngine()->get_netlist();
  auto source_input_match = netlist->findPin(input_pin_full_name.c_str(), false, false);
  auto source_output_match = netlist->findPin(output_pin_full_name.c_str(), false, false);
  runtime.source_input_pin = source_input_match.empty() ? nullptr : dynamic_cast<ista::Pin*>(source_input_match.front());
  runtime.source_output_pin = source_output_match.empty() ? nullptr : dynamic_cast<ista::Pin*>(source_output_match.front());
  runtime.source_input_vertex = FindStaVertex(input_pin_full_name);
  runtime.source_output_vertex = FindStaVertex(output_pin_full_name);
  runtime.sink_vertex = FindStaVertex(sink_pin_full_name);

  LOG_FATAL_IF(runtime.source_input_pin == nullptr) << "Cannot find source input pin: " << input_pin_full_name;
  LOG_FATAL_IF(runtime.source_output_pin == nullptr) << "Cannot find source output pin: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.source_input_vertex == nullptr) << "Cannot find source input vertex: " << input_pin_full_name;
  LOG_FATAL_IF(runtime.source_output_vertex == nullptr) << "Cannot find source output vertex: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.sink_vertex == nullptr) << "Cannot find sink input vertex: " << sink_pin_full_name;

  runtime.source_inst = runtime.source_output_pin->get_own_instance();
  LOG_FATAL_IF(runtime.source_inst == nullptr) << "Source output pin has no owning instance: " << output_pin_full_name;
  LOG_FATAL_IF(runtime.source_input_pin->get_own_instance() != runtime.source_inst)
      << "Source input/output pins do not belong to the same instance: " << input_pin_full_name << " / " << output_pin_full_name;

  runtime.source_lib_cell = runtime.source_inst->get_inst_cell();
  LOG_FATAL_IF(runtime.source_lib_cell == nullptr) << "Source instance has no liberty cell: " << runtime.source_inst->get_name();

  auto timing_arc_set = FindBufferArcSet(runtime.source_lib_cell);
  runtime.source_arc_set = timing_arc_set.value_or(nullptr);
  LOG_FATAL_IF(runtime.source_arc_set == nullptr || runtime.source_arc_set->get_arcs().empty())
      << "Cannot resolve buffer timing arc for source instance " << runtime.source_inst->get_name();

  runtime.source_lib_arc = runtime.source_arc_set->front();
  LOG_FATAL_IF(runtime.source_lib_arc == nullptr) << "Source buffer liberty arc is null for instance " << runtime.source_inst->get_name();

  runtime.is_ready = true;
}

auto STAAdapter::prepareCharTimingSample() -> void
{
  GetStaEngine()->prepareCharTiming();
}

auto STAAdapter::setCharBufferInputSlew(double slew_ns) -> void
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before slew injection.";
  runtime.source_input_vertex->resetColor();
  runtime.source_input_vertex->resetLevel();
  runtime.source_input_vertex->reset_is_slew_prop();
  runtime.source_input_vertex->reset_is_delay_prop();
  runtime.source_input_vertex->reset_is_fwd();
  runtime.source_input_vertex->reset_is_bwd();
  runtime.source_input_vertex->resetSlewBucket();
  runtime.source_input_vertex->resetClockBucket();
  runtime.source_input_vertex->resetPathDelayBucket();
  runtime.source_output_vertex->resetSlewBucket();
  ApplyCharBufferInputSlew(runtime.source_input_vertex, runtime.source_output_pin, runtime.source_output_vertex, runtime.source_inst,
                           runtime.source_lib_cell, runtime.source_arc_set, runtime.source_lib_arc, slew_ns);
}

auto STAAdapter::setCharBufferInputSlewIncremental(double slew_ns) -> void
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before incremental slew injection.";
  GetStaEngine()->prepareCharTiming();
  setCharBufferInputSlew(slew_ns);
}

auto STAAdapter::updateCharTimingSample() -> void
{
  GetStaEngine()->updateCharTiming();
}

auto STAAdapter::updateCharTimingIncrementalSample() -> void
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before incremental propagation.";
  GetStaEngine()->updateCharTiming();
}

auto STAAdapter::prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                                  std::optional<std::string> source_input_pin_full_name) -> bool
{
  auto& adapter = getInst();
  adapter.resetCharPowerState();
  auto& runtime = adapter._char_power_state;
  runtime.inst_names = inst_names;
  runtime.net_names = net_names;
  runtime.inst_name_set = std::unordered_set<std::string>(inst_names.begin(), inst_names.end());
  runtime.net_name_set = std::unordered_set<std::string>(net_names.begin(), net_names.end());
  runtime.source_input_pin_full_name = std::move(source_input_pin_full_name);

  if (runtime.inst_names.empty()) {
    LOG_WARNING << "Characterization power setup skipped: no selected instances are provided.";
    return false;
  }

  auto clocks = GetStaEngine()->get_ista()->getClocks();
  if (clocks.empty()) {
    LOG_WARNING << "Characterization power setup skipped: char-only STA has no clock objects.";
    return false;
  }

  return true;
}

auto STAAdapter::refreshCharPowerLoad() -> bool
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_power_state;
  if (!runtime.is_runtime_ready) {
    runtime.is_switch_power_cached = false;
    return true;
  }

  auto* power = runtime.char_power.get();
  if (!PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power load refresh skipped: iPA graph is not ready.";
    return false;
  }

  runtime.cached_switch_power_w = CalcSelectedNetSwitchPower(power, runtime.net_name_set);
  runtime.is_switch_power_cached = true;
  return true;
}

auto STAAdapter::updateCharPower() -> bool
{
  auto& adapter = getInst();
  auto& runtime = adapter._char_power_state;
  auto* power = runtime.char_power.get();
  if (!runtime.is_runtime_ready) {
    runtime.char_power = BuildCharPower();
    power = runtime.char_power.get();
    if (!PrimeCharPower(power)) {
      LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
      return false;
    }

    AnnotateCharSourceInputPower(power, runtime.source_input_pin_full_name);
    FilterPowerCells(power, runtime.inst_name_set);
    if (power->calcLeakagePower() == 0U) {
      LOG_WARNING << "Characterization power update skipped: leakage calculation failed.";
      return false;
    }

    runtime.cached_leakage_power_w = SumInstPowerData(power->get_leakage_powers(), runtime.inst_name_set);
    runtime.is_runtime_ready = true;
    runtime.is_switch_power_cached = false;
  } else if (!PrimeCharPower(power)) {
    LOG_WARNING << "Characterization power update skipped: iPA graph is not ready.";
    return false;
  }

  if (!runtime.is_switch_power_cached) {
    runtime.cached_switch_power_w = CalcSelectedNetSwitchPower(power, runtime.net_name_set);
    runtime.is_switch_power_cached = true;
  }

  if (power->calcInternalPower() == 0U) {
    LOG_WARNING << "Characterization power update skipped: internal-power calculation failed.";
    return false;
  }
  const double internal_power_w = SumInstPowerData(power->get_internal_powers(), runtime.inst_name_set);

  runtime.last_total_power_w = runtime.cached_leakage_power_w + internal_power_w + runtime.cached_switch_power_w;
  return true;
}

auto STAAdapter::queryCharPower() -> double
{
  return getInst()._char_power_state.last_total_power_w;
}

auto STAAdapter::queryCharNetSwitchPower(const std::string& net_name) -> double
{
  if (net_name.empty()) {
    return 0.0;
  }

  auto& runtime = getInst()._char_power_state;
  auto* power = runtime.char_power.get();
  if (!runtime.is_runtime_ready || !PrimeCharPower(power)) {
    return 0.0;
  }

  return CalcSelectedNetSwitchPower(power, std::unordered_set<std::string>{net_name});
}

auto STAAdapter::destroyCharPower() -> void
{
  auto& adapter = getInst();
  adapter.resetCharPowerState();
}

auto STAAdapter::finishCharOnly() -> void
{
  auto& adapter = getInst();
  if (!adapter._is_char_only_active) {
    return;
  }

  resetCharContext();
  adapter._is_char_only_active = false;
  auto* timing_engine = GetStaEngine();
  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
}

auto STAAdapter::updateTiming() -> void
{
  getInst().prepareFullDesignTiming();
  GetStaEngine()->updateTiming();
}

auto STAAdapter::queryCharClockAT(const std::string& clock_name) -> double
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before clock-arrival query.";
  return QueryCharClockATFromVertex(runtime.sink_vertex, clock_name);
}

auto STAAdapter::queryCharSlew() -> double
{
  auto& runtime = getInst()._char_timing_state;
  LOG_FATAL_IF(!runtime.is_ready) << "Characterization timing runtime is not prepared before slew query.";
  return QueryCharSlewFromVertex(runtime.sink_vertex);
}

auto STAAdapter::queryCharInputPinCap(const std::string& cell_master) -> double
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("input pin cap", cell_master) << " failed: liberty cell not found; return 0.0.";
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
    LOG_WARNING << "Null pin provided when querying pin capacitance.";
    return 0.0;
  }

  auto* inst = pin->get_inst();
  const std::string pin_full_name = inst != nullptr ? (inst->get_name() + "/" + pin->get_name()) : pin->get_name();
  if (inst == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: CTS pin has no owning instance for " << pin_full_name << ".";
    return 0.0;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    LOG_WARNING << "Pin-cap query skipped: CTS instance has no cell master for " << pin_full_name << ".";
    return 0.0;
  }

  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty cell \"" << cell_master << "\" is not found for " << pin_full_name << ".";
    return 0.0;
  }

  const auto port_name = NormalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    LOG_WARNING << "Pin-cap query skipped: liberty port \"" << port_name << "\" is not found on cell " << cell_master << ".";
    return 0.0;
  }
  if (lib_port->isInput() == 0U) {
    return 0.0;
  }

  return QueryLibPortCapacitancePf(lib_cell, lib_port);
}

auto STAAdapter::queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>
{
  auto* lib_cell = GetStaEngine()->findLibertyCell(cell_master.c_str());
  if (lib_cell == nullptr) {
    LOG_WARNING << MakeCharQueryContext("buffer ports", cell_master) << " failed: liberty cell not found; return empty port names.";
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
  (void) inst_name;
  resetCharContext();
}

auto STAAdapter::destroyCharNet(const std::string& net_name) -> void
{
  (void) net_name;
  resetCharContext();
}

auto STAAdapter::resetStaTransientState() -> void
{
  resetCharTimingState();
  resetCharPowerState();
  ipower::Power::destroyPower();

  auto* timing_engine = GetStaEngine();
  auto* ista = timing_engine->get_ista();
  if (ista != nullptr) {
    ista->resetAllRcNet();
    ista->resetSdcConstrain();
  }
  timing_engine->resetNetlist();
  timing_engine->resetGraph();
}

auto STAAdapter::prepareFullDesignTiming() -> void
{
  auto* timing_engine = GetStaEngine();
  LOG_FATAL_IF(timing_engine->get_db_adapter() == nullptr) << "STA full-design preparation requires STAAdapter::init() first.";

  if (_is_char_only_active) {
    resetStaTransientState();
    _is_char_only_active = false;
  }
  if (HasFullDesignTimingContext()) {
    return;
  }

  timing_engine->set_num_threads(kStaThreadCount);
  ConfigureStaWorkspace(timing_engine, "sta");
  resetStaTransientState();
  timing_engine->get_db_adapter()->convertDBToTimingNetlist();
  LoadConfiguredSdcIfPresent(timing_engine);
  timing_engine->buildGraph();
  timing_engine->initRcTree();
  timing_engine->get_ista()->set_n_worst_path_per_clock(kWorstPathPerClock);
}

auto STAAdapter::resetCharTimingState() -> void
{
  _char_timing_state = CharTimingState{};
}

auto STAAdapter::resetCharPowerState() -> void
{
  _char_power_state = CharPowerState{};
}

}  // namespace icts
