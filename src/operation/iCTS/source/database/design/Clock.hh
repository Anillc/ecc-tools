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
 * @file Clock.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief Main inputs of CTS
 */

#pragma once
#include <string>
#include <vector>

#include "Inst.hh"
#include "Net.hh"
#include "Pin.hh"
namespace icts {

class Clock
{
 public:
  Clock() = default;
  Clock(const std::string& clock_name, const std::string& clock_net_name) : _clock_name(clock_name), _clock_net_name(clock_net_name) {}
  Clock(const std::string& clock_name, const std::string& clock_net_name, Pin* clock_source, const std::vector<Pin*>& loads)
      : _clock_name(clock_name), _clock_net_name(clock_net_name), _clock_source(clock_source), _loads(loads)
  {
  }
  ~Clock() = default;

  // Getter
  const std::string& get_clock_name() const { return _clock_name; }
  const std::string& get_clock_net_name() const { return _clock_net_name; }
  Pin* get_clock_source() const { return _clock_source; }
  const std::vector<Pin*>& get_loads() const { return _loads; }

  // Setter
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_clock_net_name(const std::string& clock_net_name) { _clock_net_name = clock_net_name; }
  void set_clock_source(Pin* clock_source) { _clock_source = clock_source; }
  void set_loads(const std::vector<Pin*>& loads) { _loads = loads; }

  // Adder
  void add_load(Pin* load) { _loads.push_back(load); }
  void add_inserted_inst(Inst* inst) { _inserted_insts.push_back(inst); }
  void add_inserted_net(Net* net) { _inserted_nets.push_back(net); }

 private:
  std::string _clock_name = "";
  std::string _clock_net_name = "";
  Pin* _clock_source = nullptr;
  std::vector<Pin*> _loads;
  // CTS Result
  std::vector<Inst*> _inserted_insts;
  std::vector<Net*> _inserted_nets;
};

}  // namespace icts
