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
 * @file FlowManagerTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief Lightweight interface and sink-group wiring tests for FlowManager.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CTSAPI.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "feature_icts.h"
#include "flow/FlowManager.hh"
#include "flow/netlist/ClockNetManager.hh"

namespace icts_test {
namespace {

class ScopedFlowReset
{
 public:
  ScopedFlowReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
    icts::FlowManager::reset();
  }
  ~ScopedFlowReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
    icts::FlowManager::reset();
  }
};

struct TestClockPins
{
  icts::Clock* clock = nullptr;
  icts::Net* clock_net = nullptr;
  icts::Pin* clock_source = nullptr;
  icts::Pin* macro_sink = nullptr;
  icts::Pin* regular_sink = nullptr;
};

auto findInstByNamePart(const std::vector<icts::Inst*>& insts, const std::string& name_part) -> icts::Inst*
{
  const auto iter = std::ranges::find_if(
      insts, [&name_part](const auto* inst) -> bool { return inst != nullptr && inst->get_name().find(name_part) != std::string::npos; });
  return iter == insts.end() ? nullptr : *iter;
}

auto findNetByNamePart(const std::vector<icts::Net*>& nets, const std::string& name_part) -> icts::Net*
{
  const auto iter = std::ranges::find_if(
      nets, [&name_part](const auto* net) -> bool { return net != nullptr && net->get_name().find(name_part) != std::string::npos; });
  return iter == nets.end() ? nullptr : *iter;
}

auto findInputPin(const icts::Inst* inst) -> icts::Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }
  const auto& pins = inst->get_pins();
  const auto iter
      = std::ranges::find_if(pins, [](const auto* pin) -> bool { return pin != nullptr && pin->get_type() == icts::PinType::kIn; });
  return iter == pins.end() ? nullptr : *iter;
}

auto containsPin(const std::vector<icts::Pin*>& pins, const icts::Pin* pin) -> bool
{
  return std::ranges::find(pins, pin) != pins.end();
}

auto makeDesignInst(const std::string& name, const std::string& cell_master, icts::InstType type, const icts::Point<int>& location)
    -> icts::Inst*
{
  auto* inst = DESIGN_INST.makeInst(name);
  if (inst == nullptr) {
    return nullptr;
  }
  inst->set_name(name);
  inst->set_cell_master(cell_master);
  inst->set_type(type);
  inst->set_location(location);
  return inst;
}

auto addOwnedLoad(icts::Clock& clock, icts::Net* clock_net, icts::Inst& inst, const std::string& pin_name) -> icts::Pin*
{
  auto* pin = DESIGN_INST.makePin(pin_name);
  if (pin == nullptr) {
    return nullptr;
  }
  pin->set_name(pin_name);
  pin->set_type(icts::PinType::kClock);
  pin->set_location(inst.get_location());
  pin->set_inst(&inst);
  pin->set_net(clock_net);
  pin->set_io(false);
  clock.add_load(pin);
  if (clock_net != nullptr) {
    clock_net->add_load(pin);
  }
  inst.add_pin(pin);
  (void) DESIGN_INST.indexPin(pin);
  return pin;
}

auto addClockToDesign(icts::Inst* macro_inst, icts::Inst* regular_inst) -> TestClockPins
{
  auto* clock_ptr = DESIGN_INST.makeClock("clk", "clk_net");
  clock_ptr->set_clock_name("clk");
  clock_ptr->set_clock_net_name("clk_net");
  auto* clock_net = DESIGN_INST.makeNet("clk_net");
  clock_net->set_name("clk_net");
  clock_net->set_loads({});
  auto* source = DESIGN_INST.makePin("clk");
  source->set_name("clk");
  source->set_type(icts::PinType::kOut);
  source->set_location(icts::Point<int>(0, 0));
  source->set_inst(nullptr);
  source->set_net(clock_net);
  source->set_io(false);
  (void) DESIGN_INST.indexPin(source);
  clock_ptr->set_clock_source_net(clock_net);
  clock_ptr->set_clock_source(source);
  clock_net->set_driver(source);

  TestClockPins pins{
      .clock = clock_ptr,
      .clock_net = clock_net,
      .clock_source = source,
      .macro_sink = nullptr,
      .regular_sink = nullptr,
  };
  if (macro_inst != nullptr) {
    pins.macro_sink = addOwnedLoad(*clock_ptr, clock_net, *macro_inst, "CLK");
  }
  if (regular_inst != nullptr) {
    pins.regular_sink = addOwnedLoad(*clock_ptr, clock_net, *regular_inst, "CLK");
  }

  return pins;
}

auto prepareDirectRootBufferNets(icts::Clock& clock, const std::string& cell_master, const std::string& input_pin_name,
                                 const std::string& output_pin_name) -> void
{
  icts::ClockNetManager::restoreClockSourceNetToClockLoads(clock);
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();

  std::vector<icts::Pin*> macro_sinks;
  std::vector<icts::Pin*> regular_sinks;
  icts::ClockNetManager::partitionClockSinks(clock.get_loads(), macro_sinks, regular_sinks);

  std::vector<icts::Pin*> root_inputs;

  auto build_group = [&](const std::string& sink_group, const std::vector<icts::Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    const auto group_prefix = icts::ClockNetManager::makeSinkGroupPrefix(clock, 0U, sink_group);
    icts::Inst* root_buffer = nullptr;
    icts::Pin* root_input = nullptr;
    icts::Pin* root_output = nullptr;
    if (!icts::ClockNetManager::addRootBufferForSinkGroup(clock, group_prefix, cell_master, input_pin_name, output_pin_name, sinks,
                                                          root_buffer, root_input, root_output)) {
      return false;
    }
    if (root_input != nullptr) {
      root_inputs.push_back(root_input);
    }
    return icts::ClockNetManager::connectSinkGroupDownstreamNet(clock, group_prefix, root_output, sinks) != nullptr;
  };

  ASSERT_TRUE(build_group("hard_macro", macro_sinks));
  ASSERT_TRUE(build_group("regular", regular_sinks));
  icts::ClockNetManager::reuseClockSourceNetAsSourceToRootBuffers(clock, clock.get_clock_source(), root_inputs);
}

TEST(FlowManagerTest, EmptyAPIFlowEntryIsCallable)
{
  const ScopedFlowReset scoped_flow_reset;

  icts::CTSAPI::ctsFlow();
}

TEST(FlowManagerTest, MixedMacroAndRegularSingleSinkGroupsUseSeparateDownstreamNets)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* macro_inst = makeDesignInst("macro0", "MACRO_CELL", icts::InstType::kMacroBlock, icts::Point<int>(100, 0));
  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(200, 0));
  auto pins = addClockToDesign(macro_inst, reg_inst);

  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");

  ASSERT_NE(pins.clock, nullptr);
  auto* macro_root = findInstByNamePart(pins.clock->get_insts(), "hard_macro_root_buf");
  auto* regular_root = findInstByNamePart(pins.clock->get_insts(), "regular_root_buf");
  ASSERT_NE(macro_root, nullptr);
  ASSERT_NE(regular_root, nullptr);
  auto* macro_root_input = findInputPin(macro_root);
  auto* regular_root_input = findInputPin(regular_root);
  auto* macro_root_output = macro_root->findDriverPin();
  auto* regular_root_output = regular_root->findDriverPin();
  ASSERT_NE(macro_root_input, nullptr);
  ASSERT_NE(regular_root_input, nullptr);
  ASSERT_NE(macro_root_output, nullptr);
  ASSERT_NE(regular_root_output, nullptr);

  EXPECT_EQ(pins.clock->get_clock_source_net(), pins.clock_net);
  EXPECT_EQ(pins.clock_net->get_driver(), pins.clock_source);
  EXPECT_EQ(pins.clock_source->get_net(), pins.clock_net);
  EXPECT_EQ(pins.clock_net->get_loads().size(), 2U);
  EXPECT_TRUE(containsPin(pins.clock_net->get_loads(), macro_root_input));
  EXPECT_TRUE(containsPin(pins.clock_net->get_loads(), regular_root_input));

  auto* macro_sink_net = pins.macro_sink->get_net();
  auto* regular_sink_net = pins.regular_sink->get_net();
  ASSERT_NE(macro_sink_net, nullptr);
  ASSERT_NE(regular_sink_net, nullptr);
  EXPECT_NE(macro_sink_net, regular_sink_net);
  EXPECT_EQ(macro_sink_net, findNetByNamePart(pins.clock->get_nets(), "hard_macro_downstream_net"));
  EXPECT_EQ(regular_sink_net, findNetByNamePart(pins.clock->get_nets(), "regular_downstream_net"));
  EXPECT_EQ(macro_sink_net->get_driver(), macro_root_output);
  EXPECT_EQ(regular_sink_net->get_driver(), regular_root_output);
  ASSERT_EQ(macro_sink_net->get_loads().size(), 1U);
  ASSERT_EQ(regular_sink_net->get_loads().size(), 1U);
  EXPECT_EQ(macro_sink_net->get_loads().front(), pins.macro_sink);
  EXPECT_EQ(regular_sink_net->get_loads().front(), pins.regular_sink);
}

TEST(FlowManagerTest, RepeatedNetPreparationRestoresClockSourceNetBeforeRebuildingInsertedNets)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);

  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");
  ASSERT_NE(pins.clock, nullptr);
  EXPECT_NE(pins.regular_sink->get_net(), pins.clock_net);
  ASSERT_EQ(pins.clock->get_nets().size(), 1U);
  const auto first_design_inst_count = DESIGN_INST.get_insts().size();
  const auto first_design_pin_count = DESIGN_INST.get_pins().size();
  const auto first_design_net_count = DESIGN_INST.get_nets().size();

  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");
  EXPECT_EQ(DESIGN_INST.get_insts().size(), first_design_inst_count);
  EXPECT_EQ(DESIGN_INST.get_pins().size(), first_design_pin_count);
  EXPECT_EQ(DESIGN_INST.get_nets().size(), first_design_net_count);
  ASSERT_EQ(pins.clock->get_insts().size(), 1U);
  ASSERT_EQ(pins.clock->get_nets().size(), 1U);
  auto* root_buffer = pins.clock->get_insts().front();
  auto* root_input = findInputPin(root_buffer);
  ASSERT_NE(root_input, nullptr);

  ASSERT_NE(pins.clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(pins.clock->get_clock_source_net(), pins.clock_net);
  EXPECT_EQ(pins.clock_net->get_driver(), pins.clock_source);
  ASSERT_EQ(pins.clock_net->get_loads().size(), 1U);
  EXPECT_EQ(pins.clock_net->get_loads().front(), root_input);
  ASSERT_NE(pins.regular_sink->get_net(), nullptr);
  EXPECT_EQ(pins.regular_sink->get_net(), pins.clock->get_nets().front());
  EXPECT_EQ(pins.regular_sink->get_net()->get_loads().front(), pins.regular_sink);
}

TEST(FlowManagerTest, ResetAPIClearsEvaluationSummary)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");

  icts::FlowManager::evaluate();
  EXPECT_EQ(icts::CTSAPI::outputSummary().buffer_num, 1);

  icts::CTSAPI::resetAPI();
  const auto summary = icts::CTSAPI::outputSummary();
  EXPECT_EQ(summary.buffer_num, 0);
  EXPECT_EQ(summary.clock_path_max_buffer, 0);
  EXPECT_TRUE(summary.clocks_timing.empty());
}

}  // namespace
}  // namespace icts_test
