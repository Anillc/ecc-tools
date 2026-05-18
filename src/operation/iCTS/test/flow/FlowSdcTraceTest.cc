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
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Clock.hh"
#include "Config.hh"
#include "Design.hh"
#include "FlowTestSupport.hh"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "Schema.hh"
#include "Wrapper.hh"
#include "common/logging/LogText.hh"
#include "instantiation/design_conversion/DesignConversion.hh"

namespace icts_test {
namespace {

using namespace flow_test;

TEST(FlowTest, SdcClockResolutionDoesNotFallbackToIdbClockNets)
{
  const ScopedFlowReset scoped_flow_reset;
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

  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  const auto empty_sdc_path = WriteTempSdc("icts_empty_clock_resolution.sdc", "");
  EXPECT_TRUE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_use_netlist(true);
  CONFIG_INST.set_net_list({{"LOGICAL_CLK", "physical_clk_net"}});
  const auto mapped_sdc_path
      = WriteTempSdc("icts_mapped_clock_resolution.sdc", "create_clock -name LOGICAL_CLK -period 2 physical_clk_net\n");
  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* mapped_clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(mapped_clock, nullptr);
  EXPECT_EQ(mapped_clock->get_clock_name(), "LOGICAL_CLK");
  EXPECT_EQ(mapped_clock->get_clock_net_name(), "physical_clk_net");
  EXPECT_DOUBLE_EQ(mapped_clock->get_clock_period_ns(), 2.0);
  EXPECT_EQ(mapped_clock->get_clock_period_source(), "sdc");

  CONFIG_INST.set_use_netlist(false);
  CONFIG_INST.set_net_list({{"DIRECT_CLK", "missing_config_net"}});
  const auto direct_sdc_path
      = WriteTempSdc("icts_direct_clock_resolution.sdc", "create_clock -name DIRECT_CLK -period 3 physical_clk_net\n");
  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* direct_clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(direct_clock, nullptr);
  EXPECT_EQ(direct_clock->get_clock_name(), "DIRECT_CLK");
  EXPECT_EQ(direct_clock->get_clock_net_name(), "physical_clk_net");
  EXPECT_DOUBLE_EQ(direct_clock->get_clock_period_ns(), 3.0);
  EXPECT_EQ(direct_clock->get_clock_period_source(), "sdc");

  const auto partial_unresolved_sdc_path = WriteTempSdc("icts_partial_missing_clock_resolution.sdc", R"(
create_clock -name DIRECT_CLK -period 3 physical_clk_net
create_clock -name MISSING_CLK -period 2 missing_physical_net
)");
  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_use_netlist(true);
  CONFIG_INST.set_net_list({{"ABSENT_FROM_SDC", "physical_clk_net"}});
  const auto config_absent_sdc_path
      = WriteTempSdc("icts_config_absent_clock_resolution.sdc", "create_clock -name OTHER_CLK -period 2 physical_clk_net\n");
  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_net_list({});
  const auto unresolved_sdc_path
      = WriteTempSdc("icts_missing_clock_resolution.sdc", "create_clock -name MISSING_CLK -period 2 missing_physical_net\n");
  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  std::error_code error_code;
  std::filesystem::remove(empty_sdc_path, error_code);
  std::filesystem::remove(mapped_sdc_path, error_code);
  std::filesystem::remove(direct_sdc_path, error_code);
  std::filesystem::remove(partial_unresolved_sdc_path, error_code);
  std::filesystem::remove(config_absent_sdc_path, error_code);
  std::filesystem::remove(unresolved_sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceResolvesVariableGetPortsToDownstreamClockTarget)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_variable_get_ports_clock_trace.sdc", R"(
set clk_name TRACE_CLK
set clk_period 1.5
set clk_port_name clock
set clk_port [get_ports $clk_port_name]
create_clock -name $clk_name -period [expr $clk_period * 2] $clk_port
)");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* clock = DESIGN_INST.get_clocks().front();
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
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  CONFIG_INST.set_max_fanout(4);
  const auto sdc_path = WriteTempSdc("icts_comb_gate_clock_trace.sdc", "create_clock -name GATED_CLK [get_ports clock] -period 4\n");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "GATED_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "gated_clock");
  EXPECT_DOUBLE_EQ(clock->get_clock_period_ns(), 4.0);

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceMaterializesAllTargetsAndSkipsVirtualClock)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_multi_target_virtual_clock_trace.sdc", R"(
create_clock -name TRACE_CLK [get_ports clock] -period 2
create_clock -name VIRTUAL_ONLY -period 5
)");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 2U);

  std::vector<std::string> materialized_nets;
  for (auto* clock : DESIGN_INST.get_clocks()) {
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
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_generated_boundary_clock_trace.sdc", R"(
create_clock -name MASTER [get_ports clock] -period 2
create_generated_clock -name GEN -master_clock MASTER -divide_by 2 -source [get_ports clock] gen_net
)");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 2U);

  std::map<std::string, std::string> net_by_clock;
  std::map<std::string, double> period_by_clock;
  for (auto* clock : DESIGN_INST.get_clocks()) {
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
  const ScopedFlowReset scoped_flow_reset;
  const auto cts_log_path = SCHEMA_WRITER_INST.getActivePath();
  ASSERT_FALSE(cts_log_path.empty());

  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_unowned_clock_like_report.sdc", "create_clock -name CORE_CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* clock = DESIGN_INST.get_clocks().front();
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
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_ambiguous_shared_clock_trace.sdc", R"(
create_clock -name CLK_A [get_ports clock_a] -period 2
create_clock -name CLK_B [get_ports clock_b] -period 3
)");

  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_TRUE(DESIGN_INST.get_clocks().empty());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceStopsAtClockLikeMuxWithoutCaseAnalysis)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_ambiguous_mux_clock_trace.sdc", "create_clock -name CLK_A [get_ports clock_a] -period 2\n");

  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_TRUE(DESIGN_INST.get_clocks().empty());

  std::error_code error_code;
  std::filesystem::remove(sdc_path, error_code);
}

TEST(FlowTest, SdcClockTraceTerminatesOnCombinationalCycle)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

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

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = WriteTempSdc("icts_comb_loop_clock_trace.sdc", "create_clock -name LOOP_CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 2U);

  std::vector<std::string> materialized_nets;
  for (auto* clock : DESIGN_INST.get_clocks()) {
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
