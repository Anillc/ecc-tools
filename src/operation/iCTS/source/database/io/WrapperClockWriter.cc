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
 * @file WrapperClockWriter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Clock writeback helpers for the iCTS iDB wrapper
 */
#include <glog/logging.h>

#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "Log.hh"
#include "Wrapper.hh"
#include "WrapperClockWriterInternal.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/Schema.hh"
#include "spatial/Point.hh"

namespace icts {
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
      backup.net_pin_membership_by_name[net_name] = SnapshotIdbNetPins(idb_net);
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
    if (_wrapper->_idb_layout == nullptr || _wrapper->_idb_layout->get_cell_master_list() == nullptr) {
      LOG_ERROR << "CTS iDB writeback failed for inst \"" << inst->get_name() << "\": iDB layout or cell master list is not ready.";
      return nullptr;
    }
    auto* idb_inst_list = _wrapper->_idb_design->get_instance_list();
    auto* cell_master = _wrapper->_idb_layout->get_cell_master_list()->find_cell_master(inst->get_cell_master());
    if (cell_master == nullptr) {
      LOG_ERROR << "CTS iDB writeback failed for inst \"" << inst->get_name() << "\": cell master \"" << inst->get_cell_master()
                << "\" is not found.";
      return nullptr;
    }

    if (_wrapper->_cts2idb_inst_map.contains(inst)) {
      auto* idb_inst = _wrapper->_cts2idb_inst_map.at(inst);
      if (idb_inst == nullptr) {
        return nullptr;
      }
      idb_inst->set_cell_master(cell_master);
      idb_inst->set_type(idb::IdbInstanceType::kTiming);
      idb_inst->set_orient(idb::IdbOrient::kN_R0, false);
      idb_inst->set_coodinate(inst->get_location().get_x(), inst->get_location().get_y());
      idb_inst->set_status(idb::IdbPlacementStatus::kPlaced);
      bindClockTreeInstPins(idb_inst, inst);
      return idb_inst;
    }

    auto* idb_inst = idb_inst_list->find_instance(inst->get_name());
    if (idb_inst == nullptr) {
      idb_inst = idb_inst_list->add_instance(inst->get_name());
      if (idb_inst == nullptr) {
        LOG_ERROR << "CTS iDB writeback failed: failed to allocate iDB inst \"" << inst->get_name() << "\".";
        return nullptr;
      }
      idb_inst->set_cell_master(cell_master);
      idb_inst->set_type(idb::IdbInstanceType::kTiming);
    } else if (idb_inst->get_cell_master() == nullptr || idb_inst->get_cell_master()->get_name() != inst->get_cell_master()) {
      idb_inst->set_cell_master(cell_master);
      idb_inst->set_type(idb::IdbInstanceType::kTiming);
    }

    idb_inst->set_orient(idb::IdbOrient::kN_R0, false);
    idb_inst->set_coodinate(inst->get_location().get_x(), inst->get_location().get_y());
    idb_inst->set_status(idb::IdbPlacementStatus::kPlaced);
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
      auto* idb_pin = FindIdbPinByTermOrPinName(idb_inst, pin->get_name());
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
      idb_pin = FindIdbPinByTermOrPinName(idb_inst, pin->get_name());
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
    auto* idb_net = _wrapper->_idb_design->get_net_list()->find_net(net_name);
    if (idb_net == nullptr) {
      idb_net = _wrapper->_idb_design->get_net_list()->add_net(net_name, idb::IdbConnectType::kClock);
    }
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

    ClearIdbNetPins(idb_net);
    AppendIdbPinToNet(idb_net, idb_driver);
    for (auto* idb_load : idb_loads) {
      AppendIdbPinToNet(idb_net, idb_load);
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
      ClearIdbNetPins(created_net);
      rollback_done = idb_net_list->remove_net(net_name) && rollback_done;
    }

    for (const auto& [net_name, snapshot] : backup.net_pin_membership_by_name) {
      auto* idb_net = idb_net_list->find_net(net_name);
      if (idb_net == nullptr) {
        rollback_done = false;
        continue;
      }
      ClearIdbNetPins(idb_net);
      for (auto* io_pin : snapshot.io_pins) {
        AppendIdbPinToNet(idb_net, io_pin);
      }
      for (auto* inst_pin : snapshot.inst_pins) {
        AppendIdbPinToNet(idb_net, inst_pin);
      }
    }

    for (const auto& inst_name : scope.clock_tree_inst_names) {
      if (backup.pre_existing_inst_names.contains(inst_name) || idb_inst_list->find_instance(inst_name) == nullptr) {
        continue;
      }
      rollback_done = idb_inst_list->remove_instance(inst_name) && rollback_done;
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

}  // namespace icts
