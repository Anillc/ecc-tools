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
 * @file OptimizationMutation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Committed design mutation helpers for CTS post-synthesis optimization.
 */

#include "optimization/mutation/OptimizationMutation.hh"

#include <glog/logging.h>

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "optimization/model/OptimizationTypes.hh"

namespace icts::optimization_internal {

namespace {

auto FindSingleBufferInputPin(Inst* inst) -> Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }
  const auto* driver_pin = inst->findDriverPin();
  for (auto* pin : inst->get_pins()) {
    if (pin == nullptr || pin == driver_pin) {
      continue;
    }
    if (pin->get_type() == PinType::kIn || pin->get_type() == PinType::kClock) {
      return pin;
    }
  }
  for (auto* pin : inst->get_pins()) {
    if (pin != nullptr && pin != driver_pin) {
      return pin;
    }
  }
  return nullptr;
}

auto CanRenamePin(Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  const auto* inst = pin->get_inst();
  const std::string full_name = inst == nullptr ? local_name : inst->get_name() + "/" + local_name;
  auto* existing_pin = DESIGN_INST.findPin(full_name);
  return existing_pin == nullptr || existing_pin == pin;
}

auto RenamePin(Pin* pin, const std::string& local_name) -> bool
{
  if (pin == nullptr || local_name.empty()) {
    return false;
  }
  if (pin->get_name() == local_name) {
    return true;
  }
  return DESIGN_INST.renamePin(pin, local_name);
}

auto ResolveBufferPorts(const std::string& cell_master) -> std::optional<std::pair<std::string, std::string>>
{
  auto [input_pin_name, output_pin_name] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin_name.empty() || output_pin_name.empty() || input_pin_name == output_pin_name) {
    return std::nullopt;
  }
  return std::make_pair(std::move(input_pin_name), std::move(output_pin_name));
}

auto UpdateClockLayoutInstMaster(ClockLayout& clock_layout, const std::string& inst_name, const std::string& cell_master) -> void
{
  for (auto& layout_clock : clock_layout.get_clocks()) {
    for (auto& layout_inst : layout_clock.insts) {
      if (layout_inst.inst_name == inst_name) {
        layout_inst.cell_master = cell_master;
      }
    }
  }
}

}  // namespace

auto ApplyMutations(const std::vector<OptimizationMutation>& mutations, const std::vector<OptimizableBuffer>& buffers,
                    ClockLayout& clock_layout) -> bool
{
  std::map<std::string, std::string> final_master_by_inst;
  std::map<std::string, std::string> expected_master_by_inst;
  for (const auto& buffer : buffers) {
    if (buffer.inst != nullptr) {
      expected_master_by_inst[buffer.inst_name] = buffer.inst->get_cell_master();
    }
  }
  for (const auto& mutation : mutations) {
    auto expected_iter = expected_master_by_inst.find(mutation.inst_name);
    if (expected_iter == expected_master_by_inst.end()) {
      LOG_ERROR << "Optimization: cannot apply mutation for unresolved inst \"" << mutation.inst_name << "\".";
      return false;
    }
    if (expected_iter->second != mutation.from_master) {
      LOG_ERROR << "Optimization: cannot apply mutation for inst \"" << mutation.inst_name << "\" because current master is \""
                << expected_iter->second << "\" but solver expected \"" << mutation.from_master << "\".";
      return false;
    }
    expected_iter->second = mutation.to_master;
    final_master_by_inst[mutation.inst_name] = mutation.to_master;
  }

  for (const auto& [inst_name, final_master] : final_master_by_inst) {
    auto* inst = DESIGN_INST.findInst(inst_name);
    auto* input_pin = FindSingleBufferInputPin(inst);
    auto* output_pin = inst == nullptr ? nullptr : inst->findDriverPin();
    const auto ports = ResolveBufferPorts(final_master);
    if (inst == nullptr || input_pin == nullptr || output_pin == nullptr || !ports.has_value() || !CanRenamePin(input_pin, ports->first)
        || !CanRenamePin(output_pin, ports->second)) {
      LOG_ERROR << "Optimization: cannot apply final master \"" << final_master << "\" to buffer inst \"" << inst_name
                << "\" because its pin pair cannot be updated.";
      return false;
    }
  }

  for (const auto& [inst_name, final_master] : final_master_by_inst) {
    auto* inst = DESIGN_INST.findInst(inst_name);
    auto* input_pin = FindSingleBufferInputPin(inst);
    auto* output_pin = inst->findDriverPin();
    const auto ports = ResolveBufferPorts(final_master);
    if (!ports.has_value()) {
      return false;
    }
    const std::string old_input_name = input_pin->get_name();
    if (!RenamePin(input_pin, ports->first)) {
      return false;
    }
    if (!RenamePin(output_pin, ports->second)) {
      LOG_FATAL_IF(!RenamePin(input_pin, old_input_name)) << "Optimization: failed to roll back buffer input-pin rename.";
      return false;
    }
    inst->set_cell_master(final_master);
    inst->set_type(InstType::kBuffer);
    input_pin->set_type(PinType::kIn);
    output_pin->set_type(PinType::kOut);
    inst->insertDriverPin(output_pin);
    UpdateClockLayoutInstMaster(clock_layout, inst->get_name(), final_master);
  }
  return true;
}

}  // namespace icts::optimization_internal
