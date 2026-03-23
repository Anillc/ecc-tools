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

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbCore.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbGeometry.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "IdbUnits.h"
#include "adapter/sta/STAAdapter.hh"
#include "builder.h"
#include "def_service.h"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "lef_service.h"
#include "logger/Logger.hh"
#include "spatial/Point.hh"

namespace icts {
namespace {

auto ConvertIdbPinType(idb::IdbConnectType idb_pin_type, idb::IdbConnectDirection idb_pin_direction) -> PinType
{
  if (idb_pin_type == idb::IdbConnectType::kClock) {
    return PinType::kClock;
  }
  if (idb_pin_direction == idb::IdbConnectDirection::kOutput) {
    return PinType::kOut;
  }
  if (idb_pin_direction == idb::IdbConnectDirection::kInput) {
    return PinType::kIn;
  }
  if (idb_pin_direction == idb::IdbConnectDirection::kInOut) {
    return PinType::kInOut;
  }
  return PinType::kOther;
}

}  // namespace

void Wrapper::init(idb::IdbBuilder* idb)
{
  _idb = idb;
  _idb_design = _idb->get_def_service()->get_design();
  _idb_layout = _idb->get_lef_service()->get_layout();
}

auto Wrapper::queryDbUnit() const -> int32_t
{
  if (_idb_design == nullptr || _idb_design->get_units() == nullptr) {
    CTS_LOG_ERROR << "iDB design units are not ready in Wrapper.";
    return 1;
  }
  return _idb_design->get_units()->get_micron_dbu();
}

auto Wrapper::withinCore(int32_t point_x, int32_t point_y) const -> bool
{
  if (_idb_layout == nullptr) {
    CTS_LOG_WARNING << "iDB layout is null when checking core boundary; treating point as inside core.";
    return true;
  }
  auto* core = _idb_layout->get_core();
  if (core == nullptr || core->get_bounding_box() == nullptr) {
    CTS_LOG_WARNING << "iDB core boundary is unavailable; treating point as inside core.";
    return true;
  }
  auto* core_box = core->get_bounding_box();
  return point_x >= core_box->get_low_x() && point_x <= core_box->get_high_x() && point_y >= core_box->get_low_y()
         && point_y <= core_box->get_high_y();
}

void Wrapper::read()
{
  if (_idb == nullptr) {
    CTS_LOG_WARNING << "Skip wrapper read: iDB builder is null.";
    return;
  }
  auto* def_service = _idb->get_def_service();
  if (def_service == nullptr) {
    CTS_LOG_WARNING << "Skip wrapper read: DEF service is null.";
    return;
  }
  auto* idb_design = def_service->get_design();
  if (idb_design == nullptr) {
    CTS_LOG_WARNING << "Skip wrapper read: iDB design is null.";
    return;
  }

  auto* idb_net_list = idb_design->get_net_list();
  if (idb_net_list == nullptr) {
    CTS_LOG_WARNING << "Skip wrapper read: iDB net list is null.";
    return;
  }

  std::ranges::for_each(DESIGN_INST.get_clocks(), [&](Clock* clock) {
    if (clock == nullptr) {
      CTS_LOG_WARNING << "Skip null clock in CTS design.";
      return;
    }

    const auto& clock_net_name = clock->get_clock_net_name();
    auto* idb_net = idb_net_list->find_net(clock_net_name);
    if (idb_net == nullptr) {
      CTS_LOG_WARNING << "Clock net \"" << clock_net_name << "\" is not found in iDB.";
      return;
    }

    auto* cts_net = idbToCts(idb_net);
    if (cts_net == nullptr) {
      CTS_LOG_WARNING << "Failed to convert clock net \"" << clock_net_name << "\" from iDB to CTS.";
      return;
    }

    clock->set_clock_source(cts_net->get_driver());
    clock->set_loads(cts_net->get_loads());
  });
}

auto Wrapper::idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>
{
  return {coord.get_x(), coord.get_y()};
}

auto Wrapper::idbToCts(idb::IdbPin* idb_pin) -> Pin*
{
  if (idb_pin == nullptr) {
    return nullptr;
  }
  if (_idb2cts_pin_map.contains(idb_pin)) {
    return _idb2cts_pin_map.at(idb_pin);
  }

  auto* idb_term = idb_pin->get_term();
  if (idb_term == nullptr) {
    CTS_LOG_WARNING << "Skip converting pin \"" << idb_pin->get_pin_name() << "\": term is null.";
    return nullptr;
  }
  auto* avg_coord = idb_pin->get_average_coordinate();
  if (avg_coord == nullptr) {
    CTS_LOG_WARNING << "Skip converting pin \"" << idb_pin->get_pin_name() << "\": average coordinate is null.";
    return nullptr;
  }

  auto name = idb_pin->get_pin_name();
  auto type = ConvertIdbPinType(idb_term->get_type(), idb_term->get_direction());
  auto location = idbToCts(*avg_coord);
  auto is_io = idb_pin->is_io_pin();

  auto cts_pin = std::make_unique<Pin>(name, type, location, nullptr, nullptr, is_io);
  auto* cts_pin_ptr = cts_pin.get();
  _owned_pins.push_back(std::move(cts_pin));
  crossRef(idb_pin, cts_pin_ptr);
  return cts_pin_ptr;
}

auto Wrapper::idbToCts(idb::IdbInstance* idb_inst) -> Inst*
{
  if (idb_inst == nullptr) {
    return nullptr;
  }
  if (_idb2cts_inst_map.contains(idb_inst)) {
    return _idb2cts_inst_map.at(idb_inst);
  }

  auto* cell_master = idb_inst->get_cell_master();
  auto* coord = idb_inst->get_coordinate();
  if (cell_master == nullptr) {
    CTS_LOG_WARNING << "Skip converting instance \"" << idb_inst->get_name() << "\": cell master is null.";
    return nullptr;
  }
  if (coord == nullptr) {
    CTS_LOG_WARNING << "Skip converting instance \"" << idb_inst->get_name() << "\": coordinate is null.";
    return nullptr;
  }

  auto name = idb_inst->get_name();
  auto cell_master_name = cell_master->get_name();
  auto location = idbToCts(*coord);
  auto type = STA_ADAPTER_INST.queryInstType(name);

  auto cts_inst = std::make_unique<Inst>(name, cell_master_name, type, location);
  auto* cts_inst_ptr = cts_inst.get();
  _owned_insts.push_back(std::move(cts_inst));
  crossRef(idb_inst, cts_inst_ptr);
  return cts_inst_ptr;
}

auto Wrapper::idbToCts(idb::IdbNet* idb_net) -> Net*
{
  if (idb_net == nullptr) {
    return nullptr;
  }
  if (_idb2cts_net_map.contains(idb_net)) {
    return _idb2cts_net_map.at(idb_net);
  }

  auto name = idb_net->get_net_name();
  auto cts_net = std::make_unique<Net>(name);
  auto* cts_net_ptr = cts_net.get();
  _owned_nets.push_back(std::move(cts_net));
  crossRef(idb_net, cts_net_ptr);

  auto* idb_driver_pin = idb_net->get_driving_pin();
  if (idb_driver_pin == nullptr) {
    CTS_LOG_WARNING << "Clock net \"" << name << "\" has no driving pin in iDB.";
  }

  auto idb_load_pins = idb_net->get_load_pins();
  std::vector<idb::IdbPin*> idb_pins;
  if (idb_driver_pin != nullptr) {
    idb_pins.push_back(idb_driver_pin);
  }
  std::ranges::copy(idb_load_pins, std::back_inserter(idb_pins));

  for (auto* idb_pin : idb_pins) {
    auto* cts_pin = idbToCts(idb_pin);
    if (cts_pin == nullptr) {
      continue;
    }

    cts_pin->set_net(cts_net_ptr);
    if (idb_pin == idb_driver_pin) {
      cts_net_ptr->set_driver(cts_pin);
    } else {
      cts_net_ptr->add_load(cts_pin);
    }

    auto* idb_inst = idb_pin->get_instance();
    if (idb_inst == nullptr) {
      continue;
    }
    auto* cts_inst = idbToCts(idb_inst);
    if (cts_inst == nullptr) {
      continue;
    }
    cts_pin->set_inst(cts_inst);
    if (idb_pin == idb_driver_pin) {
      cts_inst->insertDriverPin(cts_pin);
    } else {
      cts_inst->add_pin(cts_pin);
    }
  }
  return cts_net_ptr;
}

auto Wrapper::ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>
{
  return {loc.get_x(), loc.get_y()};
}

auto Wrapper::ctsToIdb(Pin* pin) -> idb::IdbPin*
{
  if (_cts2idb_pin_map.contains(pin)) {
    return _cts2idb_pin_map.at(pin);
  }
  return nullptr;
}

auto Wrapper::ctsToIdb(Inst* inst) -> idb::IdbInstance*
{
  if (_cts2idb_inst_map.contains(inst)) {
    return _cts2idb_inst_map.at(inst);
  }
  return nullptr;
}

auto Wrapper::ctsToIdb(Net* net) -> idb::IdbNet*
{
  if (_cts2idb_net_map.contains(net)) {
    return _cts2idb_net_map.at(net);
  }
  return nullptr;
}

}  // namespace icts
