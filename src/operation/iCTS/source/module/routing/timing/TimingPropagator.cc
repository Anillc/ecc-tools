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
 * @file TimingPropagator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-03-08
 * @brief Wire-only timing propagation helpers for stage-1 routing migration
 */
#include "TimingPropagator.hh"

#include <algorithm>
#include <ranges>

#include "CTSAPI.hh"
#include "database/config/Config.hh"

namespace icts {

bool TimingPropagator::_initialized = false;
double TimingPropagator::_unit_cap = 0;
double TimingPropagator::_unit_res = 0;
double TimingPropagator::_unit_h_cap = 0;
double TimingPropagator::_unit_h_res = 0;
double TimingPropagator::_unit_v_cap = 0;
double TimingPropagator::_unit_v_res = 0;
double TimingPropagator::_skew_bound = 0;
int TimingPropagator::_db_unit = 1;
double TimingPropagator::_max_buf_tran = 0;
double TimingPropagator::_max_sink_tran = 0;
double TimingPropagator::_max_cap = 0;
int TimingPropagator::_max_fanout = 0;
double TimingPropagator::_max_length = 0;

namespace {

int getRoutingLayer(const LayerPattern& pattern)
{
  const auto& routing_layers = CTSConfigInst.get_routing_layers();
  if (routing_layers.empty()) {
    return 1;
  }
  if (pattern == LayerPattern::kV && routing_layers.size() >= 2) {
    return static_cast<int>(routing_layers[1]);
  }
  return static_cast<int>(routing_layers.front());
}

double getWireWidth()
{
  return CTSConfigInst.get_wire_width();
}

}  // namespace

void TimingPropagator::ensureInit()
{
  if (!_initialized) {
    init();
  }
}

void TimingPropagator::init()
{
  auto wire_width = getWireWidth();
  auto single_layer = getRoutingLayer(LayerPattern::kNone);
  auto h_layer = getRoutingLayer(LayerPattern::kH);
  auto v_layer = getRoutingLayer(LayerPattern::kV);

  _unit_cap = CTSAPIInst.queryWireCapacitance(single_layer, 1.0, wire_width);
  _unit_res = CTSAPIInst.queryWireResistance(single_layer, 1.0, wire_width) / 1000.0;
  _unit_h_cap = CTSAPIInst.queryWireCapacitance(h_layer, 1.0, wire_width);
  _unit_h_res = CTSAPIInst.queryWireResistance(h_layer, 1.0, wire_width) / 1000.0;
  _unit_v_cap = CTSAPIInst.queryWireCapacitance(v_layer, 1.0, wire_width);
  _unit_v_res = CTSAPIInst.queryWireResistance(v_layer, 1.0, wire_width) / 1000.0;
  _db_unit = std::max(CTSAPIInst.queryDbUnit(), int32_t{1});

  _skew_bound = CTSConfigInst.get_skew_bound();
  _max_buf_tran = CTSConfigInst.get_max_buf_tran();
  _max_sink_tran = CTSConfigInst.get_max_sink_tran();
  _max_cap = CTSConfigInst.get_max_cap();
  _max_fanout = static_cast<int>(CTSConfigInst.get_max_fanout());
  _max_length = CTSConfigInst.get_max_length();
  _initialized = true;
}

Net* TimingPropagator::genNet(const std::string& net_name, Pin* driver_pin, const std::vector<Pin*>& load_pins)
{
  if (load_pins.empty()) {
    std::vector<Pin*> loads;
    driver_pin->preOrder([&loads](Node* node) {
      if (node->isPin() && node->isLoad()) {
        loads.push_back(dynamic_cast<Pin*>(node));
      }
    });
    return new Net(net_name, driver_pin, loads);
  }
  return new Net(net_name, driver_pin, load_pins);
}

void TimingPropagator::resetNet(Net* net)
{
  if (net == nullptr) {
    return;
  }
  auto* driver_pin = net->get_driver_pin();
  auto load_pins = net->get_load_pins();
  std::vector<Node*> to_be_removed;
  driver_pin->preOrder([&to_be_removed](Node* node) {
    if (node->isSteiner()) {
      to_be_removed.push_back(node);
    }
  });

  std::ranges::for_each(load_pins, [](Pin* pin) {
    pin->set_parent(nullptr);
    pin->set_children({});
    pin->set_sub_len(0);
    pin->set_slew_in(0);
    pin->set_cap_load(0);
    pin->set_required_snake(0);
    pin->set_net(nullptr);
    updatePinCap(pin);
    initLoadPinDelay(pin);
  });

  auto* buffer = driver_pin->get_inst();
  delete buffer;
  std::ranges::for_each(to_be_removed, [](Node* node) { delete node; });
  delete net;
}

void TimingPropagator::updateLoads(Net* net)
{
  auto* driver_pin = net->get_driver_pin();
  std::vector<Pin*> loads;
  driver_pin->preOrder([&loads](Node* node) {
    if (node->isPin() && node->isLoad()) {
      loads.push_back(dynamic_cast<Pin*>(node));
    }
  });
  net->set_load_pins(loads);
}

void TimingPropagator::updatePinCap(Pin* pin)
{
  ensureInit();

  double cap_load = 0;
  if (pin->isBufferPin()) {
    auto cell_name = pin->get_cell_master();
    if (!cell_name.empty()) {
      cap_load = CTSAPIInst.queryCharInputPinCap(cell_name);
    }
  }
  pin->set_cap_load(cap_load);
}

void TimingPropagator::update(Net* net)
{
  if (net == nullptr) {
    return;
  }
  ensureInit();
  netLenPropagate(net);
  capPropagate(net);
  slewPropagate(net);
  wireDelayPropagate(net);
}

void TimingPropagator::netLenPropagate(Net* net)
{
  auto* driver_pin = net->get_driver_pin();
  driver_pin->postOrder(updateNetLen<Node>);
}

void TimingPropagator::capPropagate(Net* net)
{
  std::ranges::for_each(net->get_load_pins(), [](Pin* pin) { updatePinCap(pin); });
  auto* driver_pin = net->get_driver_pin();
  driver_pin->postOrder(updateCapLoad<Node>);
}

void TimingPropagator::slewPropagate(Net* net)
{
  auto* driver_pin = net->get_driver_pin();
  driver_pin->set_slew_in(0);
  driver_pin->preOrder(updateSlewIn<Node>);
}

void TimingPropagator::wireDelayPropagate(Net* net)
{
  auto* driver_pin = net->get_driver_pin();
  driver_pin->postOrder(updateWireDelay<Node>);
}

double TimingPropagator::calcSkew(Node* node)
{
  auto min_delay = node->get_min_delay();
  auto max_delay = node->get_max_delay();
  return max_delay - min_delay;
}

bool TimingPropagator::skewFeasible(Node* node, const std::optional<double>& skew_bound)
{
  auto skew = calcSkew(node);
  auto target_skew = skew_bound.value_or(getSkewBound());
  auto delta = skew - target_skew;
  if (delta > 0 && delta < kEpsilon) {
    node->set_min_delay(node->get_max_delay() - target_skew);
    return true;
  }
  return skew <= target_skew;
}

void TimingPropagator::initLoadPinDelay(Pin* pin, const bool& /*by_cell*/)
{
  LOG_FATAL_IF(!pin->isLoad()) << "The pin: " << pin->get_name() << " is not load pin";
  auto* inst = pin->get_inst();
  if (inst->isSink()) {
    pin->set_min_delay(0);
    pin->set_max_delay(0);
    return;
  }

  auto* driver_pin = inst->get_driver_pin();
  if (driver_pin == nullptr || driver_pin->get_children().empty()) {
    inst->set_insert_delay(0);
    pin->set_min_delay(0);
    pin->set_max_delay(0);
    return;
  }

  inst->set_insert_delay(0);
  pin->set_min_delay(driver_pin->get_min_delay());
  pin->set_max_delay(driver_pin->get_max_delay());
}

double TimingPropagator::calcElmoreDelay(const double& cap, const double& len)
{
  ensureInit();
  return _unit_res * len * (_unit_cap * len / 2 + cap);
}

double TimingPropagator::calcElmoreDelay(const double& cap, const double& x, const double& y, const RCPattern& pattern)
{
  ensureInit();
  double delay = 0;
  switch (pattern) {
    case RCPattern::kSingle:
      delay = calcElmoreDelay(cap, x + y);
      break;
    case RCPattern::kHV:
      delay = _unit_h_res * x * (_unit_h_cap * x / 2 + cap) + _unit_v_res * y * (_unit_v_cap * y / 2 + cap + _unit_h_cap * x);
      break;
    case RCPattern::kVH:
      delay = _unit_v_res * y * (_unit_v_cap * y / 2 + cap) + _unit_h_res * x * (_unit_h_cap * x / 2 + cap + _unit_v_cap * y);
      break;
    default:
      break;
  }
  return delay;
}

double TimingPropagator::getUnitCap(const LayerPattern& pattern)
{
  ensureInit();
  switch (pattern) {
    case LayerPattern::kH:
      return _unit_h_cap;
    case LayerPattern::kV:
      return _unit_v_cap;
    default:
      return _unit_cap;
  }
}

double TimingPropagator::getUnitRes(const LayerPattern& pattern)
{
  ensureInit();
  switch (pattern) {
    case LayerPattern::kH:
      return _unit_h_res;
    case LayerPattern::kV:
      return _unit_v_res;
    default:
      return _unit_res;
  }
}

double TimingPropagator::getSkewBound()
{
  ensureInit();
  return _skew_bound;
}

int TimingPropagator::getDbUnit()
{
  ensureInit();
  return _db_unit;
}

double TimingPropagator::getMaxBufTran()
{
  ensureInit();
  return _max_buf_tran;
}

double TimingPropagator::getMaxSinkTran()
{
  ensureInit();
  return _max_sink_tran;
}

double TimingPropagator::getMaxCap()
{
  ensureInit();
  return _max_cap;
}

int TimingPropagator::getMaxFanout()
{
  ensureInit();
  return _max_fanout;
}

double TimingPropagator::getMaxLength()
{
  ensureInit();
  return _max_length;
}

}  // namespace icts
