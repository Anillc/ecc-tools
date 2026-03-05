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
 * @file Inst.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief Minimal element representing a cell pin/port in CTS
 */

#pragma once

#include <string>
#include <vector>

#include "../spatial/Point.hh"
namespace icts {
class Pin;

enum class InstType
{
  kBuffer,     // up level buffer
  kFlipFlop,   // flip flop
  kInverter,   // inverter (like buffer but different polarity)
  kClockGate,  // clock gate (with enable pin)
  kMux,        // with multiple input pins
  kUnknown
};

class Inst
{
 public:
  Inst() = default;
  Inst(const std::string& _name, const std::string& cell_master, InstType type, const Point<int>& location)
      : _name(_name), _cell_master(cell_master), _type(type), _location(location)
  {
  }
  ~Inst() = default;

  // Getter
  const std::string& get_name() const { return _name; }
  const std::string& get_cell_master() const { return _cell_master; }
  InstType get_type() const { return _type; }
  const Point<int>& get_location() const { return _location; }

  // Setter
  void set_name(const std::string& name) { _name = name; }
  void set_cell_master(const std::string& cell_master) { _cell_master = cell_master; }
  void set_type(InstType type) { _type = type; }
  void set_location(const Point<int>& location) { _location = location; }

  const std::vector<Pin*>& get_pins() const { return _pins; }
  std::vector<Pin*>& get_pins() { return _pins; }
  Pin* findDriverPin() const { return _pins.empty() ? nullptr : _pins.front(); }
  void set_pins(const std::vector<Pin*>& pins) { _pins = pins; }
  void add_pin(Pin* pin) { _pins.push_back(pin); }
  void insertDriverPin(Pin* pin)
  {
    if (pin == nullptr) {
      return;
    }
    if (_pins.empty()) {
      _pins.push_back(pin);
      return;
    }
    if (_pins.front() == pin) {
      return;
    }
    _pins.insert(_pins.begin(), pin);
  }

  // Boolean functions
  bool is_buffer() const { return _type == InstType::kBuffer; }
  bool is_flipflop() const { return _type == InstType::kFlipFlop; }
  bool is_inverter() const { return _type == InstType::kInverter; }
  bool is_clock_gate() const { return _type == InstType::kClockGate; }
  bool is_mux() const { return _type == InstType::kMux; }

 private:
  std::string _name = "";
  std::string _cell_master = "";
  InstType _type = InstType::kUnknown;
  Point<int> _location = Point<int>(-1, -1);
  std::vector<Pin*> _pins;
};

}  // namespace icts
