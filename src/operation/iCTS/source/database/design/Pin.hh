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
 * @file Pin.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-12
 * @brief Minimal pin representation for CTS
 */

#pragma once

#include <string>

#include "spatial/Point.hh"
namespace icts {

class Inst;
class Net;

enum class PinType
{
  kClock,
  kIn,
  kOut,
  kInOut,
  kOther
};

class Pin
{
 public:
  Pin() = default;
  Pin(const std::string& name, PinType type = PinType::kOther, Point<int> location = Point<int>(-1, -1), Inst* inst = nullptr,
      Net* net = nullptr, bool b_io = false)
      : _name(name), _type(type), _location(location), _inst(inst), _net(net), _b_io(b_io)
  {
  }
  ~Pin() = default;

  // Getter
  const std::string& get_name() const { return _name; }
  PinType get_type() const { return _type; }
  Point<int> get_location() const { return _location; }
  Inst* get_inst() const { return _inst; }
  Net* get_net() const { return _net; }

  // Setter
  void set_name(const std::string& name) { _name = name; }
  void set_type(PinType type) { _type = type; }
  void set_location(const Point<int>& location) { _location = location; }
  void set_inst(Inst* inst) { _inst = inst; }
  void set_net(Net* net) { _net = net; }
  void set_io(bool b_io) { _b_io = b_io; }

  // Boolean functions
  bool is_io() const { return _b_io; }

 private:
  std::string _name = "";
  PinType _type = PinType::kOther;
  Point<int> _location = Point<int>(-1, -1);
  Inst* _inst = nullptr;
  Net* _net = nullptr;
  bool _b_io = false;
};

}  // namespace icts
