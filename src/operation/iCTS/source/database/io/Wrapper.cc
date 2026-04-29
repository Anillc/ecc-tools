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

#include <glog/logging.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
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
#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "builder.h"
#include "def_service.h"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "idm.h"
#include "lef_service.h"
#include "spatial/Point.hh"

namespace icts {
namespace {

auto convertIdbPinType(idb::IdbConnectType idb_pin_type, idb::IdbConnectDirection idb_pin_direction) -> PinType
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

auto appendIdbPinToNet(idb::IdbNet* idb_net, idb::IdbPin* idb_pin) -> void
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
  if (pin_list != nullptr && pin_list->find_pin(idb_pin) == nullptr) {
    if (idb_pin->is_io_pin()) {
      idb_net->add_io_pin(idb_pin);
    } else {
      idb_net->add_instance_pin(idb_pin);
    }
  }
}

auto BuildCellGeometry(idb::IdbInstance* idb_inst) -> WrapperCellGeometry
{
  WrapperCellGeometry geometry;
  if (idb_inst == nullptr) {
    return geometry;
  }

  auto* cell_master = idb_inst->get_cell_master();
  auto* bbox = idb_inst->get_bounding_box();
  auto* coordinate = idb_inst->get_coordinate();
  geometry.name = idb_inst->get_name();
  geometry.cell_master = cell_master == nullptr ? std::string{} : cell_master->get_name();
  if (bbox != nullptr) {
    geometry.origin = Point<int>(bbox->get_low_x(), bbox->get_low_y());
    geometry.width_dbu = bbox->get_width();
    geometry.height_dbu = bbox->get_height();
  } else if (coordinate != nullptr) {
    geometry.origin = Point<int>(coordinate->get_x(), coordinate->get_y());
    if (cell_master != nullptr) {
      geometry.width_dbu = static_cast<int32_t>(cell_master->get_width());
      geometry.height_dbu = static_cast<int32_t>(cell_master->get_height());
    }
  }
  return geometry;
}

auto clearIdbNetPins(idb::IdbNet* idb_net) -> void
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

}  // namespace

auto Wrapper::init(idb::IdbBuilder* idb) -> void
{
  _idb = idb;
  _idb_design = _idb->get_def_service()->get_design();
  _idb_layout = _idb->get_lef_service()->get_layout();
}

auto Wrapper::queryDbUnit() const -> int32_t
{
  if (_idb_design == nullptr || _idb_design->get_units() == nullptr) {
    LOG_ERROR << "iDB design units are not ready in Wrapper.";
    return 1;
  }
  return _idb_design->get_units()->get_micron_dbu();
}

auto Wrapper::withinCore(int32_t point_x, int32_t point_y) const -> bool
{
  if (_idb_layout == nullptr) {
    LOG_WARNING << "iDB layout is null when checking core boundary; treating point as inside core.";
    return true;
  }
  auto* core = _idb_layout->get_core();
  if (core == nullptr || core->get_bounding_box() == nullptr) {
    LOG_WARNING << "iDB core boundary is unavailable; treating point as inside core.";
    return true;
  }
  auto* core_box = core->get_bounding_box();
  return point_x >= core_box->get_low_x() && point_x <= core_box->get_high_x() && point_y >= core_box->get_low_y()
         && point_y <= core_box->get_high_y();
}

auto Wrapper::isClockNet(const std::string& net_name) -> bool
{
  if (_idb_design == nullptr || _idb_design->get_net_list() == nullptr) {
    LOG_WARNING << "Cannot query clock net \"" << net_name << "\": iDB net list is not ready.";
    return false;
  }

  auto* idb_net = _idb_design->get_net_list()->find_net(net_name);
  if (idb_net == nullptr) {
    return false;
  }
  return idb_net->is_clock();
}

auto Wrapper::collectClockNetPairs() -> std::vector<std::pair<std::string, std::string>>
{
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  if (_idb_design == nullptr || _idb_design->get_net_list() == nullptr) {
    LOG_WARNING << "Cannot collect iDB clock nets: iDB net list is not ready.";
    return clock_net_pairs;
  }

  if (dmInst->get_idb_design() == nullptr || dmInst->get_idb_design()->get_net_list() == nullptr) {
    LOG_WARNING << "Cannot collect iDB clock nets: idm net list is not ready.";
    return clock_net_pairs;
  }

  for (auto* idb_net : dmInst->getClockNetList()) {
    if (idb_net == nullptr) {
      continue;
    }
    const auto net_name = idb_net->get_net_name();
    clock_net_pairs.emplace_back(net_name, net_name);
  }
  return clock_net_pairs;
}

auto Wrapper::read() -> void
{
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  for (const auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    // TBD: maybe bug if clock net name is different from clock name; need to query clock name from STA
    clock_net_pairs.emplace_back(clock->get_clock_name(), clock->get_clock_net_name());
  }
  readClocks(clock_net_pairs);
}

auto Wrapper::readClocks() -> void
{
  readClocks(collectClockNetPairs());
}

auto Wrapper::readClocks(const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> void
{
  if (_idb == nullptr) {
    LOG_WARNING << "Skip wrapper read: iDB builder is null.";
    return;
  }
  auto* def_service = _idb->get_def_service();
  if (def_service == nullptr) {
    LOG_WARNING << "Skip wrapper read: DEF service is null.";
    return;
  }
  auto* idb_design = def_service->get_design();
  if (idb_design == nullptr) {
    LOG_WARNING << "Skip wrapper read: iDB design is null.";
    return;
  }

  auto* idb_net_list = idb_design->get_net_list();
  if (idb_net_list == nullptr) {
    LOG_WARNING << "Skip wrapper read: iDB net list is null.";
    return;
  }

  _cts2idb_inst_map.clear();
  _idb2cts_inst_map.clear();
  _cts2idb_net_map.clear();
  _idb2cts_net_map.clear();
  _cts2idb_pin_map.clear();
  _idb2cts_pin_map.clear();
  DESIGN_INST.clearClocks();
  DESIGN_INST.clearTopologyObjects();

  for (const auto& [clock_name, clock_net_name] : clock_net_pairs) {
    auto* idb_net = idb_net_list->find_net(clock_net_name);
    if (idb_net == nullptr) {
      LOG_WARNING << "Clock net \"" << clock_net_name << "\" is not found in iDB.";
      continue;
    }

    auto* clock = readClock(clock_name, clock_net_name, idb_net);
    if (clock == nullptr || clock->get_clock_source_net() == nullptr) {
      LOG_WARNING << "Failed to convert clock net \"" << clock_net_name << "\" from iDB to CTS.";
      continue;
    }
  }
}

auto Wrapper::idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>
{
  return {coord.get_x(), coord.get_y()};
}

auto Wrapper::idbToCts(idb::IdbInstance* idb_inst) -> Inst*
{
  if (idb_inst == nullptr) {
    return nullptr;
  }

  auto* cell_master = idb_inst->get_cell_master();
  auto* coord = idb_inst->get_coordinate();
  if (cell_master == nullptr) {
    LOG_WARNING << "Skip converting instance \"" << idb_inst->get_name() << "\": cell master is null.";
  }
  if (coord == nullptr) {
    LOG_WARNING << "Skip converting instance \"" << idb_inst->get_name() << "\": coordinate is null.";
  }
  if (cell_master == nullptr || coord == nullptr) {
    return nullptr;
  }

  const auto inst_name = idb_inst->get_name();
  auto* cts_inst = DESIGN_INST.makeInst(inst_name);
  if (cts_inst == nullptr) {
    return nullptr;
  }

  cts_inst->set_name(inst_name);
  cts_inst->set_cell_master(cell_master->get_name());
  cts_inst->set_type(STA_ADAPTER_INST.queryInstType(inst_name));
  cts_inst->set_location(idbToCts(*coord));
  crossRef(idb_inst, cts_inst);
  return cts_inst;
}

auto Wrapper::idbToCts(idb::IdbPin* idb_pin) -> Pin*
{
  if (idb_pin == nullptr) {
    return nullptr;
  }

  auto* idb_term = idb_pin->get_term();
  if (idb_term == nullptr) {
    LOG_WARNING << "Skip converting pin \"" << idb_pin->get_pin_name() << "\": term is null.";
    return nullptr;
  }
  auto* avg_coord = idb_pin->get_average_coordinate();
  if (avg_coord == nullptr) {
    LOG_WARNING << "Skip converting pin \"" << idb_pin->get_pin_name() << "\": average coordinate is null.";
    return nullptr;
  }

  auto* cts_pin = DESIGN_INST.makePin(idb_term->get_name());
  if (cts_pin == nullptr) {
    return nullptr;
  }

  cts_pin->set_name(idb_term->get_name());
  cts_pin->set_type(convertIdbPinType(idb_term->get_type(), idb_term->get_direction()));
  cts_pin->set_location(idbToCts(*avg_coord));
  cts_pin->set_io(idb_pin->is_io_pin());
  crossRef(idb_pin, cts_pin);
  return cts_pin;
}

auto Wrapper::idbToCts(idb::IdbNet* idb_net) -> Net*
{
  if (idb_net == nullptr) {
    return nullptr;
  }

  auto* cts_net_ptr = DESIGN_INST.makeNet(idb_net->get_net_name());
  if (cts_net_ptr == nullptr) {
    return nullptr;
  }

  cts_net_ptr->set_name(idb_net->get_net_name());
  crossRef(idb_net, cts_net_ptr);
  return cts_net_ptr;
}

auto Wrapper::readClock(const std::string& clock_name, const std::string& clock_net_name, idb::IdbNet* idb_net) -> Clock*
{
  if (idb_net == nullptr) {
    return nullptr;
  }

  auto* clock = DESIGN_INST.makeClock(clock_name, clock_net_name);
  if (clock == nullptr) {
    return nullptr;
  }
  clock->set_clock_name(clock_name);
  clock->set_clock_net_name(clock_net_name);
  clock->set_clock_source(nullptr);
  clock->set_clock_source_net(nullptr);
  clock->clear_loads();
  clock->clearMembership();

  auto* cts_net_ptr = idbToCts(idb_net);
  if (cts_net_ptr == nullptr) {
    return nullptr;
  }
  cts_net_ptr->set_driver(nullptr);
  cts_net_ptr->set_loads({});
  clock->set_clock_source_net(cts_net_ptr);

  auto* idb_driver_pin = idb_net->get_driving_pin();
  if (idb_driver_pin == nullptr) {
    LOG_WARNING << "Clock net \"" << idb_net->get_net_name() << "\" has no driving pin in iDB.";
  }

  auto idb_load_pins = idb_net->get_load_pins();
  std::vector<idb::IdbPin*> idb_pins;
  if (idb_driver_pin != nullptr) {
    idb_pins.push_back(idb_driver_pin);
  }
  std::ranges::copy(idb_load_pins, std::back_inserter(idb_pins));

  std::unordered_map<idb::IdbInstance*, Inst*> cts_inst_by_idb;
  for (auto* idb_pin : idb_pins) {
    if (idb_pin == nullptr) {
      continue;
    }

    Inst* cts_inst = nullptr;
    if (auto* idb_inst = idb_pin->get_instance(); idb_inst != nullptr) {
      if (cts_inst_by_idb.contains(idb_inst)) {
        cts_inst = cts_inst_by_idb.at(idb_inst);
      } else {
        cts_inst = idbToCts(idb_inst);
        if (cts_inst != nullptr) {
          cts_inst_by_idb[idb_inst] = cts_inst;
        }
      }
    }

    auto* cts_pin = idbToCts(idb_pin);
    if (cts_pin == nullptr) {
      continue;
    }
    cts_pin->set_inst(cts_inst);
    if (cts_inst != nullptr) {
      if (idb_pin == idb_driver_pin) {
        cts_inst->insertDriverPin(cts_pin);
      } else {
        cts_inst->add_pin(cts_pin);
      }
    }
    if (!DESIGN_INST.indexPin(cts_pin)) {
      continue;
    }

    if (idb_pin == idb_driver_pin) {
      clock->set_clock_source(cts_pin);
      cts_net_ptr->set_driver(cts_pin);
    } else {
      clock->add_load(cts_pin);
      cts_net_ptr->add_load(cts_pin);
    }
    cts_pin->set_net(cts_net_ptr);
  }
  return clock;
}

auto Wrapper::ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>
{
  return {loc.get_x(), loc.get_y()};
}

auto Wrapper::ctsToIdb(Pin* pin) -> idb::IdbPin*
{
  if (pin == nullptr) {
    return nullptr;
  }
  if (_cts2idb_pin_map.contains(pin)) {
    return _cts2idb_pin_map.at(pin);
  }
  auto* inst = pin->get_inst();
  if (inst == nullptr) {
    return nullptr;
  }

  auto* idb_inst = ctsToIdb(inst);
  if (idb_inst == nullptr) {
    return nullptr;
  }

  auto* idb_pin = idb_inst->get_pin_by_term(pin->get_name());
  if (idb_pin == nullptr) {
    LOG_WARNING << "Failed to find iDB pin for CTS pin \"" << Design::getPinFullName(pin) << "\".";
    return nullptr;
  }
  crossRef(idb_pin, pin);
  return idb_pin;
}

auto Wrapper::ctsToIdb(Inst* inst) -> idb::IdbInstance*
{
  if (inst == nullptr) {
    return nullptr;
  }
  if (_cts2idb_inst_map.contains(inst)) {
    return _cts2idb_inst_map.at(inst);
  }

  if (_idb_design == nullptr || _idb_layout == nullptr || _idb_layout->get_cell_master_list() == nullptr) {
    LOG_WARNING << "Cannot create iDB inst for CTS inst \"" << inst->get_name() << "\": iDB design/layout is not ready.";
    return nullptr;
  }

  auto* inst_list = _idb_design->get_instance_list();
  if (inst_list == nullptr) {
    LOG_WARNING << "Cannot create iDB inst for CTS inst \"" << inst->get_name() << "\": iDB inst list is null.";
    return nullptr;
  }

  auto* cell_master = _idb_layout->get_cell_master_list()->find_cell_master(inst->get_cell_master());
  if (cell_master == nullptr) {
    LOG_WARNING << "Cannot create iDB inst \"" << inst->get_name() << "\": cell master " << inst->get_cell_master() << " is not found.";
    return nullptr;
  }

  auto* idb_inst = inst_list->find_instance(inst->get_name());
  if (idb_inst == nullptr) {
    idb_inst = inst_list->add_instance(inst->get_name());
    if (idb_inst == nullptr) {
      LOG_WARNING << "Failed to allocate iDB inst for CTS inst \"" << inst->get_name() << "\".";
      return nullptr;
    }
    idb_inst->set_cell_master(cell_master);
    idb_inst->set_type(idb::IdbInstanceType::kTiming);
    idb_inst->set_orient(idb::IdbOrient::kN_R0, false);
    idb_inst->set_coodinate(inst->get_location().get_x(), inst->get_location().get_y());
    idb_inst->set_status(idb::IdbPlacementStatus::kPlaced);
  } else if (idb_inst->get_cell_master() == nullptr || idb_inst->get_cell_master()->get_name() != inst->get_cell_master()) {
    const std::string actual_master = idb_inst->get_cell_master() == nullptr ? "<null>" : idb_inst->get_cell_master()->get_name();
    LOG_WARNING << "Cannot reuse iDB inst \"" << inst->get_name() << "\" for CTS writeback: expected cell master "
                << inst->get_cell_master() << ", found " << actual_master << ".";
    return nullptr;
  } else {
    idb_inst->set_orient(idb::IdbOrient::kN_R0, false);
    idb_inst->set_coodinate(inst->get_location().get_x(), inst->get_location().get_y());
    idb_inst->set_status(idb::IdbPlacementStatus::kPlaced);
  }

  crossRef(idb_inst, inst);
  for (auto* pin : inst->get_pins()) {
    if (pin == nullptr || _cts2idb_pin_map.contains(pin)) {
      continue;
    }
    auto* idb_pin = idb_inst->get_pin_by_term(pin->get_name());
    if (idb_pin != nullptr) {
      crossRef(idb_pin, pin);
    }
  }
  return idb_inst;
}

auto Wrapper::ctsToIdb(Net* net) -> idb::IdbNet*
{
  if (net == nullptr) {
    return nullptr;
  }
  if (_cts2idb_net_map.contains(net)) {
    return _cts2idb_net_map.at(net);
  }
  if (_idb_design != nullptr && _idb_design->get_net_list() != nullptr) {
    auto* idb_net = _idb_design->get_net_list()->find_net(net->get_name());
    if (idb_net != nullptr) {
      crossRef(idb_net, net);
      return idb_net;
    }
  }
  return nullptr;
}

auto Wrapper::ensureIdbNet(Net* cts_net, const std::string& default_net_name) -> idb::IdbNet*
{
  if (_idb_design == nullptr || _idb_design->get_net_list() == nullptr || cts_net == nullptr) {
    return nullptr;
  }

  auto* idb_net = ctsToIdb(cts_net);
  if (idb_net != nullptr) {
    return idb_net;
  }

  const auto net_name = cts_net->get_name().empty() ? default_net_name : cts_net->get_name();
  idb_net = _idb_design->get_net_list()->find_net(net_name);
  if (idb_net != nullptr) {
    crossRef(idb_net, cts_net);
    return idb_net;
  }

  idb_net = _idb_design->get_net_list()->add_net(net_name, idb::IdbConnectType::kClock);
  if (idb_net != nullptr) {
    crossRef(idb_net, cts_net);
  }
  return idb_net;
}

auto Wrapper::rewriteIdbNetPins(idb::IdbNet* idb_net, Net* cts_net) -> bool
{
  if (idb_net == nullptr || cts_net == nullptr) {
    return false;
  }

  auto* driver = cts_net->get_driver();
  if (driver == nullptr) {
    LOG_WARNING << "Skip iDB net rewrite for \"" << cts_net->get_name() << "\": driver pin is null.";
    return false;
  }

  auto* idb_driver = ctsToIdb(driver);
  if (idb_driver == nullptr) {
    LOG_WARNING << "Skip iDB net rewrite for \"" << cts_net->get_name() << "\": driver pin " << driver->get_name()
                << " is not materialized in iDB.";
    return false;
  }

  std::vector<idb::IdbPin*> idb_loads;
  idb_loads.reserve(cts_net->get_loads().size());
  for (auto* load : cts_net->get_loads()) {
    if (load == nullptr) {
      continue;
    }
    auto* idb_load = ctsToIdb(load);
    if (idb_load == nullptr) {
      LOG_WARNING << "Skip load pin " << load->get_name() << " when rewriting iDB net \"" << cts_net->get_name()
                  << "\": pin is not materialized in iDB.";
      return false;
    }
    idb_loads.push_back(idb_load);
  }

  clearIdbNetPins(idb_net);
  appendIdbPinToNet(idb_net, idb_driver);
  for (auto* idb_load : idb_loads) {
    appendIdbPinToNet(idb_net, idb_load);
  }
  return idb_driver != nullptr || !idb_loads.empty();
}

auto Wrapper::writeClock(Clock& clock) -> bool
{
  if (!is_design_ready()) {
    LOG_WARNING << "Skip CTS writeback for clock \"" << clock.get_clock_name() << "\": iDB design is not ready.";
    return false;
  }

  bool success = true;
  for (auto* inst : clock.get_insts()) {
    if (inst == nullptr) {
      continue;
    }
    if (ctsToIdb(inst) == nullptr) {
      success = false;
    }
  }

  auto write_net = [this, &clock, &success](Net* cts_net) -> void {
    if (cts_net == nullptr) {
      return;
    }
    auto* idb_net = ensureIdbNet(cts_net, clock.get_clock_net_name());
    if (idb_net == nullptr || !rewriteIdbNetPins(idb_net, cts_net)) {
      LOG_WARNING << "CTS writeback skipped or failed for clock net \"" << cts_net->get_name() << "\".";
      success = false;
    }
  };

  write_net(clock.get_clock_source_net());
  for (auto* net : clock.get_nets()) {
    if (net == clock.get_clock_source_net()) {
      continue;
    }
    write_net(net);
  }

  return success;
}

auto Wrapper::writeClocks(const std::vector<Clock*>& clocks) -> bool
{
  bool success = true;
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }
    success = writeClock(*clock) && success;
  }
  return success;
}

auto Wrapper::collectLogicCellGeometries() const -> std::vector<WrapperCellGeometry>
{
  std::vector<WrapperCellGeometry> geometries;
  if (_idb_design == nullptr || _idb_design->get_instance_list() == nullptr) {
    LOG_WARNING << "Cannot collect iDB logic-cell geometry: iDB design or instance list is not ready.";
    return geometries;
  }

  const auto& idb_insts = _idb_design->get_instance_list()->get_instance_list();
  geometries.reserve(idb_insts.size());
  for (auto* idb_inst : idb_insts) {
    if (idb_inst == nullptr || idb_inst->is_clock_instance() || idb_inst->is_physical()) {
      continue;
    }
    auto* cell_master = idb_inst->get_cell_master();
    if (cell_master == nullptr || !cell_master->is_logic()) {
      continue;
    }
    geometries.push_back(BuildCellGeometry(idb_inst));
  }
  return geometries;
}

auto Wrapper::queryInstGeometry(const std::string& inst_name) const -> std::optional<WrapperCellGeometry>
{
  if (inst_name.empty()) {
    return std::nullopt;
  }
  if (_idb_design == nullptr || _idb_design->get_instance_list() == nullptr) {
    LOG_WARNING << "Cannot query iDB inst geometry: iDB design or instance list is not ready.";
    return std::nullopt;
  }

  const auto& idb_insts = _idb_design->get_instance_list()->get_instance_list();
  auto iter = std::ranges::find_if(
      idb_insts, [&inst_name](idb::IdbInstance* idb_inst) -> bool { return idb_inst != nullptr && idb_inst->get_name() == inst_name; });
  if (iter == idb_insts.end()) {
    return std::nullopt;
  }
  return BuildCellGeometry(*iter);
}

}  // namespace icts
