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
 * @file ClockNetManager.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS final clock-netlist mutation helper implementation.
 */

#include "netlist/ClockNetManager.hh"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <limits>
#include <memory>
#include <ostream>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Log.hh"
#include "adapter/sta/STAAdapter.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "logger/Schema.hh"
#include "spatial/Point.hh"
#include "usage/usage.hh"

namespace icts {
namespace {

auto makeSafeNameToken(const std::string& value, const std::string& fallback) -> std::string
{
  std::string token;
  token.reserve(value.size());
  for (const auto character : value) {
    const auto uch = static_cast<unsigned char>(character);
    if (std::isalnum(uch) != 0) {
      token.push_back(static_cast<char>(character));
    } else {
      token.push_back('_');
    }
  }
  const auto duplicate_underscores = std::ranges::unique(token, [](char lhs, char rhs) -> bool { return lhs == '_' && rhs == '_'; });
  token.erase(duplicate_underscores.begin(), duplicate_underscores.end());
  while (!token.empty() && token.front() == '_') {
    token.erase(token.begin());
  }
  while (!token.empty() && token.back() == '_') {
    token.pop_back();
  }
  return token.empty() ? fallback : token;
}

auto makeClockPrefix(const Clock& clock, std::size_t clock_index) -> std::string
{
  return "cts_flow_clk_" + std::to_string(clock_index) + "_" + makeSafeNameToken(clock.get_clock_name(), "clock");
}

auto resolveBufferDriveCap(const std::string& cell_master) -> double
{
  double drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapLimit(cell_master);
  if (drive_cap_pf <= 0.0) {
    drive_cap_pf = STA_ADAPTER_INST.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return drive_cap_pf;
}

auto resolveBufferPortsAndDrive(const std::string& cell_master, bool require_output_drive, std::string& input_pin_name,
                                std::string& output_pin_name, double& output_drive_cap_pf) -> bool
{
  input_pin_name.clear();
  output_pin_name.clear();
  output_drive_cap_pf = 0.0;
  if (cell_master.empty()) {
    return false;
  }

  auto [input_pin, output_pin] = STA_ADAPTER_INST.queryBufferPorts(cell_master);
  if (input_pin.empty() || output_pin.empty()) {
    LOG_WARNING << "ClockNetManager: skip buffer master \"" << cell_master << "\" because buffer ports are unresolved.";
    return false;
  }

  if (require_output_drive) {
    output_drive_cap_pf = resolveBufferDriveCap(cell_master);
    if (output_drive_cap_pf <= 0.0) {
      LOG_WARNING << "ClockNetManager: skip buffer master \"" << cell_master << "\" because output drive cap is unresolved.";
      return false;
    }
  }

  input_pin_name = std::move(input_pin);
  output_pin_name = std::move(output_pin);
  return true;
}

auto resolveMinimumDriveRootBuffer(std::string& cell_master, std::string& input_pin_name, std::string& output_pin_name) -> bool
{
  cell_master.clear();
  input_pin_name.clear();
  output_pin_name.clear();

  const auto& buffer_types = CONFIG_INST.get_buffer_types();
  if (buffer_types.empty()) {
    LOG_ERROR << "ClockNetManager: no configured buffer types are available for root-buffer insertion.";
    return false;
  }

  std::string best_cell_master;
  std::string best_input_pin;
  std::string best_output_pin;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& candidate_cell_master : buffer_types) {
    std::string candidate_input_pin;
    std::string candidate_output_pin;
    double candidate_drive_cap_pf = 0.0;
    if (!resolveBufferPortsAndDrive(candidate_cell_master, true, candidate_input_pin, candidate_output_pin, candidate_drive_cap_pf)) {
      continue;
    }

    if (best_cell_master.empty() || candidate_drive_cap_pf < best_drive_cap_pf
        || (candidate_drive_cap_pf == best_drive_cap_pf && candidate_cell_master < best_cell_master)) {
      best_cell_master = candidate_cell_master;
      best_input_pin = std::move(candidate_input_pin);
      best_output_pin = std::move(candidate_output_pin);
      best_drive_cap_pf = candidate_drive_cap_pf;
    }
  }

  if (best_cell_master.empty()) {
    LOG_ERROR << "ClockNetManager: failed to resolve a minimum-drive root buffer from configured buffer types.";
    return false;
  }

  cell_master = std::move(best_cell_master);
  input_pin_name = std::move(best_input_pin);
  output_pin_name = std::move(best_output_pin);
  return true;
}

auto resolveSinkGroupLocation(Pin* clock_source, const std::vector<Pin*>& sinks) -> Point<int>
{
  long long sum_x = 0;
  long long sum_y = 0;
  std::size_t count = 0U;
  for (const auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    const auto location = sink->get_location();
    if (location.get_x() < 0 || location.get_y() < 0) {
      continue;
    }
    sum_x += location.get_x();
    sum_y += location.get_y();
    ++count;
  }

  if (count > 0U) {
    return Point<int>(static_cast<int>(sum_x / static_cast<long long>(count)), static_cast<int>(sum_y / static_cast<long long>(count)));
  }
  if (clock_source != nullptr) {
    return clock_source->get_location();
  }
  return Point<int>(0, 0);
}

auto createInsertedBuffer(Clock& clock, const std::string& inst_name, const std::string& cell_master, const std::string& input_pin_name,
                          const std::string& output_pin_name, const Point<int>& location, Inst*& buffer, Pin*& input_pin, Pin*& output_pin)
    -> bool
{
  buffer = nullptr;
  input_pin = nullptr;
  output_pin = nullptr;
  if (inst_name.empty() || cell_master.empty() || input_pin_name.empty() || output_pin_name.empty()) {
    LOG_ERROR << "ClockNetManager: reject root-buffer insertion because the inst, master, or pin name is empty.";
    return false;
  }
  if (input_pin_name == output_pin_name) {
    LOG_ERROR << "ClockNetManager: reject root-buffer insertion for \"" << inst_name
              << "\" because input and output pin names both resolve to \"" << input_pin_name << "\".";
    return false;
  }
  if (DESIGN_INST.findInst(inst_name) != nullptr) {
    LOG_ERROR << "ClockNetManager: reject root-buffer insertion because inst \"" << inst_name << "\" already exists.";
    return false;
  }

  buffer = DESIGN_INST.makeInst(inst_name);
  if (buffer == nullptr) {
    LOG_ERROR << "ClockNetManager: failed to create root-buffer inst \"" << inst_name << "\".";
    return false;
  }
  buffer->set_name(inst_name);
  buffer->set_cell_master(cell_master);
  buffer->set_type(InstType::kBuffer);
  buffer->set_location(location);
  buffer->set_pins({});

  input_pin = DESIGN_INST.makePin(input_pin_name);
  if (input_pin == nullptr) {
    LOG_ERROR << "ClockNetManager: failed to create root-buffer input pin \"" << input_pin_name << "\".";
    return false;
  }
  input_pin->set_name(input_pin_name);
  input_pin->set_type(PinType::kIn);
  input_pin->set_location(location);
  input_pin->set_inst(buffer);
  input_pin->set_net(nullptr);
  input_pin->set_io(false);
  buffer->add_pin(input_pin);
  if (!DESIGN_INST.indexPin(input_pin)) {
    return false;
  }

  output_pin = DESIGN_INST.makePin(output_pin_name);
  if (output_pin == nullptr) {
    LOG_ERROR << "ClockNetManager: failed to create root-buffer output pin \"" << output_pin_name << "\".";
    return false;
  }
  output_pin->set_name(output_pin_name);
  output_pin->set_type(PinType::kOut);
  output_pin->set_location(location);
  output_pin->set_inst(buffer);
  output_pin->set_net(nullptr);
  output_pin->set_io(false);
  buffer->insertDriverPin(output_pin);
  if (!DESIGN_INST.indexPin(output_pin)) {
    return false;
  }

  clock.add_inst(buffer);
  return true;
}

auto createInsertedNet(Clock& clock, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads) -> Net*
{
  if (net_name.empty()) {
    LOG_ERROR << "ClockNetManager: reject inserted net creation because the net name is empty.";
    return nullptr;
  }
  if (DESIGN_INST.findNet(net_name) != nullptr) {
    LOG_ERROR << "ClockNetManager: reject inserted net creation because net \"" << net_name << "\" already exists.";
    return nullptr;
  }

  auto* net_ptr = DESIGN_INST.makeNet(net_name);
  if (net_ptr == nullptr) {
    LOG_ERROR << "ClockNetManager: failed to create inserted net \"" << net_name << "\".";
    return nullptr;
  }
  ClockNetManager::reconnectNet(*net_ptr, driver, loads);
  clock.add_net(net_ptr);
  return net_ptr;
}

}  // namespace

auto ClockNetManager::readClockData() -> void
{
  const ieda::Stats stats;
  std::string clock_source = "Config::net_list";
  std::vector<std::pair<std::string, std::string>> clock_net_pairs;

  if (CONFIG_INST.is_use_netlist()) {
    const auto& net_list = CONFIG_INST.get_net_list();
    for (const auto& [clock_name, net_name] : net_list) {
      clock_net_pairs.emplace_back(clock_name, net_name);
    }
  } else {
    clock_source = "Wrapper::collectClockNetPairs";
    for (const auto& [clock_name, net_name] : WRAPPER_INST.collectClockNetPairs()) {
      clock_net_pairs.emplace_back(clock_name, net_name);
    }
  }

  WRAPPER_INST.readClocks(clock_net_pairs);
  DESIGN_INST.emitClockDistributionSummary();

  std::unordered_map<std::string, std::size_t> clock_domain_counter;
  for (const auto* clock : DESIGN_INST.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    ++clock_domain_counter[clock->get_clock_name()];
  }

  schema::EmitKeyValueTable("ReadData Summary", {
                                                    {"clock_source", clock_source},
                                                    {"added_clock_nets", std::to_string(DESIGN_INST.get_clocks().size())},
                                                    {"unique_clock_domains", std::to_string(clock_domain_counter.size())},
                                                    {"total_clock_nets", std::to_string(DESIGN_INST.get_clocks().size())},
                                                    {"elapsed_time_s", std::to_string(stats.elapsedRunTime())},
                                                    {"memory_delta_mb", std::to_string(stats.memoryDelta())},
                                                });
}

auto ClockNetManager::partitionClockSinks(const std::vector<Pin*>& sinks, std::vector<Pin*>& macro_sinks, std::vector<Pin*>& regular_sinks)
    -> void
{
  macro_sinks.clear();
  regular_sinks.clear();
  macro_sinks.reserve(sinks.size());
  regular_sinks.reserve(sinks.size());

  for (auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }

    const auto* inst = sink->get_inst();
    if (inst != nullptr && inst->is_macro_block()) {
      macro_sinks.push_back(sink);
    } else {
      regular_sinks.push_back(sink);
    }
  }
}

auto ClockNetManager::makeSinkGroupPrefix(const Clock& clock, std::size_t clock_index, const std::string& sink_group) -> std::string
{
  return makeClockPrefix(clock, clock_index) + "_" + sink_group;
}

auto ClockNetManager::addRootBufferForSinkGroup(Clock& clock, const std::string& group_prefix, const std::vector<Pin*>& sinks,
                                                Inst*& root_buffer, Pin*& root_input, Pin*& root_output) -> bool
{
  std::string cell_master;
  std::string input_pin_name;
  std::string output_pin_name;
  if (!resolveMinimumDriveRootBuffer(cell_master, input_pin_name, output_pin_name)) {
    root_buffer = nullptr;
    root_input = nullptr;
    root_output = nullptr;
    return false;
  }
  return addRootBufferForSinkGroup(clock, group_prefix, cell_master, input_pin_name, output_pin_name, sinks, root_buffer, root_input,
                                   root_output);
}

auto ClockNetManager::addRootBufferForSinkGroup(Clock& clock, const std::string& group_prefix, const std::string& cell_master,
                                                const std::string& input_pin_name, const std::string& output_pin_name,
                                                const std::vector<Pin*>& sinks, Inst*& root_buffer, Pin*& root_input, Pin*& root_output)
    -> bool
{
  return createInsertedBuffer(clock, group_prefix + "_root_buf", cell_master, input_pin_name, output_pin_name,
                              resolveSinkGroupLocation(clock.get_clock_source(), sinks), root_buffer, root_input, root_output);
}

auto ClockNetManager::reconnectNet(Net& net, Pin* driver, const std::vector<Pin*>& loads) -> void
{
  auto* old_driver = net.get_driver();
  if (old_driver != nullptr && old_driver != driver && old_driver->get_net() == &net) {
    old_driver->set_net(nullptr);
  }

  const auto old_loads = net.get_loads();
  for (auto* old_load : old_loads) {
    if (old_load == nullptr || old_load->get_net() != &net) {
      continue;
    }
    if (std::ranges::find(loads, old_load) == loads.end()) {
      old_load->set_net(nullptr);
    }
  }

  net.set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(&net);
  }

  net.set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net.add_load(load);
    load->set_net(&net);
  }
}

auto ClockNetManager::connectSinkGroupDownstreamNet(Clock& clock, const std::string& group_prefix, Pin* root_output,
                                                    const std::vector<Pin*>& sinks) -> Net*
{
  return createInsertedNet(clock, group_prefix + "_downstream_net", root_output, sinks);
}

auto ClockNetManager::restoreClockSourceNetToClockLoads(Clock& clock) -> void
{
  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source_net != nullptr) {
    reconnectNet(*clock_source_net, clock_source, clock.get_loads());
  }
}

auto ClockNetManager::reuseClockSourceNetAsSourceToRootBuffers(Clock& clock, Pin* clock_source, const std::vector<Pin*>& root_buffer_inputs)
    -> Net*
{
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source_net != nullptr) {
    reconnectNet(*clock_source_net, clock_source, root_buffer_inputs);
  }
  return clock_source_net;
}

auto ClockNetManager::commitInsertedObjects(Clock& clock, std::vector<std::unique_ptr<Inst>>& inserted_insts,
                                            std::vector<std::unique_ptr<Pin>>& inserted_pins,
                                            std::vector<std::unique_ptr<Net>>& inserted_nets) -> bool
{
  std::unordered_set<std::string> inst_names;
  for (const auto& inst : inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    if (!inst_names.insert(inst->get_name()).second) {
      LOG_ERROR << "ClockNetManager: reject committing duplicate algorithm inst \"" << inst->get_name() << "\".";
      return false;
    }
    if (DESIGN_INST.findInst(inst->get_name()) != nullptr) {
      LOG_ERROR << "ClockNetManager: reject committing algorithm inst \"" << inst->get_name()
                << "\" because a final inst with the same name already exists.";
      return false;
    }
  }

  std::unordered_set<std::string> pin_full_names;
  for (const auto& pin : inserted_pins) {
    if (pin == nullptr) {
      continue;
    }
    const auto pin_full_name = Design::getPinFullName(pin.get());
    if (pin_full_name.empty()) {
      LOG_ERROR << "ClockNetManager: reject committing algorithm pin because its full name is empty.";
      return false;
    }
    if (!pin_full_names.insert(pin_full_name).second) {
      LOG_ERROR << "ClockNetManager: reject committing duplicate algorithm pin \"" << pin_full_name << "\".";
      return false;
    }
    auto* existing_pin = DESIGN_INST.findPin(pin_full_name);
    if (existing_pin != nullptr && existing_pin != pin.get()) {
      LOG_ERROR << "ClockNetManager: reject committing algorithm pin \"" << pin_full_name
                << "\" because a final pin with the same full name already exists.";
      return false;
    }
  }

  std::unordered_set<std::string> net_names;
  for (const auto& net : inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    if (!net_names.insert(net->get_name()).second) {
      LOG_ERROR << "ClockNetManager: reject committing duplicate algorithm net \"" << net->get_name() << "\".";
      return false;
    }
    if (DESIGN_INST.findNet(net->get_name()) != nullptr) {
      LOG_ERROR << "ClockNetManager: reject committing algorithm net \"" << net->get_name()
                << "\" because a final net with the same name already exists.";
      return false;
    }
  }

  for (auto& inst : inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    auto* committed_inst = DESIGN_INST.commitInst(std::move(inst));
    if (committed_inst == nullptr) {
      LOG_ERROR << "ClockNetManager: failed to commit algorithm inst.";
      return false;
    }
    clock.add_inst(committed_inst);
  }
  inserted_insts.clear();

  for (auto& pin : inserted_pins) {
    if (pin == nullptr) {
      continue;
    }
    if (DESIGN_INST.commitPin(std::move(pin)) == nullptr) {
      LOG_ERROR << "ClockNetManager: failed to commit algorithm pin.";
      return false;
    }
  }
  inserted_pins.clear();

  for (auto& net : inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    auto* committed_net = DESIGN_INST.commitNet(std::move(net));
    if (committed_net == nullptr) {
      LOG_ERROR << "ClockNetManager: failed to commit algorithm net.";
      return false;
    }
    clock.add_net(committed_net);
  }
  inserted_nets.clear();
  return true;
}

}  // namespace icts
