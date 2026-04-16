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
 * @file Router.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "Router.hh"

#include <sstream>
#include <unordered_set>
#include <utility>

#include "CTSContext.hh"
#include "CtsClock.hh"
#include "CtsDesign.hh"
#include "CtsInstance.hh"
#include "CtsNet.hh"
#include "CtsPin.hh"
#include "CtsRuntime.hh"
#include "Solver.hh"
#include "usage/usage.hh"
namespace icts {

namespace {

struct RoutingState
{
  std::vector<Pin*> pins;
  std::vector<Net*> nets;
  std::unordered_map<std::string, Pin*> pin_map;
  std::unordered_map<std::string, Net*> net_map;
  std::vector<CtsClock*> clocks;
};

void addPin(RoutingState& state, Pin* pin)
{
  if (state.pin_map.count(pin->get_name()) == 0) {
    state.pins.push_back(pin);
    state.pin_map.emplace(pin->get_name(), pin);
  }
}

void addNet(RoutingState& state, Net* net)
{
  if (state.net_map.count(net->get_name()) == 0) {
    state.nets.push_back(net);
    state.net_map.emplace(net->get_name(), net);
  }
}

std::vector<CtsPin*> getSinkPins(CtsNet* clk_net)
{
  std::vector<CtsPin*> pins;
  for (auto* load_pin : clk_net->get_load_pins()) {
    auto* load_inst = load_pin->get_instance();
    if (load_inst == nullptr) {
      continue;
    }
    auto inst_type = load_inst->get_type();
    if (inst_type != CtsInstanceType::kBuffer && inst_type != CtsInstanceType::kMux) {
      pins.push_back(load_pin);
    }
  }
  return pins;
}

std::vector<CtsPin*> getBufferPins(CtsNet* clk_net)
{
  std::vector<CtsPin*> pins;
  for (auto* load_pin : clk_net->get_load_pins()) {
    auto* load_inst = load_pin->get_instance();
    if (load_inst == nullptr) {
      continue;
    }
    auto inst_type = load_inst->get_type();
    if (inst_type != CtsInstanceType::kSink) {
      pins.push_back(load_pin);
    }
  }
  return pins;
}

void init(const CTSContext& context, RoutingState& state)
{
  auto log_info = [&](const std::string& msg) {
    LOG_INFO << msg;
    context.solver_runtime.saveToLog(msg);
  };
  auto check_negative_rc = [&](const std::string& label, double value) {
    if (value >= 0.0) {
      return;
    }
    const std::string msg = "Negative RC detected: " + label + " = " + std::to_string(value);
    context.solver_runtime.saveToLog("[FATAL] " + msg);
    LOG_FATAL << msg;
  };

  const double unit_res_h = context.source_runtime->getClockUnitRes(LayerPattern::kH);
  const double unit_cap_h = context.source_runtime->getClockUnitCap(LayerPattern::kH);
  const double unit_res_v = context.source_runtime->getClockUnitRes(LayerPattern::kV);
  const double unit_cap_v = context.source_runtime->getClockUnitCap(LayerPattern::kV);
  check_negative_rc("Unit RES (H)", unit_res_h);
  check_negative_rc("Unit CAP (H)", unit_cap_h);
  check_negative_rc("Unit RES (V)", unit_res_v);
  check_negative_rc("Unit CAP (V)", unit_cap_v);

  log_info("--RC Info--");
  log_info("Unit RES (H): " + std::to_string(unit_res_h) + " ohm");
  log_info("Unit CAP (H): " + std::to_string(unit_cap_h) + " pF");
  log_info("Unit RES (V): " + std::to_string(unit_res_v) + " ohm");
  log_info("Unit CAP (V): " + std::to_string(unit_cap_v) + " pF");

  size_t total_clock_nets = 0;
  auto& clocks = context.design->get_clocks();
  for (auto* clock : clocks) {
    state.clocks.push_back(clock);
    total_clock_nets += clock->get_clock_nets().size();
  }
  std::ostringstream router_summary;
  router_summary << "Router summary => clocks: " << state.clocks.size() << ", clock nets: " << total_clock_nets
                 << ", routing layers(H/V): " << context.config->get_h_layer() << "/" << context.config->get_v_layer();
  log_info(router_summary.str());
}

void routeClockNet(const CTSContext& context, RoutingState& state, CtsNet* clk_net)
{
  auto log_warning = [&](const std::string& msg) {
    LOG_WARNING << msg;
    context.solver_runtime.saveToLog("[WARNING] " + msg);
  };

  auto pins = clk_net->get_load_pins();
  auto* driver_pin = clk_net->get_driver_pin();
  if (pins.empty()) {
    log_warning("Net [" + clk_net->get_net_name() + "] load pin is empty.");
    return;
  }
  if (driver_pin == nullptr) {
    log_warning("Net [" + clk_net->get_net_name() + "] driver pin is empty.");
    return;
  }
  if (pins.size() == 1) {
    log_warning("Net [" + clk_net->get_net_name() + "] has only one load pin.");
  }

  auto clk_nets = Solver::getInst().solve(clk_net->get_net_name(), driver_pin, pins, context.solver_runtime);
  if (clk_nets.empty()) {
    return;
  }
  std::ranges::for_each(clk_nets, [&](Net* net) {
    addNet(state, net);
    std::ranges::for_each(net->get_pins(), [&](Pin* pin) { addPin(state, pin); });
  });
}

void build(const CTSContext& context, RoutingState& state)
{
  auto log_info = [&](const std::string& msg) {
    LOG_INFO << msg;
    context.solver_runtime.saveToLog(msg);
  };
  auto log_warning = [&](const std::string& msg) {
    LOG_WARNING << msg;
    context.solver_runtime.saveToLog("[WARNING] " + msg);
  };

  log_info("--Clock Net Info--");
  for (auto* clock : state.clocks) {
    auto& clock_nets = clock->get_clock_nets();
    for (auto* clk_net : clock_nets) {
      auto sink_pins = getSinkPins(clk_net);
      auto buf_pins = getBufferPins(clk_net);
      auto net_pins = clk_net->get_pins();
      std::unordered_set<std::string> inst_names;
      std::ranges::for_each(net_pins, [&](CtsPin* pin) {
        if (pin == nullptr) {
          return;
        }
        auto* inst = pin->get_instance();
        inst_names.insert(inst == nullptr ? pin->get_full_name() : inst->get_name());
      });
      log_info("Net name: " + clk_net->get_net_name());
      log_info("\tTotal pins num: " + std::to_string(net_pins.size()));
      log_info("\tTotal insts num: " + std::to_string(inst_names.size()));
      log_info("\tSink pins num: " + std::to_string(sink_pins.size()));
      log_info("\tBuffer pins num: " + std::to_string(buf_pins.size()));
      if (net_pins.empty()) {
        log_warning("Net [" + clk_net->get_net_name() + "] has no pins and will be skipped.");
        continue;
      }
      if (net_pins.size() == 1) {
        log_warning("Net [" + clk_net->get_net_name() + "] has only one pin and will be skipped.");
        continue;
      }
      context.design->resetId();
      routeClockNet(context, state, clk_net);
      clk_net->setClockRouted();
    }
  }
}

void update(const CTSContext& context, const RoutingState& state)
{
  DesignCommitter::getInst().commit(state.nets, context.committer_runtime);
}

}  // namespace

Router& Router::getInst()
{
  static Router instance;
  return instance;
}

void Router::run(const CTSContext& context)
{
  RoutingState state;
  init(context, state);
  build(context, state);
  update(context, state);
}

void Router::reset()
{
}

}  // namespace icts
