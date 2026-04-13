// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

#include "log/Log.hh"
#include "netlist/Net.hh"
#include "sta/Sta.hh"

using namespace ista;

namespace {

std::set<std::string> collectPinPortNames(Net* net) {
  std::set<std::string> pin_port_names;
  for (auto* pin_port : net->get_pin_ports()) {
    pin_port_names.emplace(pin_port->get_name());
  }
  return pin_port_names;
}

class AssignMergeTest : public testing::Test {
 protected:
  void SetUp() override {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }

  void TearDown() override {
    Sta::destroySta();
    Log::end();

    if (!_verilog_path.empty()) {
      std::error_code ec;
      std::filesystem::remove(_verilog_path, ec);
    }
  }

  std::string writeTempVerilog(const std::string& contents) {
    auto path =
        std::filesystem::temp_directory_path() /
        ("ista_assign_merge_" + std::to_string(::getpid()) + ".v");
    std::ofstream output(path);
    output << contents;
    output.close();
    _verilog_path = path.string();
    return _verilog_path;
  }

 private:
  std::string _verilog_path;
};

TEST_F(AssignMergeTest, alias_chain_reuses_merged_net_for_later_assigns) {
  const auto verilog_path = writeTempVerilog(R"(module top(a, y);
input a;
output y;
wire n1;
wire n2;
assign n1 = a;
assign n2 = n1;
assign y = n2;
endmodule
)");

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);

  ASSERT_TRUE(ista->readVerilogWithRustParser(verilog_path.c_str()));
  ista->set_top_module_name("top");
  ista->linkDesignWithRustParser("top");

  auto* design_netlist = ista->get_netlist();
  ASSERT_NE(design_netlist, nullptr);

  auto* n1_net = design_netlist->findNet("n1");
  ASSERT_NE(n1_net, nullptr);
  EXPECT_EQ(design_netlist->findNet("n2"), nullptr);

  EXPECT_EQ(collectPinPortNames(n1_net), (std::set<std::string>{"a", "y"}));
}

}  // namespace
