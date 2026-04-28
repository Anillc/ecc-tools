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
 * @file ClockSynthesisTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-17
 * @brief Guard-behavior coverage for ClockSynthesis invalid inputs.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "adapter/sta/STAAdapter.hh"
#include "database/config/Config.hh"
#include "database/design/Clock.hh"
#include "database/design/Design.hh"
#include "database/design/Inst.hh"
#include "database/design/Net.hh"
#include "database/design/Pin.hh"
#include "database/spatial/Point.hh"
#include "flow/synthesis/ClockSynthesis.hh"
#include "utils/logger/Schema.hh"

namespace icts_test {
namespace {

class ScopedConfigReset
{
 public:
  ScopedConfigReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
  }
  ~ScopedConfigReset()
  {
    CONFIG_INST.reset();
    DESIGN_INST.reset();
  }
};

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return buffer.str();
}

auto MakeUniqueTempPath(const std::string& file_name) -> std::filesystem::path
{
  return std::filesystem::temp_directory_path() / ("icts_clock_synthesis_test_" + file_name);
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

auto makeDesignPin(icts::Inst* inst, const std::string& name, icts::PinType type, const icts::Point<int>& location) -> icts::Pin*
{
  auto* pin = DESIGN_INST.makePin(name);
  if (pin == nullptr) {
    return nullptr;
  }
  pin->set_name(name);
  pin->set_type(type);
  pin->set_location(location);
  pin->set_inst(inst);
  pin->set_net(nullptr);
  pin->set_io(false);
  if (inst != nullptr) {
    inst->add_pin(pin);
  }
  (void) DESIGN_INST.indexPin(pin);
  return pin;
}

auto makeDesignNet(const std::string& name, icts::Pin* driver = nullptr, const std::vector<icts::Pin*>& loads = {}) -> icts::Net*
{
  auto* net = DESIGN_INST.makeNet(name);
  if (net == nullptr) {
    return nullptr;
  }
  net->set_name(name);
  net->set_driver(driver);
  if (driver != nullptr) {
    driver->set_net(net);
  }
  net->set_loads({});
  for (auto* load : loads) {
    if (load == nullptr) {
      continue;
    }
    net->add_load(load);
    load->set_net(net);
  }
  return net;
}

TEST(ClockSynthesisTest, RootNetWithNullDriverFailsWithoutInsertedObjects)
{
  icts::Pin sink("sink_0", icts::PinType::kClock);
  icts::Net root_net("root_net");
  root_net.add_load(&sink);
  sink.set_net(&root_net);

  const auto result = icts::ClockSynthesis::build(root_net);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, RootNetWithEmptyLoadListFailsWithoutInsertedObjects)
{
  icts::Pin source("clk_src", icts::PinType::kClock);
  icts::Net root_net("root_net");
  root_net.set_driver(&source);
  source.set_net(&root_net);

  const auto result = icts::ClockSynthesis::build(root_net);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, RootNetWithMissingDriverAndLoadsFailsSafely)
{
  icts::Net root_net("root_net");
  const auto result = icts::ClockSynthesis::build(root_net);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, BuildFailurePreservesBorrowedMembership)
{
  const ScopedConfigReset scoped_config_reset;
  icts::Clock invalid_clock("clk", "clk_net");
  auto* stale_inst = makeDesignInst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  auto* stale_net = makeDesignNet("cts_net_0");
  icts::Net invalid_root_net("invalid_root_net");

  invalid_clock.add_inst(stale_inst);
  invalid_clock.add_net(stale_net);
  ASSERT_EQ(invalid_clock.get_insts().size(), 1U);
  ASSERT_EQ(invalid_clock.get_nets().size(), 1U);

  const auto result = icts::ClockSynthesis::build(invalid_root_net);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.failure_reason.empty());
  ASSERT_EQ(invalid_clock.get_insts().size(), 1U);
  ASSERT_EQ(invalid_clock.get_nets().size(), 1U);
  EXPECT_EQ(invalid_clock.get_insts().front(), stale_inst);
  EXPECT_EQ(invalid_clock.get_nets().front(), stale_net);
  EXPECT_EQ(DESIGN_INST.findInst("cts_buf_0"), stale_inst);
  EXPECT_EQ(DESIGN_INST.findNet("cts_net_0"), stale_net);
}

TEST(ClockSynthesisTest, DesignCommitRejectsFinalNameCollisions)
{
  const ScopedConfigReset scoped_config_reset;

  auto* existing_inst = makeDesignInst("u0", "REG_X1", icts::InstType::kFlipFlop, icts::Point<int>(10, 20));
  auto* existing_pin = makeDesignPin(existing_inst, "CLK", icts::PinType::kClock, existing_inst->get_location());
  auto* existing_net = makeDesignNet("clk_net");
  ASSERT_NE(existing_inst, nullptr);
  ASSERT_NE(existing_pin, nullptr);
  ASSERT_NE(existing_net, nullptr);

  auto colliding_inst = std::make_unique<icts::Inst>("u0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  auto colliding_pin
      = std::make_unique<icts::Pin>("CLK", icts::PinType::kClock, existing_inst->get_location(), existing_inst, nullptr, false);
  auto colliding_net = std::make_unique<icts::Net>("clk_net");

  EXPECT_EQ(DESIGN_INST.commitInst(std::move(colliding_inst)), nullptr);
  EXPECT_EQ(DESIGN_INST.commitPin(std::move(colliding_pin)), nullptr);
  EXPECT_EQ(DESIGN_INST.commitNet(std::move(colliding_net)), nullptr);

  EXPECT_EQ(DESIGN_INST.findInst("u0"), existing_inst);
  EXPECT_EQ(DESIGN_INST.findPin(icts::Design::getPinFullName(existing_pin)), existing_pin);
  EXPECT_EQ(DESIGN_INST.findNet("clk_net"), existing_net);
  EXPECT_EQ(existing_inst->get_cell_master(), "REG_X1");
  EXPECT_EQ(existing_inst->get_type(), icts::InstType::kFlipFlop);
  EXPECT_EQ(DESIGN_INST.get_insts().size(), 1U);
  EXPECT_EQ(DESIGN_INST.get_pins().size(), 1U);
  EXPECT_EQ(DESIGN_INST.get_nets().size(), 1U);
}

TEST(ClockSynthesisTest, DesignOwnsFinalObjectsAndClockKeepsMembershipOnly)
{
  const ScopedConfigReset scoped_config_reset;
  icts::Clock clock("clk", "clk_net");

  auto* inst = makeDesignInst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(10, 20));
  ASSERT_NE(inst, nullptr);
  auto* input_pin = makeDesignPin(inst, "A", icts::PinType::kIn, inst->get_location());
  auto* output_pin = makeDesignPin(inst, "Y", icts::PinType::kOut, inst->get_location());
  ASSERT_NE(input_pin, nullptr);
  ASSERT_NE(output_pin, nullptr);
  inst->insertDriverPin(output_pin);

  auto* net = makeDesignNet("cts_net_0", output_pin, std::vector<icts::Pin*>{input_pin});
  ASSERT_NE(net, nullptr);
  clock.add_inst(inst);
  clock.add_net(net);

  ASSERT_EQ(clock.get_insts().size(), 1U);
  ASSERT_EQ(clock.get_nets().size(), 1U);
  EXPECT_EQ(clock.get_insts().front()->get_name(), "cts_buf_0");
  EXPECT_EQ(clock.get_nets().front()->get_name(), "cts_net_0");
  EXPECT_EQ(DESIGN_INST.get_insts().size(), 1U);
  EXPECT_EQ(DESIGN_INST.get_pins().size(), 2U);
  EXPECT_EQ(DESIGN_INST.get_nets().size(), 1U);
  EXPECT_EQ(net->get_driver(), output_pin);
  EXPECT_EQ(output_pin->get_net(), net);
  ASSERT_EQ(net->get_loads().size(), 1U);
  EXPECT_EQ(net->get_loads().front(), input_pin);
  EXPECT_EQ(input_pin->get_net(), net);

  DESIGN_INST.removeClockMembershipObjects(clock);
  clock.clearMembership();
  EXPECT_TRUE(clock.get_insts().empty());
  EXPECT_TRUE(clock.get_nets().empty());
  EXPECT_TRUE(DESIGN_INST.get_insts().empty());
  EXPECT_TRUE(DESIGN_INST.get_pins().empty());
  EXPECT_TRUE(DESIGN_INST.get_nets().empty());
}

TEST(ClockSynthesisTest, InstInsertDriverPinHandlesEmptyPinList)
{
  icts::Inst inst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  icts::Pin driver_pin("Y", icts::PinType::kOut);

  inst.insertDriverPin(&driver_pin);

  ASSERT_EQ(inst.get_pins().size(), 1U);
  EXPECT_EQ(inst.findDriverPin(), &driver_pin);

  inst.insertDriverPin(&driver_pin);

  ASSERT_EQ(inst.get_pins().size(), 1U);
  EXPECT_EQ(inst.findDriverPin(), &driver_pin);
}

TEST(ClockSynthesisTest, EnableSinkClusteringDefaultsTrueAndEmitsRuntimeConfigReport)
{
  const ScopedConfigReset scoped_config_reset;
  EXPECT_TRUE(CONFIG_INST.is_enable_sink_clustering());
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_htree_topology_tolerance(), 0.1);

  const auto json_path = MakeUniqueTempPath("config.json");
  const auto cts_log_path = MakeUniqueTempPath("cts.log");

  {
    std::ofstream output_stream(json_path);
    ASSERT_TRUE(output_stream.is_open());
    output_stream
        << R"({"enable_sink_clustering": false, "htree_topology_tolerance": 0.25, "routing_layer": [5, 6], "wire_width": 0.12, "wirelength_unit_um": 12.5, "wirelength_iterations": 7})";
  }

  CONFIG_INST.parse(json_path.string());
  EXPECT_FALSE(CONFIG_INST.is_enable_sink_clustering());
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_htree_topology_tolerance(), 0.25);
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_wirelength_unit_um(), 12.5);
  EXPECT_EQ(CONFIG_INST.get_wirelength_iterations(), 7U);

  SCHEMA_WRITER_INST.open(cts_log_path, "Clock Synthesis Unit Test");
  CONFIG_INST.emitRuntimeConfigReport("ClockSynthesis Config");
  SCHEMA_WRITER_INST.close();

  const auto cts_log_content = ReadTextFile(cts_log_path);
  EXPECT_NE(cts_log_content.find("enable_sink_clustering"), std::string::npos);
  EXPECT_NE(cts_log_content.find("false"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_topology_tolerance"), std::string::npos);
  EXPECT_NE(cts_log_content.find("25.00 %"), std::string::npos);
  EXPECT_NE(cts_log_content.find("routing_layers"), std::string::npos);
  EXPECT_NE(cts_log_content.find("configured order: 5, 6"), std::string::npos);
  EXPECT_NE(cts_log_content.find("wire_width"), std::string::npos);
  EXPECT_NE(cts_log_content.find("0.1200 um"), std::string::npos);
  EXPECT_NE(cts_log_content.find("wirelength_unit"), std::string::npos);
  EXPECT_NE(cts_log_content.find("12.5000 um"), std::string::npos);
  EXPECT_NE(cts_log_content.find("wirelength_iterations"), std::string::npos);
  EXPECT_FALSE(std::regex_search(cts_log_content, std::regex(R"(\|\s*routing_layer\s*\|)")));

  std::error_code error_code;
  std::filesystem::remove(json_path, error_code);
  error_code.clear();
  std::filesystem::remove(cts_log_path, error_code);
}

TEST(ClockSynthesisTest, SourceToRootWithEmptyRootsFailsWithoutChangingSourceNet)
{
  icts::Pin source("clk_src", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Pin original_load("sink", icts::PinType::kClock, icts::Point<int>(100, 0));
  icts::Net source_net("clk_net");
  source_net.set_driver(&source);
  source.set_net(&source_net);
  source_net.add_load(&original_load);
  original_load.set_net(&source_net);

  const auto result = icts::ClockSynthesis::buildSourceToRoot(source_net, &source, {}, icts::ClockSynthesis::SourceToRootBuildOptions{});

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.failure_reason.empty());
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_EQ(source_net.get_driver(), &source);
  ASSERT_EQ(source_net.get_loads().size(), 1U);
  EXPECT_EQ(source_net.get_loads().front(), &original_load);
  EXPECT_EQ(original_load.get_net(), &source_net);
}

TEST(ClockSynthesisTest, SourceToRootSingleRootSameLocationDirectConnectsWithoutInsertedObjects)
{
  icts::Pin source("clk_src", icts::PinType::kOut, icts::Point<int>(100, 200));
  icts::Pin root_input("A", icts::PinType::kIn, icts::Point<int>(100, 200));
  icts::Net source_net("clk_net");
  source_net.set_driver(&source);
  source.set_net(&source_net);

  const auto result
      = icts::ClockSynthesis::buildSourceToRoot(source_net, &source, {&root_input}, icts::ClockSynthesis::SourceToRootBuildOptions{});

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.stage, "top_segment");
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_EQ(source_net.get_driver(), &source);
  ASSERT_EQ(source_net.get_loads().size(), 1U);
  EXPECT_EQ(source_net.get_loads().front(), &root_input);
  EXPECT_EQ(root_input.get_net(), &source_net);
  EXPECT_EQ(source.get_net(), &source_net);
}

TEST(ClockSynthesisTest, ClockSourceDriveCapUsesRuntimeMaxCapForTopLevelIoPort)
{
  const ScopedConfigReset scoped_config_reset;
  CONFIG_INST.set_max_cap(0.23);

  icts::Pin source("clk_i", icts::PinType::kOut, icts::Point<int>(100, 200), nullptr, nullptr, true);

  EXPECT_DOUBLE_EQ(STA_ADAPTER_INST.queryClockSourceDriveCapLimit(&source), 0.23);
}

}  // namespace
}  // namespace icts_test
