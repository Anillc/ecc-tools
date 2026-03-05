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
 * @file CTSAPI.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../../../database/interaction/ids.hpp"
namespace ieda_feature {
struct CTSSummary;
}  // namespace ieda_feature

namespace icts {

#define CTSAPIInst (icts::CTSAPI::getInst())

class CTSAPI
{
 public:
  static CTSAPI& getInst()
  {
    static CTSAPI inst;
    return inst;
  }

  // CTS CLI
  void runCTS();
  void report(const std::string& save_dir);

  // Flow API
  void resetAPI();
  void init(const std::string& config_file, const std::string& work_dir = "");
  void readData();
  void summaryClockDistribution();

  // Feature API
  ieda_feature::CTSSummary outputSummary();

  // STA API
  void initSTA();
  icts::InstType queryInstType(const std::string& inst_name) const;
  bool isFlipFlop(const std::string& inst_name) const;
  bool isClockNet(const std::string& net_name) const;
  double queryWireResistance(int routing_layer, double length, double wire_width = 0.0) const;
  double queryWireCapacitance(int routing_layer, double length, double wire_width = 0.0) const;
  double queryCellOutPinCapLimit(const std::string& cell_master) const;
  double queryCellInPinSlewLimit(const std::string& cell_master) const;

  // DB query API
  int32_t queryDbUnit() const;

  // Characterization API
  std::string createCharInstance(const std::string& cell_master, const std::string& inst_name);
  std::string createCharNet(const std::string& net_name);
  void attachCharPin(const std::string& inst_name, const std::string& port_name, const std::string& net_name);
  void buildCharRcTree(const std::string& net_name, double wire_res, double wire_cap, double load_cap);
  void createCharClock(const std::string& source_pin_full_name, const std::string& clock_name, double period_ns);
  void destroyCharClock();
  void setCharInputSlew(const std::string& pin_full_name, double slew_ns);
  void updateCharTiming();
  double queryCharClockAT(const std::string& pin_full_name, const std::string& clock_name) const;
  double queryCharSlew(const std::string& pin_full_name) const;
  double queryCharInputPinCap(const std::string& cell_master) const;
  std::pair<std::string, std::string> queryBufferPorts(const std::string& cell_master) const;
  void destroyCharInstance(const std::string& inst_name);
  void destroyCharNet(const std::string& net_name);

 private:
  CTSAPI() = default;
  CTSAPI(const CTSAPI& other) = delete;
  CTSAPI(CTSAPI&& other) = delete;
  ~CTSAPI() = default;
  CTSAPI& operator=(const CTSAPI& other) = delete;
  CTSAPI& operator=(CTSAPI&& other) = delete;
};

}  // namespace icts
