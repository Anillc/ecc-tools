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
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "database/config/Config.hh"
#include "database/design/Clock.hh"
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
  ScopedConfigReset() { CONFIG_INST.reset(); }
  ~ScopedConfigReset() { CONFIG_INST.reset(); }
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

TEST(ClockSynthesisTest, NullClockSourceFailsWithoutInsertedObjects)
{
  icts::Pin sink("sink_0", icts::PinType::kClock);
  std::vector<icts::Pin*> sinks{&sink};

  const auto result = icts::ClockSynthesis::build(nullptr, sinks);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_EQ(result.source_to_root_net, nullptr);
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, EmptySinkListFailsWithoutInsertedObjects)
{
  icts::Pin source("clk_src", icts::PinType::kClock);
  const std::vector<icts::Pin*> sinks;

  const auto result = icts::ClockSynthesis::build(&source, sinks);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_EQ(result.source_to_root_net, nullptr);
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, ClockFacadeWithMissingSourceAndLoadsFailsSafely)
{
  icts::Clock invalid_clock("clk", "clk_net");
  const auto result = icts::ClockSynthesis::build(invalid_clock);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.inserted_insts.size(), 0U);
  EXPECT_EQ(result.inserted_nets.size(), 0U);
  EXPECT_TRUE(result.cluster_buffers.empty());
  EXPECT_EQ(result.source_to_root_net, nullptr);
  EXPECT_FALSE(result.failure_reason.empty());
}

TEST(ClockSynthesisTest, ClockFacadeClearsBorrowedInsertedObjectsBeforeFailingRebuild)
{
  icts::Clock invalid_clock("clk", "clk_net");
  icts::Inst stale_inst("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(0, 0));
  icts::Net stale_net("cts_net_0");

  invalid_clock.add_inserted_inst(&stale_inst);
  invalid_clock.add_inserted_net(&stale_net);
  ASSERT_EQ(invalid_clock.get_inserted_insts().size(), 1U);
  ASSERT_EQ(invalid_clock.get_inserted_nets().size(), 1U);

  const auto result = icts::ClockSynthesis::build(invalid_clock);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.failure_reason.empty());
  EXPECT_TRUE(invalid_clock.get_inserted_insts().empty());
  EXPECT_TRUE(invalid_clock.get_inserted_nets().empty());
}

TEST(ClockSynthesisTest, ClockRetainsAdoptedInsertedObjectOwnership)
{
  icts::Clock clock("clk", "clk_net");

  auto inst = std::make_unique<icts::Inst>("cts_buf_0", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(10, 20));
  auto* inst_ptr = inst.get();
  auto input_pin = std::make_unique<icts::Pin>("cts_buf_0/A", icts::PinType::kIn, inst_ptr->get_location(), inst_ptr, nullptr, false);
  auto output_pin = std::make_unique<icts::Pin>("cts_buf_0/Y", icts::PinType::kOut, inst_ptr->get_location(), inst_ptr, nullptr, false);
  auto* input_pin_ptr = input_pin.get();
  auto* output_pin_ptr = output_pin.get();
  inst_ptr->add_pin(input_pin_ptr);
  inst_ptr->insertDriverPin(output_pin_ptr);

  auto net = std::make_unique<icts::Net>("cts_net_0");
  auto* net_ptr = net.get();
  net_ptr->set_driver(output_pin_ptr);
  output_pin_ptr->set_net(net_ptr);
  net_ptr->add_load(input_pin_ptr);
  input_pin_ptr->set_net(net_ptr);

  std::vector<std::unique_ptr<icts::Inst>> inst_storage;
  std::vector<std::unique_ptr<icts::Pin>> pin_storage;
  std::vector<std::unique_ptr<icts::Net>> net_storage;
  inst_storage.push_back(std::move(inst));
  pin_storage.push_back(std::move(output_pin));
  pin_storage.push_back(std::move(input_pin));
  net_storage.push_back(std::move(net));

  clock.adoptInsertedCtsOwnership(std::move(inst_storage), std::move(pin_storage), std::move(net_storage));
  clock.add_inserted_inst(inst_ptr);
  clock.add_inserted_net(net_ptr);

  ASSERT_EQ(clock.get_inserted_insts().size(), 1U);
  ASSERT_EQ(clock.get_inserted_nets().size(), 1U);
  EXPECT_EQ(clock.get_inserted_insts().front()->get_name(), "cts_buf_0");
  EXPECT_EQ(clock.get_inserted_nets().front()->get_name(), "cts_net_0");

  clock.clearInsertedCtsObjects();
  EXPECT_TRUE(clock.get_inserted_insts().empty());
  EXPECT_TRUE(clock.get_inserted_nets().empty());
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
    output_stream << R"({"enable_sink_clustering": false, "htree_topology_tolerance": 0.25})";
  }

  CONFIG_INST.parse(json_path.string());
  EXPECT_FALSE(CONFIG_INST.is_enable_sink_clustering());
  EXPECT_DOUBLE_EQ(CONFIG_INST.get_htree_topology_tolerance(), 0.25);

  SCHEMA_WRITER_INST.open(cts_log_path, "Clock Synthesis Unit Test");
  CONFIG_INST.emitRuntimeConfigReport("ClockSynthesis Config");
  SCHEMA_WRITER_INST.close();

  const auto cts_log_content = ReadTextFile(cts_log_path);
  EXPECT_NE(cts_log_content.find("enable_sink_clustering"), std::string::npos);
  EXPECT_NE(cts_log_content.find("false"), std::string::npos);
  EXPECT_NE(cts_log_content.find("htree_topology_tolerance"), std::string::npos);
  EXPECT_NE(cts_log_content.find("25.00 %"), std::string::npos);

  std::error_code error_code;
  std::filesystem::remove(json_path, error_code);
  error_code.clear();
  std::filesystem::remove(cts_log_path, error_code);
}

}  // namespace
}  // namespace icts_test
