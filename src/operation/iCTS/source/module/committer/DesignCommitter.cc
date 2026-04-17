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
 * @file DesignCommitter.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 */
#include "DesignCommitter.hh"

#include <cmath>
#include <ranges>

#include "CtsDBWrapper.hh"
#include "CtsInstance.hh"
#include "CtsNet.hh"
#include "CtsSignalWire.hh"
#include "TimingPropagator.hh"

namespace icts {

DesignCommitter& DesignCommitter::getInst()
{
  static DesignCommitter instance;
  return instance;
}

std::pair<Point, Point> DesignCommitter::buildSnakeGuidePoints(const Point& parent_loc, const Point& current_loc, int64_t require_snake,
                                                               const std::function<bool(const Point&)>& is_in_die)
{
  const auto delta_x = std::abs(current_loc.x() - parent_loc.x());
  auto trunk_x = (parent_loc.x() + current_loc.x() + delta_x + require_snake) / 2;
  auto snake_p1 = Point(trunk_x, parent_loc.y());
  auto snake_p2 = Point(trunk_x, current_loc.y());

  if (is_in_die != nullptr && !(is_in_die(snake_p1) && is_in_die(snake_p2))) {
    trunk_x = (parent_loc.x() + current_loc.x() - delta_x - require_snake) / 2;
    snake_p1 = Point(trunk_x, parent_loc.y());
    snake_p2 = Point(trunk_x, current_loc.y());
  }
  return {snake_p1, snake_p2};
}

void DesignCommitter::commit(const std::vector<Net*>& nets, const RuntimeContext& runtime_context) const
{
  LOG_FATAL_IF(!runtime_context.isValid()) << "DesignCommitter runtime context is not initialized.";
  auto* design = runtime_context._design;
  auto* db_wrapper = runtime_context._db_wrapper;
  std::ranges::for_each(nets, [&](Net* net) {
    std::ranges::for_each(net->get_pins(), [&](Pin* pin) { synthesisPin(pin, *design, *db_wrapper); });
    synthesisNet(net, runtime_context);
  });
  runtime_context._sync_timing();
}

void DesignCommitter::synthesisPin(Pin* pin, CtsDesign& design, CtsDBWrapper& db_wrapper) const
{
  if (design.findPin(pin->get_name()) != nullptr) {
    return;
  }

  auto* buf = pin->get_inst();
  if (buf == nullptr) {
    return;
  }
  auto* cts_buf = new CtsInstance(buf->get_name(), buf->get_cell_master(), CtsInstanceType::kBuffer, buf->get_location());
  db_wrapper.linkIdb(cts_buf);

  auto* driver_pin = buf->get_driver_pin();
  auto* cts_driver_pin = cts_buf->get_out_pin();
  driver_pin->set_name(cts_driver_pin->get_full_name());
  auto* load_pin = buf->get_load_pin();
  auto* cts_load_pin = cts_buf->get_in_pin();
  load_pin->set_name(cts_load_pin->get_full_name());

  design.addInstance(cts_buf);
  design.addPin(cts_driver_pin);
  design.addPin(cts_load_pin);
}

void DesignCommitter::synthesisNet(Net* net, const RuntimeContext& runtime_context) const
{
  auto* design = runtime_context._design;
  auto* db_wrapper = runtime_context._db_wrapper;
  auto* cts_net = design->findNet(net->get_name());
  if (cts_net == nullptr) {
    cts_net = new CtsNet(net->get_name());
    db_wrapper->makeIdbNet(cts_net);
    design->addNet(cts_net);
  }

  std::ranges::for_each(net->get_pins(), [&](Pin* pin) {
    auto* cts_pin = design->findPin(pin->get_name());
    LOG_FATAL_IF(!cts_pin) << "Can't found pin " << pin->get_name() << " in net " << net->get_name();
    db_wrapper->idbDisconnect(cts_pin);
    db_wrapper->idbConnect(cts_pin, cts_net);
  });

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
      auto require_snake = static_cast<int64_t>(std::ceil(require_nake * TimingPropagator::getDbUnit()));
      auto [snake_p1, snake_p2] = buildSnakeGuidePoints(parent_loc, current_loc, require_snake, runtime_context._is_in_die);
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
  runtime_context._register_synthesis_net(net);
}

}  // namespace icts
