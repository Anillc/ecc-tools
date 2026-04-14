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

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Port.hh"
#include "sta/Sta.hh"
#include "sta/StaClusterTiming.hh"

using namespace ista;

namespace {

LibCell* addCell(
    LibLibrary& lib, const char* cell_name,
    std::initializer_list<std::pair<const char*, LibPort::LibertyPortType>>
        ports) {
  auto lib_cell = std::make_unique<LibCell>(cell_name, &lib);
  auto* lib_cell_ptr = lib_cell.get();

  for (const auto& [port_name, port_type] : ports) {
    auto lib_port = std::make_unique<LibPort>(port_name);
    lib_port->set_port_type(port_type);
    lib_port->set_ower_cell(lib_cell_ptr);
    lib_cell_ptr->addLibertyPort(std::move(lib_port));
  }

  lib.addLibertyCell(std::move(lib_cell));
  return lib.findCell(cell_name);
}

std::size_t countPortsByName(Netlist& netlist, const char* port_name) {
  auto& ports = netlist.get_ports();
  return std::count_if(ports.begin(), ports.end(),
                       [port_name](const Port& port) {
                         return std::string(port.get_name()) == port_name;
                       });
}

class RestoredClusterTimingTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!Log::isInit()) {
      char config[] = "test";
      char* argv[] = {config};
      Log::init(argv);
    }
    Sta::destroySta();
  }

  void TearDown() override {
    Sta::destroySta();
    if (Log::isInit()) {
      Log::end();
    }
  }
};

TEST_F(RestoredClusterTimingTest,
       subnetlist_reuses_shared_top_ports_and_keeps_boundary_ports_connected) {
  LibLibrary lib("cluster_restore_test");
  auto* boundary_cell = addCell(
      lib, "boundary_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* shared_cell = addCell(
      lib, "shared_cell",
      {{"C", LibPort::LibertyPortType::kInput},
       {"D", LibPort::LibertyPortType::kInput},
       {"Z", LibPort::LibertyPortType::kOutput}});
  auto* sink_cell = addCell(
      lib, "sink_cell", {{"I", LibPort::LibertyPortType::kInput}});

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  auto* netlist = ista->get_netlist();
  ASSERT_NE(netlist, nullptr);
  netlist->set_name("top");

  auto& bound_in = netlist->addPort(Port("BOUND_IN", PortDir::kIn));
  auto& shared_in = netlist->addPort(Port("SHARED_IN", PortDir::kIn));
  auto& top_out = netlist->addPort(Port("TOP_OUT", PortDir::kOut));

  auto& bound_in_net = netlist->addNet(Net("bound_in_net"));
  auto& shared_in_net = netlist->addNet(Net("shared_in_net"));
  auto& boundary_net = netlist->addNet(Net("boundary_net"));
  auto& top_out_net = netlist->addNet(Net("top_out_net"));
  bound_in_net.addPinPort(&bound_in);
  shared_in_net.addPinPort(&shared_in);
  top_out_net.addPinPort(&top_out);

  auto& boundary_inst = netlist->addInstance(Instance("u1", boundary_cell));
  auto* boundary_in_pin =
      boundary_inst.addPin("A", boundary_cell->get_cell_port_or_port_bus("A"));
  auto* boundary_out_pin =
      boundary_inst.addPin("Y", boundary_cell->get_cell_port_or_port_bus("Y"));
  bound_in_net.addPinPort(boundary_in_pin);
  boundary_net.addPinPort(boundary_out_pin);

  auto& shared_inst = netlist->addInstance(Instance("u2", shared_cell));
  auto* shared_in_pin_c =
      shared_inst.addPin("C", shared_cell->get_cell_port_or_port_bus("C"));
  auto* shared_in_pin_d =
      shared_inst.addPin("D", shared_cell->get_cell_port_or_port_bus("D"));
  auto* shared_out_pin =
      shared_inst.addPin("Z", shared_cell->get_cell_port_or_port_bus("Z"));
  shared_in_net.addPinPort(shared_in_pin_c);
  shared_in_net.addPinPort(shared_in_pin_d);
  top_out_net.addPinPort(shared_out_pin);

  auto& outside_sink = netlist->addInstance(Instance("u3", sink_cell));
  auto* outside_sink_pin =
      outside_sink.addPin("I", sink_cell->get_cell_port_or_port_bus("I"));
  boundary_net.addPinPort(outside_sink_pin);

  std::vector<std::set<std::string>> clusters = {{"u1", "u2"}};
  StaClusterTiming sta_cluster_timing(std::move(clusters));
  sta_cluster_timing.addHierSubNetlist();

  auto hier_sub_netlists = netlist->get_hier_sub_netlists();
  ASSERT_EQ(hier_sub_netlists.size(), 1U);
  auto* subnetlist = hier_sub_netlists.front();
  ASSERT_NE(subnetlist, nullptr);

  auto* copied_boundary_port = subnetlist->findPort("BOUND_IN");
  auto* copied_boundary_net = subnetlist->findNet("bound_in_net");
  ASSERT_NE(copied_boundary_port, nullptr);
  ASSERT_NE(copied_boundary_net, nullptr);
  EXPECT_TRUE(copied_boundary_net->isNetPinPort(copied_boundary_port))
      << "expected copied boundary port to stay electrically connected in the "
         "subnetlist";

  auto* copied_shared_port = subnetlist->findPort("SHARED_IN");
  auto* copied_shared_net = subnetlist->findNet("shared_in_net");
  ASSERT_NE(copied_shared_port, nullptr);
  ASSERT_NE(copied_shared_net, nullptr);
  EXPECT_TRUE(copied_shared_net->isNetPinPort(copied_shared_port));
  EXPECT_EQ(countPortsByName(*subnetlist, "SHARED_IN"), 1U)
      << "expected shared top-level ports to be emitted once per subnetlist";
}

}  // namespace
