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
  static STAAdapter& getInst()
  {
    static STAAdapter inst;
    return inst;
  }

  STAAdapter(const STAAdapter& rhs) = delete;
  STAAdapter(STAAdapter&& rhs) = delete;
  STAAdapter& operator=(const STAAdapter& rhs) = delete;
  STAAdapter& operator=(STAAdapter&& rhs) = delete;

  static void init();

  static icts::InstType queryInstType(const std::string& inst_name);
  static bool isFlipFlop(const std::string& inst_name);
  static bool isClockNet(const std::string& net_name);
  static auto collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>;
  static double queryWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt);
  static double queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt);
  static double queryCellOutPinCapLimit(const std::string& cell_master);
  static double queryCellInPinSlewLimit(const std::string& cell_master);

  static std::string createCharInstance(const std::string& cell_master, const std::string& inst_name);
  static std::string createCharNet(const std::string& net_name);
  static void attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name);
  static void buildCharRcTree(const std::string& net_name, const CharRcTreeConfig& rc_tree_config);
  static void createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns);
  static void destroyCharClock();
  static void setCharInputSlew(const std::string& pin_full_name, double slew_ns);
  static void updateTiming();
  static double queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name);
  static double queryCharSlew(const std::string& pin_full_name);
  static double queryCharInputPinCap(const std::string& cell_master);
  static std::pair<std::string, std::string> queryBufferPorts(const std::string& cell_master);
  static void destroyCharInstance(const std::string& inst_name);
  static void destroyCharNet(const std::string& net_name);

 private:
  STAAdapter() = default;
  ~STAAdapter() = default;
};

}  // namespace icts
