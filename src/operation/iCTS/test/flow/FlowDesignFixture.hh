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
 * @file FlowDesignFixture.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Shared fixtures for CTS flow tests.
 */

#pragma once

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

namespace icts_test::flow_test {

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

inline auto FindInstByNamePart(const std::vector<icts::Inst*>& insts, const std::string& name_part) -> icts::Inst*
{
  const auto iter = std::ranges::find_if(
      insts, [&name_part](const auto* inst) -> bool { return inst != nullptr && inst->get_name().find(name_part) != std::string::npos; });
  return iter == insts.end() ? nullptr : *iter;
}

inline auto FindNetByNamePart(const std::vector<icts::Net*>& nets, const std::string& name_part) -> icts::Net*
{
  const auto iter = std::ranges::find_if(
      nets, [&name_part](const auto* net) -> bool { return net != nullptr && net->get_name().find(name_part) != std::string::npos; });
  return iter == nets.end() ? nullptr : *iter;
}

inline auto FindInputPin(const icts::Inst* inst) -> icts::Pin*
{
  if (inst == nullptr) {
    return nullptr;
  }
  const auto& pins = inst->get_pins();
  const auto iter
      = std::ranges::find_if(pins, [](const auto* pin) -> bool { return pin != nullptr && pin->get_type() == icts::PinType::kIn; });
  return iter == pins.end() ? nullptr : *iter;
}

inline auto ContainsPin(const std::vector<icts::Pin*>& pins, const icts::Pin* pin) -> bool
{
  return std::ranges::find(pins, pin) != pins.end();
}

inline auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return buffer.str();
}

inline auto WriteTempSdc(const std::string& file_name, const std::string& content) -> std::filesystem::path
{
  const auto path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream output_stream(path);
  EXPECT_TRUE(output_stream.is_open());
  output_stream << content;
  dmInst->get_config().set_sdc_path(path.string());
  return path;
}

inline auto MakeDesignInst(const std::string& name, const std::string& cell_master, icts::InstType type, const icts::Point<int>& location)
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

inline auto AddOwnedLoad(icts::Clock& clock, icts::Net* clock_net, icts::Inst& inst, const std::string& pin_name) -> icts::Pin*
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

inline auto AddClockToDesign(icts::Inst* macro_inst, icts::Inst* regular_inst) -> TestClockPins
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
    pins.macro_sink = AddOwnedLoad(*clock_ptr, clock_net, *macro_inst, "CLK");
  }
  if (regular_inst != nullptr) {
    pins.regular_sink = AddOwnedLoad(*clock_ptr, clock_net, *regular_inst, "CLK");
  }

  return pins;
}

inline auto PrepareDirectRootBufferNets(icts::Clock& clock, const std::string& cell_master, const std::string& input_pin_name,
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

inline auto StatusRowsContain(const icts::schema::TableRows& rows, const std::string& status, const std::string& sink_domain,
                              const std::string& detail) -> bool
{
  return std::ranges::any_of(rows, [&](const auto& row) -> bool {
    return row.size() >= 7U && row.at(2) == status && row.at(3) == sink_domain && row.at(6) == detail;
  });
}

inline auto DomainStatusContains(const std::vector<icts::SynthesisTraceStatusRecord>& records, const std::string& status,
                                 const std::string& sink_domain, const std::string& detail) -> bool
{
  return std::ranges::any_of(records, [&](const auto& record) -> bool {
    return record.status == status && record.sink_domain == sink_domain && record.detail == detail;
  });
}

inline auto MakeRootBufferSpec() -> icts::ClockSinkDomainRootBufferSpec
{
  return icts::ClockSinkDomainRootBufferSpec{
      .cell_master = "CTS_TEST_BUF",
      .input_pin_name = "A",
      .output_pin_name = "Y",
  };
}

inline auto AddIdbTerm(idb::IdbCellMaster& cell_master, const std::string& term_name, idb::IdbConnectDirection direction,
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

inline auto AddIdbCellMaster(idb::IdbLayout& idb_layout, const std::string& cell_master_name) -> idb::IdbCellMaster*
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

inline auto AddIdbInst(idb::IdbDesign& idb_design, const std::string& inst_name, idb::IdbCellMaster& cell_master, int32_t loc_x = 0,
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

inline auto AddIdbIoPin(idb::IdbDesign& idb_design, const std::string& pin_name, idb::IdbConnectDirection direction,
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

inline auto AttachIdbPinToNet(idb::IdbNet& idb_net, idb::IdbPin& idb_pin) -> void
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

inline auto AddIdbClockSink(idb::IdbDesign& idb_design, idb::IdbCellMaster& reg_master, idb::IdbNet& net, const std::string& inst_name,
                            int32_t loc_x) -> idb::IdbPin*
{
  auto* inst = AddIdbInst(idb_design, inst_name, reg_master, loc_x, 0);
  if (inst == nullptr) {
    ADD_FAILURE() << "failed to add clock sink inst " << inst_name;
    return nullptr;
  }
  auto* pin = inst->get_pin_by_term("CLK");
  if (pin == nullptr) {
    ADD_FAILURE() << "failed to find clock sink pin for " << inst_name;
    return nullptr;
  }
  AttachIdbPinToNet(net, *pin);
  return pin;
}

inline auto AddIdbPassCell(idb::IdbDesign& idb_design, idb::IdbCellMaster& pass_master, idb::IdbNet& input_net, idb::IdbNet& output_net,
                           const std::string& inst_name, int32_t loc_x) -> idb::IdbInstance*
{
  auto* inst = AddIdbInst(idb_design, inst_name, pass_master, loc_x, 0);
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
  AttachIdbPinToNet(input_net, *input_pin);
  AttachIdbPinToNet(output_net, *output_pin);
  return inst;
}

inline auto BuildClockForWrapperClockTreeMaterialization(icts::Clock& clock, const std::string& source_pin_name,
                                                         const std::string& sink_inst_name, const std::string& sink_pin_name) -> void
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

  auto* sink_inst = MakeDesignInst(sink_inst_name, "REG_CELL", icts::InstType::kFlipFlop, icts::Point<int>(100, 0));
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

inline auto ExpectClockRestoredToOriginalLoads(const TestClockPins& pins) -> void
{
  ASSERT_NE(pins.clock, nullptr);
  ASSERT_NE(pins.clock_net, nullptr);
  EXPECT_TRUE(pins.clock->get_insts().empty());
  EXPECT_TRUE(pins.clock->get_nets().empty());
  EXPECT_EQ(pins.clock->get_clock_source_net(), pins.clock_net);
  EXPECT_EQ(pins.clock_net->get_driver(), pins.clock_source);
  ASSERT_EQ(pins.clock_net->get_loads().size(), pins.macro_sink == nullptr ? 1U : 2U);
  if (pins.macro_sink != nullptr) {
    EXPECT_TRUE(ContainsPin(pins.clock_net->get_loads(), pins.macro_sink));
  }
  if (pins.regular_sink != nullptr) {
    EXPECT_TRUE(ContainsPin(pins.clock_net->get_loads(), pins.regular_sink));
  }
}

}  // namespace icts_test::flow_test
