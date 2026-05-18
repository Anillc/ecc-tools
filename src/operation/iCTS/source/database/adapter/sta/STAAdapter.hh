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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace icts {

enum class InstType;
class Net;
class Pin;
template <typename T>
class ClockSteinerTree;

}  // namespace icts

namespace icts {

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
  struct RootDriverCost
  {
    bool valid = false;
    std::string method;
    std::string cell_master;
    double input_slew_ns = 0.0;
    double output_load_pf = 0.0;
    double cell_delay_ns = 0.0;
    double output_slew_ns = 0.0;
    double internal_power_w = 0.0;
    double leakage_power_w = 0.0;
    double cell_power_w = 0.0;
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
  static auto queryInstType(const std::string& inst_name) -> icts::InstType;
  static auto isFlipFlop(const std::string& inst_name) -> bool;
  static auto queryWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryRequiredWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryRequiredWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryCellOutPinCapLimit(const std::string& cell_master) -> double;
  static auto queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double;
  static auto queryClockSourceDriveCapLimit(const Pin* clock_source) -> double;
  static auto queryCellInPinSlewLimit(const std::string& cell_master) -> double;
  static auto queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double;
  static auto queryCellHeightUm(const std::string& cell_master) -> double;
  static auto queryCellAreaUm2(const std::string& cell_master) -> double;
  static auto setPropagatedClocks() -> std::size_t;
  static auto updateTiming() -> void;
  static auto reportTiming() -> bool;
  static auto refreshFullDesignTimingContext() -> void;
  static auto queryClockTiming(const std::string& clock_name) -> std::optional<ClockTimingMetrics>;
  static auto queryClockTimings() -> std::vector<ClockTimingRecord>;
  static auto queryClockLatencySkew() -> std::vector<ClockLatencySkewMetrics>;
  static auto installClockNetRcTree(const Net& cts_net, const ClockSteinerTree<int>& clock_tree) -> bool;
  static auto queryCharInputPinCap(const std::string& cell_master) -> double;
  static auto queryPinCapacitance(const Pin* pin) -> double;
  static auto queryPinSlewLimit(const Pin* pin) -> double;
  static auto queryPinClockArrival(const Pin* pin, const std::string& clock_name) -> std::optional<double>;
  static auto queryPinSlew(const Pin* pin) -> std::optional<double>;
  static auto queryRootDriverCostDirect(const std::string& cell_master, double input_slew_ns, double output_load_pf, double clock_period_ns)
      -> RootDriverCost;
  static auto queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>;
  static auto emitUnitWireRcReport(const std::string& title, int routing_layer, std::optional<double> wire_width = std::nullopt) -> void;
  static auto emitConfiguredUnitWireRcReport(const std::string& title) -> void;

 private:
  STAAdapter() = default;
  ~STAAdapter() = default;
  auto resetStaTransientState() -> void;
};

}  // namespace icts
