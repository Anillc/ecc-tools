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
 * @file WrapperClockReader.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Clock readback helpers for the iCTS iDB wrapper
 */
#include <glog/logging.h>

#include <algorithm>
#include <iterator>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "Log.hh"
#include "Wrapper.hh"
#include "adapter/sdc/SdcClockReader.hh"
#include "builder.h"
#include "def_service.h"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "logger/Schema.hh"

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

struct IdbClockNetPins
{
  idb::IdbPin* driver = nullptr;
  std::vector<idb::IdbPin*> loads;
};

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

auto idbPinTermName(idb::IdbPin* idb_pin) -> std::string
{
  auto* term = idb_pin == nullptr ? nullptr : idb_pin->get_term();
  return term == nullptr ? std::string{} : term->get_name();
}

auto ctsPinFullName(idb::IdbPin* idb_pin, Inst* cts_inst) -> std::string
{
  const auto pin_name = idbPinTermName(idb_pin);
  if (pin_name.empty()) {
    return {};
  }
  return cts_inst == nullptr ? pin_name : cts_inst->get_name() + "/" + pin_name;
}

}  // namespace

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

  auto readTraceClockTargets(const std::vector<ClockTraceClockTarget>& clock_targets) -> bool
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
    for (const auto& clock_target : clock_targets) {
      auto* idb_net = findSdcClockNetOrError(clock_target.clock_name, clock_target.clock_net_name, idb_net_list);
      if (idb_net == nullptr) {
        clearClockReadData();
        return false;
      }
      Clock* clock = nullptr;
      if (clock_target.preclustered_sink_reuse) {
        clock = buildPreclusteredClockFromTraceTarget(clock_target, idb_net);
      } else {
        clock = buildClockFromIdbNet(clock_target.clock_name, clock_target.clock_net_name, idb_net);
      }
      if (clock == nullptr) {
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
    clock->set_preclustered_sink_reuse(false);
    clock->clear_preclustered_anchor_input_net_names();
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
    std::vector<Pin*> cts_loads;
    cts_loads.reserve(idb_net_pins.loads.size());
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
        cts_loads.push_back(cts_pin);
      }
    }
    clock->set_loads(cts_loads);
    cts_net->set_loads(cts_loads);
    return clock;
  }

  auto buildPreclusteredClockFromTraceTarget(const ClockTraceClockTarget& clock_target, idb::IdbNet* source_idb_net) -> Clock*
  {
    if (source_idb_net == nullptr || clock_target.preclustered_sink_anchors.empty()) {
      return nullptr;
    }

    auto* clock = DESIGN_INST.makeClock(clock_target.clock_name, clock_target.clock_net_name);
    if (clock == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_target.clock_name << "\": failed to create CTS clock object.";
      return nullptr;
    }
    clock->set_clock_name(clock_target.clock_name);
    clock->set_clock_net_name(clock_target.clock_net_name);
    clock->set_clock_source(nullptr);
    clock->set_clock_source_net(nullptr);
    clock->set_preclustered_sink_reuse(true);
    clock->clear_preclustered_anchor_input_net_names();
    clock->clear_loads();
    clock->clearMembership();

    auto* cts_net = buildNetFromIdbNet(source_idb_net);
    if (cts_net == nullptr) {
      return nullptr;
    }
    cts_net->set_driver(nullptr);
    cts_net->set_loads({});
    clock->set_clock_source_net(cts_net);

    const auto source_pins = collectIdbClockNetPins(source_idb_net);
    if (source_pins.driver == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_target.clock_name << "\": source iDB net \"" << clock_target.clock_net_name
                << "\" has no resolvable driver pin.";
      return nullptr;
    }

    Inst* source_cts_inst = nullptr;
    if (auto* source_idb_inst = source_pins.driver->get_instance(); source_idb_inst != nullptr) {
      source_cts_inst = buildInstFromIdbInst(source_idb_inst);
      if (source_cts_inst == nullptr) {
        return nullptr;
      }
    }
    auto* source_pin = buildOrFindPinFromIdbPin(source_pins.driver, source_cts_inst);
    if (source_pin == nullptr) {
      return nullptr;
    }
    source_pin->set_net(cts_net);
    if (source_cts_inst != nullptr) {
      source_cts_inst->insertDriverPin(source_pin);
    }
    clock->set_clock_source(source_pin);
    cts_net->set_driver(source_pin);

    std::vector<Pin*> cts_loads;
    cts_loads.reserve(clock_target.preclustered_sink_anchors.size());
    for (const auto& anchor : clock_target.preclustered_sink_anchors) {
      auto* idb_inst = findIdbInstOrError(clock_target.clock_name, anchor.driver_inst_name);
      if (idb_inst == nullptr) {
        return nullptr;
      }
      auto* cts_inst = buildInstFromIdbInst(idb_inst);
      if (cts_inst == nullptr) {
        return nullptr;
      }

      auto* output_idb_pin = findIdbInstPinOrError(clock_target.clock_name, idb_inst, anchor.output_pin_name);
      auto* input_idb_pin = findIdbInstPinOrError(clock_target.clock_name, idb_inst, anchor.input_pin_name);
      if (output_idb_pin == nullptr || input_idb_pin == nullptr) {
        return nullptr;
      }

      auto* output_pin = buildOrFindPinFromIdbPin(output_idb_pin, cts_inst);
      auto* input_pin = buildOrFindPinFromIdbPin(input_idb_pin, cts_inst);
      if (output_pin == nullptr || input_pin == nullptr) {
        return nullptr;
      }
      output_pin->set_net(nullptr);
      input_pin->set_net(cts_net);
      cts_inst->insertDriverPin(output_pin);
      cts_inst->add_pin(input_pin);
      cts_loads.push_back(input_pin);
      clock->add_preclustered_anchor_input_net_name(anchor.input_net_name);
    }

    clock->set_loads(cts_loads);
    cts_net->set_loads(cts_loads);
    LOG_INFO << "CTS clock read: clock \"" << clock_target.clock_name << "\" reuses " << clock_target.preclustered_sink_anchors.size()
             << " preclustered sink buffer anchor(s).";
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

  auto buildOrFindPinFromIdbPin(idb::IdbPin* idb_pin, Inst* cts_inst) -> Pin*
  {
    if (idb_pin == nullptr) {
      return nullptr;
    }
    if (_wrapper->_idb2cts_pin_map.contains(idb_pin)) {
      return _wrapper->_idb2cts_pin_map.at(idb_pin);
    }
    const auto pin_full_name = ctsPinFullName(idb_pin, cts_inst);
    if (!pin_full_name.empty()) {
      auto* existing_pin = DESIGN_INST.findPin(pin_full_name);
      if (existing_pin != nullptr) {
        bindIdbPin(idb_pin, existing_pin);
        return existing_pin;
      }
    }
    auto* cts_pin = buildPinFromIdbPin(idb_pin, cts_inst);
    if (cts_pin == nullptr) {
      return nullptr;
    }
    if (!DESIGN_INST.indexPin(cts_pin)) {
      LOG_ERROR << "CTS clock read failed: failed to index CTS pin \"" << Design::getPinFullName(cts_pin) << "\".";
      return nullptr;
    }
    return cts_pin;
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

  auto findIdbInstOrError(const std::string& clock_name, const std::string& inst_name) -> idb::IdbInstance*
  {
    auto* idb_design = findIdbDesign();
    auto* inst_list = idb_design == nullptr ? nullptr : idb_design->get_instance_list();
    auto* idb_inst = inst_list == nullptr ? nullptr : inst_list->find_instance(inst_name);
    if (idb_inst == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": preclustered sink anchor inst \"" << inst_name
                << "\" is not found in iDB.";
      return nullptr;
    }
    return idb_inst;
  }

  static auto findIdbInstPinOrError(const std::string& clock_name, idb::IdbInstance* idb_inst, const std::string& pin_name) -> idb::IdbPin*
  {
    auto* idb_pin = idb_inst == nullptr ? nullptr : idb_inst->get_pin_by_term(pin_name);
    if (idb_pin == nullptr && idb_inst != nullptr && idb_inst->get_pin_list() != nullptr) {
      for (auto* candidate_pin : idb_inst->get_pin_list()->get_pin_list()) {
        if (candidate_pin != nullptr && candidate_pin->get_pin_name() == pin_name) {
          idb_pin = candidate_pin;
          break;
        }
      }
    }
    if (idb_pin == nullptr) {
      LOG_ERROR << "CTS clock read failed for clock \"" << clock_name << "\": preclustered sink anchor pin \"" << pin_name
                << "\" is not found in iDB inst \"" << (idb_inst == nullptr ? "" : idb_inst->get_name()) << "\".";
    }
    return idb_pin;
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

auto Wrapper::readClocks(const std::vector<std::pair<std::string, std::string>>& clock_net_pairs) -> bool
{
  return CtsClockReader(*this).readClocks(clock_net_pairs);
}

auto Wrapper::readTraceClockTargets(const std::vector<ClockTraceClockTarget>& clock_targets) -> bool
{
  return CtsClockReader(*this).readTraceClockTargets(clock_targets);
}

}  // namespace icts
