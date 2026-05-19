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
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
#include "builder.h"
#include "def_service.h"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "lef_service.h"
#include "logger/Schema.hh"
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

auto appendIdbPinToNet(idb::IdbDesign* idb_design, idb::IdbNet* idb_net, idb::IdbPin* idb_pin) -> bool
{
  if (idb_design == nullptr || idb_net == nullptr || idb_pin == nullptr) {
    return false;
  }

  return idb_design->connectPinToNet(idb_pin, idb_net);
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

auto clearIdbNetPins(idb::IdbDesign* idb_design, idb::IdbNet* idb_net) -> void
{
  if (idb_design == nullptr || idb_net == nullptr) {
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
    idb_design->disconnectPinFromNet(pin);
  }
}

struct IdbNetPinSnapshot
{
  std::vector<idb::IdbPin*> io_pins;
  std::vector<idb::IdbPin*> inst_pins;
};

struct IdbClockNetPins
{
  idb::IdbPin* driver = nullptr;
  std::vector<idb::IdbPin*> loads;
};

struct ClockIdbWriteScope
{
  std::set<std::string> touched_net_names;
  std::set<std::string> clock_tree_inst_names;
  std::unordered_map<const Clock*, std::vector<Net*>> reachable_nets_by_clock;
};

struct ClockIdbWriteBackup
{
  std::set<std::string> pre_existing_net_names;
  std::unordered_map<std::string, IdbNetPinSnapshot> net_pin_membership_by_name;
  std::set<std::string> pre_existing_inst_names;
};

auto findIdbPinByTermOrPinName(idb::IdbInstance* idb_inst, const std::string& pin_name) -> idb::IdbPin*
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

auto appendUniqueIdbPin(std::vector<idb::IdbPin*>& pins, std::unordered_set<idb::IdbPin*>& seen_pins, idb::IdbPin* idb_pin) -> void
{
  if (idb_pin == nullptr || !seen_pins.insert(idb_pin).second) {
    return;
  }
  pins.push_back(idb_pin);
}

auto collectIdbClockNetPins(idb::IdbNet* idb_net) -> IdbClockNetPins
{
  IdbClockNetPins net_pins;
  if (idb_net == nullptr) {
    return net_pins;
  }

  std::vector<idb::IdbPin*> all_pins;
  const auto io_pin_count = idb_net->get_io_pins() == nullptr ? 0U : idb_net->get_io_pins()->get_pin_list().size();
  const auto inst_pin_count = idb_net->get_instance_pin_list() == nullptr ? 0U : idb_net->get_instance_pin_list()->get_pin_list().size();
  all_pins.reserve(io_pin_count + inst_pin_count);
  std::unordered_set<idb::IdbPin*> seen_pins;
  seen_pins.reserve(io_pin_count + inst_pin_count);
  if (idb_net->get_io_pins() != nullptr) {
    for (auto* idb_pin : idb_net->get_io_pins()->get_pin_list()) {
      appendUniqueIdbPin(all_pins, seen_pins, idb_pin);
    }
  }
  if (idb_net->get_instance_pin_list() != nullptr) {
    for (auto* idb_pin : idb_net->get_instance_pin_list()->get_pin_list()) {
      appendUniqueIdbPin(all_pins, seen_pins, idb_pin);
    }
  }

  for (auto* idb_pin : all_pins) {
    auto* idb_term = idb_pin == nullptr ? nullptr : idb_pin->get_term();
    if (idb_term == nullptr) {
      continue;
    }
    if (!idb_pin->is_io_pin()
        && (idb_term->get_direction() == idb::IdbConnectDirection::kOutput
            || idb_term->get_direction() == idb::IdbConnectDirection::kOutputTriState)) {
      net_pins.driver = idb_pin;
      break;
    }
  }

  if (net_pins.driver == nullptr) {
    for (auto* idb_pin : all_pins) {
      auto* idb_term = idb_pin == nullptr ? nullptr : idb_pin->get_term();
      if (idb_term != nullptr && idb_pin->is_io_pin() && idb_term->get_direction() == idb::IdbConnectDirection::kInput) {
        net_pins.driver = idb_pin;
        break;
      }
    }
  }

  if (net_pins.driver == nullptr) {
    for (auto* idb_pin : all_pins) {
      auto* idb_term = idb_pin == nullptr ? nullptr : idb_pin->get_term();
      if (idb_term != nullptr && idb_pin->is_io_pin() && idb_term->get_direction() == idb::IdbConnectDirection::kInOut) {
        net_pins.driver = idb_pin;
        break;
      }
    }
  }

  if (net_pins.driver == nullptr && !all_pins.empty()) {
    net_pins.driver = all_pins.front();
  }

  for (auto* idb_pin : all_pins) {
    if (idb_pin != net_pins.driver) {
      net_pins.loads.push_back(idb_pin);
    }
  }
  return net_pins;
}

auto inferCtsInstTypeFromIdbInst(idb::IdbInstance* idb_inst) -> InstType
{
  if (idb_inst == nullptr) {
    return InstType::kUnknown;
  }
  auto* cell_master = idb_inst->get_cell_master();
  if (cell_master != nullptr && cell_master->is_block()) {
    return InstType::kMacroBlock;
  }
  if (idb_inst->is_flip_flop()) {
    return InstType::kFlipFlop;
  }
  if (idb_inst->is_clock_instance()) {
    return InstType::kBuffer;
  }
  return InstType::kUnknown;
}

auto snapshotIdbNetPins(idb::IdbNet* idb_net) -> IdbNetPinSnapshot
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

auto Wrapper::read() -> void
{
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;
  for (const auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    clock_net_pairs.emplace_back(clock->get_clock_name(), clock->get_clock_net_name());
  }
  if (!readClocks(clock_net_pairs)) {
    LOG_WARNING << "Wrapper read failed while materializing predeclared CTS clocks.";
  }
}

auto Wrapper::idbToCts(idb::IdbCoordinate<int32_t>& coord) -> Point<int>
{
  return {coord.get_x(), coord.get_y()};
}

auto Wrapper::ctsToIdb(const Point<int>& loc) -> idb::IdbCoordinate<int32_t>
{
  return {loc.get_x(), loc.get_y()};
}

class Wrapper::CtsClockReader
{
 public:
  explicit CtsClockReader(Wrapper& wrapper) : _wrapper(&wrapper) {}

  auto readClocks(const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> bool
  {
    auto* idb_design = findIdbDesign();
    if (idb_design == nullptr) {
      LOG_ERROR << "CTS clock read failed: iDB design is null.";
      return false;
    }

    auto* idb_net_list = idb_design->get_net_list();
    if (idb_net_list == nullptr) {
      LOG_ERROR << "CTS clock read failed: iDB net list is null.";
      return false;
    }

    clearClockReadData();
    for (const auto& [clock_name, clock_net_name] : clock_net_pairs) {
      auto* idb_net = findSdcClockNetOrError(clock_name, clock_net_name, idb_net_list);
      if (idb_net == nullptr || buildClockFromIdbNet(clock_name, clock_net_name, idb_net) == nullptr) {
        clearClockReadData();
        return false;
      }
    }
    return true;
  }

 private:
  auto clearClockReadData() -> void
  {
    _wrapper->_cts2idb_inst_map.clear();
    _wrapper->_idb2cts_inst_map.clear();
    _wrapper->_cts2idb_net_map.clear();
    _wrapper->_idb2cts_net_map.clear();
    _wrapper->_cts2idb_pin_map.clear();
    _wrapper->_idb2cts_pin_map.clear();
    DESIGN_INST.clearClocks();
    DESIGN_INST.clearTopologyObjects();
  }

  auto findIdbDesign() -> idb::IdbDesign*
  {
    auto* idb_design = _wrapper->_idb_design;
    if (idb_design == nullptr && _wrapper->_idb != nullptr && _wrapper->_idb->get_def_service() != nullptr) {
      idb_design = _wrapper->_idb->get_def_service()->get_design();
      _wrapper->_idb_design = idb_design;
    }
    return idb_design;
  }

  static auto findSdcClockNetOrError(const std::string& clock_name, const std::string& clock_net_name, idb::IdbNetList* idb_net_list)
      -> idb::IdbNet*
  {
    auto* idb_net = idb_net_list->find_net(clock_net_name);
    if (idb_net == nullptr) {
      schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "Wrapper", "failed to resolve SDC-declared clock net in iDB.",
                             {{"clock", clock_name}, {"net", clock_net_name}, {"reason", "unresolved_sdc_clock_source"}});
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": SDC-declared net \"" << clock_net_name
                << "\" is not found in iDB.";
      return nullptr;
    }
    return idb_net;
  }

  auto buildClockFromIdbNet(const std::string& clock_name, const std::string& clock_net_name, idb::IdbNet* idb_net) -> Clock*
  {
    if (idb_net == nullptr) {
      return nullptr;
    }

    auto* clock = DESIGN_INST.makeClock(clock_name, clock_net_name);
    if (clock == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": failed to create CTS clock object.";
      return nullptr;
    }
    clock->set_clock_name(clock_name);
    clock->set_clock_net_name(clock_net_name);
    clock->set_clock_source(nullptr);
    clock->set_clock_source_net(nullptr);
    clock->clear_loads();
    clock->clearMembership();

    auto* cts_net = buildNetFromIdbNet(idb_net);
    if (cts_net == nullptr) {
      return nullptr;
    }
    cts_net->set_driver(nullptr);
    cts_net->set_loads({});
    clock->set_clock_source_net(cts_net);

    const auto idb_net_pins = collectIdbClockNetPins(idb_net);
    if (idb_net_pins.driver == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": iDB net \"" << clock_net_name
                << "\" has no resolvable driver pin.";
      return nullptr;
    }

    std::vector<idb::IdbPin*> idb_pins;
    idb_pins.reserve(idb_net_pins.loads.size() + 1U);
    idb_pins.push_back(idb_net_pins.driver);
    std::ranges::copy(idb_net_pins.loads, std::back_inserter(idb_pins));

    std::unordered_map<idb::IdbInstance*, Inst*> cts_inst_by_idb;
    cts_inst_by_idb.reserve(idb_pins.size());
    for (auto* idb_pin : idb_pins) {
      if (idb_pin == nullptr) {
        continue;
      }

      Inst* cts_inst = nullptr;
      if (auto* idb_inst = idb_pin->get_instance(); idb_inst != nullptr) {
        if (cts_inst_by_idb.contains(idb_inst)) {
          cts_inst = cts_inst_by_idb.at(idb_inst);
        } else {
          cts_inst = buildInstFromIdbInst(idb_inst);
          if (cts_inst == nullptr) {
            return nullptr;
          }
          cts_inst_by_idb[idb_inst] = cts_inst;
        }
      } else if (!idb_pin->is_io_pin()) {
        LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": instance pin \"" << idb_pin->get_pin_name()
                  << "\" has no iDB inst.";
        return nullptr;
      }

      auto* cts_pin = buildPinFromIdbPin(idb_pin, cts_inst);
      if (cts_pin == nullptr) {
        return nullptr;
      }
      cts_pin->set_net(cts_net);
      if (!DESIGN_INST.indexPin(cts_pin)) {
        LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": failed to index CTS pin \""
                  << Design::getPinFullName(cts_pin) << "\".";
        return nullptr;
      }

      if (cts_inst != nullptr) {
        if (idb_pin == idb_net_pins.driver) {
          cts_inst->insertDriverPin(cts_pin);
        } else {
          cts_inst->add_pin(cts_pin);
        }
      }
      if (idb_pin == idb_net_pins.driver) {
        clock->set_clock_source(cts_pin);
        cts_net->set_driver(cts_pin);
      } else {
        clock->add_load(cts_pin);
        cts_net->add_load(cts_pin);
      }
    }
    return clock;
  }

  auto buildInstFromIdbInst(idb::IdbInstance* idb_inst) -> Inst*
  {
    if (idb_inst == nullptr) {
      return nullptr;
    }
    auto* cell_master = idb_inst->get_cell_master();
    auto* coord = idb_inst->get_coordinate();
    if (cell_master == nullptr || coord == nullptr) {
      LOG_ERROR << "CTS clock read failed: iDB inst \"" << idb_inst->get_name() << "\" is missing required cell master or coordinate.";
      return nullptr;
    }

    const auto inst_name = idb_inst->get_name();
    auto* cts_inst = DESIGN_INST.makeInst(inst_name);
    if (cts_inst == nullptr) {
      LOG_ERROR << "CTS clock read failed: failed to create CTS inst \"" << inst_name << "\".";
      return nullptr;
    }
    cts_inst->set_name(inst_name);
    cts_inst->set_cell_master(cell_master->get_name());
    cts_inst->set_type(inferCtsInstTypeFromIdbInst(idb_inst));
    cts_inst->set_location(Wrapper::idbToCts(*coord));
    bindIdbInst(idb_inst, cts_inst);
    return cts_inst;
  }

  auto buildPinFromIdbPin(idb::IdbPin* idb_pin, Inst* cts_inst) -> Pin*
  {
    if (idb_pin == nullptr) {
      return nullptr;
    }
    auto* idb_term = idb_pin->get_term();
    auto* avg_coord = idb_pin->get_average_coordinate();
    if (idb_term == nullptr || avg_coord == nullptr) {
      LOG_ERROR << "CTS clock read failed: iDB pin \"" << idb_pin->get_pin_name() << "\" is missing required term or average coordinate.";
      return nullptr;
    }

    const auto pin_name = idb_term->get_name();
    const auto pin_full_name = cts_inst == nullptr ? pin_name : cts_inst->get_name() + "/" + pin_name;
    if (DESIGN_INST.findPin(pin_full_name) != nullptr) {
      LOG_ERROR << "CTS clock read failed: duplicate CTS pin \"" << pin_full_name << "\".";
      return nullptr;
    }

    auto* cts_pin = DESIGN_INST.makePin(pin_name);
    if (cts_pin == nullptr) {
      LOG_ERROR << "CTS clock read failed: failed to create CTS pin \"" << pin_full_name << "\".";
      return nullptr;
    }
    cts_pin->set_name(pin_name);
    cts_pin->set_type(convertIdbPinType(idb_term->get_type(), idb_term->get_direction()));
    cts_pin->set_location(Wrapper::idbToCts(*avg_coord));
    cts_pin->set_inst(cts_inst);
    cts_pin->set_io(idb_pin->is_io_pin());
    bindIdbPin(idb_pin, cts_pin);
    return cts_pin;
  }

  auto buildNetFromIdbNet(idb::IdbNet* idb_net) -> Net*
  {
    if (idb_net == nullptr) {
      return nullptr;
    }
    auto* cts_net = DESIGN_INST.makeNet(idb_net->get_net_name());
    if (cts_net == nullptr) {
      LOG_ERROR << "CTS clock read failed: failed to create CTS net \"" << idb_net->get_net_name() << "\".";
      return nullptr;
    }
    cts_net->set_name(idb_net->get_net_name());
    bindIdbNet(idb_net, cts_net);
    return cts_net;
  }

  auto bindIdbPin(idb::IdbPin* idb_pin, Pin* cts_pin) -> void { _wrapper->crossRef(idb_pin, cts_pin); }
  auto bindIdbInst(idb::IdbInstance* idb_inst, Inst* cts_inst) -> void { _wrapper->crossRef(idb_inst, cts_inst); }
  auto bindIdbNet(idb::IdbNet* idb_net, Net* cts_net) -> void { _wrapper->crossRef(idb_net, cts_net); }

  Wrapper* _wrapper = nullptr;
};

class Wrapper::CtsClockIdbWriter
{
 public:
  explicit CtsClockIdbWriter(Wrapper& wrapper) : _wrapper(&wrapper) {}

  auto writeClocksDetailed(const std::vector<Clock*>& clocks) -> WrapperWriteResult
  {
    WrapperWriteResult result;
    if (!validateIdbWriteBoundary(result) || !rebuildAndValidateClockDAG(result)) {
      return result;
    }

    auto scope = collectClockIdbWriteScope(clocks);
    if (scope.touched_net_names.empty() && !clocks.empty()) {
      result.success = false;
      result.reason = "invalid_clock_dag";
      schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "Wrapper", "CTS iDB writeback preflight found no reachable clock nets.",
                             {{"reason", DESIGN_INST.get_clock_dag().get_status()}});
      return result;
    }
    const auto backup = backupClockIdbWriteScope(scope);

    for (auto* clock : clocks) {
      if (clock == nullptr) {
        continue;
      }
      if (!writeClockToIdb(*clock, scope)) {
        result.success = false;
        result.failed_clock = clock->get_clock_name();
        result.failed_net = _failed_net_name.empty() ? clock->get_clock_net_name() : _failed_net_name;
        result.reason = "write_clock_failed";
        result.rollback_done = rollbackClockIdbWrite(scope, backup);
        schema::EmitDiagnostic(result.rollback_done ? schema::DiagnosticLevel::kError : schema::DiagnosticLevel::kWarning, "Wrapper",
                               "CTS iDB writeback failed and rollback was attempted.",
                               {{"clock", result.failed_clock},
                                {"net", result.failed_net},
                                {"reason", _failure_reason.empty() ? result.reason : _failure_reason},
                                {"rollback_done", result.rollback_done ? "true" : "false"}});
        return result;
      }
    }

    result.success = true;
    result.rollback_done = false;
    return result;
  }

 private:
  auto validateIdbWriteBoundary(WrapperWriteResult& result) -> bool
  {
    if (!_wrapper->is_design_ready() || _wrapper->_idb_design->get_net_list() == nullptr
        || _wrapper->_idb_design->get_instance_list() == nullptr) {
      result.success = false;
      result.reason = "idb_design_not_ready";
      LOG_ERROR << "CTS iDB writeback failed: iDB design, net list, or inst list is not ready.";
      return false;
    }
    return true;
  }

  static auto rebuildAndValidateClockDAG(WrapperWriteResult& result) -> bool
  {
    if (DESIGN_INST.rebuildClockDAG()) {
      return true;
    }
    result.success = false;
    result.reason = "invalid_clock_dag";
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "Wrapper",
                           "CTS iDB writeback preflight failed because committed CTS topology is not a valid clock DAG.",
                           {{"reason", DESIGN_INST.get_clock_dag().get_status()}});
    return false;
  }

  static auto collectClockIdbWriteScope(const std::vector<Clock*>& clocks) -> ClockIdbWriteScope
  {
    ClockIdbWriteScope scope;
    const auto& clock_dag = DESIGN_INST.get_clock_dag();
    for (auto* clock : clocks) {
      if (clock == nullptr) {
        continue;
      }
      auto reachable_nets = clock_dag.reachableNets(clock);
      for (auto* net : reachable_nets) {
        const auto net_name = getClockTreeNetName(*clock, net);
        if (!net_name.empty()) {
          scope.touched_net_names.insert(net_name);
        }
      }
      scope.reachable_nets_by_clock[clock] = std::move(reachable_nets);

      for (auto* inst : clock->get_insts()) {
        if (inst != nullptr && !inst->get_name().empty()) {
          scope.clock_tree_inst_names.insert(inst->get_name());
        }
      }
    }
    return scope;
  }

  auto backupClockIdbWriteScope(const ClockIdbWriteScope& scope) -> ClockIdbWriteBackup
  {
    ClockIdbWriteBackup backup;
    auto* idb_net_list = _wrapper->_idb_design->get_net_list();
    auto* idb_inst_list = _wrapper->_idb_design->get_instance_list();

    for (const auto& net_name : scope.touched_net_names) {
      auto* idb_net = idb_net_list->find_net(net_name);
      if (idb_net == nullptr) {
        continue;
      }
      backup.pre_existing_net_names.insert(net_name);
      backup.net_pin_membership_by_name[net_name] = snapshotIdbNetPins(idb_net);
    }

    for (const auto& inst_name : scope.clock_tree_inst_names) {
      if (idb_inst_list->find_instance(inst_name) != nullptr) {
        backup.pre_existing_inst_names.insert(inst_name);
      }
    }
    return backup;
  }

  auto writeClockToIdb(Clock& clock, const ClockIdbWriteScope& scope) -> bool
  {
    if (!writeClockTreeInstsToIdb(clock)) {
      if (_failed_net_name.empty()) {
        _failed_net_name = clock.get_clock_net_name();
      }
      return false;
    }

    const auto reachable_iter = scope.reachable_nets_by_clock.find(&clock);
    if (reachable_iter == scope.reachable_nets_by_clock.end() || reachable_iter->second.empty()) {
      _failed_net_name = clock.get_clock_net_name();
      _failure_reason = "no_reachable_clock_nets";
      LOG_ERROR << "CTS iDB writeback failed for clock \"" << clock.get_clock_name() << "\": no reachable ClockDAG nets.";
      return false;
    }

    for (auto* net : reachable_iter->second) {
      if (net == nullptr) {
        continue;
      }
      auto* idb_net = findOrCreateClockTreeIdbNet(net, clock.get_clock_net_name());
      if (idb_net == nullptr || !rewriteClockTreeIdbNetPins(idb_net, net, scope)) {
        if (_failed_net_name.empty()) {
          _failed_net_name = getClockTreeNetName(clock, net);
        }
        return false;
      }
    }
    return true;
  }

  auto writeClockTreeInstsToIdb(Clock& clock) -> bool
  {
    for (auto* inst : clock.get_insts()) {
      if (inst == nullptr) {
        continue;
      }
      if (createOrUpdateClockTreeInst(inst) == nullptr) {
        _failure_reason = "create_or_update_clock_tree_inst_failed";
        _failed_net_name = clock.get_clock_net_name();
        return false;
      }
    }
    return true;
  }

  auto createOrUpdateClockTreeInst(Inst* inst) -> idb::IdbInstance*
  {
    if (inst == nullptr) {
      return nullptr;
    }
    if (_wrapper->_cts2idb_inst_map.contains(inst)) {
      return _wrapper->_cts2idb_inst_map.at(inst);
    }

    if (_wrapper->_idb_layout == nullptr || _wrapper->_idb_layout->get_cell_master_list() == nullptr) {
      LOG_ERROR << "CTS iDB writeback failed for inst \"" << inst->get_name() << "\": iDB layout or cell master list is not ready.";
      return nullptr;
    }
    auto* cell_master = _wrapper->_idb_layout->get_cell_master_list()->find_cell_master(inst->get_cell_master());
    if (cell_master == nullptr) {
      LOG_ERROR << "CTS iDB writeback failed for inst \"" << inst->get_name() << "\": cell master \"" << inst->get_cell_master()
                << "\" is not found.";
      return nullptr;
    }

    auto* idb_inst = _wrapper->_idb_design->get_instance_list()->find_instance(inst->get_name());
    if (idb_inst == nullptr) {
      idb_inst = _wrapper->_idb_design->createInstance(inst->get_name(), inst->get_cell_master(), idb::IdbInstanceType::kTiming);
      if (idb_inst == nullptr) {
        LOG_ERROR << "CTS iDB writeback failed: failed to allocate iDB inst \"" << inst->get_name() << "\".";
        return nullptr;
      }
    } else if (idb_inst->get_cell_master() == nullptr || idb_inst->get_cell_master()->get_name() != inst->get_cell_master()) {
      const std::string actual_master = idb_inst->get_cell_master() == nullptr ? "<null>" : idb_inst->get_cell_master()->get_name();
      LOG_ERROR << "CTS iDB writeback failed: cannot reuse iDB inst \"" << inst->get_name() << "\" with cell master \"" << actual_master
                << "\" for CTS cell master \"" << inst->get_cell_master() << "\".";
      return nullptr;
    }

    _wrapper->_idb_design->placeInstance(inst->get_name(), inst->get_location().get_x(), inst->get_location().get_y(),
                                         idb::IdbOrient::kN_R0, idb::IdbPlacementStatus::kPlaced);
    idb_inst->set_type(idb::IdbInstanceType::kTiming);
    _wrapper->crossRef(idb_inst, inst);
    bindClockTreeInstPins(idb_inst, inst);
    return idb_inst;
  }

  auto bindClockTreeInstPins(idb::IdbInstance* idb_inst, Inst* inst) -> void
  {
    if (idb_inst == nullptr || inst == nullptr) {
      return;
    }
    for (auto* pin : inst->get_pins()) {
      if (pin == nullptr || _wrapper->_cts2idb_pin_map.contains(pin)) {
        continue;
      }
      auto* idb_pin = findIdbPinByTermOrPinName(idb_inst, pin->get_name());
      if (idb_pin != nullptr) {
        _wrapper->crossRef(idb_pin, pin);
      }
    }
  }

  auto findExistingIdbPinForClockNet(Pin* pin) -> idb::IdbPin*
  {
    if (pin == nullptr) {
      return nullptr;
    }
    if (_wrapper->_cts2idb_pin_map.contains(pin)) {
      return _wrapper->_cts2idb_pin_map.at(pin);
    }

    idb::IdbPin* idb_pin = nullptr;
    auto* inst = pin->get_inst();
    if (inst == nullptr) {
      if (_wrapper->_idb_design->get_io_pin_list() != nullptr) {
        idb_pin = _wrapper->_idb_design->get_io_pin_list()->find_pin(pin->get_name());
      }
    } else {
      auto* idb_inst = _wrapper->_cts2idb_inst_map.contains(inst) ? _wrapper->_cts2idb_inst_map.at(inst) : nullptr;
      if (idb_inst == nullptr) {
        idb_inst = _wrapper->_idb_design->get_instance_list()->find_instance(inst->get_name());
        if (idb_inst != nullptr) {
          _wrapper->crossRef(idb_inst, inst);
        }
      }
      idb_pin = findIdbPinByTermOrPinName(idb_inst, pin->get_name());
    }

    if (idb_pin != nullptr) {
      _wrapper->crossRef(idb_pin, pin);
    }
    return idb_pin;
  }

  auto findExistingClockTreeIdbNet(Net* net) -> idb::IdbNet*
  {
    if (net == nullptr) {
      return nullptr;
    }
    if (_wrapper->_cts2idb_net_map.contains(net)) {
      return _wrapper->_cts2idb_net_map.at(net);
    }
    auto* idb_net = _wrapper->_idb_design->get_net_list()->find_net(net->get_name());
    if (idb_net != nullptr) {
      _wrapper->crossRef(idb_net, net);
    }
    return idb_net;
  }

  auto findOrCreateClockTreeIdbNet(Net* net, const std::string& default_net_name) -> idb::IdbNet*
  {
    if (net == nullptr) {
      return nullptr;
    }
    if (auto* idb_net = findExistingClockTreeIdbNet(net); idb_net != nullptr) {
      return idb_net;
    }

    const auto net_name = net->get_name().empty() ? default_net_name : net->get_name();
    if (net_name.empty()) {
      _failure_reason = "empty_clock_tree_net_name";
      LOG_ERROR << "CTS iDB writeback failed: clock tree net name is empty.";
      return nullptr;
    }
    auto* idb_net = _wrapper->_idb_design->createOrFindNet(net_name, idb::IdbConnectType::kClock);
    if (idb_net == nullptr) {
      _failure_reason = "create_clock_tree_net_failed";
      LOG_ERROR << "CTS iDB writeback failed: failed to create iDB net \"" << net_name << "\".";
      return nullptr;
    }
    _wrapper->crossRef(idb_net, net);
    return idb_net;
  }

  auto validatePinCurrentNetInWriteScope(idb::IdbPin* idb_pin, idb::IdbNet* target_net, const ClockIdbWriteScope& scope,
                                         const std::string& target_net_name, const std::string& cts_pin_role, Pin* cts_pin) -> bool
  {
    auto* current_net = idb_pin == nullptr ? nullptr : idb_pin->get_net();
    if (current_net == nullptr || current_net == target_net || scope.touched_net_names.contains(current_net->get_net_name())) {
      return true;
    }

    _failed_net_name = target_net_name;
    _failure_reason = "clock_tree_pin_current_net_out_of_scope";
    LOG_ERROR << "CTS iDB writeback failed for net \"" << target_net_name << "\": existing iDB pin for CTS " << cts_pin_role << " \""
              << Design::getPinFullName(cts_pin) << "\" is connected to out-of-scope iDB net \"" << current_net->get_net_name() << "\".";
    return false;
  }

  auto rewriteClockTreeIdbNetPins(idb::IdbNet* idb_net, Net* cts_net, const ClockIdbWriteScope& scope) -> bool
  {
    if (idb_net == nullptr || cts_net == nullptr) {
      _failure_reason = "null_clock_tree_net";
      return false;
    }

    const auto net_name = cts_net->get_name().empty() ? idb_net->get_net_name() : cts_net->get_name();
    _failed_net_name = net_name;
    auto* driver = cts_net->get_driver();
    if (driver == nullptr) {
      _failure_reason = "clock_tree_net_driver_missing";
      LOG_ERROR << "CTS iDB writeback failed for net \"" << net_name << "\": CTS driver pin is null.";
      return false;
    }

    auto* idb_driver = findExistingIdbPinForClockNet(driver);
    if (idb_driver == nullptr) {
      _failure_reason = "clock_tree_driver_pin_unresolved";
      LOG_ERROR << "CTS iDB writeback failed for net \"" << net_name << "\": existing iDB pin for CTS driver \""
                << Design::getPinFullName(driver) << "\" was not found.";
      return false;
    }
    if (!validatePinCurrentNetInWriteScope(idb_driver, idb_net, scope, net_name, "driver", driver)) {
      return false;
    }

    std::vector<idb::IdbPin*> idb_loads;
    idb_loads.reserve(cts_net->get_loads().size());
    for (auto* load : cts_net->get_loads()) {
      if (load == nullptr) {
        _failure_reason = "clock_tree_load_pin_missing";
        LOG_ERROR << "CTS iDB writeback failed for net \"" << net_name << "\": CTS load pin is null.";
        return false;
      }
      auto* idb_load = findExistingIdbPinForClockNet(load);
      if (idb_load == nullptr) {
        _failure_reason = "clock_tree_load_pin_unresolved";
        LOG_ERROR << "CTS iDB writeback failed for net \"" << net_name << "\": existing iDB pin for CTS load \""
                  << Design::getPinFullName(load) << "\" was not found.";
        return false;
      }
      if (!validatePinCurrentNetInWriteScope(idb_load, idb_net, scope, net_name, "load", load)) {
        return false;
      }
      idb_loads.push_back(idb_load);
    }

    clearIdbNetPins(_wrapper->_idb_design, idb_net);
    if (!appendIdbPinToNet(_wrapper->_idb_design, idb_net, idb_driver)) {
      _failure_reason = "clock_tree_driver_pin_connect_failed";
      return false;
    }
    for (auto* idb_load : idb_loads) {
      if (!appendIdbPinToNet(_wrapper->_idb_design, idb_net, idb_load)) {
        _failure_reason = "clock_tree_load_pin_connect_failed";
        return false;
      }
    }
    return true;
  }

  auto rollbackClockIdbWrite(const ClockIdbWriteScope& scope, const ClockIdbWriteBackup& backup) -> bool
  {
    auto* idb_net_list = _wrapper->_idb_design->get_net_list();
    auto* idb_inst_list = _wrapper->_idb_design->get_instance_list();
    bool rollback_done = true;
    for (const auto& net_name : scope.touched_net_names) {
      if (backup.pre_existing_net_names.contains(net_name)) {
        continue;
      }
      auto* created_net = idb_net_list->find_net(net_name);
      if (created_net == nullptr) {
        continue;
      }
      clearIdbNetPins(_wrapper->_idb_design, created_net);
      rollback_done = _wrapper->_idb_design->removeNetSafe(net_name) && rollback_done;
    }

    for (const auto& [net_name, snapshot] : backup.net_pin_membership_by_name) {
      auto* idb_net = idb_net_list->find_net(net_name);
      if (idb_net == nullptr) {
        rollback_done = false;
        continue;
      }
      clearIdbNetPins(_wrapper->_idb_design, idb_net);
      for (auto* io_pin : snapshot.io_pins) {
        rollback_done = appendIdbPinToNet(_wrapper->_idb_design, idb_net, io_pin) && rollback_done;
      }
      for (auto* inst_pin : snapshot.inst_pins) {
        rollback_done = appendIdbPinToNet(_wrapper->_idb_design, idb_net, inst_pin) && rollback_done;
      }
    }

    for (const auto& inst_name : scope.clock_tree_inst_names) {
      if (backup.pre_existing_inst_names.contains(inst_name) || idb_inst_list->find_instance(inst_name) == nullptr) {
        continue;
      }
      rollback_done = _wrapper->_idb_design->removeInstanceSafe(inst_name) && rollback_done;
    }

    _wrapper->_cts2idb_inst_map.clear();
    _wrapper->_idb2cts_inst_map.clear();
    _wrapper->_cts2idb_net_map.clear();
    _wrapper->_idb2cts_net_map.clear();
    _wrapper->_cts2idb_pin_map.clear();
    _wrapper->_idb2cts_pin_map.clear();
    return rollback_done;
  }

  static auto getClockTreeNetName(const Clock& clock, const Net* net) -> std::string
  {
    if (net == nullptr) {
      return "";
    }
    return net->get_name().empty() ? clock.get_clock_net_name() : net->get_name();
  }

  Wrapper* _wrapper = nullptr;
  std::string _failed_net_name;
  std::string _failure_reason;
};

auto Wrapper::readClocks(const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> bool
{
  return CtsClockReader(*this).readClocks(clock_net_pairs);
}

auto Wrapper::writeClock(Clock& clock) -> bool
{
  return CtsClockIdbWriter(*this).writeClocksDetailed({&clock}).success;
}

auto Wrapper::writeClocksDetailed(const std::vector<Clock*>& clocks) -> WrapperWriteResult
{
  return CtsClockIdbWriter(*this).writeClocksDetailed(clocks);
}

auto Wrapper::writeClocks(const std::vector<Clock*>& clocks) -> bool
{
  return writeClocksDetailed(clocks).success;
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
    if (idb_inst == nullptr || idb_inst->is_physical()) {
      continue;
    }
    auto* cell_master = idb_inst->get_cell_master();
    if (cell_master == nullptr || !cell_master->is_logic()) {
      continue;
    }
    if (idb_inst->is_clock_instance()) {
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

  auto* idb_inst = _idb_design->get_instance_list()->find_instance(inst_name);
  if (idb_inst == nullptr) {
    return std::nullopt;
  }
  return BuildCellGeometry(idb_inst);
}

}  // namespace icts
