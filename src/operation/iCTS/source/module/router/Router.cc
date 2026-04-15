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

#include "CTSAPI.hh"
#include "CtsDBWrapper.hh"
#include "Solver.hh"
#include "TimingPropagator.hh"
#include "usage/usage.hh"
namespace icts {
void Router::init()
{
  auto log_info = [](const std::string& msg) {
    LOG_INFO << msg;
    CTSAPIInst.saveToLog(msg);
  };
  auto check_negative_rc = [&](const std::string& label, double value) {
    if (value >= 0.0) {
      return;
    }
    const std::string msg = "Negative RC detected: " + label + " = " + std::to_string(value);
    CTSAPIInst.saveToLog("[FATAL] ", msg);
    LOG_FATAL << msg;
  };

  const double unit_res_h = CTSAPIInst.getClockUnitRes(LayerPattern::kH);
  const double unit_cap_h = CTSAPIInst.getClockUnitCap(LayerPattern::kH);
  const double unit_res_v = CTSAPIInst.getClockUnitRes(LayerPattern::kV);
  const double unit_cap_v = CTSAPIInst.getClockUnitCap(LayerPattern::kV);
  check_negative_rc("Unit RES (H)", unit_res_h);
  check_negative_rc("Unit CAP (H)", unit_cap_h);
  check_negative_rc("Unit RES (V)", unit_res_v);
  check_negative_rc("Unit CAP (V)", unit_cap_v);

  log_info("--RC Info--");
  log_info("Unit RES (H): " + std::to_string(unit_res_h) + " ohm");
  log_info("Unit CAP (H): " + std::to_string(unit_cap_h) + " pF");
  log_info("Unit RES (V): " + std::to_string(unit_res_v) + " ohm");
  log_info("Unit CAP (V): " + std::to_string(unit_cap_v) + " pF");

  auto* design = CTSAPIInst.get_design();
  size_t total_clock_nets = 0;
  auto& clocks = design->get_clocks();
  for (auto& clock : clocks) {
    _clocks.push_back(clock);
    total_clock_nets += clock->get_clock_nets().size();
  }
  std::ostringstream router_summary;
  router_summary << "Router summary => clocks: " << _clocks.size() << ", clock nets: " << total_clock_nets
                 << ", routing layers(H/V): " << CTSAPIInst.get_config()->get_h_layer() << "/"
                 << CTSAPIInst.get_config()->get_v_layer();
  log_info(router_summary.str());
}
void Router::build()
{
  auto log_info = [](const std::string& msg) {
    LOG_INFO << msg;
    CTSAPIInst.saveToLog(msg);
  };
  auto log_warning = [](const std::string& msg) {
    LOG_WARNING << msg;
    CTSAPIInst.saveToLog("[WARNING] ", msg);
  };

  log_info("--Clock Net Info--");
  for (auto* clock : _clocks) {
    auto* design = CTSAPIInst.get_design();
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
      } else if (net_pins.size() == 1) {
        log_warning("Net [" + clk_net->get_net_name() + "] has only one pin and will be skipped.");
        continue;
      }
      design->resetId();
      routing(clk_net);
      clk_net->setClockRouted();
    }
  }
}
void Router::update()
{
  // update to cts design, idb and sta
  std::ranges::for_each(_solver_set.get_nets(), [&](Net* net) {
    std::ranges::for_each(net->get_pins(), [&](Pin* pin) { synthesisPin(pin); });
    // if (net->get_name() == "iCLK_50_109") {
    //   LOG_WARNING << "net: " << net->get_name() << " ";
    // }
    synthesisNet(net);
  });
  CTSAPIInst.convertDBToTimingEngine();
}

void Router::routing(CtsNet* clk_net)
{
  auto log_warning = [](const std::string& msg) {
    LOG_WARNING << msg;
    CTSAPIInst.saveToLog("[WARNING] ", msg);
  };

  auto pins = clk_net->get_load_pins();
  auto driver_pin = clk_net->get_driver_pin();
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
  auto net_name = clk_net->get_net_name();
  // if (net_name == "iCLK_50") {
  //   LOG_WARNING << "Net: " << clk_net->get_net_name();
  // }
  // total topology
  auto solver = Solver(net_name, driver_pin, pins);
  solver.run();
  auto clk_nets = solver.get_solver_nets();
  if (clk_nets.empty()) {
    return;
  }
  std::ranges::for_each(clk_nets, [&](Net* net) {
    _solver_set.add_net(net);
    std::ranges::for_each(net->get_pins(), [&](Pin* pin) { _solver_set.add_pin(pin); });
  });
}

std::vector<CtsPin*> Router::getSinkPins(CtsNet* clk_net)
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

std::vector<CtsPin*> Router::getBufferPins(CtsNet* clk_net)
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

void Router::synthesisPin(Pin* pin)
{
  auto* design = CTSAPIInst.get_design();
  if (design->findPin(pin->get_name()) != nullptr) {
    return;
  }

  // It's a new insert buffer pin
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto buf = pin->get_inst();
  if (!buf) {
    // is CLK
    return;
  }
  auto* cts_buf = new CtsInstance(buf->get_name(), buf->get_cell_master(), CtsInstanceType::kBuffer, buf->get_location());
  db_wrapper->linkIdb(cts_buf);

  // update driver pin name
  auto* driver_pin = buf->get_driver_pin();
  auto* cts_driver_pin = cts_buf->get_out_pin();
  driver_pin->set_name(cts_driver_pin->get_full_name());
  // update load pin name
  auto* load_pin = buf->get_load_pin();
  auto* cts_load_pin = cts_buf->get_in_pin();
  load_pin->set_name(cts_load_pin->get_full_name());

  design->addInstance(cts_buf);
  design->addPin(cts_driver_pin);
  design->addPin(cts_load_pin);
}

void Router::synthesisNet(Net* net)
{
  auto* design = CTSAPIInst.get_design();
  auto* db_wrapper = CTSAPIInst.get_db_wrapper();
  auto* cts_net = design->findNet(net->get_name());
  if (!cts_net) {
    cts_net = new CtsNet(net->get_name());
    db_wrapper->makeIdbNet(cts_net);
    design->addNet(cts_net);
  }
  // It's a new insert buffer net
  std::ranges::for_each(net->get_pins(), [&](Pin* pin) {
    auto* cts_pin = design->findPin(pin->get_name());
    LOG_FATAL_IF(!cts_pin) << "Can't found pin " << pin->get_name() << " in net " << net->get_name();
    // if (cts_pin->is_io() && cts_pin->get_pin_type() == CtsPinType::kIn) {
    //   return;
    // }
    db_wrapper->idbDisconnect(cts_pin);
    db_wrapper->idbConnect(cts_pin, cts_net);
  });
  // synthesis wire
  auto* driver_pin = net->get_driver_pin();
  auto id = driver_pin->getMaxId();
  driver_pin->preOrder([&](Node* node) {
    auto* parent = node->get_parent();
    if (parent == nullptr) {
      return;
    }
    auto current_loc = node->get_location();
    auto parent_loc = parent->get_location();
    auto parent_name = parent->isPin() ? dynamic_cast<Pin*>(parent)->get_inst()->get_name() : parent->get_name();
    auto current_name = node->isPin() ? dynamic_cast<Pin*>(node)->get_inst()->get_name() : node->get_name();
    auto require_nake = node->get_required_snake();
    if (require_nake > 0) {
      auto require_snake = std::ceil(require_nake * TimingPropagator::getDbUnit());
      auto delta_x = std::abs(current_loc.x() - parent_loc.x());
      auto trunk_x = (parent_loc.x() + current_loc.x() + delta_x + require_snake) / 2;
      auto snake_p1 = Point(trunk_x, parent_loc.y());
      auto snake_p2 = Point(trunk_x, current_loc.y());
      if (!(CTSAPIInst.isInDie(snake_p1) && CTSAPIInst.isInDie(snake_p2))) {
        // is not in die
        trunk_x = (parent_loc.x() + current_loc.x() - delta_x - require_snake) / 2;
        snake_p1 = Point(trunk_x, parent_loc.y());
        snake_p2 = Point(trunk_x, current_loc.y());
      }
      std::vector<std::string> name_vec = {parent_name, "steiner_" + std::to_string(++id), "steiner_" + std::to_string(++id), current_name};
      std::vector<Point> point_vec = {parent_loc, snake_p1, snake_p2, current_loc};
      for (size_t i = 0; i < name_vec.size() - 1; ++i) {
        cts_net->add_signal_wire(CtsSignalWire(Endpoint{name_vec[i], point_vec[i]}, Endpoint{name_vec[i + 1], point_vec[i + 1]}));
      }
    } else {
      if (Point::isRectilinear(parent_loc, current_loc)) {
        cts_net->add_signal_wire(CtsSignalWire(Endpoint{parent_name, parent_loc}, Endpoint{current_name, current_loc}));
      } else {
        auto trunk_loc = Point(parent_loc.x(), current_loc.y());
        auto trunk_name = "steiner_" + std::to_string(++id);
        cts_net->add_signal_wire(CtsSignalWire(Endpoint{parent_name, parent_loc}, Endpoint{trunk_name, trunk_loc}));
        cts_net->add_signal_wire(CtsSignalWire(Endpoint{trunk_name, trunk_loc}, Endpoint{current_name, current_loc}));
      }
    }
  });
  design->addSolverNet(net);
}

}  // namespace icts
