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
 * @brief Lightweight interface and sink-domain wiring tests for FlowManager.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "CTSAPI.hh"
#include "common/logging/LogText.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "evaluation/ClockTreeEvaluator.hh"
#include "feature_icts.h"
#include "flow/FlowManager.hh"
#include "flow/clock_tree_view/ClockTreeView.hh"
#include "flow/htree/CharacterizationLibrary.hh"
#include "flow/htree/HTreeBuilder.hh"
#include "flow/netlist/ClockNetEditor.hh"
#include "flow/stage/CTSClockTreeRunSummary.hh"
#include "flow/stage/ClockSinkDomainBuilder.hh"
#include "flow/stage/ClockTreeSynthesisDriver.hh"
#include "flow/stage/ClockTreeSynthesisStatusTable.hh"
#include "flow/stage/ClockTreeSynthesisTransaction.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

class ScopedFlowReset
{
 public:
  ScopedFlowReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
    FLOW_MANAGER_INST.reset();
  }
  ~ScopedFlowReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
    FLOW_MANAGER_INST.reset();
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

auto readTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return buffer.str();
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
  icts::ClockNetEditor::restoreClockSourceNetToClockLoads(clock);
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();

  std::vector<icts::Pin*> macro_sinks;
  std::vector<icts::Pin*> regular_sinks;
  icts::ClockNetEditor::partitionClockSinks(clock.get_loads(), macro_sinks, regular_sinks);

  std::vector<icts::Pin*> root_inputs;

  auto build_domain = [&](icts::CTSSinkDomain sink_domain, const std::vector<icts::Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    const auto domain_prefix = icts::ClockNetEditor::makeSinkDomainPrefix(clock, 0U, sink_domain);
    icts::Inst* root_buffer = nullptr;
    icts::Pin* root_input = nullptr;
    icts::Pin* root_output = nullptr;
    if (!icts::ClockNetEditor::addRootBufferForSinkDomain(clock, domain_prefix, cell_master, input_pin_name, output_pin_name, sinks,
                                                          root_buffer, root_input, root_output)) {
      return false;
    }
    if (root_input != nullptr) {
      root_inputs.push_back(root_input);
    }
    return icts::ClockNetEditor::connectSinkDomainDownstreamNet(clock, domain_prefix, root_output, sinks) != nullptr;
  };

  ASSERT_TRUE(build_domain(icts::CTSSinkDomain::kHardMacro, macro_sinks));
  ASSERT_TRUE(build_domain(icts::CTSSinkDomain::kRegular, regular_sinks));
  icts::ClockNetEditor::reuseClockSourceNetAsSourceToRootBuffers(clock, clock.get_clock_source(), root_inputs);
}

auto statusRowsContain(const icts::schema::TableRows& rows, const std::string& status, const std::string& sink_domain,
                       const std::string& detail) -> bool
{
  return std::ranges::any_of(rows, [&](const auto& row) -> bool {
    return row.size() >= 7U && row.at(2) == status && row.at(3) == sink_domain && row.at(6) == detail;
  });
}

auto makeRootBufferSpec() -> icts::ClockSinkDomainRootBufferSpec
{
  return icts::ClockSinkDomainRootBufferSpec{
      .cell_master = "CTS_TEST_BUF",
      .input_pin_name = "A",
      .output_pin_name = "Y",
  };
}

auto expectClockRestoredToOriginalLoads(const TestClockPins& pins) -> void
{
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.clock_net, nullptr);
  EXPECT_TRUE(pins.clock->get_insts().empty());
  EXPECT_TRUE(pins.clock->get_nets().empty());
  EXPECT_EQ(pins.clock->get_clock_source_net(), pins.clock_net);
  EXPECT_EQ(pins.clock_net->get_driver(), pins.clock_source);
  ASSERT_EQ(pins.clock_net->get_loads().size(), pins.macro_sink == nullptr ? 1U : 2U);
  if (pins.macro_sink != nullptr) {
    EXPECT_TRUE(containsPin(pins.clock_net->get_loads(), pins.macro_sink));
  }
  if (pins.regular_sink != nullptr) {
    EXPECT_TRUE(containsPin(pins.clock_net->get_loads(), pins.regular_sink));
  }
}

TEST(FlowManagerTest, EmptyFlowManagerRunIsCallable)
{
  const ScopedFlowReset scoped_flow_reset;

  FLOW_MANAGER_INST.run();
}

TEST(FlowManagerTest, EmptyAPIRunEmitsConciseMainLogContract)
{
  const ScopedFlowReset scoped_flow_reset;

  const auto cts_log_path = SCHEMA_WRITER_INST.getActivePath();
  ASSERT_FALSE(cts_log_path.empty());
  icts::CTSAPI::runCTS();

  const auto cts_log_content = readTextFile(cts_log_path);
  ASSERT_FALSE(cts_log_content.empty());
  EXPECT_NE(cts_log_content.find("## Input Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Synthesis Summary"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("### Synthesis Flow"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Evaluation Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Runtime Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Run Results"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CTS Runtime Summary"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CTS Key Results"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CTS Evaluation Summary"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Notes"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Outcome"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("outcome"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Runtime rows are consolidated here"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("This section collects the final user-facing outcome"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("Evaluation writes the final CTS clock tree back to iDB"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("memory_delta_mb"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("elapsed_time_s"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("final_buffer_area_um2"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("max_clock_net_wirelength_um"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("total_clock_tree_wirelength_um"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("max_clock_net_wirelength_dbu"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("total_clock_tree_wirelength_dbu"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("design_dbu_per_um"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("unique_clock_domains"), std::string::npos);
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*max_clock_net_wirelength\s*\|\s*[^|\n]*um\s*\|)")));
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*total_clock_tree_wirelength\s*\|\s*[^|\n]*um\s*\|)")));
  EXPECT_EQ(cts_log_content.find("clock_path_min_buffer"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("clock_path_max_buffer"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("max_level_of_clock_tree"), std::string::npos);

  const auto runtime_read_data_summary = common::logging::ExtractTextBlock(cts_log_content, "ReadData Summary");
  const auto read_data_summary = common::logging::ExtractTextBlock(cts_log_content, "CTSReadData Read CTS clock data Summary");
  const auto flow_summary = common::logging::ExtractTextBlock(cts_log_content, "CTSFlow Run CTS synthesis flow Summary");
  const auto evaluation_summary = common::logging::ExtractTextBlock(cts_log_content, "CTSEvaluation Evaluate CTS clock tree Summary");
  const auto main_flow_summary = common::logging::ExtractTextBlock(cts_log_content, "CTS Clock tree synthesis API flow Summary");
  ASSERT_FALSE(runtime_read_data_summary.empty());
  ASSERT_FALSE(read_data_summary.empty());
  ASSERT_FALSE(flow_summary.empty());
  ASSERT_FALSE(evaluation_summary.empty());
  ASSERT_FALSE(main_flow_summary.empty());
  EXPECT_NE(runtime_read_data_summary.find("clock_source"), std::string::npos);
  EXPECT_NE(runtime_read_data_summary.find("added_clock_nets"), std::string::npos);
  EXPECT_NE(runtime_read_data_summary.find("total_clock_nets"), std::string::npos);
  EXPECT_EQ(runtime_read_data_summary.find("unique_clock_domains"), std::string::npos);
  EXPECT_NE(read_data_summary.find("status"), std::string::npos);
  EXPECT_NE(flow_summary.find("status"), std::string::npos);
  EXPECT_NE(evaluation_summary.find("status"), std::string::npos);
  EXPECT_NE(main_flow_summary.find("status"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("main_flow"), std::string::npos);
  EXPECT_EQ(read_data_summary.find("outcome"), std::string::npos);
  EXPECT_EQ(flow_summary.find("outcome"), std::string::npos);
  EXPECT_EQ(evaluation_summary.find("outcome"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("outcome"), std::string::npos);
  EXPECT_EQ(read_data_summary.find("elapsed_time"), std::string::npos);
  EXPECT_EQ(flow_summary.find("elapsed_time"), std::string::npos);
  EXPECT_EQ(evaluation_summary.find("elapsed_time"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("elapsed_time"), std::string::npos);
}

TEST(FlowManagerTest, ClockDistributionSummaryUsesMacroSinkTerminology)
{
  const ScopedFlowReset scoped_flow_reset;

  const auto cts_log_path = SCHEMA_WRITER_INST.getActivePath();
  ASSERT_FALSE(cts_log_path.empty());

  auto* macro_inst = makeDesignInst("macro0", "MACRO_CELL", icts::InstType::kMacroBlock, icts::Point<int>(100, 0));
  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(200, 0));
  auto pins = addClockToDesign(macro_inst, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.macro_sink, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  DESIGN_INST.emitClockDistributionSummary();

  const auto cts_log_content = readTextFile(cts_log_path);
  ASSERT_FALSE(cts_log_content.empty());
  const auto distribution_summary = common::logging::ExtractTextBlock(cts_log_content, "Clock Distribution Summary");
  ASSERT_FALSE(distribution_summary.empty());
  EXPECT_NE(distribution_summary.find("Macro Sinks"), std::string::npos);
  EXPECT_EQ(distribution_summary.find("Buffer Sinks"), std::string::npos);
  EXPECT_TRUE(std::regex_search(distribution_summary, std::regex(R"(\|\s*clk\s*\|\s*1\s*\|\s*2\s*\|\s*1\s*\|\s*1\s*\|\s*0\s*\|)")));
}

TEST(FlowManagerTest, MixedMacroAndRegularSingleSinkDomainsUseSeparateDownstreamNets)
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

TEST(FlowManagerTest, RootBufferInsertionFailureRollsBackClockAndRecordsSinkDomainStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::ClockTreeView clock_tree_view;
  icts::CTSClockTreeRunSummary summary;
  icts::schema::TableRows rows;
  std::size_t total_sink_domains = 0U;
  std::size_t hard_macro_sink_count = 0U;
  std::size_t regular_sink_count = 0U;

  const auto result = icts::ClockTreeSynthesisDriver::run(*pins.clock, 0U, clock_tree_view, summary, rows, total_sink_domains,
                                                          hard_macro_sink_count, regular_sink_count);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.skipped);
  EXPECT_TRUE(statusRowsContain(rows, "failed", "regular", "failed to insert root buffer"));
  expectClockRestoredToOriginalLoads(pins);
  EXPECT_TRUE(clock_tree_view.get_clocks().empty());
}

TEST(FlowManagerTest, DownstreamNetCreationFailureRollsBackClockAndRecordsSinkDomainStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  const auto domain_prefix = icts::ClockNetEditor::makeSinkDomainPrefix(*pins.clock, 0U, icts::CTSSinkDomain::kRegular);
  auto* existing_downstream_net = DESIGN_INST.makeNet(domain_prefix + "_downstream_net");
  ASSERT_NE(existing_downstream_net, nullptr);

  icts::schema::TableRows rows;
  icts::ClockTreeSynthesisStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  const bool prepared = icts::ClockSinkDomainBuilder::prepare(
      *pins.clock, 0U, icts::CTSSinkDomain::kRegular, std::vector<icts::Pin*>{pins.regular_sink}, 1U, status_table, context, &root_spec);

  EXPECT_FALSE(prepared);
  EXPECT_TRUE(statusRowsContain(rows, "failed", "regular", "failed to create downstream net"));
  icts::ClockTreeSynthesisTransaction::rollbackClock(*pins.clock);
  expectClockRestoredToOriginalLoads(pins);
}

TEST(FlowManagerTest, InsertedObjectCommitFailureRollsBackClockAndDoesNotMergePendingClockTreeView)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::schema::TableRows rows;
  icts::ClockTreeSynthesisStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  ASSERT_TRUE(icts::ClockSinkDomainBuilder::prepare(*pins.clock, 0U, icts::CTSSinkDomain::kRegular,
                                                    std::vector<icts::Pin*>{pins.regular_sink}, 1U, status_table, context, &root_spec));

  icts::ClockTreeView clock_tree_view;
  icts::CTSClockTreeRunSummary summary;
  icts::CharacterizationLibrary char_library;
  icts::ClockTreeSynthesisTransaction transaction(*pins.clock, 0U, clock_tree_view, summary, status_table, char_library, 1U);
  icts::ClockSynthesis::BuildResult synthesis_result;
  synthesis_result.success = true;
  synthesis_result.selected_htree_depth = 0U;
  synthesis_result.selected_htree_level_count = 1U;
  auto duplicate_inst = std::make_unique<icts::Inst>("reg0", "CTS_TEST_BUF", icts::InstType::kBuffer, icts::Point<int>(50, 0));
  duplicate_inst->set_name("reg0");
  duplicate_inst->set_cell_master("CTS_TEST_BUF");
  duplicate_inst->set_type(icts::InstType::kBuffer);
  duplicate_inst->set_location(icts::Point<int>(50, 0));
  synthesis_result.inserted_inst_levels.push_back(icts::HTreeBuilder::InsertedInstLevel{
      .inst = duplicate_inst.get(),
      .topology_level = 0,
      .index_in_level = 0U,
  });
  synthesis_result.inserted_insts.push_back(std::move(duplicate_inst));

  std::string failure_reason;
  EXPECT_FALSE(transaction.commitSinkDomain(context, synthesis_result, failure_reason));
  EXPECT_EQ(failure_reason, "failed to commit inserted synthesis objects");
  expectClockRestoredToOriginalLoads(pins);
  EXPECT_TRUE(clock_tree_view.get_clocks().empty());
  EXPECT_EQ(summary.htree_inserted_buffer_count, 0U);
}

TEST(FlowManagerTest, SourceToRootFailureRollsBackPreparedSinkDomainsAndRecordsStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::schema::TableRows rows;
  icts::ClockTreeSynthesisStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  ASSERT_TRUE(icts::ClockSinkDomainBuilder::prepare(*pins.clock, 0U, icts::CTSSinkDomain::kRegular,
                                                    std::vector<icts::Pin*>{pins.regular_sink}, 1U, status_table, context, &root_spec));
  ASSERT_FALSE(pins.clock->get_insts().empty());
  ASSERT_FALSE(pins.clock->get_nets().empty());

  icts::ClockTreeView clock_tree_view;
  icts::CTSClockTreeRunSummary summary;
  icts::CharacterizationLibrary char_library;
  icts::ClockTreeSynthesisTransaction transaction(*pins.clock, 0U, clock_tree_view, summary, status_table, char_library, 1U);

  EXPECT_FALSE(transaction.synthesizeSourceToRoot({}));
  EXPECT_TRUE(statusRowsContain(rows, "failed", "source_to_root", "empty_root_inputs"));
  expectClockRestoredToOriginalLoads(pins);
}

TEST(FlowManagerTest, ResetAPIClearsEvaluationSummary)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");

  FLOW_MANAGER_INST.evaluate();
  EXPECT_EQ(FLOW_MANAGER_INST.outputSummary().final_clock_buffer_count, 1);
  EXPECT_EQ(icts::CTSAPI::outputSummary().buffer_num, 1);

  icts::CTSAPI::resetAPI();
  const auto summary = icts::CTSAPI::outputSummary();
  EXPECT_EQ(summary.buffer_num, 0);
  EXPECT_EQ(summary.clock_path_min_buffer, 0);
  EXPECT_TRUE(summary.clocks_timing.empty());
}

}  // namespace
}  // namespace icts_test
