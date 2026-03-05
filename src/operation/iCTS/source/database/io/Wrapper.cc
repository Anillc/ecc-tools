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
 * @file Wrapper.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-11
 * @brief DB wrapper for iCTS
 */
#include "Wrapper.hh"

#include <unordered_map>
#include <vector>

#include "CTSAPI.hh"
#include "Design.hh"

namespace icts {

void Wrapper::read()
{
  if (_idb == nullptr) {
    return;
  }
  auto* def_service = _idb->get_def_service();
  if (def_service == nullptr) {
    return;
  }
  auto* idb_design = def_service->get_design();
  if (idb_design == nullptr) {
    return;
  }

  auto* idb_net_list = _idb_design->get_net_list();
  std::ranges::for_each(CTSDesignInst.get_clocks(), [&](Clock* clock) {
    auto clock_name = clock->get_clock_name();
    auto clock_net_name = clock->get_clock_net_name();

    auto* idb_net = idb_net_list->find_net(clock_net_name);
    idbToCts(idb_net);
  });
}

Point<int> Wrapper::idbToCts(idb::IdbCoordinate<int32_t>& coord) const
{
  return Point<int>(coord.get_x(), coord.get_y());
}

PinType Wrapper::idbToCts(idb::IdbConnectType idb_pin_type, idb::IdbConnectDirection idb_pin_direction) const
{
  PinType pin_type;

  if (idb_pin_type == idb::IdbConnectType::kClock) {
    pin_type = PinType::kClock;
  } else if (idb_pin_direction == idb::IdbConnectDirection::kOutput) {
    pin_type = PinType::kOut;
  } else if (idb_pin_direction == idb::IdbConnectDirection::kInput) {
    pin_type = PinType::kIn;
  } else if (idb_pin_direction == idb::IdbConnectDirection::kInOut) {
    pin_type = PinType::kInOut;
  } else {
    pin_type = PinType::kOther;
  }
  return pin_type;
}

Pin* Wrapper::idbToCts(idb::IdbPin* idb_pin)
{
  if (_idb2cts_pin_map.contains(idb_pin)) {
    return _idb2cts_pin_map.at(idb_pin);
  }
  auto name = idb_pin->get_pin_name();
  auto* idb_term = idb_pin->get_term();
  auto type = idbToCts(idb_term->get_type(), idb_term->get_direction());
  auto location = idbToCts(*idb_pin->get_average_coordinate());
  auto is_io = idb_pin->is_io_pin();

  auto* cts_pin = new Pin(name, type, location, nullptr, nullptr, is_io);
  crossRef(idb_pin, cts_pin);
  return cts_pin;
}

Inst* Wrapper::idbToCts(idb::IdbInstance* idb_inst)
{
  if (_idb2cts_inst_map.contains(idb_inst)) {
    return _idb2cts_inst_map.at(idb_inst);
  }
  auto name = idb_inst->get_name();
  auto cell_master = idb_inst->get_cell_master()->get_name();
  auto location = idbToCts(*idb_inst->get_coordinate());

  auto type = CTSAPIInst.queryInstType(name);

  auto* cts_inst = new Inst(name, cell_master, type, location);
  crossRef(idb_inst, cts_inst);
  return cts_inst;
}

Net* Wrapper::idbToCts(idb::IdbNet* idb_net)
{
  if (_idb2cts_net_map.contains(idb_net)) {
    return _idb2cts_net_map.at(idb_net);
  }
  auto name = idb_net->get_net_name();
  auto* cts_net = new Net(name);
  crossRef(idb_net, cts_net);

  // Build pins and connections
  auto* idb_driver_pin = idb_net->get_driving_pin();
  auto idb_load_pins = idb_net->get_load_pins();
  std::vector<idb::IdbPin*> idb_pins = {idb_driver_pin};
  std::ranges::copy(idb_load_pins, std::back_inserter(idb_pins));

  for (auto* idb_pin : idb_pins) {
    auto* cts_pin = idbToCts(idb_pin);
    if (idb_pin == idb_driver_pin) {
      cts_net->set_driver(cts_pin);
    } else {
      cts_net->add_load(cts_pin);
    }

    auto* idb_inst = idb_pin->get_instance();
    if (idb_inst == nullptr) {
      continue;
    }
    auto* cts_inst = idbToCts(idb_inst);
    cts_inst->add_pin(cts_pin);
    cts_pin->set_inst(cts_inst);
  }
  return cts_net;
}

idb::IdbCoordinate<int32_t> Wrapper::ctsToIdb(const Point<int>& loc) const
{
  return idb::IdbCoordinate<int32_t>(loc.get_x(), loc.get_y());
}

idb::IdbPin* Wrapper::ctsToIdb(Pin* pin)
{
  if (_cts2idb_pin_map.contains(pin)) {
    return _cts2idb_pin_map.at(pin);
  }
  return nullptr;
}

idb::IdbInstance* Wrapper::ctsToIdb(Inst* inst)
{
  if (_cts2idb_inst_map.contains(inst)) {
    return _cts2idb_inst_map.at(inst);
  }
  return nullptr;
}

idb::IdbNet* Wrapper::ctsToIdb(Net* net)
{
  if (_cts2idb_net_map.contains(net)) {
    return _cts2idb_net_map.at(net);
  }
  return nullptr;
}

}  // namespace icts
