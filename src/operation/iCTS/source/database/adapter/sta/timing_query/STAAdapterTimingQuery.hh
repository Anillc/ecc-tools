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
 * @file STAAdapterTimingQuery.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief STA timing, Liberty, iDB context, and wire-RC query helper declarations.
 */

#pragma once

#include <initializer_list>
#include <optional>
#include <string>

#include "liberty/Lib.hh"
#include "logger/LogFormat.hh"
#include "logger/Schema.hh"

namespace idb {
class IdbInstance;
}  // namespace idb

namespace ista {
class Pin;
class StaVertex;
class TimingEngine;
enum class TransType : int;
}  // namespace ista

namespace icts {
class Config;
class STAAdapter;
}  // namespace icts

namespace icts::sta_adapter_timing_query {

extern const unsigned kStaThreadCount;
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
  DiagnosticLevel diagnostic_level = DiagnosticLevel::kInfo;
  std::string diagnostic_summary;
  std::string status = "not_queried";
  std::string detail;
};

auto GetStaEngine() -> ista::TimingEngine*;
auto ConfigureStaWorkspace(const std::string& work_dir, ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void;
auto ConfigureStaWorkspace(const Config& config, ista::TimingEngine* timing_engine, const std::string& workspace_dir_name) -> void;
auto InstallTimingIDBAdapter(ista::TimingEngine* timing_engine) -> void;
auto LoadConfiguredLiberty(ista::TimingEngine* timing_engine) -> void;
auto LoadConfiguredSdcIfPresent(ista::TimingEngine* timing_engine) -> void;
auto HasFullDesignTimingContext() -> bool;
auto HasStaBaseContext() -> bool;
auto FindStaVertex(const std::string& pin_full_name) -> ista::StaVertex*;
auto NormalizeInstName(std::string inst_name) -> std::string;
auto NormalizePortName(const std::string& pin_name) -> std::string;
auto FindIdbInstance(const std::string& inst_name) -> idb::IdbInstance*;
auto FindLibertyCellForInstName(const std::string& inst_name, std::string& cell_master) -> ista::LibCell*;
auto FindLibertyCellByMaster(const std::string& cell_master) -> ista::LibCell*;
auto FindBufferArcSet(ista::LibCell* lib_cell) -> std::optional<ista::LibArcSet*>;
auto ConvertAxisValue(ista::LibLibrary* owner_lib, ista::LibLutTableTemplate::Variable variable, double axis_value) -> double;
auto ResolveConfiguredRoutingLayer(const Config& config) -> int;
auto ResolveConfiguredWireWidth(const Config& config) -> std::optional<double>;
auto FormatOptionalWireWidth(std::optional<double> wire_width_um) -> std::string;
auto QueryWireRcProbe(STAAdapter& sta_adapter, int routing_layer, std::optional<double> wire_width_um) -> WireRcProbe;
auto EmitWireRcProbeDiagnostic(SchemaWriter& reporter, const WireRcProbe& probe) -> void;
auto BuildWireRcRows(const WireRcProbe& probe) -> logformat::TableRows;
auto ConvertLibCapToPf(ista::LibCell* lib_cell, double cap_value) -> double;
auto ConvertLibTimeToNs(ista::LibCell* lib_cell, double time_value) -> double;
auto ConvertPfLoadToLibUnit(ista::LibCell* lib_cell, double load_pf) -> double;
auto QueryOutputNetLoadPf(ista::Pin* output_pin, ista::TransType trans_type) -> double;
auto QueryLibPortCapacitancePf(ista::LibCell* lib_cell, ista::LibPort* lib_port) -> double;
auto MakeCharQueryContext(const char* query_name, const std::string& cell_master) -> std::string;
auto QueryBufferTableAxisMax(const std::string& cell_master, const char* query_name,
                             std::initializer_list<ista::LibLutTableTemplate::Variable> target_variables) -> double;

}  // namespace icts::sta_adapter_timing_query
