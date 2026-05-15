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
 * @file ClockTraceResolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief SDC-rooted CTS clock trace resolver implementation.
 */

#include "ClockTraceResolver.hh"

#include <glog/logging.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <deque>
#include <map>
#include <ranges>
#include <set>
#include <sstream>
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
#include "LibParserRustC.hh"
#include "Log.hh"
#include "SdcClockModel.hh"
#include "api/TimingEngine.hh"
#include "config/Config.hh"
#include "idm.h"
#include "liberty/Lib.hh"
#include "logger/Schema.hh"

namespace icts {
namespace {

constexpr std::size_t kTraceNetVisitLimit = 200000U;
constexpr std::size_t kTraceDepthLimit = 512U;

struct IdbNetPins
{
  idb::IdbPin* driver = nullptr;
  std::vector<idb::IdbPin*> loads;
  std::vector<idb::IdbPin*> all_pins;
};

struct ClockSinkStats
{
  std::size_t sequential_clock_sinks = 0U;
  std::size_t macro_clock_sinks = 0U;
};

struct TraceNode
{
  idb::IdbNet* net = nullptr;
  std::string path;
  std::size_t depth = 0U;
};

struct TraceTransition
{
  idb::IdbNet* net = nullptr;
  std::string path_step;
  std::string reason;
};

struct CaseConstraintSet
{
  std::set<std::string> pin_names;
  std::set<std::string> net_names;
};

struct ClockDeclView
{
  std::string clock_kind;
  std::string master_clock_name;
  std::set<std::string> sdc_target_net_names;
};

auto netName(idb::IdbNet* net) -> std::string
{
  return net == nullptr ? std::string{} : net->get_net_name();
}

auto termName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  auto* term = pin->get_term();
  return term == nullptr ? pin->get_pin_name() : term->get_name();
}

auto pinFullName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  auto* inst = pin->get_instance();
  if (inst == nullptr) {
    return termName(pin);
  }
  return inst->get_name() + "/" + termName(pin);
}

auto pinDisplayName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  const auto full_name = pinFullName(pin);
  return full_name.empty() ? pin->get_pin_name() : full_name;
}

auto appendUniquePin(std::vector<idb::IdbPin*>& pins, std::unordered_set<idb::IdbPin*>& seen_pins, idb::IdbPin* pin) -> void
{
  if (pin == nullptr || !seen_pins.insert(pin).second) {
    return;
  }
  pins.push_back(pin);
}

auto collectNetPins(idb::IdbNet* net) -> IdbNetPins
{
  IdbNetPins net_pins;
  if (net == nullptr) {
    return net_pins;
  }

  const auto io_pin_count = net->get_io_pins() == nullptr ? 0U : net->get_io_pins()->get_pin_list().size();
  const auto inst_pin_count = net->get_instance_pin_list() == nullptr ? 0U : net->get_instance_pin_list()->get_pin_list().size();
  net_pins.all_pins.reserve(io_pin_count + inst_pin_count);
  std::unordered_set<idb::IdbPin*> seen_pins;
  seen_pins.reserve(io_pin_count + inst_pin_count);
  if (net->get_io_pins() != nullptr) {
    for (auto* pin : net->get_io_pins()->get_pin_list()) {
      appendUniquePin(net_pins.all_pins, seen_pins, pin);
    }
  }
  if (net->get_instance_pin_list() != nullptr) {
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      appendUniquePin(net_pins.all_pins, seen_pins, pin);
    }
  }

  for (auto* pin : net_pins.all_pins) {
    auto* term = pin == nullptr ? nullptr : pin->get_term();
    if (term == nullptr) {
      continue;
    }
    if (!pin->is_io_pin()
        && (term->get_direction() == idb::IdbConnectDirection::kOutput
            || term->get_direction() == idb::IdbConnectDirection::kOutputTriState)) {
      net_pins.driver = pin;
      break;
    }
  }
  if (net_pins.driver == nullptr) {
    for (auto* pin : net_pins.all_pins) {
      auto* term = pin == nullptr ? nullptr : pin->get_term();
      if (term != nullptr && pin->is_io_pin() && term->get_direction() == idb::IdbConnectDirection::kInput) {
        net_pins.driver = pin;
        break;
      }
    }
  }

  for (auto* pin : net_pins.all_pins) {
    if (pin != net_pins.driver) {
      net_pins.loads.push_back(pin);
    }
  }
  return net_pins;
}

auto isInputLike(idb::IdbPin* pin) -> bool
{
  auto* term = pin == nullptr ? nullptr : pin->get_term();
  if (term == nullptr) {
    return false;
  }
  return term->get_direction() == idb::IdbConnectDirection::kInput || term->get_direction() == idb::IdbConnectDirection::kInOut;
}

auto isOutputLike(idb::IdbPin* pin) -> bool
{
  auto* term = pin == nullptr ? nullptr : pin->get_term();
  if (term == nullptr) {
    return false;
  }
  return term->get_direction() == idb::IdbConnectDirection::kOutput || term->get_direction() == idb::IdbConnectDirection::kOutputTriState
         || term->get_direction() == idb::IdbConnectDirection::kInOut;
}

auto findLibCell(idb::IdbInstance* inst) -> ista::LibCell*
{
  auto* cell_master = inst == nullptr ? nullptr : inst->get_cell_master();
  if (cell_master == nullptr) {
    return nullptr;
  }
  auto* timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  if (timing_engine == nullptr) {
    return nullptr;
  }
  return timing_engine->findLibertyCell(cell_master->get_name().c_str());
}

auto findLibPort(ista::LibCell* lib_cell, idb::IdbPin* pin) -> ista::LibPort*
{
  if (lib_cell == nullptr || pin == nullptr) {
    return nullptr;
  }
  const auto port_name = termName(pin);
  if (port_name.empty()) {
    return nullptr;
  }
  return lib_cell->get_cell_port_or_port_bus(port_name.c_str());
}

auto isSequentialCell(idb::IdbInstance* inst, ista::LibCell* lib_cell) -> bool
{
  if (lib_cell != nullptr && lib_cell->isSequentialCell() && !lib_cell->isICG()) {
    return true;
  }
  return inst != nullptr && inst->is_flip_flop();
}

auto isClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool
{
  if (pin == nullptr || pin->is_io_pin() || !isInputLike(pin)) {
    return false;
  }
  auto* term = pin->get_term();
  auto* lib_port = findLibPort(lib_cell, pin);
  if (lib_port != nullptr && lib_port->isClock()) {
    return true;
  }
  if (pin->is_flip_flop_clk()) {
    return true;
  }
  return term != nullptr && term->get_type() == idb::IdbConnectType::kClock && pin->get_instance() != nullptr
         && pin->get_instance()->is_flip_flop();
}

auto isMacroClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool
{
  if (pin == nullptr || pin->is_io_pin() || !isInputLike(pin)) {
    return false;
  }
  auto* inst = pin->get_instance();
  auto* cell_master = inst == nullptr ? nullptr : inst->get_cell_master();
  if (cell_master == nullptr || !cell_master->is_block()) {
    return false;
  }
  auto* term = pin->get_term();
  auto* lib_port = findLibPort(lib_cell, pin);
  return (term != nullptr && term->get_type() == idb::IdbConnectType::kClock) || (lib_port != nullptr && lib_port->isClock());
}

auto countDirectClockSinks(idb::IdbNet* net) -> ClockSinkStats
{
  ClockSinkStats stats;
  const auto net_pins = collectNetPins(net);
  for (auto* load_pin : net_pins.loads) {
    auto* inst = load_pin == nullptr ? nullptr : load_pin->get_instance();
    auto* lib_cell = findLibCell(inst);
    if (isClockSinkPin(load_pin, lib_cell)) {
      ++stats.sequential_clock_sinks;
    } else if (isMacroClockSinkPin(load_pin, lib_cell)) {
      ++stats.macro_clock_sinks;
    }
  }
  return stats;
}

auto countDirectClockSinksForReport(idb::IdbNet* net) -> ClockSinkStats
{
  ClockSinkStats stats;
  auto* pin_list = net == nullptr ? nullptr : net->get_instance_pin_list();
  if (pin_list == nullptr) {
    return stats;
  }
  for (auto* pin : pin_list->get_pin_list()) {
    auto* inst = pin == nullptr ? nullptr : pin->get_instance();
    auto* lib_cell = findLibCell(inst);
    if (isClockSinkPin(pin, lib_cell)) {
      ++stats.sequential_clock_sinks;
    } else if (isMacroClockSinkPin(pin, lib_cell)) {
      ++stats.macro_clock_sinks;
    }
  }
  return stats;
}

auto isClockTarget(const ClockSinkStats& stats) -> bool
{
  return stats.sequential_clock_sinks > 0U || stats.macro_clock_sinks > 0U;
}

auto targetKind(const ClockSinkStats& stats) -> std::string
{
  if (stats.sequential_clock_sinks > 0U && stats.macro_clock_sinks > 0U) {
    return "sequential_ck+macro_clock";
  }
  if (stats.sequential_clock_sinks > 0U) {
    return "sequential_ck";
  }
  if (stats.macro_clock_sinks > 0U) {
    return "macro_clock";
  }
  return "trace_through";
}

auto clockKindName(const SdcClockDecl& clock) -> std::string
{
  return clock.kind == SdcClockDecl::Kind::kGenerated ? "generated" : "primary";
}

auto masterClockName(const SdcClockDecl& clock) -> std::string
{
  if (clock.kind == SdcClockDecl::Kind::kGenerated) {
    return clock.master_clock_name.empty() ? "n/a" : clock.master_clock_name;
  }
  return "self";
}

auto dominanceForRecord(const ClockTraceRecord& record, const std::string& clock_kind) -> std::string
{
  if (record.status == "ambiguous") {
    return "ambiguous_sdc_ownership";
  }
  if (record.target_kind == "generated_clock_boundary") {
    return "blocked_by_generated_clock";
  }
  if (record.status == "skipped") {
    return "no_netlist_source";
  }
  if (record.status == "rejected") {
    return "no_physical_dominance";
  }
  if (clock_kind == "generated") {
    return "owned_by_generated_clock";
  }
  if (clock_kind == "primary") {
    return "owned_by_primary_clock";
  }
  return "undetermined";
}

auto strongTargetSinkThreshold() -> std::size_t
{
  constexpr std::size_t min_clock_target_sinks = 4U;
  return std::max(min_clock_target_sinks, static_cast<std::size_t>(CONFIG_INST.get_max_fanout()));
}

auto isStrongClockTarget(const ClockTraceRecord& record, std::size_t sink_threshold) -> bool
{
  return record.macro_clock_sinks > 0U || record.sequential_clock_sinks > sink_threshold;
}

auto findInstPinByLibPort(idb::IdbInstance* inst, ista::LibPort* lib_port) -> idb::IdbPin*
{
  if (inst == nullptr || lib_port == nullptr || inst->get_pin_list() == nullptr) {
    return nullptr;
  }
  const std::string port_name = lib_port->get_port_name();
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (pin == nullptr) {
      continue;
    }
    if (termName(pin) == port_name || pin->get_pin_name() == port_name) {
      return pin;
    }
  }
  return nullptr;
}

auto collectOutputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>
{
  std::vector<idb::IdbPin*> outputs;
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return outputs;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (isOutputLike(pin)) {
      outputs.push_back(pin);
    }
  }
  return outputs;
}

auto collectInputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>
{
  std::vector<idb::IdbPin*> inputs;
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return inputs;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (isInputLike(pin)) {
      inputs.push_back(pin);
    }
  }
  return inputs;
}

auto isCaseConstrained(idb::IdbPin* pin, const CaseConstraintSet& case_constraints) -> bool
{
  if (pin == nullptr) {
    return false;
  }
  const auto full_name = pinFullName(pin);
  if (case_constraints.pin_names.contains(full_name) || case_constraints.pin_names.contains(termName(pin))) {
    return true;
  }
  auto* net = pin->get_net();
  return net != nullptr && case_constraints.net_names.contains(net->get_net_name());
}

auto otherInputsCaseConstrained(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, const CaseConstraintSet& case_constraints) -> bool
{
  bool has_other_input = false;
  for (auto* input_pin : collectInputPins(inst)) {
    if (input_pin == nullptr || input_pin == clock_input_pin) {
      continue;
    }
    has_other_input = true;
    if (!isCaseConstrained(input_pin, case_constraints)) {
      return false;
    }
  }
  return has_other_input;
}

auto libertyExpressionUsesPort(RustLibertyExpr* expression, const std::string& port_name) -> bool
{
  if (expression == nullptr || port_name.empty()) {
    return false;
  }
  std::vector<RustLibertyExpr*> pending_expressions = {expression};
  while (!pending_expressions.empty()) {
    auto* current = pending_expressions.back();
    pending_expressions.pop_back();
    if (current == nullptr) {
      continue;
    }
    if (current->port_name != nullptr && port_name == current->port_name) {
      return true;
    }
    if (current->left != nullptr) {
      pending_expressions.push_back(current->left);
    }
    if (current->right != nullptr) {
      pending_expressions.push_back(current->right);
    }
  }
  return false;
}

auto outputFunctionUsesInput(ista::LibCell* lib_cell, idb::IdbPin* output_pin, idb::IdbPin* input_pin) -> bool
{
  if (lib_cell == nullptr) {
    return true;
  }
  auto* output_port = findLibPort(lib_cell, output_pin);
  if (output_port == nullptr || output_port->get_func_expr() == nullptr) {
    return true;
  }
  return libertyExpressionUsesPort(output_port->get_func_expr(), termName(input_pin));
}

auto netHasDirectClockSinks(idb::IdbNet* net) -> bool
{
  return net != nullptr && isClockTarget(countDirectClockSinks(net));
}

auto countInputPinsOnNet(idb::IdbInstance* inst, idb::IdbNet* net) -> std::size_t
{
  if (inst == nullptr || net == nullptr) {
    return 0U;
  }
  std::size_t input_pin_count = 0U;
  for (auto* input_pin : collectInputPins(inst)) {
    if (input_pin != nullptr && input_pin->get_net() == net) {
      ++input_pin_count;
    }
  }
  return input_pin_count;
}

auto countClockTargetOutputs(const std::vector<idb::IdbPin*>& output_pins) -> std::size_t
{
  std::size_t clock_target_output_count = 0U;
  for (auto* output_pin : output_pins) {
    auto* output_net = output_pin == nullptr ? nullptr : output_pin->get_net();
    if (netHasDirectClockSinks(output_net)) {
      ++clock_target_output_count;
    }
  }
  return clock_target_output_count;
}

auto libertyMarksClockInput(idb::IdbPin* input_pin, ista::LibCell* lib_cell) -> bool
{
  if (input_pin == nullptr || lib_cell == nullptr) {
    return false;
  }
  auto* lib_port = findLibPort(lib_cell, input_pin);
  return lib_port != nullptr && (lib_port->isClock() || lib_port->get_clock_gate_clock_pin());
}

auto otherInputsAreControlCandidates(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, ista::LibCell* lib_cell,
                                     const CaseConstraintSet& case_constraints) -> bool
{
  bool has_other_input = false;
  for (auto* input_pin : collectInputPins(inst)) {
    if (input_pin == nullptr || input_pin == clock_input_pin) {
      continue;
    }
    has_other_input = true;
    if (isCaseConstrained(input_pin, case_constraints)) {
      continue;
    }
    if (libertyMarksClockInput(input_pin, lib_cell) || netHasDirectClockSinks(input_pin->get_net())) {
      return false;
    }
  }
  return has_other_input;
}

auto addOutputTransition(std::vector<TraceTransition>& transitions, idb::IdbPin* output_pin, const std::string& reason) -> void
{
  if (output_pin == nullptr || output_pin->get_net() == nullptr) {
    return;
  }
  transitions.push_back({
      output_pin->get_net(),
      pinDisplayName(output_pin) + "->" + output_pin->get_net()->get_net_name(),
      reason,
  });
}

auto collectSafeTransitions(idb::IdbNet* net, const CaseConstraintSet& case_constraints) -> std::vector<TraceTransition>
{
  std::vector<TraceTransition> transitions;
  const auto net_pins = collectNetPins(net);
  for (auto* load_pin : net_pins.loads) {
    if (load_pin == nullptr || load_pin->is_io_pin() || !isInputLike(load_pin)) {
      continue;
    }
    auto* inst = load_pin->get_instance();
    auto* lib_cell = findLibCell(inst);
    if (inst == nullptr || isSequentialCell(inst, lib_cell)) {
      continue;
    }

    if (lib_cell != nullptr && (lib_cell->isBuffer() || lib_cell->isInverter())) {
      ista::LibPort* input_port = nullptr;
      ista::LibPort* output_port = nullptr;
      lib_cell->bufferPorts(input_port, output_port);
      if (input_port != nullptr && output_port != nullptr) {
        auto* input_pin = findInstPinByLibPort(inst, input_port);
        auto* output_pin = findInstPinByLibPort(inst, output_port);
        if (input_pin == load_pin) {
          addOutputTransition(transitions, output_pin, lib_cell->isBuffer() ? "buffer" : "inverter");
        }
      }
      continue;
    }

    if (lib_cell != nullptr && lib_cell->isICG()) {
      auto* input_port = findLibPort(lib_cell, load_pin);
      const bool is_clock_gate_clock_pin = input_port != nullptr && (input_port->get_clock_gate_clock_pin() || input_port->isClock());
      auto* term = load_pin->get_term();
      if (is_clock_gate_clock_pin || (term != nullptr && term->get_type() == idb::IdbConnectType::kClock)) {
        for (auto* output_pin : collectOutputPins(inst)) {
          addOutputTransition(transitions, output_pin, "clock_gate");
        }
      }
      continue;
    }

    const auto input_pins = collectInputPins(inst);
    const auto output_pins = collectOutputPins(inst);
    const bool single_input_output_trace = input_pins.size() == 1U && input_pins.front() == load_pin && output_pins.size() == 1U;
    const bool constrained_gate = otherInputsCaseConstrained(inst, load_pin, case_constraints);
    const bool single_clock_provenance_input = countInputPinsOnNet(inst, net) == 1U && load_pin->get_net() == net;
    const auto clock_target_output_count = countClockTargetOutputs(output_pins);
    const bool constrained_output_count = output_pins.size() == 1U || clock_target_output_count == 1U;
    const bool control_candidate_gate = constrained_output_count && single_clock_provenance_input
                                        && otherInputsAreControlCandidates(inst, load_pin, lib_cell, case_constraints);
    if (!single_input_output_trace && !constrained_gate && !control_candidate_gate) {
      continue;
    }

    for (auto* output_pin : output_pins) {
      auto* output_net = output_pin == nullptr ? nullptr : output_pin->get_net();
      if (output_net == nullptr) {
        continue;
      }
      const auto output_stats = countDirectClockSinks(output_net);
      if (!outputFunctionUsesInput(lib_cell, output_pin, load_pin) && !(control_candidate_gate && isClockTarget(output_stats))) {
        continue;
      }
      if (isClockTarget(output_stats)) {
        std::string reason = "single_input_clock_buffer_like";
        if (constrained_gate) {
          reason = "case_constrained_comb_clock_gate";
        } else if (control_candidate_gate) {
          reason = "comb_output_direct_clock_sinks";
        }
        addOutputTransition(transitions, output_pin, reason);
      } else if (output_net->is_clock() && constrained_gate) {
        addOutputTransition(transitions, output_pin, "case_constrained_clock_net");
      }
    }
  }
  return transitions;
}

auto objectKindName(SdcObjectKind kind) -> std::string
{
  switch (kind) {
    case SdcObjectKind::kPort:
      return "port";
    case SdcObjectKind::kPin:
      return "pin";
    case SdcObjectKind::kNet:
      return "net";
    case SdcObjectKind::kClock:
      return "clock";
    case SdcObjectKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto resolvePortNet(idb::IdbDesign* idb_design, const std::string& port_name) -> idb::IdbNet*
{
  auto* io_pin_list = idb_design == nullptr ? nullptr : idb_design->get_io_pin_list();
  if (io_pin_list != nullptr) {
    for (auto* pin : io_pin_list->get_pin_list()) {
      if (pin == nullptr) {
        continue;
      }
      if (pin->get_pin_name() == port_name || termName(pin) == port_name) {
        return pin->get_net();
      }
    }
  }
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  return net_list == nullptr ? nullptr : net_list->find_net(port_name);
}

auto resolvePinNet(idb::IdbDesign* idb_design, const std::string& pin_name) -> idb::IdbNet*
{
  auto separator_pos = pin_name.rfind('/');
  if (separator_pos == std::string::npos) {
    separator_pos = pin_name.rfind(':');
  }
  if (separator_pos == std::string::npos || separator_pos == 0U || separator_pos + 1U >= pin_name.size()) {
    auto* io_pin_list = idb_design == nullptr ? nullptr : idb_design->get_io_pin_list();
    if (io_pin_list == nullptr) {
      return nullptr;
    }
    for (auto* pin : io_pin_list->get_pin_list()) {
      if (pin != nullptr && (pin->get_pin_name() == pin_name || termName(pin) == pin_name)) {
        return pin->get_net();
      }
    }
    return nullptr;
  }
  auto* inst_list = idb_design == nullptr ? nullptr : idb_design->get_instance_list();
  if (inst_list == nullptr) {
    return nullptr;
  }
  const auto inst_name = pin_name.substr(0U, separator_pos);
  const auto port_name = pin_name.substr(separator_pos + 1U);
  auto* inst = inst_list->find_instance(inst_name);
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return nullptr;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (pin != nullptr && (pin->get_pin_name() == port_name || termName(pin) == port_name)) {
      return pin->get_net();
    }
  }
  return nullptr;
}

auto resolveRefNets(idb::IdbDesign* idb_design, const SdcObjectRef& ref) -> std::vector<idb::IdbNet*>
{
  std::vector<idb::IdbNet*> nets;
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  auto append_net = [&nets](idb::IdbNet* net) -> void {
    if (net != nullptr && std::ranges::find(nets, net) == nets.end()) {
      nets.push_back(net);
    }
  };

  switch (ref.kind) {
    case SdcObjectKind::kPort:
      append_net(resolvePortNet(idb_design, ref.pattern));
      break;
    case SdcObjectKind::kPin:
      append_net(resolvePinNet(idb_design, ref.pattern));
      break;
    case SdcObjectKind::kNet:
      append_net(net_list == nullptr ? nullptr : net_list->find_net(ref.pattern));
      break;
    case SdcObjectKind::kClock:
      break;
    case SdcObjectKind::kUnknown:
      append_net(net_list == nullptr ? nullptr : net_list->find_net(ref.pattern));
      append_net(resolvePortNet(idb_design, ref.pattern));
      append_net(resolvePinNet(idb_design, ref.pattern));
      break;
  }
  return nets;
}

auto buildCaseConstraintSet(const SdcClockData& clock_data) -> CaseConstraintSet
{
  CaseConstraintSet constraints;
  for (const auto& analysis : clock_data.case_analyses) {
    for (const auto& object : analysis.objects) {
      if (object.kind == SdcObjectKind::kPin) {
        constraints.pin_names.insert(object.pattern);
      } else if (object.kind == SdcObjectKind::kNet) {
        constraints.net_names.insert(object.pattern);
      } else if (object.kind == SdcObjectKind::kUnknown) {
        constraints.pin_names.insert(object.pattern);
        constraints.net_names.insert(object.pattern);
      }
    }
  }
  return constraints;
}

auto buildGeneratedBoundaryOwners(idb::IdbDesign* idb_design, const SdcClockData& clock_data)
    -> std::unordered_map<idb::IdbNet*, std::string>
{
  std::unordered_map<idb::IdbNet*, std::string> boundary_owner_by_net;
  for (const auto& clock : clock_data.clocks) {
    if (clock.kind != SdcClockDecl::Kind::kGenerated) {
      continue;
    }
    for (const auto& target : clock.targets) {
      for (auto* net : resolveRefNets(idb_design, target)) {
        boundary_owner_by_net[net] = clock.clock_name;
      }
    }
  }
  return boundary_owner_by_net;
}

auto joinNames(const std::set<std::string>& names, std::size_t display_limit = 8U) -> std::string
{
  if (names.empty()) {
    return "n/a";
  }
  std::ostringstream stream;
  std::size_t emitted_count = 0U;
  for (const auto& name : names) {
    if (emitted_count > 0U) {
      stream << ", ";
    }
    if (emitted_count >= display_limit) {
      stream << "...";
      break;
    }
    stream << name;
    ++emitted_count;
  }
  if (names.size() > display_limit) {
    stream << " (" << names.size() << " total)";
  }
  return stream.str();
}

auto buildClockDeclViews(idb::IdbDesign* idb_design, const SdcClockData& clock_data) -> std::map<std::string, ClockDeclView>
{
  std::map<std::string, ClockDeclView> clock_view_by_name;
  for (const auto& clock : clock_data.clocks) {
    auto& view = clock_view_by_name[clock.clock_name];
    view.clock_kind = clockKindName(clock);
    view.master_clock_name = masterClockName(clock);
    for (const auto& target : clock.targets) {
      for (auto* net : resolveRefNets(idb_design, target)) {
        if (net != nullptr) {
          view.sdc_target_net_names.insert(net->get_net_name());
        }
      }
    }
  }
  return clock_view_by_name;
}

auto annotateRecordOwnership(ClockTraceRecord& record, const std::map<std::string, ClockDeclView>& clock_view_by_name) -> void
{
  const auto view_iter = clock_view_by_name.find(record.clock_name);
  if (view_iter == clock_view_by_name.end()) {
    return;
  }
  record.clock_kind = view_iter->second.clock_kind;
  record.master_clock_name = view_iter->second.master_clock_name;
  record.dominance = dominanceForRecord(record, record.clock_kind);
}

auto collectTracedNetNames(const std::vector<ClockTraceRecord>& records) -> std::set<std::string>
{
  std::set<std::string> traced_net_names;
  for (const auto& record : records) {
    if (record.net_name.empty() || record.status == "rejected" || record.status == "skipped") {
      continue;
    }
    traced_net_names.insert(record.net_name);
  }
  return traced_net_names;
}

auto collectUnownedClockLikeRecords(idb::IdbDesign* idb_design, const std::vector<ClockTraceRecord>& records)
    -> std::vector<ClockTraceRecord>
{
  std::vector<ClockTraceRecord> unowned_records;
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  if (net_list == nullptr) {
    return unowned_records;
  }

  const auto traced_net_names = collectTracedNetNames(records);
  for (auto* net : net_list->get_net_list()) {
    if (net == nullptr || traced_net_names.contains(net->get_net_name())) {
      continue;
    }
    const auto stats = countDirectClockSinksForReport(net);
    if (!isClockTarget(stats)) {
      continue;
    }
    ClockTraceRecord record;
    record.clock_name = "unowned";
    record.net_name = net->get_net_name();
    record.status = "warning";
    record.target_kind = targetKind(stats);
    record.sequential_clock_sinks = stats.sequential_clock_sinks;
    record.macro_clock_sinks = stats.macro_clock_sinks;
    record.reason = "no_sdc_clock_ownership";
    record.clock_kind = "unowned";
    record.master_clock_name = "n/a";
    record.dominance = "unowned_clock_like_net";
    unowned_records.push_back(std::move(record));
  }

  std::ranges::sort(unowned_records, [](const auto& lhs, const auto& rhs) -> bool { return lhs.net_name < rhs.net_name; });
  return unowned_records;
}

auto numberToString(std::size_t value) -> std::string;

auto emitSdcClockOwnershipReport(const SdcClockData& clock_data, const std::map<std::string, ClockDeclView>& clock_view_by_name,
                                 const std::vector<ClockTraceRecord>& records) -> void
{
  std::map<std::string, std::set<std::string>> owned_net_names_by_clock;
  std::map<std::string, std::set<std::string>> cts_target_net_names_by_clock;
  std::map<std::string, ClockSinkStats> target_stats_by_clock;
  for (const auto& record : records) {
    if (record.net_name.empty()) {
      continue;
    }
    if (record.dominance == "owned_by_primary_clock" || record.dominance == "owned_by_generated_clock") {
      owned_net_names_by_clock[record.clock_name].insert(record.net_name);
    }
    if (record.status == "accepted") {
      cts_target_net_names_by_clock[record.clock_name].insert(record.net_name);
      target_stats_by_clock[record.clock_name].sequential_clock_sinks += record.sequential_clock_sinks;
      target_stats_by_clock[record.clock_name].macro_clock_sinks += record.macro_clock_sinks;
    }
  }

  schema::TableRows rows;
  rows.reserve(clock_data.clocks.size());
  for (const auto& clock : clock_data.clocks) {
    const auto view_iter = clock_view_by_name.find(clock.clock_name);
    const auto clock_kind = view_iter == clock_view_by_name.end() ? clockKindName(clock) : view_iter->second.clock_kind;
    const auto master_clock = view_iter == clock_view_by_name.end() ? masterClockName(clock) : view_iter->second.master_clock_name;
    const auto sdc_target_nets = view_iter == clock_view_by_name.end() ? std::set<std::string>{} : view_iter->second.sdc_target_net_names;
    const auto stats_iter = target_stats_by_clock.find(clock.clock_name);
    const auto sequential_clock_sinks = stats_iter == target_stats_by_clock.end() ? 0U : stats_iter->second.sequential_clock_sinks;
    const auto macro_clock_sinks = stats_iter == target_stats_by_clock.end() ? 0U : stats_iter->second.macro_clock_sinks;
    rows.push_back({clock.clock_name, clock_kind, master_clock, joinNames(sdc_target_nets),
                    joinNames(owned_net_names_by_clock[clock.clock_name]), joinNames(cts_target_net_names_by_clock[clock.clock_name]),
                    numberToString(sequential_clock_sinks), numberToString(macro_clock_sinks)});
  }
  if (rows.empty()) {
    rows.push_back({"n/a", "n/a", "n/a", "n/a", "n/a", "n/a", "0", "0"});
  }
  schema::EmitTable("SDC Clock Ownership Overview",
                    {"Clock", "Kind", "Master Clock", "SDC Target Nets", "Owned Nets", "CTS Target Nets", "Seq CK Sinks", "Macro CK Sinks"},
                    rows);
}

auto emitUnownedClockLikeNetReport(const std::vector<ClockTraceRecord>& records) -> void
{
  schema::TableRows rows;
  rows.reserve(records.size());
  for (const auto& record : records) {
    rows.push_back({record.net_name, record.dominance, record.target_kind, numberToString(record.sequential_clock_sinks),
                    numberToString(record.macro_clock_sinks), record.reason});
  }
  if (rows.empty()) {
    rows.push_back({"n/a", "none", "n/a", "0", "0", "no_unowned_clock_like_nets"});
  }
  schema::EmitTable("Unowned Clock-like Nets", {"Net", "Dominance", "Target Kind", "Seq CK Sinks", "Macro CK Sinks", "Reason"}, rows);
}

auto traceClock(idb::IdbDesign* idb_design, const SdcClockDecl& clock, const CaseConstraintSet& case_constraints,
                const std::unordered_map<idb::IdbNet*, std::string>& generated_boundary_owner_by_net) -> std::vector<ClockTraceRecord>
{
  std::vector<ClockTraceRecord> records;
  if (clock.is_virtual || clock.targets.empty()) {
    records.push_back({clock.clock_name, "", "skipped", "virtual_clock", 0U, 0U, "", "no_netlist_source"});
    return records;
  }

  std::vector<idb::IdbNet*> seed_nets;
  for (const auto& target : clock.targets) {
    auto resolved_nets = resolveRefNets(idb_design, target);
    seed_nets.insert(seed_nets.end(), resolved_nets.begin(), resolved_nets.end());
    if (resolved_nets.empty()) {
      records.push_back(
          {clock.clock_name, target.pattern, "rejected", objectKindName(target.kind), 0U, 0U, target.pattern, "unresolved_sdc_object"});
    }
  }
  std::ranges::sort(seed_nets);
  const auto unique_seed_nets = std::ranges::unique(seed_nets);
  seed_nets.erase(unique_seed_nets.begin(), unique_seed_nets.end());
  if (seed_nets.empty()) {
    records.push_back({clock.clock_name, "", "rejected", "sdc_source", 0U, 0U, "", "no_resolved_seed_net"});
    return records;
  }

  std::deque<TraceNode> queue;
  std::unordered_set<idb::IdbNet*> visited;
  visited.reserve(1024U);
  for (auto* seed_net : seed_nets) {
    if (seed_net != nullptr) {
      queue.push_back({seed_net, seed_net->get_net_name(), 0U});
    }
  }

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop_front();
    if (node.net == nullptr || !visited.insert(node.net).second) {
      continue;
    }
    if (visited.size() > kTraceNetVisitLimit) {
      records.push_back({clock.clock_name, netName(node.net), "rejected", "trace_limit", 0U, 0U, node.path, "trace_net_visit_limit"});
      break;
    }

    if (const auto boundary_iter = generated_boundary_owner_by_net.find(node.net);
        boundary_iter != generated_boundary_owner_by_net.end() && boundary_iter->second != clock.clock_name) {
      records.push_back(
          {clock.clock_name, netName(node.net), "trace_stop", "generated_clock_boundary", 0U, 0U, node.path, boundary_iter->second});
      continue;
    }

    const auto stats = countDirectClockSinks(node.net);
    if (isClockTarget(stats)) {
      records.push_back({clock.clock_name, node.net->get_net_name(), "accepted", targetKind(stats), stats.sequential_clock_sinks,
                         stats.macro_clock_sinks, node.path, "sdc_reachable_clock_sink_net"});
    }

    if (node.depth >= kTraceDepthLimit) {
      records.push_back({clock.clock_name, netName(node.net), "trace_stop", "depth_limit", stats.sequential_clock_sinks,
                         stats.macro_clock_sinks, node.path, "trace_depth_limit"});
      continue;
    }

    for (const auto& transition : collectSafeTransitions(node.net, case_constraints)) {
      if (transition.net == nullptr || visited.contains(transition.net)) {
        continue;
      }
      auto next_path = node.path;
      if (!next_path.empty()) {
        next_path += " -> ";
      }
      next_path += transition.path_step;
      queue.push_back({transition.net, std::move(next_path), node.depth + 1U});
      records.push_back(
          {clock.clock_name, transition.net->get_net_name(), "trace_through", transition.reason, 0U, 0U, node.path, transition.reason});
    }
  }
  return records;
}

auto numberToString(std::size_t value) -> std::string
{
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

auto emitClockTraceReport(const std::vector<ClockTraceRecord>& records) -> void
{
  schema::TableRows rows;
  rows.reserve(records.size());
  for (const auto& record : records) {
    rows.push_back({
        record.clock_name,
        record.clock_kind,
        record.master_clock_name,
        record.net_name,
        record.status,
        record.dominance,
        record.target_kind,
        numberToString(record.sequential_clock_sinks),
        numberToString(record.macro_clock_sinks),
        record.trace_path,
        record.reason,
    });
  }
  if (rows.empty()) {
    rows.push_back({"n/a", "n/a", "n/a", "n/a", "skipped", "undetermined", "n/a", "0", "0", "", "no_sdc_clock_trace_records"});
  }
  schema::EmitTable("Clock Trace Overview",
                    {"Clock", "Kind", "Master Clock", "Net", "Status", "Dominance", "Target Kind", "Seq CK Sinks", "Macro CK Sinks",
                     "Trace Path", "Reason"},
                    rows);
}

}  // namespace

auto ClockTraceResolver::resolve(const SdcClockData& clock_data) -> ClockTraceResult
{
  return resolve(clock_data, dmInst->get_idb_design());
}

auto ClockTraceResolver::resolve(const SdcClockData& clock_data, idb::IdbDesign* idb_design) -> ClockTraceResult
{
  ClockTraceResult result;
  if (idb_design == nullptr || idb_design->get_net_list() == nullptr) {
    schema::EmitDiagnostic(schema::DiagnosticLevel::kError, "ClockTraceResolver", "clock tracing skipped because iDB design is not ready.",
                           {{"clock_source", "sdc"}});
    LOG_ERROR << "ClockTraceResolver: iDB design or net list is null.";
    return result;
  }

  const auto case_constraints = buildCaseConstraintSet(clock_data);
  const auto generated_boundary_owner_by_net = buildGeneratedBoundaryOwners(idb_design, clock_data);
  const auto clock_view_by_name = buildClockDeclViews(idb_design, clock_data);

  std::vector<ClockTraceRecord> candidate_records;
  for (const auto& clock : clock_data.clocks) {
    auto records = traceClock(idb_design, clock, case_constraints, generated_boundary_owner_by_net);
    candidate_records.insert(candidate_records.end(), records.begin(), records.end());
  }

  std::map<std::string, std::set<std::string>> accepted_clock_names_by_net;
  std::map<std::string, bool> has_strong_target_by_clock;
  const auto sink_threshold = strongTargetSinkThreshold();
  for (const auto& record : candidate_records) {
    if (record.status == "accepted" && !record.net_name.empty()) {
      accepted_clock_names_by_net[record.net_name].insert(record.clock_name);
      has_strong_target_by_clock[record.clock_name]
          = has_strong_target_by_clock[record.clock_name] || isStrongClockTarget(record, sink_threshold);
    }
  }

  std::set<std::pair<std::string, std::string>> emitted_pairs;
  for (auto record : candidate_records) {
    if (record.status == "accepted" && has_strong_target_by_clock[record.clock_name] && !isStrongClockTarget(record, sink_threshold)) {
      record.status = "trace_stop";
      record.reason = "source_side_clock_sinks_below_target_threshold";
    }
    if (record.status == "accepted" && accepted_clock_names_by_net[record.net_name].size() > 1U) {
      record.status = "ambiguous";
      record.reason = "target_net_reachable_from_multiple_sdc_clocks";
    }
    if (record.status == "accepted" && emitted_pairs.insert({record.clock_name, record.net_name}).second) {
      result.clock_net_pairs.emplace_back(record.clock_name, record.net_name);
    }
    annotateRecordOwnership(record, clock_view_by_name);
    result.records.push_back(std::move(record));
  }

  result.unowned_clock_like_records = collectUnownedClockLikeRecords(idb_design, result.records);
  emitClockTraceReport(result.records);
  emitSdcClockOwnershipReport(clock_data, clock_view_by_name, result.records);
  emitUnownedClockLikeNetReport(result.unowned_clock_like_records);
  LOG_INFO << "ClockTraceResolver: accepted " << result.clock_net_pairs.size() << " CTS clock target net(s) from "
           << clock_data.clocks.size() << " SDC clock declaration(s); reported " << result.unowned_clock_like_records.size()
           << " unowned clock-like net(s).";
  return result;
}

}  // namespace icts
