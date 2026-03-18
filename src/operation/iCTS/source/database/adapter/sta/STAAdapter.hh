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

#define STAAdapterInst (icts::STAAdapter::getInst())

class STAAdapter
{
 public:
  static STAAdapter& getInst()
  {
    static STAAdapter inst;
    return inst;
  }

  STAAdapter(const STAAdapter& rhs) = delete;
  STAAdapter(STAAdapter&& rhs) = delete;
  STAAdapter& operator=(const STAAdapter& rhs) = delete;
  STAAdapter& operator=(STAAdapter&& rhs) = delete;

  void init();

  icts::InstType queryInstType(const std::string& inst_name) const;
  bool isFlipFlop(const std::string& inst_name) const;
  bool isClockNet(const std::string& net_name) const;
  std::vector<std::pair<std::string, std::string>> collectClockNetPairs() const;
  double queryWireResistance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) const;
  double queryWireCapacitance(int routing_layer, double length, std::optional<double> wire_width = std::nullopt) const;
  double queryCellOutPinCapLimit(const std::string& cell_master) const;
  double queryCellInPinSlewLimit(const std::string& cell_master) const;

  std::string createCharInstance(const std::string& cell_master, const std::string& inst_name);
  std::string createCharNet(const std::string& net_name);
  void attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name);
  void buildCharRcTree(const std::string& net_name, double wire_res, double wire_cap, double load_cap);
  void createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns);
  void destroyCharClock();
  void setCharInputSlew(const std::string& pin_full_name, double slew_ns);
  void updateTiming();
  double queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name) const;
  double queryCharSlew(const std::string& pin_full_name) const;
  double queryCharInputPinCap(const std::string& cell_master) const;
  std::pair<std::string, std::string> queryBufferPorts(const std::string& cell_master) const;
  void destroyCharInstance(const std::string& inst_name);
  void destroyCharNet(const std::string& net_name);

 private:
  STAAdapter() = default;
  ~STAAdapter() = default;
};

}  // namespace icts
