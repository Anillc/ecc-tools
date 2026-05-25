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
 * @file FlowSdcTraceTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief SDC clock trace and ownership tests for Flow.
 */

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "Config.hh"
#include "Design.hh"
#include "Flow.hh"
#include "FlowDesignFixture.hh"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "Inst.hh"
#include "LibParserRustC.hh"
#include "Schema.hh"
#include "Wrapper.hh"
#include "api/TimingEngine.hh"
#include "common/CTSTestRuntime.hh"
#include "common/logging/LogText.hh"
#include "liberty/Lib.hh"
#include "setup/clock_data/ClockDataRead.hh"
#include "sta/Sta.hh"

namespace icts_test {
namespace {

using namespace flow_test;

auto ReadCurrentRuntimeClockData() -> bool
{
  auto& runtime = icts_test::runtime::CurrentRuntime();
  return icts::ClockDataRead::read(icts::ClockDataReadInput{
      .config = &runtime.config,
      .design = &runtime.design,
      .wrapper = &runtime.wrapper,
      .reporter = &runtime.reporter,
  });
}

class ScopedTestLiberty
{
 public:
  ScopedTestLiberty() : _timing_engine(resetTimingEngine()), _lib(std::make_unique<ista::LibLibrary>("icts_clock_type_test_lib")) {}

  ~ScopedTestLiberty()
  {
    _lib = nullptr;
    ista::TimingEngine::destroyTimingEngine();
  }

  auto addCell(std::unique_ptr<ista::LibCell> cell) -> void { _lib->addLibertyCell(std::move(cell)); }

  auto publish() -> void
  {
    ASSERT_NE(_timing_engine, nullptr);
    _timing_engine->get_ista()->addLib(std::move(_lib));
  }

 private:
  static auto resetTimingEngine() -> ista::TimingEngine*
  {
    ista::TimingEngine::destroyTimingEngine();
    return ista::TimingEngine::getOrCreateTimingEngine();
  }

  ista::TimingEngine* _timing_engine = nullptr;
  std::unique_ptr<ista::LibLibrary> _lib = nullptr;
};

auto MakeLibPort(ista::LibCell& cell, const std::string& port_name, ista::LibPort::LibertyPortType port_type, bool is_clock = false)
    -> ista::LibPort*
{
  auto port = std::make_unique<ista::LibPort>(port_name.c_str());
  auto* port_ptr = port.get();
  port_ptr->set_port_type(port_type);
  port_ptr->set_ower_cell(&cell);
  port_ptr->set_is_clock_pin(is_clock);
  cell.addLibertyPort(std::move(port));
  return port_ptr;
}

auto AddLibArc(ista::LibCell& cell, const std::string& src_port, const std::string& snk_port, const std::string& timing_type) -> void
{
  auto arc = std::make_unique<ista::LibArc>();
  arc->set_src_port(src_port.c_str());
  arc->set_snk_port(snk_port.c_str());
  arc->set_owner_cell(&cell);
  arc->set_timing_type(timing_type.c_str());
  cell.addLibertyArc(std::move(arc));
}

auto MakeFlipFlopLibCell(const std::string& cell_name, const std::string& clock_pin_name) -> std::unique_ptr<ista::LibCell>
{
  auto cell = std::make_unique<ista::LibCell>(cell_name.c_str(), nullptr);
  MakeLibPort(*cell, "D", ista::LibPort::LibertyPortType::kInput);
  MakeLibPort(*cell, clock_pin_name, ista::LibPort::LibertyPortType::kInput, true);
  MakeLibPort(*cell, "Q", ista::LibPort::LibertyPortType::kOutput);
  AddLibArc(*cell, clock_pin_name, "Q", "rising_edge");
  AddLibArc(*cell, clock_pin_name, "D", "setup_rising");
  AddLibArc(*cell, clock_pin_name, "D", "hold_rising");
  return cell;
}

auto MakeLatchLibCell(const std::string& cell_name, const std::string& clock_pin_name) -> std::unique_ptr<ista::LibCell>
{
  auto cell = std::make_unique<ista::LibCell>(cell_name.c_str(), nullptr);
  MakeLibPort(*cell, "D", ista::LibPort::LibertyPortType::kInput);
  MakeLibPort(*cell, clock_pin_name, ista::LibPort::LibertyPortType::kInput, true);
  MakeLibPort(*cell, "Q", ista::LibPort::LibertyPortType::kOutput);
  AddLibArc(*cell, "D", "Q", "combinational");
  AddLibArc(*cell, clock_pin_name, "Q", "falling_edge");
  AddLibArc(*cell, clock_pin_name, "D", "setup_rising");
  AddLibArc(*cell, clock_pin_name, "D", "hold_rising");
  return cell;
}

auto MakeClockLogicLibCell(const std::string& cell_name, const std::string& clock_pin_name) -> std::unique_ptr<ista::LibCell>
{
  auto cell = std::make_unique<ista::LibCell>(cell_name.c_str(), nullptr);
  MakeLibPort(*cell, clock_pin_name, ista::LibPort::LibertyPortType::kInput);
  MakeLibPort(*cell, "EN", ista::LibPort::LibertyPortType::kInput);
  auto* out_port = MakeLibPort(*cell, "Y", ista::LibPort::LibertyPortType::kOutput);
  RustLibertyExprBuilder expr_builder(clock_pin_name.c_str());
  expr_builder.execute();
  out_port->set_func_expr(expr_builder.get_result_expr());
  out_port->set_func_expr_str(clock_pin_name.c_str());
  AddLibArc(*cell, clock_pin_name, "Y", "combinational");
  AddLibArc(*cell, "EN", "Y", "combinational");
  return cell;
}

TEST(FlowTest, SdcClockResolutionUsesSdcReachableTargets)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  ASSERT_NE(idb_design.get_net_list(), nullptr);
  ASSERT_NE(idb_design.get_net_list()->add_net("idb_only_clk", idb::IdbConnectType::kClock), nullptr);
  auto* physical_clk_net = idb_design.get_net_list()->add_net("physical_clk_net", idb::IdbConnectType::kSignal);
  ASSERT_NE(physical_clk_net, nullptr);

  auto* src_master = AddIdbCellMaster(idb_layout, "SRC_CELL");
  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(src_master, nullptr);
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*src_master, "CLKOUT", idb::IdbConnectDirection::kOutput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* src_inst = AddIdbInst(idb_design, "src0", *src_master, 0, 0);
  auto* sink_inst = AddIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(src_inst, nullptr);
  ASSERT_NE(sink_inst, nullptr);
  auto* src_pin = src_inst->get_pin_by_term("CLKOUT");
  auto* sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(sink_pin, nullptr);
  AttachIdbPinToNet(*physical_clk_net, *src_pin);
  AttachIdbPinToNet(*physical_clk_net, *sink_pin);

  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  const auto empty_sdc_path = WriteTempSdc("icts_empty_clock_resolution.sdc", "");
  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  EXPECT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 0U);

  const auto direct_sdc_path
      = WriteTempSdc("icts_direct_clock_resolution.sdc", "create_clock -name DIRECT_CLK -period 3 physical_clk_net\n");
  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 1U);
  auto* direct_clock = icts_test::runtime::CurrentRuntime().design.get_clocks().front();
  ASSERT_NE(direct_clock, nullptr);
  EXPECT_EQ(direct_clock->get_clock_name(), "DIRECT_CLK");
  EXPECT_EQ(direct_clock->get_clock_net_name(), "physical_clk_net");
  EXPECT_DOUBLE_EQ(direct_clock->get_clock_period_ns(), 3.0);
  EXPECT_EQ(direct_clock->get_clock_period_source(), "sdc");

  const auto partial_unresolved_sdc_path = WriteTempSdc("icts_partial_missing_clock_resolution.sdc", R"(
create_clock -name DIRECT_CLK -period 3 physical_clk_net
create_clock -name MISSING_CLK -period 2 missing_physical_net
)");
  EXPECT_FALSE(ReadCurrentRuntimeClockData());
  EXPECT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 0U);

  const auto unresolved_sdc_path
      = WriteTempSdc("icts_missing_clock_resolution.sdc", "create_clock -name MISSING_CLK -period 2 missing_physical_net\n");
  EXPECT_FALSE(ReadCurrentRuntimeClockData());
  EXPECT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 0U);

  std::error_code error_code;
  std::filesystem::remove(empty_sdc_path, error_code);
  std::filesystem::remove(direct_sdc_path, error_code);
  std::filesystem::remove(partial_unresolved_sdc_path, error_code);
  std::filesystem::remove(unresolved_sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceResolvesVariableGetPortsToDownstreamClockTarget)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* root_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* leaf_net = idb_design.get_net_list()->add_net("buf_clock_net", idb::IdbConnectType::kSignal);
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(leaf_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*root_net, *io_pin);

  auto* buf_master = AddIdbCellMaster(idb_layout, "CLKBUF_CELL");
  ASSERT_NE(buf_master, nullptr);
  ASSERT_NE(AddIdbTerm(*buf_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*buf_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* buf_inst = AddIdbInst(idb_design, "clkbuf0", *buf_master, 50, 0);
  ASSERT_NE(buf_inst, nullptr);
  auto* buf_in = buf_inst->get_pin_by_term("A");
  auto* buf_out = buf_inst->get_pin_by_term("Y");
  ASSERT_NE(buf_in, nullptr);
  ASSERT_NE(buf_out, nullptr);
  AttachIdbPinToNet(*root_net, *buf_in);
  AttachIdbPinToNet(*leaf_net, *buf_out);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* sink_inst = AddIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(sink_inst, nullptr);
  auto* sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(sink_pin, nullptr);
  AttachIdbPinToNet(*leaf_net, *sink_pin);

  const auto sdc_path = WriteTempSdc("icts_variable_get_ports_clock_trace.sdc", R"(
set clk_name TRACE_CLK
set clk_period 1.5
set clk_port_name clock
set clk_port [get_ports $clk_port_name]
create_clock -name $clk_name -period [expr $clk_period * 2] $clk_port
)");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 1U);
  auto* clock = icts_test::runtime::CurrentRuntime().design.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "TRACE_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "buf_clock_net");
  EXPECT_DOUBLE_EQ(clock->get_clock_period_ns(), 3.0);
  EXPECT_EQ(clock->get_clock_period_source(), "sdc");

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceAllowsClockGateLikeCombTarget)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* root_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* enable_net = idb_design.get_net_list()->add_net("enable_ctrl", idb::IdbConnectType::kSignal);
  auto* gated_net = idb_design.get_net_list()->add_net("gated_clock", idb::IdbConnectType::kSignal);
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(enable_net, nullptr);
  ASSERT_NE(gated_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*root_net, *io_pin);

  auto* gate_master = AddIdbCellMaster(idb_layout, "CLK_GATE_LIKE_CELL");
  ASSERT_NE(gate_master, nullptr);
  ASSERT_NE(AddIdbTerm(*gate_master, "CLK", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*gate_master, "EN", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*gate_master, "Z", idb::IdbConnectDirection::kOutput), nullptr);
  auto* gate_inst = AddIdbInst(idb_design, "gate0", *gate_master, 50, 0);
  ASSERT_NE(gate_inst, nullptr);
  auto* gate_clk = gate_inst->get_pin_by_term("CLK");
  auto* gate_enable = gate_inst->get_pin_by_term("EN");
  auto* gate_out = gate_inst->get_pin_by_term("Z");
  ASSERT_NE(gate_clk, nullptr);
  ASSERT_NE(gate_enable, nullptr);
  ASSERT_NE(gate_out, nullptr);
  AttachIdbPinToNet(*root_net, *gate_clk);
  AttachIdbPinToNet(*enable_net, *gate_enable);
  AttachIdbPinToNet(*gated_net, *gate_out);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  for (int index = 0; index < 4; ++index) {
    auto* source_side_sink_inst = AddIdbInst(idb_design, "source_side_sink" + std::to_string(index), *reg_master, 100 + index, 0);
    ASSERT_NE(source_side_sink_inst, nullptr);
    auto* source_side_sink_pin = source_side_sink_inst->get_pin_by_term("CLK");
    ASSERT_NE(source_side_sink_pin, nullptr);
    AttachIdbPinToNet(*root_net, *source_side_sink_pin);
  }
  for (int index = 0; index < 5; ++index) {
    auto* sink_inst = AddIdbInst(idb_design, "sink" + std::to_string(index), *reg_master, 200 + index, 0);
    ASSERT_NE(sink_inst, nullptr);
    auto* sink_pin = sink_inst->get_pin_by_term("CLK");
    ASSERT_NE(sink_pin, nullptr);
    AttachIdbPinToNet(*gated_net, *sink_pin);
  }

  icts_test::runtime::CurrentRuntime().config.set_max_fanout(4);
  const auto sdc_path = WriteTempSdc("icts_comb_gate_clock_trace.sdc", "create_clock -name GATED_CLK [get_ports clock] -period 4\n");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 1U);
  auto* clock = icts_test::runtime::CurrentRuntime().design.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "GATED_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "gated_clock");
  EXPECT_DOUBLE_EQ(clock->get_clock_period_ns(), 4.0);

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, ClockReadClassifiesCombinationalClockLoadAsBoundaryLoad)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* clock_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kClock);
  auto* data_net = idb_design.get_net_list()->add_net("data_out", idb::IdbConnectType::kSignal);
  ASSERT_NE(clock_net, nullptr);
  ASSERT_NE(data_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *io_pin);

  auto* logic_master = AddIdbCellMaster(idb_layout, "CLOCK_LOAD_LOGIC");
  ASSERT_NE(logic_master, nullptr);
  ASSERT_NE(AddIdbTerm(*logic_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*logic_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* logic_inst = AddIdbInst(idb_design, "logic_load", *logic_master, 50, 0);
  ASSERT_NE(logic_inst, nullptr);
  auto* logic_input = logic_inst->get_pin_by_term("A");
  auto* logic_output = logic_inst->get_pin_by_term("Y");
  ASSERT_NE(logic_input, nullptr);
  ASSERT_NE(logic_output, nullptr);
  AttachIdbPinToNet(*clock_net, *logic_input);
  AttachIdbPinToNet(*data_net, *logic_output);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *clock_net, "sink0", 100), nullptr);

  const auto sdc_path = WriteTempSdc("icts_clock_load_boundary_classification.sdc", "create_clock -name CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  auto* cts_inst = icts_test::runtime::CurrentRuntime().design.findInst("logic_load");
  ASSERT_NE(cts_inst, nullptr);
  EXPECT_EQ(cts_inst->get_type(), icts::InstType::kBoundaryLoad);
  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().design.rebuildClockDAG());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, ClockReadClassifiesLibertyFlipFlopAndLatchSinks)
{
  ScopedFlowReset scoped_flow_reset;
  ScopedTestLiberty test_liberty;
  test_liberty.addCell(MakeFlipFlopLibCell("TEST_DFF", "CK"));
  test_liberty.addCell(MakeLatchLibCell("TEST_LATCH", "GN"));
  test_liberty.publish();

  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* clock_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kClock);
  ASSERT_NE(clock_net, nullptr);
  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *io_pin);

  auto* ff_master = AddIdbCellMaster(idb_layout, "TEST_DFF");
  ASSERT_NE(ff_master, nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "CK", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "D", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "Q", idb::IdbConnectDirection::kOutput), nullptr);
  auto* ff_inst = AddIdbInst(idb_design, "ff_sink", *ff_master, 50, 0);
  ASSERT_NE(ff_inst, nullptr);
  auto* ff_clock_pin = ff_inst->get_pin_by_term("CK");
  ASSERT_NE(ff_clock_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *ff_clock_pin);

  auto* latch_master = AddIdbCellMaster(idb_layout, "TEST_LATCH");
  ASSERT_NE(latch_master, nullptr);
  ASSERT_NE(AddIdbTerm(*latch_master, "GN", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*latch_master, "D", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*latch_master, "Q", idb::IdbConnectDirection::kOutput), nullptr);
  auto* latch_inst = AddIdbInst(idb_design, "latch_sink", *latch_master, 100, 0);
  ASSERT_NE(latch_inst, nullptr);
  auto* latch_clock_pin = latch_inst->get_pin_by_term("GN");
  ASSERT_NE(latch_clock_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *latch_clock_pin);

  const auto sdc_path = WriteTempSdc("icts_sequential_type_classification.sdc", "create_clock -name CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  auto* cts_ff = icts_test::runtime::CurrentRuntime().design.findInst("ff_sink");
  auto* cts_latch = icts_test::runtime::CurrentRuntime().design.findInst("latch_sink");
  ASSERT_NE(cts_ff, nullptr);
  ASSERT_NE(cts_latch, nullptr);
  EXPECT_EQ(cts_ff->get_type(), icts::InstType::kFlipFlop);
  EXPECT_EQ(cts_latch->get_type(), icts::InstType::kLatch);
  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().design.rebuildClockDAG());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, ClockReadClassifiesClockDependentLogicAsClockLogicBoundary)
{
  ScopedFlowReset scoped_flow_reset;
  ScopedTestLiberty test_liberty;
  test_liberty.addCell(MakeClockLogicLibCell("TEST_CLOCK_AND", "CLK"));
  test_liberty.addCell(MakeFlipFlopLibCell("TEST_DFF", "CK"));
  test_liberty.publish();

  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* clock_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kClock);
  auto* enable_net = idb_design.get_net_list()->add_net("enable", idb::IdbConnectType::kSignal);
  auto* gated_net = idb_design.get_net_list()->add_net("gated_clock", idb::IdbConnectType::kClock);
  ASSERT_NE(clock_net, nullptr);
  ASSERT_NE(enable_net, nullptr);
  ASSERT_NE(gated_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *io_pin);

  auto* logic_master = AddIdbCellMaster(idb_layout, "TEST_CLOCK_AND");
  ASSERT_NE(logic_master, nullptr);
  ASSERT_NE(AddIdbTerm(*logic_master, "CLK", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*logic_master, "EN", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*logic_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* logic_inst = AddIdbInst(idb_design, "clock_logic", *logic_master, 50, 0);
  ASSERT_NE(logic_inst, nullptr);
  auto* logic_clock = logic_inst->get_pin_by_term("CLK");
  auto* logic_enable = logic_inst->get_pin_by_term("EN");
  auto* logic_output = logic_inst->get_pin_by_term("Y");
  ASSERT_NE(logic_clock, nullptr);
  ASSERT_NE(logic_enable, nullptr);
  ASSERT_NE(logic_output, nullptr);
  AttachIdbPinToNet(*clock_net, *logic_clock);
  AttachIdbPinToNet(*enable_net, *logic_enable);
  AttachIdbPinToNet(*gated_net, *logic_output);

  auto* ff_master = AddIdbCellMaster(idb_layout, "TEST_DFF");
  ASSERT_NE(ff_master, nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "CK", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "D", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*ff_master, "Q", idb::IdbConnectDirection::kOutput), nullptr);
  auto* sink_inst = AddIdbInst(idb_design, "downstream_ff", *ff_master, 100, 0);
  ASSERT_NE(sink_inst, nullptr);
  auto* sink_clock = sink_inst->get_pin_by_term("CK");
  ASSERT_NE(sink_clock, nullptr);
  AttachIdbPinToNet(*gated_net, *sink_clock);

  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().wrapper.readClocks(icts_test::runtime::CurrentRuntime().design,
                                                                      icts_test::runtime::CurrentRuntime().reporter, {{"CLK", "clock"}}));
  auto* cts_logic = icts_test::runtime::CurrentRuntime().design.findInst("clock_logic");
  ASSERT_NE(cts_logic, nullptr);
  EXPECT_EQ(cts_logic->get_type(), icts::InstType::kClockLogic);
  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().design.rebuildClockDAG());
}

TEST(FlowTest, SdcClockTraceMaterializesAllTargetsAndSkipsVirtualClock)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* root_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* branch_a_net = idb_design.get_net_list()->add_net("branch_a_clock", idb::IdbConnectType::kSignal);
  auto* branch_b_net = idb_design.get_net_list()->add_net("branch_b_clock", idb::IdbConnectType::kSignal);
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(branch_a_net, nullptr);
  ASSERT_NE(branch_b_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = AddIdbCellMaster(idb_layout, "CLK_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(AddIdbPassCell(idb_design, *pass_master, *root_net, *branch_a_net, "pass_a", 10), nullptr);
  ASSERT_NE(AddIdbPassCell(idb_design, *pass_master, *root_net, *branch_b_net, "pass_b", 20), nullptr);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *branch_a_net, "sink_a", 100), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *branch_b_net, "sink_b", 200), nullptr);

  const auto sdc_path = WriteTempSdc("icts_multi_target_virtual_clock_trace.sdc", R"(
create_clock -name TRACE_CLK [get_ports clock] -period 2
create_clock -name VIRTUAL_ONLY -period 5
)");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 2U);

  std::vector<std::string> materialized_nets;
  for (auto* clock : icts_test::runtime::CurrentRuntime().design.get_clocks()) {
    ASSERT_NE(clock, nullptr);
    EXPECT_EQ(clock->get_clock_name(), "TRACE_CLK");
    EXPECT_DOUBLE_EQ(clock->get_clock_period_ns(), 2.0);
    materialized_nets.push_back(clock->get_clock_net_name());
  }
  std::ranges::sort(materialized_nets);
  EXPECT_EQ(materialized_nets, (std::vector<std::string>{"branch_a_clock", "branch_b_clock"}));

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcGeneratedClockBoundaryKeepsMasterAndGeneratedOwnershipSeparate)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* root_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* generated_net = idb_design.get_net_list()->add_net("gen_net", idb::IdbConnectType::kSignal);
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(generated_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = AddIdbCellMaster(idb_layout, "GEN_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(AddIdbPassCell(idb_design, *pass_master, *root_net, *generated_net, "gen_pass", 50), nullptr);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *root_net, "master_sink", 100), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *generated_net, "generated_sink", 200), nullptr);

  const auto sdc_path = WriteTempSdc("icts_generated_boundary_clock_trace.sdc", R"(
create_clock -name MASTER [get_ports clock] -period 2
create_generated_clock -name GEN -master_clock MASTER -divide_by 2 -source [get_ports clock] gen_net
)");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 2U);

  std::map<std::string, std::string> net_by_clock;
  std::map<std::string, double> period_by_clock;
  for (auto* clock : icts_test::runtime::CurrentRuntime().design.get_clocks()) {
    ASSERT_NE(clock, nullptr);
    net_by_clock[clock->get_clock_name()] = clock->get_clock_net_name();
    period_by_clock[clock->get_clock_name()] = clock->get_clock_period_ns();
  }
  EXPECT_EQ(net_by_clock["MASTER"], "clock");
  EXPECT_EQ(net_by_clock["GEN"], "gen_net");
  EXPECT_DOUBLE_EQ(period_by_clock["MASTER"], 2.0);
  EXPECT_DOUBLE_EQ(period_by_clock["GEN"], 4.0);

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceReportsUnownedClockLikeNetsWithoutMaterializingThem)
{
  ScopedFlowReset scoped_flow_reset;
  const auto cts_log_path = icts_test::runtime::CurrentRuntime().reporter.getActivePath();
  ASSERT_FALSE(cts_log_path.empty());

  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* clock_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* noc_clock_net = idb_design.get_net_list()->add_net("noc_clock", idb::IdbConnectType::kSignal);
  ASSERT_NE(clock_net, nullptr);
  ASSERT_NE(noc_clock_net, nullptr);

  auto* clock_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* noc_clock_pin = AddIdbIoPin(idb_design, "noc_clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_pin, nullptr);
  ASSERT_NE(noc_clock_pin, nullptr);
  AttachIdbPinToNet(*clock_net, *clock_pin);
  AttachIdbPinToNet(*noc_clock_net, *noc_clock_pin);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *clock_net, "core_sink", 100), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *noc_clock_net, "noc_sink", 200), nullptr);

  const auto sdc_path = WriteTempSdc("icts_unowned_clock_like_report.sdc", "create_clock -name CORE_CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 1U);
  auto* clock = icts_test::runtime::CurrentRuntime().design.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "CORE_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "clock");

  const auto cts_log_content = ReadTextFile(cts_log_path);
  const auto ownership_summary = common::logging::ExtractTextBlock(cts_log_content, "SDC Clock Ownership Overview");
  ASSERT_FALSE(ownership_summary.empty());
  EXPECT_NE(ownership_summary.find("CORE_CLK"), std::string::npos);
  EXPECT_NE(ownership_summary.find("primary"), std::string::npos);
  EXPECT_NE(ownership_summary.find("self"), std::string::npos);
  EXPECT_NE(ownership_summary.find("clock"), std::string::npos);

  const auto unowned_summary = common::logging::ExtractTextBlock(cts_log_content, "Unowned Clock-like Nets");
  ASSERT_FALSE(unowned_summary.empty());
  EXPECT_TRUE(std::regex_search(unowned_summary, std::regex(R"(\|\s*noc_clock\s*\|\s*unowned_clock_like_net\s*\|)")));
  EXPECT_NE(unowned_summary.find("no_sdc_clock_ownership"), std::string::npos);
  EXPECT_EQ(unowned_summary.find("CORE_CLK"), std::string::npos);

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceRejectsAmbiguousSharedTargetNet)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* shared_net = idb_design.get_net_list()->add_net("shared_clock", idb::IdbConnectType::kSignal);
  ASSERT_NE(shared_net, nullptr);
  auto* clock_a_pin = AddIdbIoPin(idb_design, "clock_a", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* clock_b_pin = AddIdbIoPin(idb_design, "clock_b", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_a_pin, nullptr);
  ASSERT_NE(clock_b_pin, nullptr);
  AttachIdbPinToNet(*shared_net, *clock_a_pin);
  AttachIdbPinToNet(*shared_net, *clock_b_pin);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *shared_net, "shared_sink", 100), nullptr);

  const auto sdc_path = WriteTempSdc("icts_ambiguous_shared_clock_trace.sdc", R"(
create_clock -name CLK_A [get_ports clock_a] -period 2
create_clock -name CLK_B [get_ports clock_b] -period 3
)");

  EXPECT_FALSE(ReadCurrentRuntimeClockData());
  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().design.get_clocks().empty());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceStopsAtClockLikeMuxWithoutCaseAnalysis)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* clock_a_net = idb_design.get_net_list()->add_net("clock_a", idb::IdbConnectType::kSignal);
  auto* clock_b_net = idb_design.get_net_list()->add_net("clock_b", idb::IdbConnectType::kSignal);
  auto* mux_out_net = idb_design.get_net_list()->add_net("mux_out", idb::IdbConnectType::kSignal);
  ASSERT_NE(clock_a_net, nullptr);
  ASSERT_NE(clock_b_net, nullptr);
  ASSERT_NE(mux_out_net, nullptr);

  auto* clock_a_pin = AddIdbIoPin(idb_design, "clock_a", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* clock_b_pin = AddIdbIoPin(idb_design, "clock_b", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_a_pin, nullptr);
  ASSERT_NE(clock_b_pin, nullptr);
  AttachIdbPinToNet(*clock_a_net, *clock_a_pin);
  AttachIdbPinToNet(*clock_b_net, *clock_b_pin);

  auto* mux_master = AddIdbCellMaster(idb_layout, "CLOCK_MUX_CELL");
  ASSERT_NE(mux_master, nullptr);
  ASSERT_NE(AddIdbTerm(*mux_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*mux_master, "B", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*mux_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* mux_inst = AddIdbInst(idb_design, "mux0", *mux_master, 50, 0);
  ASSERT_NE(mux_inst, nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("A"), nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("B"), nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("Y"), nullptr);
  AttachIdbPinToNet(*clock_a_net, *mux_inst->get_pin_by_term("A"));
  AttachIdbPinToNet(*clock_b_net, *mux_inst->get_pin_by_term("B"));
  AttachIdbPinToNet(*mux_out_net, *mux_inst->get_pin_by_term("Y"));

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *clock_b_net, "clock_b_sink", 100), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *mux_out_net, "mux_sink", 200), nullptr);

  const auto sdc_path = WriteTempSdc("icts_ambiguous_mux_clock_trace.sdc", "create_clock -name CLK_A [get_ports clock_a] -period 2\n");

  EXPECT_FALSE(ReadCurrentRuntimeClockData());
  EXPECT_TRUE(icts_test::runtime::CurrentRuntime().design.get_clocks().empty());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceTerminatesOnCombinationalCycle)
{
  ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_design(&idb_design);
  icts_test::runtime::CurrentRuntime().wrapper.set_idb_layout(&idb_layout);

  auto* root_net = idb_design.get_net_list()->add_net("clock", idb::IdbConnectType::kSignal);
  auto* loop_net = idb_design.get_net_list()->add_net("loop_clock", idb::IdbConnectType::kSignal);
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(loop_net, nullptr);

  auto* io_pin = AddIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  AttachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = AddIdbCellMaster(idb_layout, "LOOP_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(AddIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(AddIdbPassCell(idb_design, *pass_master, *root_net, *loop_net, "loop_pass0", 50), nullptr);
  ASSERT_NE(AddIdbPassCell(idb_design, *pass_master, *loop_net, *root_net, "loop_pass1", 60), nullptr);

  auto* reg_master = AddIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(AddIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *root_net, "root_sink", 100), nullptr);
  ASSERT_NE(AddIdbClockSink(idb_design, *reg_master, *loop_net, "loop_sink", 200), nullptr);

  const auto sdc_path = WriteTempSdc("icts_comb_loop_clock_trace.sdc", "create_clock -name LOOP_CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(ReadCurrentRuntimeClockData());
  ASSERT_EQ(icts_test::runtime::CurrentRuntime().design.get_clocks().size(), 2U);

  std::vector<std::string> materialized_nets;
  for (auto* clock : icts_test::runtime::CurrentRuntime().design.get_clocks()) {
    ASSERT_NE(clock, nullptr);
    materialized_nets.push_back(clock->get_clock_net_name());
  }
  std::ranges::sort(materialized_nets);
  EXPECT_EQ(materialized_nets, (std::vector<std::string>{"clock", "loop_clock"}));

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

}  // namespace
}  // namespace icts_test
