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
 * @file ClockTracePins.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Pin, liberty, and transition helpers for SDC clock tracing.
 */

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ClockTraceResolver.hh"
#include "ClockTraceResolverInternal.hh"
#include "IdbCellMaster.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "LibParserRustC.hh"
#include "SdcClockModel.hh"
#include "api/TimingEngine.hh"
#include "config/Config.hh"
#include "liberty/Lib.hh"

namespace icts::clock_trace {

auto NetName(idb::IdbNet* net) -> std::string
{
  return net == nullptr ? std::string{} : net->get_net_name();
}

auto TermName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  auto* term = pin->get_term();
  return term == nullptr ? pin->get_pin_name() : term->get_name();
}

auto PinFullName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  auto* inst = pin->get_instance();
  if (inst == nullptr) {
    return TermName(pin);
  }
  return inst->get_name() + "/" + TermName(pin);
}

auto PinDisplayName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  const auto full_name = PinFullName(pin);
  return full_name.empty() ? pin->get_pin_name() : full_name;
}

namespace {

auto AppendUniquePin(std::vector<idb::IdbPin*>& pins, std::unordered_set<idb::IdbPin*>& seen_pins, idb::IdbPin* pin) -> void
{
  if (pin == nullptr || !seen_pins.insert(pin).second) {
    return;
  }
  pins.push_back(pin);
}

}  // namespace

auto CollectNetPins(idb::IdbNet* net) -> IdbNetPins
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
      AppendUniquePin(net_pins.all_pins, seen_pins, pin);
    }
  }
  if (net->get_instance_pin_list() != nullptr) {
    for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
      AppendUniquePin(net_pins.all_pins, seen_pins, pin);
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

auto IsInputLike(idb::IdbPin* pin) -> bool
{
  auto* term = pin == nullptr ? nullptr : pin->get_term();
  if (term == nullptr) {
    return false;
  }
  return term->get_direction() == idb::IdbConnectDirection::kInput || term->get_direction() == idb::IdbConnectDirection::kInOut;
}

auto IsOutputLike(idb::IdbPin* pin) -> bool
{
  auto* term = pin == nullptr ? nullptr : pin->get_term();
  if (term == nullptr) {
    return false;
  }
  return term->get_direction() == idb::IdbConnectDirection::kOutput || term->get_direction() == idb::IdbConnectDirection::kOutputTriState
         || term->get_direction() == idb::IdbConnectDirection::kInOut;
}

auto FindLibCell(idb::IdbInstance* inst) -> ista::LibCell*
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

auto FindLibPort(ista::LibCell* lib_cell, idb::IdbPin* pin) -> ista::LibPort*
{
  if (lib_cell == nullptr || pin == nullptr) {
    return nullptr;
  }
  const auto port_name = TermName(pin);
  if (port_name.empty()) {
    return nullptr;
  }
  return lib_cell->get_cell_port_or_port_bus(port_name.c_str());
}

auto IsSequentialCell(idb::IdbInstance* inst, ista::LibCell* lib_cell) -> bool
{
  if (lib_cell != nullptr && lib_cell->isSequentialCell() && !lib_cell->isICG()) {
    return true;
  }
  return inst != nullptr && inst->is_flip_flop();
}

auto IsClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool
{
  if (pin == nullptr || pin->is_io_pin() || !IsInputLike(pin)) {
    return false;
  }
  auto* term = pin->get_term();
  auto* lib_port = FindLibPort(lib_cell, pin);
  if (lib_port != nullptr && lib_port->isClock()) {
    return true;
  }
  if (pin->is_flip_flop_clk()) {
    return true;
  }
  return term != nullptr && term->get_type() == idb::IdbConnectType::kClock && pin->get_instance() != nullptr
         && pin->get_instance()->is_flip_flop();
}

auto IsMacroClockSinkPin(idb::IdbPin* pin, ista::LibCell* lib_cell) -> bool
{
  if (pin == nullptr || pin->is_io_pin() || !IsInputLike(pin)) {
    return false;
  }
  auto* inst = pin->get_instance();
  auto* cell_master = inst == nullptr ? nullptr : inst->get_cell_master();
  if (cell_master == nullptr || !cell_master->is_block()) {
    return false;
  }
  auto* term = pin->get_term();
  auto* lib_port = FindLibPort(lib_cell, pin);
  return (term != nullptr && term->get_type() == idb::IdbConnectType::kClock) || (lib_port != nullptr && lib_port->isClock());
}

auto CountDirectClockSinks(idb::IdbNet* net) -> ClockSinkStats
{
  ClockSinkStats stats;
  const auto net_pins = CollectNetPins(net);
  for (auto* load_pin : net_pins.loads) {
    auto* inst = load_pin == nullptr ? nullptr : load_pin->get_instance();
    auto* lib_cell = FindLibCell(inst);
    if (IsClockSinkPin(load_pin, lib_cell)) {
      ++stats.sequential_clock_sinks;
    } else if (IsMacroClockSinkPin(load_pin, lib_cell)) {
      ++stats.macro_clock_sinks;
    }
  }
  return stats;
}

auto CountDirectClockSinksForReport(idb::IdbNet* net) -> ClockSinkStats
{
  ClockSinkStats stats;
  auto* pin_list = net == nullptr ? nullptr : net->get_instance_pin_list();
  if (pin_list == nullptr) {
    return stats;
  }
  for (auto* pin : pin_list->get_pin_list()) {
    auto* inst = pin == nullptr ? nullptr : pin->get_instance();
    auto* lib_cell = FindLibCell(inst);
    if (IsClockSinkPin(pin, lib_cell)) {
      ++stats.sequential_clock_sinks;
    } else if (IsMacroClockSinkPin(pin, lib_cell)) {
      ++stats.macro_clock_sinks;
    }
  }
  return stats;
}

auto IsClockTarget(const ClockSinkStats& stats) -> bool
{
  return stats.sequential_clock_sinks > 0U || stats.macro_clock_sinks > 0U;
}

auto TargetKind(const ClockSinkStats& stats) -> std::string
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

auto ClockKindName(const SdcClockDecl& clock) -> std::string
{
  return clock.kind == SdcClockDecl::Kind::kGenerated ? "generated" : "primary";
}

auto MasterClockName(const SdcClockDecl& clock) -> std::string
{
  if (clock.kind == SdcClockDecl::Kind::kGenerated) {
    return clock.master_clock_name.empty() ? "n/a" : clock.master_clock_name;
  }
  return "self";
}

auto DominanceForRecord(const ClockTraceRecord& record, const std::string& clock_kind) -> std::string
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

auto StrongTargetSinkThreshold() -> std::size_t
{
  constexpr std::size_t min_clock_target_sinks = 4U;
  return std::max(min_clock_target_sinks, static_cast<std::size_t>(CONFIG_INST.get_max_fanout()));
}

auto IsStrongClockTarget(const ClockTraceRecord& record, std::size_t sink_threshold) -> bool
{
  return record.macro_clock_sinks > 0U || record.sequential_clock_sinks > sink_threshold;
}

auto FindInstPinByLibPort(idb::IdbInstance* inst, ista::LibPort* lib_port) -> idb::IdbPin*
{
  if (inst == nullptr || lib_port == nullptr || inst->get_pin_list() == nullptr) {
    return nullptr;
  }
  const std::string port_name = lib_port->get_port_name();
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (pin == nullptr) {
      continue;
    }
    if (TermName(pin) == port_name || pin->get_pin_name() == port_name) {
      return pin;
    }
  }
  return nullptr;
}

auto CollectOutputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>
{
  std::vector<idb::IdbPin*> outputs;
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return outputs;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (IsOutputLike(pin)) {
      outputs.push_back(pin);
    }
  }
  return outputs;
}

auto CollectInputPins(idb::IdbInstance* inst) -> std::vector<idb::IdbPin*>
{
  std::vector<idb::IdbPin*> inputs;
  if (inst == nullptr || inst->get_pin_list() == nullptr) {
    return inputs;
  }
  for (auto* pin : inst->get_pin_list()->get_pin_list()) {
    if (IsInputLike(pin)) {
      inputs.push_back(pin);
    }
  }
  return inputs;
}

auto IsCaseConstrained(idb::IdbPin* pin, const CaseConstraintSet& case_constraints) -> bool
{
  if (pin == nullptr) {
    return false;
  }
  const auto full_name = PinFullName(pin);
  if (case_constraints.pin_names.contains(full_name) || case_constraints.pin_names.contains(TermName(pin))) {
    return true;
  }
  auto* net = pin->get_net();
  return net != nullptr && case_constraints.net_names.contains(net->get_net_name());
}

auto OtherInputsCaseConstrained(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, const CaseConstraintSet& case_constraints) -> bool
{
  bool has_other_input = false;
  for (auto* input_pin : CollectInputPins(inst)) {
    if (input_pin == nullptr || input_pin == clock_input_pin) {
      continue;
    }
    has_other_input = true;
    if (!IsCaseConstrained(input_pin, case_constraints)) {
      return false;
    }
  }
  return has_other_input;
}

auto LibertyExpressionUsesPort(RustLibertyExpr* expression, const std::string& port_name) -> bool
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

auto OutputFunctionUsesInput(ista::LibCell* lib_cell, idb::IdbPin* output_pin, idb::IdbPin* input_pin) -> bool
{
  if (lib_cell == nullptr) {
    return true;
  }
  auto* output_port = FindLibPort(lib_cell, output_pin);
  if (output_port == nullptr || output_port->get_func_expr() == nullptr) {
    return true;
  }
  return LibertyExpressionUsesPort(output_port->get_func_expr(), TermName(input_pin));
}

auto NetHasDirectClockSinks(idb::IdbNet* net) -> bool
{
  return net != nullptr && IsClockTarget(CountDirectClockSinks(net));
}

auto CountInputPinsOnNet(idb::IdbInstance* inst, idb::IdbNet* net) -> std::size_t
{
  if (inst == nullptr || net == nullptr) {
    return 0U;
  }
  std::size_t input_pin_count = 0U;
  for (auto* input_pin : CollectInputPins(inst)) {
    if (input_pin != nullptr && input_pin->get_net() == net) {
      ++input_pin_count;
    }
  }
  return input_pin_count;
}

auto CountClockTargetOutputs(const std::vector<idb::IdbPin*>& output_pins) -> std::size_t
{
  std::size_t clock_target_output_count = 0U;
  for (auto* output_pin : output_pins) {
    auto* output_net = output_pin == nullptr ? nullptr : output_pin->get_net();
    if (NetHasDirectClockSinks(output_net)) {
      ++clock_target_output_count;
    }
  }
  return clock_target_output_count;
}

auto LibertyMarksClockInput(idb::IdbPin* input_pin, ista::LibCell* lib_cell) -> bool
{
  if (input_pin == nullptr || lib_cell == nullptr) {
    return false;
  }
  auto* lib_port = FindLibPort(lib_cell, input_pin);
  return lib_port != nullptr && (lib_port->isClock() || lib_port->get_clock_gate_clock_pin());
}

auto OtherInputsAreControlCandidates(idb::IdbInstance* inst, idb::IdbPin* clock_input_pin, ista::LibCell* lib_cell,
                                     const CaseConstraintSet& case_constraints) -> bool
{
  bool has_other_input = false;
  for (auto* input_pin : CollectInputPins(inst)) {
    if (input_pin == nullptr || input_pin == clock_input_pin) {
      continue;
    }
    has_other_input = true;
    if (IsCaseConstrained(input_pin, case_constraints)) {
      continue;
    }
    if (LibertyMarksClockInput(input_pin, lib_cell) || NetHasDirectClockSinks(input_pin->get_net())) {
      return false;
    }
  }
  return has_other_input;
}

auto AddOutputTransition(std::vector<TraceTransition>& transitions, idb::IdbPin* output_pin, const std::string& reason) -> void
{
  if (output_pin == nullptr || output_pin->get_net() == nullptr) {
    return;
  }
  transitions.push_back({
      output_pin->get_net(),
      PinDisplayName(output_pin) + "->" + output_pin->get_net()->get_net_name(),
      reason,
  });
}

auto CollectSafeTransitions(idb::IdbNet* net, const CaseConstraintSet& case_constraints) -> std::vector<TraceTransition>
{
  std::vector<TraceTransition> transitions;
  const auto net_pins = CollectNetPins(net);
  for (auto* load_pin : net_pins.loads) {
    if (load_pin == nullptr || load_pin->is_io_pin() || !IsInputLike(load_pin)) {
      continue;
    }
    auto* inst = load_pin->get_instance();
    auto* lib_cell = FindLibCell(inst);
    if (inst == nullptr || IsSequentialCell(inst, lib_cell)) {
      continue;
    }

    if (lib_cell != nullptr && (lib_cell->isBuffer() || lib_cell->isInverter())) {
      ista::LibPort* input_port = nullptr;
      ista::LibPort* output_port = nullptr;
      lib_cell->bufferPorts(input_port, output_port);
      if (input_port != nullptr && output_port != nullptr) {
        auto* input_pin = FindInstPinByLibPort(inst, input_port);
        auto* output_pin = FindInstPinByLibPort(inst, output_port);
        if (input_pin == load_pin) {
          AddOutputTransition(transitions, output_pin, lib_cell->isBuffer() ? "buffer" : "inverter");
        }
      }
      continue;
    }

    if (lib_cell != nullptr && lib_cell->isICG()) {
      auto* input_port = FindLibPort(lib_cell, load_pin);
      const bool is_clock_gate_clock_pin = input_port != nullptr && (input_port->get_clock_gate_clock_pin() || input_port->isClock());
      auto* term = load_pin->get_term();
      if (is_clock_gate_clock_pin || (term != nullptr && term->get_type() == idb::IdbConnectType::kClock)) {
        for (auto* output_pin : CollectOutputPins(inst)) {
          AddOutputTransition(transitions, output_pin, "clock_gate");
        }
      }
      continue;
    }

    const auto input_pins = CollectInputPins(inst);
    const auto output_pins = CollectOutputPins(inst);
    const bool single_input_output_trace = input_pins.size() == 1U && input_pins.front() == load_pin && output_pins.size() == 1U;
    const bool constrained_gate = OtherInputsCaseConstrained(inst, load_pin, case_constraints);
    const bool single_clock_provenance_input = CountInputPinsOnNet(inst, net) == 1U && load_pin->get_net() == net;
    const auto clock_target_output_count = CountClockTargetOutputs(output_pins);
    const bool constrained_output_count = output_pins.size() == 1U || clock_target_output_count == 1U;
    const bool control_candidate_gate = constrained_output_count && single_clock_provenance_input
                                        && OtherInputsAreControlCandidates(inst, load_pin, lib_cell, case_constraints);
    if (!single_input_output_trace && !constrained_gate && !control_candidate_gate) {
      continue;
    }

    for (auto* output_pin : output_pins) {
      auto* output_net = output_pin == nullptr ? nullptr : output_pin->get_net();
      if (output_net == nullptr) {
        continue;
      }
      const auto output_stats = CountDirectClockSinks(output_net);
      if (!OutputFunctionUsesInput(lib_cell, output_pin, load_pin) && !(control_candidate_gate && IsClockTarget(output_stats))) {
        continue;
      }
      if (IsClockTarget(output_stats)) {
        std::string reason = "single_input_clock_buffer_like";
        if (constrained_gate) {
          reason = "case_constrained_comb_clock_gate";
        } else if (control_candidate_gate) {
          reason = "comb_output_direct_clock_sinks";
        }
        AddOutputTransition(transitions, output_pin, reason);
      } else if (output_net->is_clock() && constrained_gate) {
        AddOutputTransition(transitions, output_pin, "case_constrained_clock_net");
      }
    }
  }
  return transitions;
}

}  // namespace icts::clock_trace
