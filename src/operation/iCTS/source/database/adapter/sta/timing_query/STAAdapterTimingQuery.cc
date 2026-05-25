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
 * @file STAAdapterTimingQuery.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief STA timing, Liberty, iDB context, and wire-RC query helper implementation.
 */

#include "STAAdapterTimingQuery.hh"

#include <glog/logging.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "Log.hh"
#include "STAAdapter.hh"
#include "TimingDBAdapter.hh"
#include "Type.hh"
#include "Vector.hh"
#include "api/TimingEngine.hh"
#include "api/TimingIDBAdapter.hh"
#include "builder.h"
#include "config/Config.hh"
#include "def_service.h"
#include "delay/ElmoreDelayCalc.hh"
#include "dm_config.h"
#include "idm.h"
#include "liberty/Lib.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "logger/SchemaForward.hh"
#include "netlist/Net.hh"
#include "netlist/Pin.hh"
#include "sta/Sta.hh"

namespace icts::sta_adapter_timing_query {

namespace {

auto ResolveThreadCount(const char* env_name, unsigned default_value) noexcept -> unsigned
{
  constexpr unsigned min_thread_count = 1U;
  constexpr unsigned max_thread_count = 80U;
  const auto* value = std::getenv(env_name);
  if (value == nullptr || *value == '\0') {
    return default_value;
  }
  char* parse_end = nullptr;
  errno = 0;
  const auto parsed = std::strtoul(value, &parse_end, 10);
  if (errno != 0 || parse_end == value || *parse_end != '\0') {
    return default_value;
  }
  return std::clamp(static_cast<unsigned>(parsed), min_thread_count, max_thread_count);
}

}  // namespace

const unsigned kStaThreadCount = ResolveThreadCount("ICTS_STA_THREADS", 80U);
const unsigned kWorstPathPerClock = 10U;
constexpr double kMilliOhmPerOhm = 1000.0;
constexpr const char* kStaAdapterOwner = "STAAdapter";
constexpr std::array<ista::LibTable::TableType, 4> kCharArcTableTypes = {
    ista::LibTable::TableType::kCellRise,
    ista::LibTable::TableType::kCellFall,
    ista::LibTable::TableType::kRiseTransition,
    ista::LibTable::TableType::kFallTransition,
};

namespace {

auto GetDbConfig() -> decltype(auto)
{
  return (dmInst->get_config());
}

}  // namespace

auto GetStaEngine() -> ista::TimingEngine*
{
  return ista::TimingEngine::getOrCreateTimingEngine();
}

auto ConfigureStaWorkspace(const std::string& work_dir, ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void
{
  auto sta_work_dir = std::filesystem::path(work_dir).append(workspace_dir_name).string();
  if (!std::filesystem::exists(sta_work_dir)) {
    std::filesystem::create_directories(sta_work_dir);
  }
  timing_engine->set_design_work_space(sta_work_dir.c_str());
}

auto ConfigureStaWorkspace(const Config& config, ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void
{
  ConfigureStaWorkspace(config.get_work_dir(), timing_engine, workspace_dir_name);
}

namespace {

auto BuildStaLibPathList() -> std::vector<const char*>
{
  std::vector<const char*> lib_paths;
  std::ranges::transform(GetDbConfig().get_lib_paths(), std::back_inserter(lib_paths),
                         [](const std::string& lib_path) -> auto { return lib_path.c_str(); });
  return lib_paths;
}

}  // namespace

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

auto FindLibertyCellByMaster(const std::string& cell_master) -> ista::LibCell*
{
  return GetStaEngine()->findLibertyCell(cell_master.c_str());
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

auto ResolveConfiguredRoutingLayer(const Config& config) -> int
{
  const auto& routing_layers = config.get_routing_layers();
  if (routing_layers.empty()) {
    LOG_ERROR << kStaAdapterOwner << ": routing layer is not configured.";
    return 0;
  }
  return static_cast<int>(routing_layers.front());
}

auto ResolveConfiguredWireWidth(const Config& config) -> std::optional<double>
{
  const double wire_width = config.get_wire_width();
  return wire_width > 0.0 ? std::optional<double>(wire_width) : std::nullopt;
}

auto FormatOptionalWireWidth(std::optional<double> wire_width_um) -> std::string
{
  return wire_width_um.has_value() ? logformat::FormatWithUnit(*wire_width_um, "um") : "library_default";
}

auto QueryWireRcProbe(STAAdapter& sta_adapter, int routing_layer, std::optional<double> wire_width_um) -> WireRcProbe
{
  WireRcProbe probe;
  probe.routing_layer = routing_layer;
  probe.wire_width_um = wire_width_um;

  if (routing_layer <= 0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = DiagnosticLevel::kError;
    probe.diagnostic_summary = "effective routing layer is invalid for unit RC probing.";
    probe.status = "invalid_layer";
    probe.detail = "routing layer must be positive";
    return probe;
  }

  if (GetStaEngine()->getIDBAdapter() == nullptr) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = DiagnosticLevel::kError;
    probe.diagnostic_summary = "STA IDB adapter is not ready for unit RC probing.";
    probe.status = "adapter_unavailable";
    probe.detail = "STA IDB adapter must be initialized before RC probing";
    return probe;
  }

  probe.queried = true;
  probe.resistance_per_um_ohm = sta_adapter.queryWireResistance(routing_layer, probe.query_length_um, wire_width_um) / kMilliOhmPerOhm;
  probe.capacitance_per_um_pf = sta_adapter.queryWireCapacitance(routing_layer, probe.query_length_um, wire_width_um);

  if (!std::isfinite(probe.resistance_per_um_ohm) || !std::isfinite(probe.capacitance_per_um_pf)) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = DiagnosticLevel::kError;
    probe.diagnostic_summary = "unit wire RC query returned non-finite values.";
    probe.status = "invalid_nonfinite";
    probe.detail = "queried unit RC must be finite";
    return probe;
  }

  if (probe.resistance_per_um_ohm < 0.0 || probe.capacitance_per_um_pf < 0.0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = DiagnosticLevel::kError;
    probe.diagnostic_summary = "unit wire RC query returned negative values.";
    probe.status = "invalid_negative";
    probe.detail = "negative resistance/capacitance is physically invalid";
    return probe;
  }

  if (probe.resistance_per_um_ohm == 0.0 || probe.capacitance_per_um_pf == 0.0) {
    probe.has_diagnostic = true;
    probe.diagnostic_level = DiagnosticLevel::kWarning;
    probe.diagnostic_summary = "unit wire RC query returned zero on at least one metric.";
    probe.status = "warning_zero";
    probe.detail = "zero unit RC is suspicious; flow continues";
    return probe;
  }

  probe.status = "ok";
  probe.detail = "positive finite unit RC";
  return probe;
}

auto EmitWireRcProbeDiagnostic(SchemaWriter& reporter, const WireRcProbe& probe) -> void
{
  if (!probe.has_diagnostic) {
    return;
  }

  if (probe.diagnostic_level == DiagnosticLevel::kError) {
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

  EmitDiagnostic(
      reporter, probe.diagnostic_level, kStaAdapterOwner, probe.diagnostic_summary,
      {
          {"routing_setup_source", "Runtime Configuration"},
          {"query_length", logformat::FormatWithUnit(probe.query_length_um, "um")},
          {"unit_resistance", probe.queried ? logformat::FormatEngineering(probe.resistance_per_um_ohm, "Ohm/um") : std::string{"n/a"}},
          {"unit_capacitance", probe.queried ? logformat::FormatWithUnit(probe.capacitance_per_um_pf, "pF/um") : std::string{"n/a"}},
          {"status", probe.status},
      });
}

auto BuildWireRcRows(const WireRcProbe& probe) -> logformat::TableRows
{
  return {
      {"routing_setup_source", "Runtime Configuration", "routing layer and wire width are reported once there"},
      {"query_length", logformat::FormatWithUnit(probe.query_length_um, "um"), "single-unit probe length"},
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

}  // namespace icts::sta_adapter_timing_query
