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
 * @file STAAdapter.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-18
 * @brief iCTS STA adapter over iSTA.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace icts {

enum class InstType;
class Net;
class Pin;
template <typename T>
class ClockSteinerTree;

}  // namespace icts

namespace ista {
class Instance;
class LibArc;
class LibArcSet;
class LibCell;
class Pin;
class StaVertex;
}  // namespace ista

namespace ipower {
class Power;
}  // namespace ipower

namespace icts {

using IctsCharPowerPtr = std::shared_ptr<ipower::Power>;

#define STA_ADAPTER_INST (icts::STAAdapter::getInst())

class STAAdapter
{
 public:
  struct ClockTimingMetrics
  {
    double setup_tns = 0.0;
    double setup_wns = 0.0;
    double hold_tns = 0.0;
    double hold_wns = 0.0;
    double suggest_freq = 0.0;
  };
  struct ClockTimingRecord
  {
    std::string clock_name;
    ClockTimingMetrics metrics;
  };
  struct ClockLatencySkewMetrics
  {
    std::string clock_name;
    std::string analysis_mode;
    std::string launch_pin;
    std::string capture_pin;
    double launch_latency_ns = 0.0;
    double capture_latency_ns = 0.0;
    double worst_skew_ns = 0.0;
    double average_worst_skew_ns = 0.0;
    std::size_t path_count = 0U;
    std::size_t average_sample_count = 0U;
  };
  static auto getInst() -> STAAdapter&
  {
    static STAAdapter inst;
    return inst;
  }

  STAAdapter(const STAAdapter& rhs) = delete;
  STAAdapter(STAAdapter&& rhs) = delete;
  auto operator=(const STAAdapter& rhs) -> STAAdapter& = delete;
  auto operator=(STAAdapter&& rhs) -> STAAdapter& = delete;

  static auto init() -> void;
  static auto initCharOnly() -> void;
  static auto queryInstType(const std::string& inst_name) -> icts::InstType;
  static auto isFlipFlop(const std::string& inst_name) -> bool;
  static auto queryWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryCellOutPinCapLimit(const std::string& cell_master) -> double;
  static auto queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double;
  static auto queryCellInPinSlewLimit(const std::string& cell_master) -> double;
  static auto queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double;
  static auto queryCellHeightUm(const std::string& cell_master) -> double;
  static auto queryCellAreaUm2(const std::string& cell_master) -> double;

  static auto createCharInstance(const std::string& cell_master, const std::string& inst_name) -> std::string;
  static auto createCharNet(const std::string& net_name) -> std::string;
  static auto attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name) -> void;
  static auto buildCharNetGraph(const std::string& net_name) -> void;
  static auto buildCharRcTree(const std::string& net_name, double wire_res, double wire_cap, double load_cap) -> void;
  static auto createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns) -> void;
  static auto destroyCharClock() -> void;
  static auto resetCharContext() -> void;

  // Characterization-only runtime facade used by iCTS CharBuilder.
  static auto prepareCharTimingContext(const std::string& input_pin_full_name, const std::string& output_pin_full_name,
                                       const std::string& sink_pin_full_name) -> void;
  static auto prepareCharTimingSample() -> void;
  static auto setCharBufferInputSlew(double slew_ns) -> void;
  static auto setCharBufferInputSlewIncremental(double slew_ns) -> void;
  static auto updateCharTimingSample() -> void;
  static auto updateCharTimingIncrementalSample() -> void;
  static auto prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                               std::optional<std::string> source_input_pin_full_name = std::nullopt) -> bool;
  static auto refreshCharPowerLoad() -> bool;
  static auto updateCharPower() -> bool;
  static auto queryCharPower() -> double;
  static auto queryCharNetSwitchPower(const std::string& net_name) -> double;
  static auto destroyCharPower() -> void;
  static auto finishCharOnly() -> void;
  static auto setPropagatedClocks() -> std::size_t;
  static auto updateTiming() -> void;
  static auto reportTiming() -> bool;
  static auto refreshFullDesignTimingContext() -> void;
  static auto queryClockTiming(const std::string& clock_name) -> std::optional<ClockTimingMetrics>;
  static auto queryClockTimings() -> std::vector<ClockTimingRecord>;
  static auto queryClockLatencySkew() -> std::vector<ClockLatencySkewMetrics>;
  static auto installClockNetRcTree(const Net& cts_net, const ClockSteinerTree<int>& clock_tree) -> bool;
  static auto queryCharClockAT(const std::string& clock_name) -> double;
  static auto queryCharSlew() -> double;
  static auto queryCharInputPinCap(const std::string& cell_master) -> double;
  static auto queryPinCapacitance(const Pin* pin) -> double;
  static auto queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>;
  static auto emitUnitWireRcReport(const std::string& title, int routing_layer, std::optional<double> wire_width = std::nullopt) -> void;
  static auto emitConfiguredUnitWireRcReport(const std::string& title) -> void;
  static auto destroyCharInstance(const std::string& inst_name) -> void;
  static auto destroyCharNet(const std::string& net_name) -> void;

 private:
  STAAdapter() = default;
  ~STAAdapter();
  struct CharTimingState
  {
    bool is_ready = false;
    ista::Pin* source_input_pin = nullptr;
    ista::Pin* source_output_pin = nullptr;
    ista::StaVertex* source_input_vertex = nullptr;
    ista::StaVertex* source_output_vertex = nullptr;
    ista::StaVertex* sink_vertex = nullptr;
    ista::Instance* source_inst = nullptr;
    ista::LibCell* source_lib_cell = nullptr;
    ista::LibArcSet* source_arc_set = nullptr;
    ista::LibArc* source_lib_arc = nullptr;
  };

  struct CharPowerState
  {
    CharPowerState() = default;
    ~CharPowerState();
    CharPowerState(CharPowerState&& rhs) noexcept = default;
    auto operator=(CharPowerState&& rhs) noexcept -> CharPowerState& = default;
    CharPowerState(const CharPowerState& rhs) = delete;
    auto operator=(const CharPowerState& rhs) -> CharPowerState& = delete;

    std::vector<std::string> inst_names;
    std::vector<std::string> net_names;
    std::unordered_set<std::string> inst_name_set;
    std::unordered_set<std::string> net_name_set;
    std::optional<std::string> source_input_pin_full_name = std::nullopt;
    bool is_runtime_ready = false;
    bool is_switch_power_cached = false;
    double cached_leakage_power_w = 0.0;
    double cached_switch_power_w = 0.0;
    double last_total_power_w = 0.0;
    IctsCharPowerPtr char_power;
  };

  auto resetCharTimingState() -> void;
  auto resetCharPowerState() -> void;
  auto resetStaTransientState() -> void;

  bool _is_char_only_active = false;
  CharTimingState _char_timing_state;
  CharPowerState _char_power_state;
};

}  // namespace icts
