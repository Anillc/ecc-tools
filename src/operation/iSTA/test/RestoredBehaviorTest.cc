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

#include "CharacterTimingTestCommon.hh"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>

#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "log/Log.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Port.hh"
#include "sta/Sta.hh"

using namespace ista;

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& file_path) {
  std::ifstream input(file_path);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

class RestoredStaBehaviorTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
    unsetenv("IEDA_CHARACTER_TIMING_REUSE_OUTPUT");
    _timing_engine = ista::test::prepareGoldenCaseTimingRuntime(
        ista::test::goldenCaseRuntimeOutputDir());
  }

  static void TearDownTestSuite() {
    TimingEngine::destroyTimingEngine();
    Sta::destroySta();
    Log::end();
  }

  static TimingEngine* _timing_engine;
};

TimingEngine* RestoredStaBehaviorTest::_timing_engine = nullptr;

TEST(RestoredBehaviorTest, sta_constructor_initializes_log_when_needed) {
  if (Log::isInit()) {
    Log::end();
  }
  Sta::destroySta();
  ASSERT_FALSE(Log::isInit());

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  EXPECT_TRUE(Log::isInit());

  Sta::destroySta();
  Log::end();
}

TEST(RestoredBehaviorTest, netlist_writer_escapes_assign_net_names) {
  char config[] = "test";
  char* argv[] = {config};
  Log::init(argv);

  Netlist netlist;
  netlist.set_name("top");

  auto& input_port = netlist.addPort(Port("a", PortDir::kIn));
  auto& output_port = netlist.addPort(Port("y", PortDir::kOut));
  auto& escaped_net = netlist.addNet(Net("foo/bar"));
  escaped_net.addPinPort(&input_port);
  escaped_net.addPinPort(&output_port);

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_behavior_writer.v";
  std::set<std::string> exclude_cell_names;
  netlist.writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = readText(output_path);
  EXPECT_NE(verilog_text.find("assign \\foo/bar = a ;"), std::string::npos);
  EXPECT_NE(verilog_text.find("assign y = \\foo/bar ;"), std::string::npos);

  std::error_code ec;
  fs::remove(output_path, ec);
  Log::end();
}

TEST_F(RestoredStaBehaviorTest, report_timing_data_returns_runtime_payload) {
  auto* timing_engine = _timing_engine;
  ASSERT_NE(timing_engine, nullptr);
  auto* ista = timing_engine->get_ista();
  ASSERT_NE(ista, nullptr);

  const auto timing_data = ista->reportTimingData(1);
  EXPECT_FALSE(timing_data.empty());
}

TEST_F(RestoredStaBehaviorTest, report_wire_paths_cleans_stale_files_and_writes_report) {
  auto* timing_engine = _timing_engine;
  ASSERT_NE(timing_engine, nullptr);
  auto* ista = timing_engine->get_ista();
  ASSERT_NE(ista, nullptr);

  const fs::path design_workspace = ista->get_design_work_space();
  const fs::path wire_path_dir = design_workspace / "wire_paths";
  std::error_code ec;
  fs::create_directories(wire_path_dir, ec);
  ASSERT_FALSE(ec) << "failed to create wire path dir: " << wire_path_dir
                   << ", error=" << ec.message();

  const fs::path stale_file = wire_path_dir / "stale.txt";
  {
    std::ofstream output(stale_file);
    output << "stale";
  }
  ASSERT_TRUE(fs::exists(stale_file));

  EXPECT_EQ(ista->reportWirePaths(), 1U);
  EXPECT_FALSE(fs::exists(stale_file));

  const fs::path report_path =
      design_workspace / (ista->get_design_name() + ".rpt");
  ASSERT_TRUE(fs::exists(report_path));
  EXPECT_GT(fs::file_size(report_path), 0U);
}

}  // namespace
