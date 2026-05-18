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
 * @file STAAdapterInternal.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief Shared internal helper declarations for the iCTS STA adapter.
 */

#pragma once

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "STAAdapter.hh"
#include "liberty/Lib.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"
#include "netlist/Instance.hh"

namespace idb {
class IdbInstance;
}  // namespace idb

namespace ipower {
class Power;
}  // namespace ipower

namespace ista {
class Pin;
class StaVertex;
class TimingEngine;
enum class TransType : int;
}  // namespace ista

namespace icts::sta_adapter_internal {

extern const unsigned kStaThreadCount;
extern const unsigned kCharStaThreadCount;
extern const unsigned kWorstPathPerClock;

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

auto GetStaEngine() -> ista::TimingEngine*;
auto BuildCharPower() -> IctsCharPowerPtr;
auto ConfigureStaWorkspace(ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void;
auto InstallTimingIDBAdapter(ista::TimingEngine* timing_engine) -> void;
auto LoadConfiguredLiberty(ista::TimingEngine* timing_engine) -> void;
auto LoadConfiguredSdcIfPresent(ista::TimingEngine* timing_engine) -> void;
auto HasFullDesignTimingContext() -> bool;
auto HasStaBaseContext() -> bool;
auto PrimeCharPower(ipower::Power* power) -> bool;
auto FindStaVertex(const std::string& pin_full_name) -> ista::StaVertex*;
auto NormalizeInstName(std::string inst_name) -> std::string;
auto NormalizePortName(const std::string& pin_name) -> std::string;
auto FindIdbInstance(const std::string& inst_name) -> idb::IdbInstance*;
auto FindLibertyCellForInstName(const std::string& inst_name, std::string& cell_master) -> ista::LibCell*;
auto FindLibertyCellByMaster(const std::string& cell_master) -> ista::LibCell*;
auto AnnotateCharSourceInputPower(ipower::Power* power, const std::optional<std::string>& source_input_pin_full_name) -> void;
auto FindBufferArcSet(ista::LibCell* lib_cell) -> std::optional<ista::LibArcSet*>;
auto ConvertAxisValue(ista::LibLibrary* owner_lib, ista::LibLutTableTemplate::Variable variable, double axis_value) -> double;
auto ResolveConfiguredRoutingLayer() -> int;
auto ResolveConfiguredWireWidth() -> std::optional<double>;
auto FormatOptionalWireWidth(std::optional<double> wire_width_um) -> std::string;
auto QueryWireRcProbe(int routing_layer, std::optional<double> wire_width_um) -> WireRcProbe;
auto EmitWireRcProbeDiagnostic(const WireRcProbe& probe) -> void;
auto BuildWireRcRows(const WireRcProbe& probe) -> logformat::TableRows;
auto ConvertLibCapToPf(ista::LibCell* lib_cell, double cap_value) -> double;
auto ConvertLibTimeToNs(ista::LibCell* lib_cell, double time_value) -> double;
auto ConvertPfLoadToLibUnit(ista::LibCell* lib_cell, double load_pf) -> double;
auto QueryOutputNetLoadPf(ista::Pin* output_pin, ista::TransType trans_type) -> double;
auto QueryLibPortCapacitancePf(ista::LibCell* lib_cell, ista::LibPort* lib_port) -> double;
auto ApplyCharBufferInputSlew(ista::StaVertex* input_vertex, ista::Pin* output_pin, ista::StaVertex* output_vertex,
                              ista::Instance* source_inst, ista::LibCell* lib_cell, ista::LibArcSet* source_arc_set, ista::LibArc* lib_arc,
                              double slew_ns) -> void;
auto QueryCharClockATFromVertex(ista::StaVertex* vertex, const std::string& clock_name) -> double;
auto QueryCharSlewFromVertex(ista::StaVertex* vertex) -> double;
auto MakeCharQueryContext(const char* query_name, const std::string& cell_master) -> std::string;
auto QueryBufferTableAxisMax(const std::string& cell_master, const char* query_name,
                             std::initializer_list<ista::LibLutTableTemplate::Variable> target_variables) -> double;

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

}  // namespace icts::sta_adapter_internal
