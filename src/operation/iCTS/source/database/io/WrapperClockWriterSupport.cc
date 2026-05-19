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
 * @file WrapperClockWriterSupport.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Internal clock writeback support routines for the iCTS iDB wrapper
 */

#include <string>
#include <vector>

#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "WrapperClockWriterInternal.hh"

namespace icts {

auto AppendIdbPinToNet(idb::IdbNet* idb_net, idb::IdbPin* idb_pin) -> void
{
  if (idb_net == nullptr || idb_pin == nullptr) {
    return;
  }
  auto* old_net = idb_pin->get_net();
  if (old_net != nullptr && old_net != idb_net) {
    old_net->remove_pin(idb_pin);
  }
  idb_pin->set_net(idb_net);
  idb_pin->set_net_name(idb_net->get_net_name());
  auto* pin_list = idb_pin->is_io_pin() ? idb_net->get_io_pins() : idb_net->get_instance_pin_list();
  if (pin_list == nullptr || pin_list->find_pin(idb_pin) != nullptr) {
    return;
  }
  if (idb_pin->is_io_pin()) {
    idb_net->add_io_pin(idb_pin);
  } else {
    idb_net->add_instance_pin(idb_pin);
  }
}

auto ClearIdbNetPins(idb::IdbNet* idb_net) -> void
{
  if (idb_net == nullptr) {
    return;
  }
  std::vector<idb::IdbPin*> pins;
  if (idb_net->get_io_pins() != nullptr) {
    const auto& io_pins = idb_net->get_io_pins()->get_pin_list();
    pins.insert(pins.end(), io_pins.begin(), io_pins.end());
  }
  if (idb_net->get_instance_pin_list() != nullptr) {
    const auto& inst_pins = idb_net->get_instance_pin_list()->get_pin_list();
    pins.insert(pins.end(), inst_pins.begin(), inst_pins.end());
  }
  for (auto* pin : pins) {
    idb_net->remove_pin(pin);
  }
}

auto FindIdbPinByTermOrPinName(idb::IdbInstance* idb_inst, const std::string& pin_name) -> idb::IdbPin*
{
  if (idb_inst == nullptr || idb_inst->get_pin_list() == nullptr || pin_name.empty()) {
    return nullptr;
  }
  for (auto* idb_pin : idb_inst->get_pin_list()->get_pin_list()) {
    if (idb_pin == nullptr) {
      continue;
    }
    if (idb_pin->get_pin_name() == pin_name) {
      return idb_pin;
    }
    auto* idb_term = idb_pin->get_term();
    if (idb_term != nullptr && idb_term->get_name() == pin_name) {
      return idb_pin;
    }
  }
  return nullptr;
}

auto SnapshotIdbNetPins(idb::IdbNet* idb_net) -> IdbNetPinSnapshot
{
  IdbNetPinSnapshot snapshot;
  if (idb_net == nullptr) {
    return snapshot;
  }
  if (idb_net->get_io_pins() != nullptr) {
    snapshot.io_pins = idb_net->get_io_pins()->get_pin_list();
  }
  if (idb_net->get_instance_pin_list() != nullptr) {
    snapshot.inst_pins = idb_net->get_instance_pin_list()->get_pin_list();
  }
  return snapshot;
}

}  // namespace icts
