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

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace icts {

enum class InstType;
class Pin;

#define STA_ADAPTER_INST (icts::STAAdapter::getInst())

class STAAdapter
{
 public:
  struct CharRcTreeConfig
  {
    double wire_res = 0.0;
    double wire_cap = 0.0;
    double load_cap = 0.0;
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
  static auto isClockNet(const std::string& net_name) -> bool;
  static auto collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>;
  static auto queryWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) -> double;
  static auto queryCellOutPinCapLimit(const std::string& cell_master) -> double;
  static auto queryCellOutPinCapTableAxisMax(const std::string& cell_master) -> double;
  static auto queryCellInPinSlewLimit(const std::string& cell_master) -> double;
  static auto queryCellInPinSlewTableAxisMax(const std::string& cell_master) -> double;
  static auto queryCellHeightUm(const std::string& cell_master) -> double;

  static auto createCharInstance(const std::string& cell_master, const std::string& inst_name) -> std::string;
  static auto createCharNet(const std::string& net_name) -> std::string;
  static auto attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name) -> void;
  static auto buildCharNetGraph(const std::string& net_name) -> void;
  static auto buildCharRcTree(const std::string& net_name, const CharRcTreeConfig& rc_tree_config) -> void;
  static auto createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns) -> void;
  static auto destroyCharClock() -> void;
  static auto setCharInputSlew(const std::string& pin_full_name, double slew_ns) -> void;
  static auto setCharBufferInputSlew(const std::string& input_pin_full_name, const std::string& output_pin_full_name, double slew_ns)
      -> void;
  static auto prepareCharTiming() -> void;
  static auto updateCharTiming() -> void;
  static auto prepareCharPower(const std::vector<std::string>& inst_names, const std::vector<std::string>& net_names,
                               std::optional<std::string> source_input_pin_full_name = std::nullopt) -> bool;
  static auto updateCharPower() -> bool;
  static auto queryCharPower() -> double;
  static auto destroyCharPower() -> void;
  static auto finishCharOnly() -> void;
  static auto updateTiming() -> void;
  static auto queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name) -> double;
  static auto queryCharSlew(const std::string& pin_full_name) -> double;
  static auto queryCharInputPinCap(const std::string& cell_master) -> double;
  static auto queryPinCapacitance(const Pin* pin) -> double;
  static auto queryBufferPorts(const std::string& cell_master) -> std::pair<std::string, std::string>;
  static auto destroyCharInstance(const std::string& inst_name) -> void;
  static auto destroyCharNet(const std::string& net_name) -> void;

 private:
  STAAdapter() = default;
  ~STAAdapter() = default;

  bool _is_char_only_active = false;
  std::vector<std::string> _char_power_inst_names;
  std::vector<std::string> _char_power_net_names;
  std::optional<std::string> _char_power_source_input_pin_full_name = std::nullopt;
  double _last_char_power_w = 0.0;
};

}  // namespace icts
