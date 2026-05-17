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
 * @file FlowTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-25
 * @brief Lightweight interface and sink-domain wiring tests for Flow.
 */

#include <gtest/gtest.h>
#include <stdint.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "CTSAPI.hh"
#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "common/logging/LogText.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/io/Wrapper.hh"
#include "database/spatial/Point.hh"
#include "design/ClockLayout.hh"
#include "dm_config.h"
#include "evaluation/qor/QorEvaluation.hh"
#include "feature_icts.h"
#include "flow/Flow.hh"
#include "flow/instantiation/design_conversion/DesignConversion.hh"
#include "flow/synthesis/Synthesis.hh"
#include "flow/synthesis/distribution/ClockDistribution.hh"
#include "flow/synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "flow/synthesis/topology/Topology.hh"
#include "flow/synthesis/trace/SynthesisTrace.hh"
#include "flow/synthesis/trace/domain_status/DomainStatus.hh"
#include "idm.h"
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
    WRAPPER_INST.reset();
    FLOW_INST.reset();
  }
  ~ScopedFlowReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
    WRAPPER_INST.reset();
    FLOW_INST.reset();
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

auto writeTempSdc(const std::string& file_name, const std::string& content) -> std::filesystem::path
{
  const auto path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream output_stream(path);
  EXPECT_TRUE(output_stream.is_open());
  output_stream << content;
  dmInst->get_config().set_sdc_path(path.string());
  return path;
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
  icts::DesignConversion::restoreClockSourceNetToClockLoads(clock);
  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();

  std::vector<icts::Pin*> macro_sinks;
  std::vector<icts::Pin*> regular_sinks;
  icts::DesignConversion::partitionClockSinks(clock.get_loads(), macro_sinks, regular_sinks);

  std::vector<icts::Pin*> root_inputs;

  auto build_domain = [&](icts::SinkDomainKind sink_domain, const std::vector<icts::Pin*>& sinks) -> bool {
    if (sinks.empty()) {
      return true;
    }
    const auto domain_prefix = icts::DesignConversion::makeSinkDomainPrefix(clock, 0U, sink_domain);
    icts::Inst* root_buffer = nullptr;
    icts::Pin* root_input = nullptr;
    icts::Pin* root_output = nullptr;
    if (!icts::DesignConversion::addRootBufferForSinkDomain(clock, domain_prefix, cell_master, input_pin_name, output_pin_name, sinks,
                                                            root_buffer, root_input, root_output)) {
      return false;
    }
    if (root_input != nullptr) {
      root_inputs.push_back(root_input);
    }
    return icts::DesignConversion::connectSinkDomainDownstreamNet(clock, domain_prefix, root_output, sinks) != nullptr;
  };

  ASSERT_TRUE(build_domain(icts::SinkDomainKind::kHardMacro, macro_sinks));
  ASSERT_TRUE(build_domain(icts::SinkDomainKind::kRegular, regular_sinks));
  icts::DesignConversion::reuseClockSourceNetAsSourceToRootBuffers(clock, clock.get_clock_source(), root_inputs);
}

auto statusRowsContain(const icts::schema::TableRows& rows, const std::string& status, const std::string& sink_domain,
                       const std::string& detail) -> bool
{
  return std::ranges::any_of(rows, [&](const auto& row) -> bool {
    return row.size() >= 7U && row.at(2) == status && row.at(3) == sink_domain && row.at(6) == detail;
  });
}

auto domainStatusContains(const std::vector<icts::SynthesisTraceStatusRecord>& records, const std::string& status,
                          const std::string& sink_domain, const std::string& detail) -> bool
{
  return std::ranges::any_of(records, [&](const auto& record) -> bool {
    return record.status == status && record.sink_domain == sink_domain && record.detail == detail;
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

auto addIdbTerm(idb::IdbCellMaster& cell_master, const std::string& term_name, idb::IdbConnectDirection direction,
                idb::IdbConnectType type = idb::IdbConnectType::kSignal) -> idb::IdbTerm*
{
  auto* term = cell_master.add_term(term_name);
  if (term == nullptr) {
    return nullptr;
  }
  term->set_direction(direction);
  term->set_type(type);
  term->set_average_position(0, 0);
  return term;
}

auto addIdbCellMaster(idb::IdbLayout& idb_layout, const std::string& cell_master_name) -> idb::IdbCellMaster*
{
  auto* cell_master = idb_layout.get_cell_master_list()->set_cell_master(cell_master_name);
  if (cell_master == nullptr) {
    return nullptr;
  }
  cell_master->set_type(idb::CellMasterType::kCore);
  cell_master->set_width(10);
  cell_master->set_height(10);
  return cell_master;
}

auto addIdbInst(idb::IdbDesign& idb_design, const std::string& inst_name, idb::IdbCellMaster& cell_master, int32_t loc_x = 0,
                int32_t loc_y = 0) -> idb::IdbInstance*
{
  auto* idb_inst = idb_design.get_instance_list()->add_instance(inst_name);
  if (idb_inst == nullptr) {
    return nullptr;
  }
  idb_inst->set_cell_master(&cell_master);
  idb_inst->set_orient(idb::IdbOrient::kN_R0, false);
  idb_inst->set_coodinate(loc_x, loc_y, false);
  idb_inst->set_status(idb::IdbPlacementStatus::kPlaced);
  for (auto* idb_pin : idb_inst->get_pin_list()->get_pin_list()) {
    if (idb_pin != nullptr) {
      idb_pin->set_average_coordinate(loc_x, loc_y);
    }
  }
  return idb_inst;
}

auto addIdbIoPin(idb::IdbDesign& idb_design, const std::string& pin_name, idb::IdbConnectDirection direction,
                 idb::IdbConnectType type = idb::IdbConnectType::kSignal) -> idb::IdbPin*
{
  auto* idb_pin = idb_design.get_io_pin_list()->add_pin_list(pin_name);
  if (idb_pin == nullptr) {
    return nullptr;
  }
  idb_pin->set_as_io();
  auto* term = idb_pin->set_term();
  if (term == nullptr) {
    return nullptr;
  }
  term->set_name(pin_name);
  term->set_direction(direction);
  term->set_type(type);
  idb_pin->set_average_coordinate(0, 0);
  return idb_pin;
}

auto attachIdbPinToNet(idb::IdbNet& idb_net, idb::IdbPin& idb_pin) -> void
{
  auto* old_net = idb_pin.get_net();
  if (old_net != nullptr && old_net != &idb_net) {
    old_net->remove_pin(&idb_pin);
  }

  idb_pin.set_net(&idb_net);
  idb_pin.set_net_name(idb_net.get_net_name());
  auto* pin_list = idb_pin.is_io_pin() ? idb_net.get_io_pins() : idb_net.get_instance_pin_list();
  if (pin_list != nullptr && pin_list->find_pin(&idb_pin) == nullptr) {
    if (idb_pin.is_io_pin()) {
      idb_net.add_io_pin(&idb_pin);
    } else {
      idb_net.add_instance_pin(&idb_pin);
    }
  }
}

auto addIdbClockSink(idb::IdbDesign& idb_design, idb::IdbCellMaster& reg_master, idb::IdbNet& net, const std::string& inst_name,
                     int32_t loc_x) -> idb::IdbPin*
{
  auto* inst = addIdbInst(idb_design, inst_name, reg_master, loc_x, 0);
  if (inst == nullptr) {
    ADD_FAILURE() << "failed to add clock sink inst " << inst_name;
    return nullptr;
  }
  auto* pin = inst->get_pin_by_term("CLK");
  if (pin == nullptr) {
    ADD_FAILURE() << "failed to find clock sink pin for " << inst_name;
    return nullptr;
  }
  attachIdbPinToNet(net, *pin);
  return pin;
}

auto addIdbPassCell(idb::IdbDesign& idb_design, idb::IdbCellMaster& pass_master, idb::IdbNet& input_net, idb::IdbNet& output_net,
                    const std::string& inst_name, int32_t loc_x) -> idb::IdbInstance*
{
  auto* inst = addIdbInst(idb_design, inst_name, pass_master, loc_x, 0);
  if (inst == nullptr) {
    ADD_FAILURE() << "failed to add pass cell inst " << inst_name;
    return nullptr;
  }
  auto* input_pin = inst->get_pin_by_term("A");
  auto* output_pin = inst->get_pin_by_term("Y");
  if (input_pin == nullptr || output_pin == nullptr) {
    ADD_FAILURE() << "failed to find pass cell pins for " << inst_name;
    return nullptr;
  }
  attachIdbPinToNet(input_net, *input_pin);
  attachIdbPinToNet(output_net, *output_pin);
  return inst;
}

auto buildClockForWrapperWriteback(icts::Clock& clock, const std::string& source_pin_name, const std::string& sink_inst_name,
                                   const std::string& sink_pin_name) -> void
{
  auto* clock_net = DESIGN_INST.makeNet(clock.get_clock_net_name());
  ASSERT_NE(clock_net, nullptr);
  auto* source_pin = DESIGN_INST.makePin(source_pin_name);
  ASSERT_NE(source_pin, nullptr);
  source_pin->set_name(source_pin_name);
  source_pin->set_type(icts::PinType::kOut);
  source_pin->set_location(icts::Point<int>(0, 0));
  source_pin->set_net(clock_net);
  ASSERT_TRUE(DESIGN_INST.indexPin(source_pin));

  auto* sink_inst = makeDesignInst(sink_inst_name, "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  ASSERT_NE(sink_inst, nullptr);
  auto* sink_pin = DESIGN_INST.makePin(sink_pin_name);
  ASSERT_NE(sink_pin, nullptr);
  sink_pin->set_name(sink_pin_name);
  sink_pin->set_type(icts::PinType::kClock);
  sink_pin->set_location(sink_inst->get_location());
  sink_pin->set_inst(sink_inst);
  sink_pin->set_net(clock_net);
  sink_inst->add_pin(sink_pin);
  ASSERT_TRUE(DESIGN_INST.indexPin(sink_pin));

  clock_net->set_driver(source_pin);
  clock_net->add_load(sink_pin);
  clock.set_clock_source(source_pin);
  clock.set_clock_source_net(clock_net);
  clock.add_load(sink_pin);
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

TEST(FlowTest, EmptyFlowRunIsCallable)
{
  const ScopedFlowReset scoped_flow_reset;

  FLOW_INST.run();

  const auto summary = FLOW_INST.outputRunSummary();
  EXPECT_FALSE(summary.success);
  EXPECT_EQ(summary.outcome, icts::SynthesisOutcome::kNoOp);
  EXPECT_EQ(summary.no_op_reason, "no_clocks_discovered");
  EXPECT_EQ(summary.total_clocks, 0U);
  EXPECT_EQ(summary.successful_clocks, 0U);
  EXPECT_EQ(summary.skipped_clocks, 0U);
  EXPECT_EQ(summary.failed_clocks, 0U);
}

TEST(FlowTest, RunWithoutSetupFailsExplicitlyAndSkipsPipelineStages)
{
  const ScopedFlowReset scoped_flow_reset;

  const auto cts_log_path = SCHEMA_WRITER_INST.getActivePath();
  ASSERT_FALSE(cts_log_path.empty());
  icts::CTSAPI::runCTS();

  const auto cts_log_content = readTextFile(cts_log_path);
  ASSERT_FALSE(cts_log_content.empty());
  EXPECT_EQ(cts_log_content.find("## Input Overview"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("## Synthesis Overview"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("### Synthesis Flow"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("## Evaluation Overview"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Runtime Overview"), std::string::npos);
  EXPECT_NE(cts_log_content.find("## Run Results"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CTS Runtime Overview"), std::string::npos);
  EXPECT_NE(cts_log_content.find("CTS Key Results"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("CTS Evaluation Overview"), std::string::npos);
  EXPECT_NE(cts_log_content.find("setup_failed"), std::string::npos);
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
  EXPECT_EQ(cts_log_content.find("total_clock_network_wirelength_um"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("max_clock_net_wirelength_dbu"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("total_clock_network_wirelength_dbu"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("design_dbu_per_um"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("unique_clock_domains"), std::string::npos);
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*max_clock_net_wirelength\s*\|\s*[^|\n]*um\s*\|)")));
  EXPECT_TRUE(std::regex_search(cts_log_content, std::regex(R"(\|\s*total_clock_network_wirelength\s*\|\s*[^|\n]*um\s*\|)")));
  EXPECT_EQ(cts_log_content.find("clock_path_min_buffer"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("clock_path_max_buffer"), std::string::npos);
  EXPECT_EQ(cts_log_content.find("feature_max_clock_network_level"), std::string::npos);

  const auto runtime_read_data_summary = common::logging::ExtractTextBlock(cts_log_content, "ReadData Overview");
  const auto read_data_summary = common::logging::ExtractTextBlock(cts_log_content, "CTSReadData Read CTS clock data Overview");
  const auto flow_summary = common::logging::ExtractTextBlock(cts_log_content, "CTSFlow Run CTS synthesis flow Overview");
  const auto evaluation_summary = common::logging::ExtractTextBlock(cts_log_content, "Evaluation Evaluate CTS clock tree Overview");
  const auto main_flow_summary = common::logging::ExtractTextBlock(cts_log_content, "CTS Clock tree synthesis API flow Summary");
  EXPECT_TRUE(runtime_read_data_summary.empty());
  EXPECT_TRUE(read_data_summary.empty());
  EXPECT_TRUE(flow_summary.empty());
  EXPECT_TRUE(evaluation_summary.empty());
  ASSERT_FALSE(main_flow_summary.empty());
  EXPECT_NE(main_flow_summary.find("setup_failed"), std::string::npos);
  EXPECT_NE(main_flow_summary.find("status"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("main_flow"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("outcome"), std::string::npos);
  EXPECT_EQ(main_flow_summary.find("elapsed_time"), std::string::npos);
}

TEST(FlowTest, AllSkippedSynthesisReportsNoOp)
{
  const ScopedFlowReset scoped_flow_reset;

  auto pins = addClockToDesign(nullptr, nullptr);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.clock_source, nullptr);
  ASSERT_NE(pins.clock_net, nullptr);
  ASSERT_TRUE(pins.clock->get_loads().empty());

  icts::ClockLayout clock_layout;
  icts::CharacterizationLibrary char_library;
  const auto summary = icts::Synthesis::run(clock_layout, char_library);

  EXPECT_FALSE(summary.success);
  EXPECT_EQ(summary.outcome, icts::SynthesisOutcome::kNoOp);
  EXPECT_EQ(summary.no_op_reason, "all_clocks_skipped");
  EXPECT_EQ(summary.total_clocks, 1U);
  EXPECT_EQ(summary.successful_clocks, 0U);
  EXPECT_EQ(summary.skipped_clocks, 1U);
  EXPECT_EQ(summary.failed_clocks, 0U);
  EXPECT_TRUE(domainStatusContains(summary.domain_status, "skipped", "none", "no valid sinks"));
  EXPECT_TRUE(clock_layout.get_clocks().empty());
}

TEST(FlowTest, NoOpRunDoesNotExposeFinalEvaluationSummary)
{
  const ScopedFlowReset scoped_flow_reset;

  FLOW_INST.run();
  FLOW_INST.evaluate();

  const auto run_summary = FLOW_INST.outputRunSummary();
  EXPECT_EQ(run_summary.outcome, icts::SynthesisOutcome::kNoOp);

  const auto qor_summary = FLOW_INST.outputSummary();
  EXPECT_FALSE(qor_summary.has_evaluation_result);
  EXPECT_EQ(qor_summary.qor_metric_status, "unavailable");
  EXPECT_EQ(qor_summary.timing_metric_source, "unavailable");
  EXPECT_EQ(qor_summary.physical_metric_source, "unavailable");
  EXPECT_TRUE(qor_summary.clocks_timing.empty());
  EXPECT_TRUE(qor_summary.clocks_latency_skew.empty());
}

TEST(FlowTest, ClockDistributionSummaryUsesMacroSinkTerminology)
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
  const auto distribution_summary = common::logging::ExtractTextBlock(cts_log_content, "Clock Distribution Overview");
  ASSERT_FALSE(distribution_summary.empty());
  EXPECT_NE(distribution_summary.find("Macro Sinks"), std::string::npos);
  EXPECT_EQ(distribution_summary.find("Buffer Sinks"), std::string::npos);
  EXPECT_TRUE(std::regex_search(distribution_summary, std::regex(R"(\|\s*clk\s*\|\s*1\s*\|\s*2\s*\|\s*1\s*\|\s*1\s*\|\s*0\s*\|)")));
}

TEST(FlowTest, MixedMacroAndRegularSingleSinkDomainsUseSeparateDownstreamNets)
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

TEST(FlowTest, RepeatedNetPreparationRestoresClockSourceNetBeforeRebuildingInsertedNets)
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

TEST(FlowTest, RootBufferInsertionFailureRollsBackClockAndRecordsSinkDomainStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::ClockLayout clock_layout;
  icts::CharacterizationLibrary char_library;
  const auto summary = icts::Synthesis::run(clock_layout, char_library);

  EXPECT_FALSE(summary.success);
  EXPECT_EQ(summary.failed_clocks, 1U);
  EXPECT_EQ(summary.skipped_clocks, 0U);
  EXPECT_TRUE(domainStatusContains(summary.domain_status, "failed", "regular", "failed to insert root buffer"));
  expectClockRestoredToOriginalLoads(pins);
  EXPECT_TRUE(clock_layout.get_clocks().empty());
}

TEST(FlowTest, DownstreamNetCreationFailureRollsBackClockAndRecordsSinkDomainStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  const auto domain_prefix = icts::DesignConversion::makeSinkDomainPrefix(*pins.clock, 0U, icts::SinkDomainKind::kRegular);
  auto* existing_downstream_net = DESIGN_INST.makeNet(domain_prefix + "_downstream_net");
  ASSERT_NE(existing_downstream_net, nullptr);

  icts::schema::TableRows rows;
  icts::DomainStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  const bool prepared = icts::ClockDistribution::prepare(*pins.clock, 0U, icts::SinkDomainKind::kRegular,
                                                         std::vector<icts::Pin*>{pins.regular_sink}, 1U, status_table, context, &root_spec);

  EXPECT_FALSE(prepared);
  EXPECT_TRUE(statusRowsContain(rows, "failed", "regular", "failed to create downstream net"));
  icts::Topology::resetClockTopology(*pins.clock);
  expectClockRestoredToOriginalLoads(pins);
}

TEST(FlowTest, TopologyResetRollsBackPreparedSinkDomainAndDoesNotMergePendingClockLayout)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::schema::TableRows rows;
  icts::DomainStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  ASSERT_TRUE(icts::ClockDistribution::prepare(*pins.clock, 0U, icts::SinkDomainKind::kRegular, std::vector<icts::Pin*>{pins.regular_sink},
                                               1U, status_table, context, &root_spec));

  icts::ClockLayout clock_layout;
  icts::Topology::resetClockTopology(*pins.clock);
  expectClockRestoredToOriginalLoads(pins);
  EXPECT_TRUE(clock_layout.get_clocks().empty());
}

TEST(FlowTest, SourceToRootFailureRollsBackPreparedSinkDomainsAndRecordsStatus)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.regular_sink, nullptr);

  icts::schema::TableRows rows;
  icts::DomainStatusTable status_table(rows);
  icts::ClockSinkDomainContext context;
  const auto root_spec = makeRootBufferSpec();
  ASSERT_TRUE(icts::ClockDistribution::prepare(*pins.clock, 0U, icts::SinkDomainKind::kRegular, std::vector<icts::Pin*>{pins.regular_sink},
                                               1U, status_table, context, &root_spec));
  ASSERT_FALSE(pins.clock->get_insts().empty());
  ASSERT_FALSE(pins.clock->get_nets().empty());

  icts::ClockLayout clock_layout;
  icts::SynthesisTraceSummary summary;
  icts::CharacterizationLibrary char_library;
  context.root_input = nullptr;

  EXPECT_FALSE(icts::Topology::formClock(*pins.clock, 0U, clock_layout, summary, status_table, char_library, 1U, {context}));
  EXPECT_TRUE(statusRowsContain(rows, "failed", "source_to_root", "empty_root_inputs"));
  expectClockRestoredToOriginalLoads(pins);
}

TEST(FlowTest, EvaluateWithoutSuccessfulInstantiationLeavesSummaryUnavailableAndResetKeepsItCleared)
{
  const ScopedFlowReset scoped_flow_reset;

  auto* reg_inst = makeDesignInst("reg0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
  auto pins = addClockToDesign(nullptr, reg_inst);
  prepareDirectRootBufferNets(*pins.clock, "CTS_TEST_BUF", "A", "Y");

  FLOW_INST.evaluate();
  EXPECT_FALSE(FLOW_INST.outputSummary().has_evaluation_result);
  EXPECT_EQ(FLOW_INST.outputSummary().final_clock_buffer_count, 0);
  EXPECT_EQ(icts::CTSAPI::outputSummary().buffer_num, 0);

  icts::CTSAPI::resetAPI();
  const auto summary = icts::CTSAPI::outputSummary();
  EXPECT_EQ(summary.buffer_num, 0);
  EXPECT_EQ(summary.clock_path_min_buffer, 0);
  EXPECT_TRUE(summary.clocks_timing.empty());
}

TEST(FlowTest, SdcClockResolutionDoesNotFallbackToIdbClockNets)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  ASSERT_NE(idb_design.get_net_list(), nullptr);
  ASSERT_NE(idb_design.get_net_list()->add_net("idb_only_clk", idb::IdbConnectType::kClock), nullptr);
  auto* physical_clk_net = idb_design.get_net_list()->add_net("physical_clk_net", idb::IdbConnectType::kSignal);
  ASSERT_NE(physical_clk_net, nullptr);

  auto* src_master = addIdbCellMaster(idb_layout, "SRC_CELL");
  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(src_master, nullptr);
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*src_master, "CLKOUT", idb::IdbConnectDirection::kOutput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* src_inst = addIdbInst(idb_design, "src0", *src_master, 0, 0);
  auto* sink_inst = addIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(src_inst, nullptr);
  ASSERT_NE(sink_inst, nullptr);
  auto* src_pin = src_inst->get_pin_by_term("CLKOUT");
  auto* sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(sink_pin, nullptr);
  attachIdbPinToNet(*physical_clk_net, *src_pin);
  attachIdbPinToNet(*physical_clk_net, *sink_pin);

  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  const auto empty_sdc_path = writeTempSdc("icts_empty_clock_resolution.sdc", "");
  EXPECT_TRUE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_use_netlist(true);
  CONFIG_INST.set_net_list({{"LOGICAL_CLK", "physical_clk_net"}});
  const auto mapped_sdc_path
      = writeTempSdc("icts_mapped_clock_resolution.sdc", "create_clock -name LOGICAL_CLK -period 2 physical_clk_net\n");
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
      = writeTempSdc("icts_direct_clock_resolution.sdc", "create_clock -name DIRECT_CLK -period 3 physical_clk_net\n");
  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* direct_clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(direct_clock, nullptr);
  EXPECT_EQ(direct_clock->get_clock_name(), "DIRECT_CLK");
  EXPECT_EQ(direct_clock->get_clock_net_name(), "physical_clk_net");
  EXPECT_DOUBLE_EQ(direct_clock->get_clock_period_ns(), 3.0);
  EXPECT_EQ(direct_clock->get_clock_period_source(), "sdc");

  const auto partial_unresolved_sdc_path = writeTempSdc("icts_partial_missing_clock_resolution.sdc", R"(
create_clock -name DIRECT_CLK -period 3 physical_clk_net
create_clock -name MISSING_CLK -period 2 missing_physical_net
)");
  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_use_netlist(true);
  CONFIG_INST.set_net_list({{"ABSENT_FROM_SDC", "physical_clk_net"}});
  const auto config_absent_sdc_path
      = writeTempSdc("icts_config_absent_clock_resolution.sdc", "create_clock -name OTHER_CLK -period 2 physical_clk_net\n");
  EXPECT_FALSE(icts::DesignConversion::readClockData());
  EXPECT_EQ(DESIGN_INST.get_clocks().size(), 0U);

  CONFIG_INST.set_net_list({});
  const auto unresolved_sdc_path
      = writeTempSdc("icts_missing_clock_resolution.sdc", "create_clock -name MISSING_CLK -period 2 missing_physical_net\n");
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

  auto* io_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  attachIdbPinToNet(*root_net, *io_pin);

  auto* buf_master = addIdbCellMaster(idb_layout, "CLKBUF_CELL");
  ASSERT_NE(buf_master, nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* buf_inst = addIdbInst(idb_design, "clkbuf0", *buf_master, 50, 0);
  ASSERT_NE(buf_inst, nullptr);
  auto* buf_in = buf_inst->get_pin_by_term("A");
  auto* buf_out = buf_inst->get_pin_by_term("Y");
  ASSERT_NE(buf_in, nullptr);
  ASSERT_NE(buf_out, nullptr);
  attachIdbPinToNet(*root_net, *buf_in);
  attachIdbPinToNet(*leaf_net, *buf_out);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* sink_inst = addIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(sink_inst, nullptr);
  auto* sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(sink_pin, nullptr);
  attachIdbPinToNet(*leaf_net, *sink_pin);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_variable_get_ports_clock_trace.sdc", R"(
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

  auto* io_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  attachIdbPinToNet(*root_net, *io_pin);

  auto* gate_master = addIdbCellMaster(idb_layout, "CLK_GATE_LIKE_CELL");
  ASSERT_NE(gate_master, nullptr);
  ASSERT_NE(addIdbTerm(*gate_master, "CLK", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*gate_master, "EN", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*gate_master, "Z", idb::IdbConnectDirection::kOutput), nullptr);
  auto* gate_inst = addIdbInst(idb_design, "gate0", *gate_master, 50, 0);
  ASSERT_NE(gate_inst, nullptr);
  auto* gate_clk = gate_inst->get_pin_by_term("CLK");
  auto* gate_enable = gate_inst->get_pin_by_term("EN");
  auto* gate_out = gate_inst->get_pin_by_term("Z");
  ASSERT_NE(gate_clk, nullptr);
  ASSERT_NE(gate_enable, nullptr);
  ASSERT_NE(gate_out, nullptr);
  attachIdbPinToNet(*root_net, *gate_clk);
  attachIdbPinToNet(*enable_net, *gate_enable);
  attachIdbPinToNet(*gated_net, *gate_out);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  for (int index = 0; index < 4; ++index) {
    auto* source_side_sink_inst = addIdbInst(idb_design, "source_side_sink" + std::to_string(index), *reg_master, 100 + index, 0);
    ASSERT_NE(source_side_sink_inst, nullptr);
    auto* source_side_sink_pin = source_side_sink_inst->get_pin_by_term("CLK");
    ASSERT_NE(source_side_sink_pin, nullptr);
    attachIdbPinToNet(*root_net, *source_side_sink_pin);
  }
  for (int index = 0; index < 5; ++index) {
    auto* sink_inst = addIdbInst(idb_design, "sink" + std::to_string(index), *reg_master, 200 + index, 0);
    ASSERT_NE(sink_inst, nullptr);
    auto* sink_pin = sink_inst->get_pin_by_term("CLK");
    ASSERT_NE(sink_pin, nullptr);
    attachIdbPinToNet(*gated_net, *sink_pin);
  }

  CONFIG_INST.set_use_netlist(false);
  CONFIG_INST.set_max_fanout(4);
  const auto sdc_path = writeTempSdc("icts_comb_gate_clock_trace.sdc", "create_clock -name GATED_CLK [get_ports clock] -period 4\n");

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

  auto* io_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  attachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = addIdbCellMaster(idb_layout, "CLK_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(addIdbPassCell(idb_design, *pass_master, *root_net, *branch_a_net, "pass_a", 10), nullptr);
  ASSERT_NE(addIdbPassCell(idb_design, *pass_master, *root_net, *branch_b_net, "pass_b", 20), nullptr);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *branch_a_net, "sink_a", 100), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *branch_b_net, "sink_b", 200), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_multi_target_virtual_clock_trace.sdc", R"(
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

  auto* io_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  attachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = addIdbCellMaster(idb_layout, "GEN_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(addIdbPassCell(idb_design, *pass_master, *root_net, *generated_net, "gen_pass", 50), nullptr);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *root_net, "master_sink", 100), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *generated_net, "generated_sink", 200), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_generated_boundary_clock_trace.sdc", R"(
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

  auto* clock_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* noc_clock_pin = addIdbIoPin(idb_design, "noc_clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_pin, nullptr);
  ASSERT_NE(noc_clock_pin, nullptr);
  attachIdbPinToNet(*clock_net, *clock_pin);
  attachIdbPinToNet(*noc_clock_net, *noc_clock_pin);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *clock_net, "core_sink", 100), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *noc_clock_net, "noc_sink", 200), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_unowned_clock_like_report.sdc", "create_clock -name CORE_CLK [get_ports clock] -period 2\n");

  EXPECT_TRUE(icts::DesignConversion::readClockData());
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "CORE_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "clock");

  const auto cts_log_content = readTextFile(cts_log_path);
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
  auto* clock_a_pin = addIdbIoPin(idb_design, "clock_a", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* clock_b_pin = addIdbIoPin(idb_design, "clock_b", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_a_pin, nullptr);
  ASSERT_NE(clock_b_pin, nullptr);
  attachIdbPinToNet(*shared_net, *clock_a_pin);
  attachIdbPinToNet(*shared_net, *clock_b_pin);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *shared_net, "shared_sink", 100), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_ambiguous_shared_clock_trace.sdc", R"(
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

  auto* clock_a_pin = addIdbIoPin(idb_design, "clock_a", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  auto* clock_b_pin = addIdbIoPin(idb_design, "clock_b", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(clock_a_pin, nullptr);
  ASSERT_NE(clock_b_pin, nullptr);
  attachIdbPinToNet(*clock_a_net, *clock_a_pin);
  attachIdbPinToNet(*clock_b_net, *clock_b_pin);

  auto* mux_master = addIdbCellMaster(idb_layout, "CLOCK_MUX_CELL");
  ASSERT_NE(mux_master, nullptr);
  ASSERT_NE(addIdbTerm(*mux_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*mux_master, "B", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*mux_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  auto* mux_inst = addIdbInst(idb_design, "mux0", *mux_master, 50, 0);
  ASSERT_NE(mux_inst, nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("A"), nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("B"), nullptr);
  ASSERT_NE(mux_inst->get_pin_by_term("Y"), nullptr);
  attachIdbPinToNet(*clock_a_net, *mux_inst->get_pin_by_term("A"));
  attachIdbPinToNet(*clock_b_net, *mux_inst->get_pin_by_term("B"));
  attachIdbPinToNet(*mux_out_net, *mux_inst->get_pin_by_term("Y"));

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *clock_b_net, "clock_b_sink", 100), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *mux_out_net, "mux_sink", 200), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_ambiguous_mux_clock_trace.sdc", "create_clock -name CLK_A [get_ports clock_a] -period 2\n");

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

  auto* io_pin = addIdbIoPin(idb_design, "clock", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock);
  ASSERT_NE(io_pin, nullptr);
  attachIdbPinToNet(*root_net, *io_pin);

  auto* pass_master = addIdbCellMaster(idb_layout, "LOOP_PASS_CELL");
  ASSERT_NE(pass_master, nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "A", idb::IdbConnectDirection::kInput), nullptr);
  ASSERT_NE(addIdbTerm(*pass_master, "Y", idb::IdbConnectDirection::kOutput), nullptr);
  ASSERT_NE(addIdbPassCell(idb_design, *pass_master, *root_net, *loop_net, "loop_pass0", 50), nullptr);
  ASSERT_NE(addIdbPassCell(idb_design, *pass_master, *loop_net, *root_net, "loop_pass1", 60), nullptr);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *root_net, "root_sink", 100), nullptr);
  ASSERT_NE(addIdbClockSink(idb_design, *reg_master, *loop_net, "loop_sink", 200), nullptr);

  CONFIG_INST.set_use_netlist(false);
  const auto sdc_path = writeTempSdc("icts_comb_loop_clock_trace.sdc", "create_clock -name LOOP_CLK [get_ports clock] -period 2\n");

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

TEST(FlowTest, WritebackFailureRemovesCreatedIdbNetsOnRollback)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbDesign idb_design;
  WRAPPER_INST.set_idb_design(&idb_design);

  auto* clock = DESIGN_INST.makeClock("LOGICAL_CLK", "cts_inserted_clk_net");
  ASSERT_NE(clock, nullptr);
  buildClockForWrapperWriteback(*clock, "LOGICAL_CLK_SRC", "rollback_ff", "CLK");
  ASSERT_EQ(idb_design.get_net_list()->find_net("cts_inserted_clk_net"), nullptr);

  const auto result = WRAPPER_INST.writeClocksDetailed({clock});

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.failed_clock, "LOGICAL_CLK");
  EXPECT_EQ(result.failed_net, "cts_inserted_clk_net");
  EXPECT_EQ(result.reason, "write_clock_failed");
  EXPECT_TRUE(result.rollback_done);
  EXPECT_EQ(idb_design.get_net_list()->find_net("cts_inserted_clk_net"), nullptr);
}

TEST(FlowTest, WrapperReadClocksBuildsCtsClockFromSdcDeclaredIdbNet)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  auto* src_master = addIdbCellMaster(idb_layout, "SRC_CELL");
  ASSERT_NE(src_master, nullptr);
  ASSERT_NE(addIdbTerm(*src_master, "CLKOUT", idb::IdbConnectDirection::kOutput, idb::IdbConnectType::kClock), nullptr);
  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);

  auto* src_inst = addIdbInst(idb_design, "src0", *src_master, 0, 0);
  auto* sink_inst = addIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(src_inst, nullptr);
  ASSERT_NE(sink_inst, nullptr);
  auto* src_pin = src_inst->get_pin_by_term("CLKOUT");
  auto* sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(sink_pin, nullptr);

  auto* idb_net = idb_design.get_net_list()->add_net("physical_clk_net", idb::IdbConnectType::kClock);
  ASSERT_NE(idb_net, nullptr);
  idb_net->add_instance_pin(src_pin);
  src_pin->set_net(idb_net);
  src_pin->set_net_name("physical_clk_net");
  idb_net->add_instance_pin(sink_pin);
  sink_pin->set_net(idb_net);
  sink_pin->set_net_name("physical_clk_net");

  EXPECT_TRUE(WRAPPER_INST.readClocks({{"LOGICAL_CLK", "physical_clk_net"}}));
  ASSERT_EQ(DESIGN_INST.get_clocks().size(), 1U);
  auto* clock = DESIGN_INST.get_clocks().front();
  ASSERT_NE(clock, nullptr);
  EXPECT_EQ(clock->get_clock_name(), "LOGICAL_CLK");
  EXPECT_EQ(clock->get_clock_net_name(), "physical_clk_net");
  ASSERT_NE(clock->get_clock_source(), nullptr);
  ASSERT_NE(clock->get_clock_source_net(), nullptr);
  EXPECT_EQ(clock->get_clock_source()->get_name(), "CLKOUT");
  ASSERT_EQ(clock->get_loads().size(), 1U);
  EXPECT_EQ(clock->get_loads().front()->get_name(), "CLK");
  EXPECT_NE(DESIGN_INST.findPin("src0/CLKOUT"), nullptr);
  EXPECT_NE(DESIGN_INST.findPin("sink0/CLK"), nullptr);
}

TEST(FlowTest, WrapperWritebackResolvesExistingPinsAndMaterializesCtsBufferInst)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* buf_master = addIdbCellMaster(idb_layout, "CTS_BUF");
  ASSERT_NE(buf_master, nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "A", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "Y", idb::IdbConnectDirection::kOutput, idb::IdbConnectType::kClock), nullptr);

  auto* sink_inst = addIdbInst(idb_design, "sink0", *reg_master, 100, 0);
  ASSERT_NE(sink_inst, nullptr);
  auto* idb_sink_pin = sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(idb_sink_pin, nullptr);

  auto* clock = DESIGN_INST.makeClock("LOGICAL_CLK", "root_clk_net");
  ASSERT_NE(clock, nullptr);
  auto* source_net = DESIGN_INST.makeNet("root_clk_net");
  auto* leaf_net = DESIGN_INST.makeNet("leaf_clk_net");
  ASSERT_NE(source_net, nullptr);
  ASSERT_NE(leaf_net, nullptr);
  auto* io_driver = DESIGN_INST.makePin("clk_port");
  ASSERT_NE(io_driver, nullptr);
  io_driver->set_name("clk_port");
  io_driver->set_type(icts::PinType::kOut);
  io_driver->set_net(source_net);
  io_driver->set_io(true);
  ASSERT_TRUE(DESIGN_INST.indexPin(io_driver));
  auto* buf_inst = makeDesignInst("cts_buf0", "CTS_BUF", icts::InstType::kBuffer, icts::Point<int>(10, 0));
  ASSERT_NE(buf_inst, nullptr);
  auto* buf_in = addOwnedLoad(*clock, source_net, *buf_inst, "A");
  ASSERT_NE(buf_in, nullptr);
  auto* buf_out = DESIGN_INST.makePin("Y");
  ASSERT_NE(buf_out, nullptr);
  buf_out->set_name("Y");
  buf_out->set_type(icts::PinType::kOut);
  buf_out->set_inst(buf_inst);
  buf_out->set_net(leaf_net);
  buf_inst->insertDriverPin(buf_out);
  ASSERT_TRUE(DESIGN_INST.indexPin(buf_out));
  auto* sink_pin = DESIGN_INST.makePin("CLK");
  ASSERT_NE(sink_pin, nullptr);
  sink_pin->set_name("CLK");
  sink_pin->set_type(icts::PinType::kClock);
  sink_pin->set_inst(makeDesignInst("sink0", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0)));
  ASSERT_NE(sink_pin->get_inst(), nullptr);
  sink_pin->set_net(leaf_net);
  sink_pin->get_inst()->add_pin(sink_pin);
  ASSERT_TRUE(DESIGN_INST.indexPin(sink_pin));

  source_net->set_driver(io_driver);
  source_net->add_load(buf_in);
  leaf_net->set_driver(buf_out);
  leaf_net->add_load(sink_pin);
  clock->set_clock_source(io_driver);
  clock->set_clock_source_net(source_net);
  clock->add_inst(buf_inst);
  clock->add_net(leaf_net);
  clock->add_load(sink_pin);

  auto* idb_io_pin = idb_design.get_io_pin_list()->add_pin_list("clk_port");
  ASSERT_NE(idb_io_pin, nullptr);
  idb_io_pin->set_as_io();
  auto* io_term = idb_io_pin->set_term();
  ASSERT_NE(io_term, nullptr);
  io_term->set_name("clk_port");
  io_term->set_direction(idb::IdbConnectDirection::kInput);
  io_term->set_type(idb::IdbConnectType::kClock);
  idb_io_pin->set_average_coordinate(0, 0);

  const auto result = WRAPPER_INST.writeClocksDetailed({clock});

  EXPECT_TRUE(result.success);
  auto* idb_buf = idb_design.get_instance_list()->find_instance("cts_buf0");
  ASSERT_NE(idb_buf, nullptr);
  EXPECT_EQ(idb_buf->get_cell_master()->get_name(), "CTS_BUF");
  auto* root_net = idb_design.get_net_list()->find_net("root_clk_net");
  auto* leaf_idb_net = idb_design.get_net_list()->find_net("leaf_clk_net");
  ASSERT_NE(root_net, nullptr);
  ASSERT_NE(leaf_idb_net, nullptr);
  EXPECT_EQ(idb_io_pin->get_net(), root_net);
  EXPECT_EQ(idb_buf->get_pin_by_term("A")->get_net(), root_net);
  EXPECT_EQ(idb_buf->get_pin_by_term("Y")->get_net(), leaf_idb_net);
  EXPECT_EQ(idb_sink_pin->get_net(), leaf_idb_net);
}

TEST(FlowTest, WrapperWritebackDoesNotCreateNonCtsInstWhenResolvingClockSinkPin)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  auto* clock = DESIGN_INST.makeClock("LOGICAL_CLK", "root_clk_net");
  ASSERT_NE(clock, nullptr);
  buildClockForWrapperWriteback(*clock, "clk_port", "missing_sink", "CLK");
  auto* idb_io_pin = idb_design.get_io_pin_list()->add_pin_list("clk_port");
  ASSERT_NE(idb_io_pin, nullptr);
  idb_io_pin->set_as_io();
  auto* io_term = idb_io_pin->set_term();
  ASSERT_NE(io_term, nullptr);
  io_term->set_name("clk_port");
  io_term->set_direction(idb::IdbConnectDirection::kInput);
  io_term->set_type(idb::IdbConnectType::kClock);
  idb_io_pin->set_average_coordinate(0, 0);

  const auto result = WRAPPER_INST.writeClocksDetailed({clock});

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.rollback_done);
  EXPECT_EQ(idb_design.get_instance_list()->find_instance("missing_sink"), nullptr);
  EXPECT_EQ(idb_design.get_net_list()->find_net("root_clk_net"), nullptr);
}

TEST(FlowTest, WrapperWritebackRollbackRemovesNewCtsInstAndRestoresTouchedNetPins)
{
  const ScopedFlowReset scoped_flow_reset;
  idb::IdbLayout idb_layout;
  idb::IdbDesign idb_design(&idb_layout);
  WRAPPER_INST.set_idb_design(&idb_design);
  WRAPPER_INST.set_idb_layout(&idb_layout);

  auto* reg_master = addIdbCellMaster(idb_layout, "REG_CELL");
  ASSERT_NE(reg_master, nullptr);
  ASSERT_NE(addIdbTerm(*reg_master, "CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  auto* buf_master = addIdbCellMaster(idb_layout, "CTS_BUF");
  ASSERT_NE(buf_master, nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "A", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock), nullptr);
  ASSERT_NE(addIdbTerm(*buf_master, "Y", idb::IdbConnectDirection::kOutput, idb::IdbConnectType::kClock), nullptr);
  auto* old_sink_inst = addIdbInst(idb_design, "old_sink", *reg_master, 200, 0);
  ASSERT_NE(old_sink_inst, nullptr);
  auto* old_sink_pin = old_sink_inst->get_pin_by_term("CLK");
  ASSERT_NE(old_sink_pin, nullptr);
  auto* root_net = idb_design.get_net_list()->add_net("root_clk_net", idb::IdbConnectType::kClock);
  ASSERT_NE(root_net, nullptr);
  auto* idb_io_pin = idb_design.get_io_pin_list()->add_pin_list("clk_port");
  ASSERT_NE(idb_io_pin, nullptr);
  idb_io_pin->set_as_io();
  auto* io_term = idb_io_pin->set_term();
  ASSERT_NE(io_term, nullptr);
  io_term->set_name("clk_port");
  io_term->set_direction(idb::IdbConnectDirection::kInput);
  io_term->set_type(idb::IdbConnectType::kClock);
  idb_io_pin->set_average_coordinate(0, 0);
  attachIdbPinToNet(*root_net, *idb_io_pin);
  attachIdbPinToNet(*root_net, *old_sink_pin);

  auto* clock = DESIGN_INST.makeClock("LOGICAL_CLK", "root_clk_net");
  ASSERT_NE(clock, nullptr);
  auto* source_net = DESIGN_INST.makeNet("root_clk_net");
  auto* leaf_net = DESIGN_INST.makeNet("leaf_clk_net");
  ASSERT_NE(source_net, nullptr);
  ASSERT_NE(leaf_net, nullptr);
  auto* source_pin = DESIGN_INST.makePin("clk_port");
  ASSERT_NE(source_pin, nullptr);
  source_pin->set_name("clk_port");
  source_pin->set_type(icts::PinType::kOut);
  source_pin->set_io(true);
  source_pin->set_net(source_net);
  ASSERT_TRUE(DESIGN_INST.indexPin(source_pin));
  auto* buf_inst = makeDesignInst("cts_buf_rollback", "CTS_BUF", icts::InstType::kBuffer, icts::Point<int>(10, 0));
  ASSERT_NE(buf_inst, nullptr);
  auto* buf_in = addOwnedLoad(*clock, source_net, *buf_inst, "A");
  auto* buf_out = DESIGN_INST.makePin("Y");
  ASSERT_NE(buf_in, nullptr);
  ASSERT_NE(buf_out, nullptr);
  buf_out->set_name("Y");
  buf_out->set_type(icts::PinType::kOut);
  buf_out->set_inst(buf_inst);
  buf_out->set_net(leaf_net);
  buf_inst->insertDriverPin(buf_out);
  ASSERT_TRUE(DESIGN_INST.indexPin(buf_out));
  auto* missing_sink_inst = makeDesignInst("missing_leaf_sink", "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(300, 0));
  auto* missing_sink_pin = DESIGN_INST.makePin("CLK");
  ASSERT_NE(missing_sink_inst, nullptr);
  ASSERT_NE(missing_sink_pin, nullptr);
  missing_sink_pin->set_name("CLK");
  missing_sink_pin->set_type(icts::PinType::kClock);
  missing_sink_pin->set_inst(missing_sink_inst);
  missing_sink_pin->set_net(leaf_net);
  missing_sink_inst->add_pin(missing_sink_pin);
  ASSERT_TRUE(DESIGN_INST.indexPin(missing_sink_pin));

  source_net->set_driver(source_pin);
  source_net->add_load(buf_in);
  leaf_net->set_driver(buf_out);
  leaf_net->add_load(missing_sink_pin);
  clock->set_clock_source(source_pin);
  clock->set_clock_source_net(source_net);
  clock->add_inst(buf_inst);
  clock->add_net(leaf_net);
  clock->add_load(missing_sink_pin);

  const auto result = WRAPPER_INST.writeClocksDetailed({clock});

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(result.rollback_done);
  EXPECT_EQ(idb_design.get_instance_list()->find_instance("cts_buf_rollback"), nullptr);
  EXPECT_EQ(idb_design.get_net_list()->find_net("leaf_clk_net"), nullptr);
  EXPECT_EQ(idb_io_pin->get_net(), root_net);
  EXPECT_EQ(old_sink_pin->get_net(), root_net);
  EXPECT_EQ(root_net->get_pin_number(), 2);
}

}  // namespace
}  // namespace icts_test
