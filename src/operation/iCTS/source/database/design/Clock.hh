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

#include <memory>
#include <string>
#include <utility>
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
  auto get_clock_name() const -> const std::string& { return _clock_name; }
  auto get_clock_net_name() const -> const std::string& { return _clock_net_name; }
  auto get_clock_source() const -> Pin* { return _clock_source; }
  auto get_loads() const -> const std::vector<Pin*>& { return _loads; }
  auto get_inserted_insts() const -> const std::vector<Inst*>& { return _inserted_insts; }
  auto get_inserted_nets() const -> const std::vector<Net*>& { return _inserted_nets; }

  // Setter
  auto set_clock_name(const std::string& clock_name) -> void { _clock_name = clock_name; }
  auto set_clock_net_name(const std::string& clock_net_name) -> void { _clock_net_name = clock_net_name; }
  auto set_clock_source(Pin* clock_source) -> void { _clock_source = clock_source; }
  auto set_loads(const std::vector<Pin*>& loads) -> void { _loads = loads; }

  // Adder
  auto add_load(Pin* load) -> void { _loads.push_back(load); }
  auto add_inserted_inst(Inst* inst) -> void { _inserted_insts.push_back(inst); }
  auto add_inserted_net(Net* net) -> void { _inserted_nets.push_back(net); }
  auto adoptInsertedCtsOwnership(std::vector<std::unique_ptr<Inst>> inserted_inst_storage,
                                 std::vector<std::unique_ptr<Pin>> inserted_pin_storage,
                                 std::vector<std::unique_ptr<Net>> inserted_net_storage) -> void
  {
    _inserted_inst_storage = std::move(inserted_inst_storage);
    _inserted_pin_storage = std::move(inserted_pin_storage);
    _inserted_net_storage = std::move(inserted_net_storage);
  }
  auto clearInsertedCtsObjects() -> void
  {
    _inserted_insts.clear();
    _inserted_nets.clear();
    _inserted_inst_storage.clear();
    _inserted_pin_storage.clear();
    _inserted_net_storage.clear();
  }

 private:
  std::string _clock_name = "";
  std::string _clock_net_name = "";
  Pin* _clock_source = nullptr;
  std::vector<Pin*> _loads;
  // CTS Result
  std::vector<Inst*> _inserted_insts;
  std::vector<Net*> _inserted_nets;
  std::vector<std::unique_ptr<Inst>> _inserted_inst_storage;
  std::vector<std::unique_ptr<Pin>> _inserted_pin_storage;
  std::vector<std::unique_ptr<Net>> _inserted_net_storage;
};

}  // namespace icts
