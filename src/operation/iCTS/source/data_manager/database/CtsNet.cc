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
 * @file CtsNet.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "CtsNet.hh"
#include <algorithm>
#include <set>

#include "log/Log.hh"
namespace icts {
class CtsInstance;

void CtsNet::addPin(CtsPin* pin)
{
  pin->set_net(this);
  _pins.push_back(pin);
}

CtsPin* CtsNet::get_driver_pin() const
{
  std::vector<CtsPin*> driver_pin_candidates;
  for (auto* pin : _pins) {
    if (pin == nullptr) {
      continue;
    }
    if (pin->is_io()) {
      if (pin->get_pin_type() == CtsPinType::kIn || pin->get_pin_type() == CtsPinType::kInOut
          || pin->get_pin_type() == CtsPinType::kClock) {
        driver_pin_candidates.push_back(pin);
      }
    } else {
      if (pin->get_pin_type() == CtsPinType::kOut || pin->get_pin_type() == CtsPinType::kInOut) {
        driver_pin_candidates.push_back(pin);
      }
    }
  }
  if (driver_pin_candidates.size() == 1) {
    return driver_pin_candidates[0];
  } else if (driver_pin_candidates.size() > 1) {
    for (auto* pin : driver_pin_candidates) {
      if (pin->get_pin_type() == CtsPinType::kInOut) {
        return pin;  // Prefer InOut pins as driver
      }
    }
    return driver_pin_candidates[0];  // If no InOut pins, return the first candidate
  }

  for (auto* pin : _pins) {
    if (pin != nullptr && pin->is_io()) {
      LOG_WARNING << "Fallback to IO pin " << pin->get_full_name() << " as driver for net " << _net_name;
      return pin;
    }
  }

  if (!_pins.empty() && _pins.front() != nullptr) {
    LOG_WARNING << "Fallback to first pin " << _pins.front()->get_full_name() << " as driver for net " << _net_name;
    return _pins.front();
  }

  LOG_WARNING << "No driver pin found for net " << _net_name;
  return nullptr;
}

CtsInstance* CtsNet::get_driver_inst(CtsPin* pin) const
{
  CtsPin* this_pin = pin != nullptr ? pin : get_driver_pin();

  return this_pin ? this_pin->get_instance() : nullptr;
}

CtsPin* CtsNet::findPin(const std::string& pin_name)
{
  for (auto pin : _pins) {
    if (pin_name == pin->get_pin_name()) {
      return pin;
    }
  }
  return nullptr;
}

std::vector<CtsInstance*> CtsNet::get_instances() const
{
  std::vector<CtsInstance*> insts;
  for (auto* pin : _pins) {
    auto* inst = pin->get_instance();
    insts.emplace_back(inst);
  }
  return insts;
}

std::vector<CtsPin*> CtsNet::get_load_pins() const
{
  auto driver_pin = get_driver_pin();
  std::vector<CtsPin*> load_pins;
  // std::set<std::string> unique_insts;
  for (auto* pin : _pins) {
    if (pin != nullptr && pin != driver_pin) {
      // if (unique_insts.count(pin->get_instance()->get_name()) == 0) {
      //   unique_insts.insert(pin->get_instance()->get_name());
      // } else {
      //   continue;
      // }
      load_pins.push_back(pin);
    }
  }
  return load_pins;
}

std::vector<CtsInstance*> CtsNet::get_load_insts() const
{
  std::vector<CtsInstance*> insts;
  for (auto* pin : get_load_pins()) {
    insts.push_back(pin->get_instance());
  }
  return insts;
}

void CtsNet::removePin(CtsPin* pin)
{
  auto iter = std::find(_pins.begin(), _pins.end(), pin);
  if (iter != _pins.end()) {
    _pins.erase(iter);
  }
}

std::vector<CtsSignalWire>& CtsNet::get_signal_wires()
{
  if (_signal_wires.empty()) {
    const auto* driver_pin = get_driver_pin();
    LOG_FATAL_IF(driver_pin == nullptr) << "No driver pin found for net " << _net_name;
    Endpoint first = {driver_pin->get_full_name(), driver_pin->get_location()};
    for (auto* load_pin : get_load_pins()) {
      Endpoint second = {load_pin->get_full_name(), load_pin->get_location()};
      add_signal_wire(CtsSignalWire(first, second));
    }
  }
  return _signal_wires;
}
CtsInstance* CtsNet::findInstance(const std::string& inst_name)
{
  for (auto* pin : _pins) {
    if (pin->get_instance() && pin->get_instance()->get_name() == inst_name) {
      return pin->get_instance();
    }
  }
  return nullptr;
}
}  // namespace icts
